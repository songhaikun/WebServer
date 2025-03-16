#include "server.h"

#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <fstream>
#include <cassert>
#include <errno.h>
#include <sys/epoll.h>
#include <sstream>
#include <memory>

#include "fd_manager.hpp"

std::unordered_map<int, std::function<void(int)>> signal_handlers;

Server::Server(EventLoop* loop, int port, int thread_num) 
    : loop_(loop), port_(port), thread_num_(thread_num), listen_fd_(-1) {
    users_ = new HttpConn[MAX_FD];
    assert(users_ != nullptr);
    thread_pool_ = std::make_unique<EventLoopThreadPool>(loop, thread_num);
}

Server::~Server() {
    if (listen_fd_ != -1) SafeFdManager::getInstance().safeClose(listen_fd_);
    if (pipefd_[0] != -1) SafeFdManager::getInstance().safeClose(pipefd_[0]);
    if (pipefd_[1] != -1) SafeFdManager::getInstance().safeClose(pipefd_[1]);
    delete[] users_;
}

void Server::start() {
    initSocket();
    initSignal();
    setupEvents();
    thread_pool_->start();
    loop_->loop();
}

void Server::initSocket() {
    listen_fd_ = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        perror("make socket error!");
        exit(-1);
    }
    SafeFdManager::getInstance().registerFd(listen_fd_, "listen_fd");
    EpollUtil::getInstance()->setNonblocking(listen_fd_);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind error!");
        exit(-1);
    }
    if (listen(listen_fd_, 10000) < 0) {
        perror("listen error!");
        exit(-1);
    }
}

void Server::initSignal() {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigfillset(&sa.sa_mask);
    assert(sigaction(SIGPIPE, &sa, NULL) != -1);
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd_) < 0) {
        perror("socketpair");
        exit(-1);
    }
    SafeFdManager::getInstance().registerFd(pipefd_[0], "pipefd[0]");
    SafeFdManager::getInstance().registerFd(pipefd_[1], "pipefd[1]");
    EpollUtil::getInstance()->setNonblocking(pipefd_[1]);
    loop_->addEvent(pipefd_[0], EPOLLIN, [this](uint32_t events) { handleSignal(); });
    addsig(SIGALRM, [this](int) {
        int msg = SIGALRM;
        send(pipefd_[1], (char*)&msg, 1, 0);
    }, false);
    addsig(SIGTERM, [this](int) {
        int msg = SIGTERM;
        send(pipefd_[1], (char*)&msg, 1, 0);
    }, false);
    alarm(TIMESLOT);
}

void Server::setupEvents() {
    loop_->addEvent(listen_fd_, EPOLLIN, [this](uint32_t events) { handleAccept(); });
}

void Server::handleAccept() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (true) {
        total_conns_++;
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("accept error!");
            refused_conns_++;
            continue;
        }

        if (HttpConn::user_count >= MAX_FD) {
            show_error(client_fd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            SafeFdManager::getInstance().safeClose(client_fd);  // 安全关闭
            refused_conns_++;
            continue;
        }

        if (total_conns_ % 1000 == 0) {
            LOG_INFO("Total connections reached: %d, refused: %d", total_conns_, refused_conns_);
        }

        SafeFdManager::getInstance().registerFd(client_fd, "client_conn");
        EpollUtil::getInstance()->setNonblocking(client_fd);

        EventLoop* ioLoop = thread_pool_->getNextLoop();
        ioLoop->runInLoop([this, client_fd, client_addr, ioLoop]() {
            // 在工作线程的 EventLoop 中注册事件
            users_[client_fd].init(client_fd, client_addr, ioLoop);
            timer_lst_.add_timer(client_fd, 3, [this](int fd) { cb_func(fd); });
            ioLoop->addEvent(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLERR, 
                [this, client_fd](uint32_t events) {
                    if (events & (EPOLLRDHUP | EPOLLERR)) {
                        LOG_INFO("rdhup or err, delete timer, fd: %d", client_fd);
                        timer_lst_.del_timer(client_fd);
                    } else {
                        handleClientEvent(client_fd);
                    }
                });
        });
    }
}

void Server::handleClientEvent(int client_fd) {
    HttpConn* conn = &users_[client_fd];
    if (conn->read()) {
        conn->process();
        switch (conn->write()) {
            case HttpConn::WRITE_RES::WRITE_END:
            case HttpConn::WRITE_RES::WRITE_LINGER:
                // 在连接所属的 EventLoop 中修改事件
                conn->getLoop()->runInLoop([conn]() {
                    conn->getLoop()->modifyEvent(conn->getFd(), EPOLLIN | EPOLLRDHUP | EPOLLERR);
                });
                timer_lst_.adjust_timer(client_fd, 3);
                break;
            case HttpConn::WRITE_RES::WRITE_AGAIN:
                conn->getLoop()->runInLoop([conn]() {
                    conn->getLoop()->modifyEvent(conn->getFd(), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR);
                });
                break;
            case HttpConn::WRITE_RES::WRITE_CLOSED:
            case HttpConn::WRITE_RES::WRITE_FAILED:
                LOG_INFO("write closed/failed, delete timer, fd: %d", client_fd);
                conn->getLoop()->runInLoop([conn]() {
                    conn->getLoop()->removeEvent(conn->getFd());
                });
                timer_lst_.del_timer(client_fd);
                break;
            default:
                break;
        }
    } else {
        LOG_INFO("read failed, delete timer, fd: %d", client_fd);
        conn->getLoop()->runInLoop([conn]() {
            conn->getLoop()->removeEvent(conn->getFd());
        });
        timer_lst_.del_timer(client_fd);
    }
}

void Server::handleSignal() {
    char signals[1024];
    int ret = recv(pipefd_[0], signals, sizeof(signals), 0);
    if (ret <= 0) return;

    for (int i = 0; i < ret; ++i) {
        switch (signals[i]) {
            case SIGALRM:
                timer_lst_.tick();
                alarm(TIMESLOT);
                break;
            case SIGTERM:
                loop_->quit();
                break;
        }
    }
}

void Server::cb_func(int fd) {
    loop_->removeEvent(fd); // 内部调用safeclose fd
    LOG_INFO("timer close fd: %d", fd);
    SafeFdManager::getInstance().safeClose(fd);
    HttpConn::user_count--;
}

void signalHandler(int sig, siginfo_t* info, void* context) {
    auto it = signal_handlers.find(sig);
    if (it != signal_handlers.end()) {
        it->second(sig);
    }
}

void Server::addsig(int sig, std::function<void(int)> handler, bool restart) {
    signal_handlers[sig] = handler;

    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_sigaction = signalHandler;
    sa.sa_flags |= SA_SIGINFO;
    if (restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

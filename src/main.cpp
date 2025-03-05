#include "header.h"
#include "thread_pool.h"
#include "http_conn.h"
#include "epoll_util.h"
#include "lst_timer.hpp"
#include "log.h"

#include <fstream>
#include <cassert>
#include <errno.h>
#include <sys/epoll.h>
#include <sstream>
// #define LISTEN_LT
#define SYNLOG


#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5
#define HTTP_THREAD_NUM 16

// 创建管道处理错误信号
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epoll_fd = 0;

struct EventContext {
    int sock_fd;
    HttpConn* conn;  // 直接引用 users[sock_fd]，避免拷贝
    ThreadPool* pool;
};

// 定时器回调函数，每5s触发一次
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数
void cb_func(int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    LOG_INFO("timer close fd: %d", fd);
    close(fd);
    HttpConn::user_count--;
}

void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// send与recv在主线程中完成

int main() {
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); //同步日志模型
#endif    
    addsig(SIGPIPE, SIG_IGN);
    ThreadPool pool(HTTP_THREAD_NUM);

    // 创建列表
    HttpConn* users = new HttpConn[MAX_FD];
    assert(users != nullptr);

    // 创建socket
    int fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("make socket error!");
        return -1;
    }
    // 绑定端口，设置端口重用
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind error!");
        return -1;
    }

    // 监听端口号
    if (listen(fd, 10) < 0) {
        perror("listen error!");
        return -1;
    }

    // 创建epoll实例
    struct epoll_event events[MAX_EVENT_NUMBER]; // from 10 to 10000
    epoll_fd = epoll_create(3);
    if (-1 == epoll_fd) {
        perror("epoll_create");
        return -1;
    }

    EpollUtil::getInstance()->addFd(epoll_fd, fd, false);

    // 存储epoll_fd
    HttpConn::epoll_fd = epoll_fd;

    // 创建管道处理错误信息
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd) < 0) {
        perror("socketpair");
        return -1;
    }
    EpollUtil::getInstance()->setNonblocking(pipefd[1]);
    // 监听管道读端
    EpollUtil::getInstance()->addFd(epoll_fd, pipefd[0], false);
    // 注册信号处理函数
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    bool stop_server = false;

    bool timeout = false;
    alarm(TIMESLOT);

    while (!stop_server) {
        int ret = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }
        if (ret == 0) {
            continue;
        }
        
        for (int i = 0; i < ret; ++i) {
            // event事件fd等于监听fd，新连接
            int sock_fd = events[i].data.fd;
            
            if (sock_fd == fd) {
                LOG_INFO("sock_fd && listen_fd: %d", sock_fd);
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
#ifdef LISTEN_ET
                while (true) {
                    int client_fd = accept(fd, (struct sockaddr*)&client_addr, &client_addr_len);
                    if (client_fd < 0) {
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept error!");
                        break;
                    }

                    // 最大连接数，拒绝连接，策略为放弃新进连接
                    if (HttpConn::user_count >= MAX_FD) {

                        show_error(client_fd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }


                    users[client_fd].init(client_fd, client_addr);
                    timer_lst.add_timer(client_fd, 3, cb_func);
                }
                continue;
#endif
#ifdef LISTEN_LT
                int client_fd = accept(fd, (struct sockaddr*) &client_addr, &client_addr_len);
                if (client_fd < 0) {
                    perror("accept error!");
                    break;
                }
                if (HttpConn::user_count >= MAX_FD) {
                    show_error(client_fd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[client_fd].init(client_fd, client_addr);
                timer_lst.add_timer(client_fd, 3, cb_func);
#endif
            }

            // 中断连接
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                LOG_INFO("sock_fd && error_fd: %d", sock_fd);
                timer_lst.del_timer(sock_fd);
            }
            // 定时器信号处理
            else if ((sock_fd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                LOG_INFO("sock_fd && signal_fd: %d", sock_fd);
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                }
                for (int i = 0; i < ret; ++i) {
                    switch (signals[i]) {
                        case SIGALRM: {
                            timeout = true;
                            break;
                        }
                        case SIGTERM: {
                            stop_server = true;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                }
            }

            // 读事件
            else if (events[i].events & EPOLLIN) {
                // TODO change it to threadpool

                LOG_INFO("sock_fd && epoll in: %d", sock_fd);
                if (users[sock_fd].readOnce()) {
                    auto context = std::make_shared<EventContext>();
                    context->sock_fd = sock_fd;
                    context->conn = &users[sock_fd];  // 直接传递引用，避免拷贝
                    context->pool = &pool;
                    pool.append([](std::shared_ptr<void> args) {
                        auto ctx = std::static_pointer_cast<EventContext> (args);
                        ctx->conn->process();
                    }, context);
                    // pool.append(users + sock_fd); // append需要是非阻塞接口
                    timer_lst.adjust_timer(sock_fd, 3);
                } else {
                    timer_lst.del_timer(sock_fd);
                }
            } else if (events[i].events & EPOLLOUT) {
                LOG_INFO("sock_fd && epoll out: %d", sock_fd);
                if (!users[sock_fd].write()) {
                    LOG_INFO("need close connect");
                    timer_lst.del_timer(sock_fd);
                }
                LOG_INFO("send data to client");
            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(fd);
    close(epoll_fd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users;
    return 0;

} 
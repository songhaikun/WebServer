#include "header.h"
#include "thread_pool.h"
#include "http_conn.h"
#include "epoll_util.h"

#include <cassert>
#include <errno.h>
#include <sys/epoll.h>


#define MAX_FD 65536

// 创建管道处理错误信号
static int pipefd[2];

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

// send与recv在主线程中完成

int main() {
    
    std::cout << "Hello, webServer!" << std::endl;
    // 终止信号处理
    signal(SIGPIPE, SIG_IGN);
    
    
    // 创建socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
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
    int epoll_fd = epoll_create(3);
    if (-1 == epoll_fd) {
        perror("epoll_create");
        return -1;
    }

    EpollUtil::getInstance()->addFd(epoll_fd, fd, false);

    // 存储epoll_fd
    HttpConn::epoll_fd = epoll_fd;
    std::cout << "epoll_fd: " << epoll_fd << std::endl;

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

    // 创建连接列表
    HttpConn* users = new HttpConn[MAX_FD];
    assert(users != nullptr);

    ThreadPool<HttpConn> pool(8);
    bool stop_server = false;

    while (!stop_server) {
        struct epoll_event events[10];
        int ret = epoll_wait(epoll_fd, events, 10, -1);
        if (ret < 0) {
            perror("epoll_wait");
            return -1;
        }
        for (int i = 0; i < ret; ++i) {
            // event事件fd等于监听fd，新连接
            int sock_fd = events[i].data.fd;
            if (sock_fd == fd) {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
#ifdef LISTEN_ET
                while (true) {
                    int client_fd = accept(fd, (struct sockaddr*)&client_addr, &client_addr_len);
                    if (client_fd < 0) {
                        perror("accept error!");
                        break;
                    }

                    // 最大连接数，拒绝连接，策略为放弃新进连接
                    if (HttpConn::user_count >= MAX_FD) {
                        std::cout << "Internal server busy, link num out of bound" << std::endl;
                        break;
                    }


                    std::cout << "--------------------------accept a new client-----------------" << std::endl;
                    std::cout << "client_fd: " << client_fd << std::endl;
                    users[client_fd].init(client_fd, addr);
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
                    std::cout << "Internal server busy, link num out of bound" << std::endl;
                    break;
                }
                std::cout << "--------------------------accept a new client-----------------" << std::endl;
                std::cout << "client_fd: " << client_fd << std::endl;
                users[client_fd].init(client_fd, addr);
#endif
            }

            // 中断连接
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // TODO change to time list
                std::cout << "close the client(" << inet_ntoa(users[sock_fd].get_address()->sin_addr) << ")" << std::endl;
                users[sock_fd].closeConn();
            }
            // 定时器信号处理
            else if ((sock_fd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                }
                for (int i = 0; i < ret; ++i) {
                    switch (signals[i]) {
                        case SIGALRM: {
                            // TODO change to time list
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
                if (users[sock_fd].readOnce()) {
                    std::cout << "deal with the client(" << inet_ntoa(users[sock_fd].get_address()->sin_addr) << ")" << std::endl;
                    pool.append(users + sock_fd);
                } else {
                    std::cout << "read error, readOnce() return false" << std::endl;
                    users[sock_fd].closeConn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sock_fd].write()) {
                    std::cout << "write error, write() return false" << std::endl;
                    users[sock_fd].closeConn();
                }

            }
            
            
        }


    }
    close(fd);
    close(epoll_fd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users;
    return 0;

}
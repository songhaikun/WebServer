#pragma once
#include "event_loop.h"
#include "thread_pool.h"
#include "event_loop_thread_pool.h"
#include "log.h"
#include "epoll_util.h"
#include "http_conn.h"
#include "thread_pool.h"

#include <functional>
#include <memory>
#include <signal.h>
#include <unordered_map>

#define MAX_FD 65536
#define TIMESLOT 5
#define HTTP_THREAD_NUM 16

static void show_error(int connfd, const char* info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

struct EventContext {
    int sock_fd;
    HttpConn* conn;  // 直接引用 users[sock_fd]，避免拷贝
    ThreadPool* pool;
};


static void signalHandler(int sig, siginfo_t* info, void* context);

extern std::unordered_map<int, std::function<void(int)>> signal_handlers;

class Server {
public:
    Server(EventLoop* loop, int port, int thread_num);   
    ~Server();
    void start();

private:
    void initSocket();
    void initSignal();
    void setupEvents();
    void handleAccept();
    void handleClientEvent(int client_fd);
    void handleSignal();
    void cb_func(int fd);
    static void addsig(int sig, std::function<void(int)> handler, bool restart = true);
private:
    EventLoop* loop_;
    int port_;
    int thread_num_;
    int listen_fd_;
    int pipefd_[2] = {-1, -1};
    HttpConn* users_;
    // ThreadPool pool_;
    SortTimerList timer_lst_;
    std::unique_ptr<EventLoopThreadPool> thread_pool_;
    int total_conns_{0};
    int refused_conns_{0};
};

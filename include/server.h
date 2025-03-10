#pragma once
#include "event_loop.h"
#include "log.h"
#include "epoll_util.h"
#include "http_conn.h"
#include "thread_pool.h"

#include <cstring>
#include <iostream>
#include <signal.h>
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
#include <unordered_map>

#define MAX_FD 65536
#define TIMESLOT 5
#define HTTP_THREAD_NUM 32

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
        ThreadPool pool_;
        SortTimerList timer_lst_;
        int total_conns_{0};
        int refused_conns_{0};
        
    };
    

#pragma once
#include "lst_timer.hpp"

#include <functional>
#include <mutex>
#include <queue>
#include <sys/types.h>
#include <sys/epoll.h>
#include <thread>
class EventLoop {
public:
    using EventCallback = std::function<void(uint32_t events)>; // 修改回调签名，传递事件类型
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();
    void loop();
    void quit();
    void addEvent(int fd, uint32_t events, EventCallback cb);
    void removeEvent(int fd);
    void modifyEvent(int fd, uint32_t events);

     int getEpollFd() const;
     void runInLoop(Functor cb);
     bool isInLoopThread() const;

private:
    int epoll_fd_;
    bool quit_;
    std::mutex mutex_;
    std::unordered_map<int, EventCallback> callbacks_;
    SortTimerList timer_list_;

    std::queue<Functor> pendingFunctors_;
    std::mutex functorMutex_; // 保护任务队列
    std::thread::id threadId_; // 记录所属线程 ID

    int pipe_fd_[2];

    void doPendingFunctors();
    void handleEvents(const std::vector<epoll_event>& events, int num_events);
    void handleRead(int fd);
    void handleWrite(int fd);
    void handleSignal();

    int setNonblocking(int fd);
};
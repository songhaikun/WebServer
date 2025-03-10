#pragma once
#include "lst_timer.hpp"

#include <functional>
#include <sys/types.h>
#include <sys/epoll.h>
#include <mutex>
class EventLoop {
public:
    using EventCallback = std::function<void(uint32_t events)>; // 修改回调签名，传递事件类型
    EventLoop();
    ~EventLoop();
    void loop();
    void quit();
    void addEvent(int fd, uint32_t events, EventCallback cb);
    void removeEvent(int fd);
    void modifyEvent(int fd, uint32_t events);
    void handleSignal();

private:
    int epoll_fd_;
    bool quit_;
    std::mutex mutex_;
    std::unordered_map<int, EventCallback> callbacks_;
    SortTimerList timer_list_;
    int pipe_fd_[2];

    void handleEvents(const std::vector<epoll_event>& events, int num_events);
    void handleRead(int fd);
    void handleWrite(int fd);
    int setNonblocking(int fd);
};
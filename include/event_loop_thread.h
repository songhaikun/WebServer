#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include "event_loop.h"

class EventLoopThread {
public:
    EventLoopThread();
    ~EventLoopThread();
    // 使用了cond，当线程中eventloop创建成功后才返回
    EventLoop* startLoop();
private:
    void threadFunc();
    EventLoop* loop_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool exiting_;
};

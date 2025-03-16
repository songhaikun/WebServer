#pragma once

#include <vector>
#include <memory>
#include "event_loop_thread.h"

class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* baseLoop, int numThreads);
    ~EventLoopThreadPool();
    void start();
    EventLoop* getNextLoop(); // 轮询分发新连接
private:
    EventLoop* baseLoop_; // TODO change to smart point
    int numThreads_;
    std::vector<std::unique_ptr<EventLoopThread> > threads_; // 工作线程列表
    std::vector<EventLoop*> loops_; // 工作线程eventloop列表
    int next_; // 轮询索引
};
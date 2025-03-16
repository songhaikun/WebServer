#include "event_loop_thread_pool.h"
#include "log.h"
#include <iostream>

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, int numThreads)
    : baseLoop_(baseLoop), numThreads_(numThreads), next_(0) {
    if (numThreads <= 0) {
        LOG_ERROR("numThreads equal to 0.");
        exit(-1);
    }
}

EventLoopThreadPool::~EventLoopThreadPool() {
    // threads_ 的析构会自动销毁 EventLoopThread，调用其析构函数
}

void EventLoopThreadPool::start() {
    for (int i = 0; i < numThreads_; ++i) {
        threads_.push_back(std::make_unique<EventLoopThread>());
        loops_.push_back(threads_.back()->startLoop());
    }
}


EventLoop* EventLoopThreadPool::getNextLoop() {
    if (loops_.empty()) {
        return baseLoop_; // 若无工作线程，返回主线程的 EventLoop
    }
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % numThreads_; // 轮询分发
    return loop;
}
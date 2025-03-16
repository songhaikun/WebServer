#include "event_loop.h"
#include "epoll_util.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cassert>
EventLoop::EventLoop()
    : epoll_fd_(-1), quit_(false), timer_list_() {
    // 创建 epoll 实例
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    assert(epoll_fd_ >= 0);

    // 创建管道用于信号处理和唤醒
    if (pipe(pipe_fd_) < 0) {
        close(epoll_fd_);
        std::cout << "pipe failed" << std::endl;
    }
    setNonblocking(pipe_fd_[0]);
    setNonblocking(pipe_fd_[1]);

    // 将管道读端添加到 epoll
    addEvent(pipe_fd_[0], EPOLLIN, [this](uint32_t events) { handleSignal(); });
}

EventLoop::~EventLoop() {
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
    if (pipe_fd_[0] >= 0) {
        close(pipe_fd_[0]);
    }
    if (pipe_fd_[1] >= 0) {
        close(pipe_fd_[1]);
    }
}

void EventLoop::loop() {
    const int MAX_EVENTS = 10000;
    std::vector<epoll_event> events(MAX_EVENTS);
    quit_ = false;
    while (!quit_) {
        int num_events = epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, -1);
        if (num_events < 0) {
            if (errno == EINTR) continue;
            std::cout << "epoll_wait return failed!" << std::endl;
            break;
        }
        handleEvents(events, num_events);
        doPendingFunctors(); // 处理任务队列
    }
}

void EventLoop::quit() {
    quit_ = true;
    // 写入管道以唤醒 epoll_wait
    if (!isInLoopThread()) {
        char buf = 'q';
        write(pipe_fd_[1], &buf, 1);
    }
}

void EventLoop::addEvent(int fd, uint32_t events, EventCallback cb) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            // 如果已经存在，则修改
            modifyEvent(fd, events);
        } 
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[fd] = std::move(cb);
    }
}

void EventLoop::removeEvent(int fd) {
    EpollUtil::getInstance()->removeFd(epoll_fd_, fd);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.erase(fd);
    }
}

void EventLoop::modifyEvent(int fd, uint32_t events) {
    EpollUtil::getInstance()->modFd(epoll_fd_, fd, events);
}


void EventLoop::handleSignal() {
    char buf[1024];
    ssize_t n = read(pipe_fd_[0], buf, sizeof(buf));
    if (n <= 0) return;

    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == 'q') {
            quit_ = true;
        } else if (buf[i] == 't') {
            // 任务通知，处理任务队列
            doPendingFunctors();
        }
    }
}

void EventLoop::handleEvents(const std::vector<epoll_event>& events, int num_events) {
    for (int i = 0; i < num_events; ++i) {
        int fd = events[i].data.fd;
        auto it = callbacks_.find(fd);
        if (it != callbacks_.end()) {
            // 检查 std::function 是否为空
            if (it->second) {
                it->second(events[i].events); // 调用回调
            } else {
                // 回调为空，移除事件
                removeEvent(fd);
            }
        } else {
            // 未找到回调，可能 fd 已移除但事件仍触发，清理
            // struct epoll_event ev = {0};
            // epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev);
            EpollUtil::getInstance()->removeFd(epoll_fd_, fd);
        }
    }
}

int EventLoop::setNonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        return -1;
    }
    return 0;
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        // 如果已在所属线程，直接执行
        cb();
    } else {
        // 否则加入任务队列并唤醒
        {
            std::lock_guard<std::mutex> lock(functorMutex_);
            pendingFunctors_.push(std::move(cb));
        }
        // 写入管道以唤醒 epoll_wait
        char buf = 't'; // 't' 表示任务通知
        write(pipe_fd_[1], &buf, 1);
    }
}

void EventLoop::doPendingFunctors() {
    std::queue<Functor> functors;
    {
        std::lock_guard<std::mutex> lock(functorMutex_);
        std::swap(functors, pendingFunctors_); // 交换队列以减少锁持有时间
    }

    while (!functors.empty()) {
        Functor cb = std::move(functors.front());
        functors.pop();
        cb();
    }
}

int EventLoop::getEpollFd() const {
    return epoll_fd_;
}

bool EventLoop::isInLoopThread() const {
    return threadId_ == std::this_thread::get_id(); 
}
#pragma once
#include <thread>
#include <list>
#include <mutex>
#include <semaphore.h>
#include <vector>
#include <iostream>
#include <functional>
#include <memory>

struct ThreadPoolTask {
    std::function<void(std::shared_ptr<void>)> fun;
    std::shared_ptr<void> args;
};

class ThreadPool {
private:
    int threadNum;
    std::vector<std::thread> threads;
    std::list<ThreadPoolTask> tasks;  // 任务队列存储 ThreadPoolTask
    std::mutex pool_mutex;
    sem_t task_sem;
    int max_request;
    bool stop{false};

public:
    explicit ThreadPool(int threadNum = 8);
    ~ThreadPool();
    void append(std::function<void(std::shared_ptr<void>)> fun, std::shared_ptr<void> args);
};

ThreadPool::ThreadPool(int threadNum) : threadNum(threadNum), max_request(10000) {
    if (sem_init(&task_sem, 0, 0) != 0) {
        throw std::runtime_error("sem_init failed");
    }

    for (int i = 0; i < threadNum; ++i) {
        auto thr = std::thread([this]() {
            while (!stop) {
                sem_wait(&task_sem);
                ThreadPoolTask task;
                {
                    std::unique_lock<std::mutex> lock(this->pool_mutex);
                    if (tasks.empty()) {
                        continue; // 理论上不应发生，除非 stop 设置后队列清空
                    }
                    task = tasks.front();
                    tasks.pop_front();
                }
                if (task.fun) {
                    task.fun(task.args);  // 执行任务函数
                }
            }
        });
        thr.detach();
        threads.emplace_back(std::move(thr));
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        stop = true;
    }
    for (int i = 0; i < threadNum; ++i) {
        sem_post(&task_sem);
    }
    for (auto& thr : threads) {
        if (thr.joinable()) {
            thr.join();
        }
    }
    sem_destroy(&task_sem);
}

void ThreadPool::append(std::function<void(std::shared_ptr<void>)> fun, std::shared_ptr<void> args) {
    std::unique_lock<std::mutex> lock(pool_mutex, std::defer_lock_t());
    if (lock.try_lock()) {
        if (tasks.size() < max_request) {
            tasks.push_back({fun, args});  // 构造 ThreadPoolTask 并存入队列
            sem_post(&task_sem);
        }
        lock.unlock();
    }
    // 如果加锁失败或队列满，丢弃任务
}
#pragma once
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <memory>
#include <iostream>


#include "log.h"
struct ThreadPoolTask {
    std::function<void(std::shared_ptr<void>)> fun;
    std::shared_ptr<void> args;
};

class ThreadPool {
private:
    int threadNum;
    std::vector<std::thread> threads;
    std::deque<ThreadPoolTask> tasks; // 使用 deque
    mutable std::mutex pool_mutex;
    std::condition_variable cond;
    int max_request;
    bool stop{false};
    int idle_threads{0}; // 空闲线程计数，用于优化唤醒

public:
    explicit ThreadPool(int threadNum = std::thread::hardware_concurrency() * 2, int max_request = 65535)
        : threadNum(threadNum), max_request(max_request), idle_threads(threadNum) {
        if (max_request <= 0) {
            throw std::invalid_argument("max_request must be positive");
        }
        for (int i = 0; i < threadNum; ++i) {
            threads.emplace_back([this] {
                while (true) {
                    ThreadPoolTask task;
                    {
                        std::unique_lock<std::mutex> lock(pool_mutex);
                        // 等待条件：停止或有任务且当前线程可以处理
                        cond.wait(lock, [this] { 
                            return stop || (!tasks.empty() && idle_threads > 0); 
                        });
                        if (stop && tasks.empty()) return;

                        // 减少空闲线程计数，确保只有一个线程处理
                        --idle_threads;
                        task = std::move(tasks.front());
                        tasks.pop_front();
                    }
                    // 执行任务
                    task.fun(task.args);
                    {
                        std::lock_guard<std::mutex> lock(pool_mutex);
                        ++idle_threads; // 任务完成，恢复空闲状态
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(pool_mutex);
            stop = true;
        }
        cond.notify_all(); // 唤醒所有线程以退出
        for (auto& thread : threads) {
            thread.join();
        }
    }

    bool append(std::function<void(std::shared_ptr<void>)> fun, std::shared_ptr<void> args) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        if (stop) return false;
        if (tasks.size() >= max_request) {
            LOG_WARN("ThreadPool task queue full, dropping task. Current size: %zu", tasks.size());
            return false;
        }
        tasks.push_back({std::move(fun), args});
        cond.notify_all(); // 唤醒所有线程，但只有符合条件的执行
        return true;
    }

    size_t queueSize() const {
        std::lock_guard<std::mutex> lock(pool_mutex);
        return tasks.size();
    }

    int idleThreads() const {
        std::lock_guard<std::mutex> lock(pool_mutex);
        return idle_threads;
    }
};
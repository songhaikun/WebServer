#pragma once
#include <thread>
#include <list>
#include <mutex>
#include <semaphore.h>
#include <vector>
#include <iostream>
#include <chrono>
#include <iomanip>

template <typename T>
class ThreadPool {
private:
    int threadNum;
    std::vector<std::thread> threads;
    std::list<T*> tasks;
    std::mutex pool_mutex;
    sem_t task_sem;
    int max_request;
    bool stop{false};

public:
    explicit ThreadPool(int threadNum = 8);
    ~ThreadPool();
    void append(T* task);
};

template <typename T>
ThreadPool<T>::ThreadPool(int threadNum) : threadNum(threadNum), max_request(10000) {
    // 初始化信号量，初始值为 0（无任务），共享于线程间
    if (sem_init(&task_sem, 0, 0) != 0) {
        throw std::runtime_error("sem_init failed");
    }

    for (int i = 0; i < threadNum; ++i) {
        auto thr = std::thread([this]() {
            T* task;
            while (!stop) {
                // 等待信号量，表示有任务可用
                sem_wait(&task_sem);
                {
                    std::unique_lock<std::mutex> lock(this->pool_mutex);
                    if (tasks.empty()) {
                        continue; // 理论上不应发生，除非 stop 设置后队列清空
                    }
                    task = this->tasks.front();
                    this->tasks.pop_front();
                }
                if (task) {
                    task->process();
                }
            }
        });
        thr.detach();
        threads.emplace_back(std::move(thr));
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        stop = true;
    }
    // 释放信号量以唤醒所有线程并退出
    for (int i = 0; i < threadNum; ++i) {
        sem_post(&task_sem);
    }
    for (auto& thr : threads) {
        if (thr.joinable()) {
            thr.join();
        }
    }
    sem_destroy(&task_sem); // 销毁信号量
}

template <typename T>
void ThreadPool<T>::append(T* task) {
    std::unique_lock<std::mutex> lock(pool_mutex, std::defer_lock_t());
    if (lock.try_lock()) { // 尝试加锁
        if (tasks.size() < max_request) {
            tasks.push_back(task);
            sem_post(&task_sem); // 增加信号量，通知线程
        }
        lock.unlock();
    }
    // 如果加锁失败或队列满，丢弃任务
}

#pragma once
// 搭建线程池

#include <thread>
#include <functional>
#include <list>
#include <mutex>
#include <condition_variable>

// typedef std::function<void(void)> Task;
template <typename T>
class ThreadPool {
// 有哪些功能？ 1初始化 2增加任务 3销毁
private:
    // 线程数
    int threadNum;
    // 线程数组存放实体线程
    std::vector<std::thread> threads;
    // 任务队列
    std::list<T *> tasks;
    // 互斥锁
    std::mutex pool_mutex;
    // 条件变量
    std::condition_variable cond;
    // 是否结束
    bool stop{false};

    // T t;

public:
    explicit ThreadPool(int threadNum = 8);
    ~ThreadPool();

    void append(T *task);

};

template <typename T>
ThreadPool<T>::ThreadPool(int threadNum) : threadNum(threadNum) {
    
    for(int i = 0; i < threadNum; ++i) {
        auto thr = std::thread([this]() {
            T* task;
            while(!stop) {
                {
                    std::unique_lock<std::mutex> lock(this->pool_mutex);
                    this->cond.wait(lock, [this]() {
                        return !this->tasks.empty();
                    });
                    task = this->tasks.front();
                    this->tasks.pop_front();
                }

                if (nullptr == task) {
                    continue;
                }
                task->process();
            }
        });
        // thr.detach();
        threads.emplace_back(std::move(thr));
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    stop = true;
    for (auto& thr : threads) {
        if (thr.joinable()) {
            thr.join();
        }
    }
}

template <typename T>
void ThreadPool<T>::append(T* task) {
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        tasks.push_back(task);
    }
    cond.notify_one();
}
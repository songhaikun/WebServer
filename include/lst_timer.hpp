#ifndef LST_TIMER_H
#define LST_TIMER_H

#include "timer_base.h"
#include <unordered_map>
#include <mutex>  // 添加 mutex 头文件

struct TimerNode {
    int fd;                            // 文件描述符
    time_t expire;                     // 超时时间
    std::function<void(int)> callback; // 回调函数
    TimerNode* prev;                  // 前驱指针
    TimerNode* next;                  // 后继指针
};

class SortTimerList : public TimerBase {
public:
    SortTimerList(time_t unit = 5) : head(nullptr), tail(nullptr), timeout_unit(unit) {}
    ~SortTimerList() {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护析构
        TimerNode* tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(int fd, time_t timeout_sec, std::function<void(int)> callback) override {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁
        TimerNode* timer = new TimerNode{fd, time(nullptr) + timeout_sec * timeout_unit, callback, nullptr, nullptr};
        nodes[fd] = timer; // 记录 fd 到 timer 的映射
        if (!head) {
            head = tail = timer;
        } else if (timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
        } else {
            add_timer_internal(timer, head);
        }
    }

    void adjust_timer(int fd, time_t timeout_sec) override {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁
        auto it = nodes.find(fd);
        if (it == nodes.end()) return;
        TimerNode* timer = it->second;
        timer->expire = time(nullptr) + timeout_sec * timeout_unit;
        if (timer == head) {
            if (!timer->next || timer->expire <= timer->next->expire) return;
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer_internal(timer, head);
        } else if (timer->next && timer->expire > timer->next->expire) {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer_internal(timer, timer->next);
        }
    }

    void del_timer(int fd) override {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁
        auto it = nodes.find(fd);
        if (it == nodes.end()) return;
        TimerNode* timer = it->second;
        timer->callback(fd); // 调用回调关闭连接
        nodes.erase(it);
        if (timer == head && timer == tail) {
            delete timer;
            head = tail = nullptr;
        } else if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            delete timer;
        } else if (timer == tail) {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
        } else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }
    }

    void tick() override {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁
        if (!head) return;
        time_t cur = time(nullptr);
        while (head && head->expire <= cur) {
            TimerNode* tmp = head;
            tmp->callback(tmp->fd);
            nodes.erase(tmp->fd);
            head = tmp->next;
            if (head) head->prev = nullptr;
            delete tmp;
        }
    }

    void set_timeout_unit(time_t unit) override {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁
        timeout_unit = unit;
    }

private:
    void add_timer_internal(TimerNode* timer, TimerNode* lst_head) {
        TimerNode* prev = lst_head;
        TimerNode* tmp = prev->next;
        while (tmp) {
            if (timer->expire < tmp->expire) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (!tmp) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = nullptr;
            tail = timer;
        }
    }

private:
    TimerNode* head;
    TimerNode* tail;
    std::unordered_map<int, TimerNode*> nodes; // fd 到 timer 的映射
    time_t timeout_unit;                        // 超时单位
    std::mutex mutex_;                          // 互斥锁，用于线程安全
};

#endif
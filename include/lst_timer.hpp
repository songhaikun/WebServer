#ifndef LST_TIMER_H
#define LST_TIMER_H

#include "timer_base.h"
#include <unordered_map>

struct timer_node {
    int fd;                            // 文件描述符
    time_t expire;                     // 超时时间
    std::function<void(int)> callback; // 回调函数
    timer_node* prev;                  // 前驱指针
    timer_node* next;                  // 后继指针
};

class sort_timer_lst : public TimerBase {
public:
    sort_timer_lst(time_t unit = 5) : head(nullptr), tail(nullptr), timeout_unit(unit) {}
    ~sort_timer_lst() {
        timer_node* tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(int fd, time_t timeout_sec, std::function<void(int)> callback) override {
        timer_node* timer = new timer_node{fd, time(nullptr) + timeout_sec * timeout_unit, callback, nullptr, nullptr};
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
        auto it = nodes.find(fd);
        if (it == nodes.end()) return;
        timer_node* timer = it->second;
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
        auto it = nodes.find(fd);
        if (it == nodes.end()) return;
        timer_node* timer = it->second;
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
        if (!head) return;
        // LOG_INFO("%s", "timer tick");
        // Log::get_instance()->flush();
        time_t cur = time(nullptr);
        while (head && head->expire <= cur) {
            // std::cout << "link expired" << std::endl;
            timer_node* tmp = head;
            tmp->callback(tmp->fd);
            nodes.erase(tmp->fd);
            head = tmp->next;
            if (head) head->prev = nullptr;
            delete tmp;
        }
    }

    void set_timeout_unit(time_t unit) override {
        timeout_unit = unit;
    }

private:
    void add_timer_internal(timer_node* timer, timer_node* lst_head) {
        timer_node* prev = lst_head;
        timer_node* tmp = prev->next;
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
    timer_node* head;
    timer_node* tail;
    std::unordered_map<int, timer_node*> nodes; // fd 到 timer 的映射
    time_t timeout_unit;                        // 超时单位
};

#endif
#pragma once

#include <time.h>
#include <functional>

class TimerBase {
public:
    virtual ~TimerBase() = default;

    // 添加定时器，超时时间和回调函数可自定义
    virtual void add_timer(int fd, time_t timeout_sec, std::function<void(int)> callback) = 0;

    // 调整定时器的超时时间
    virtual void adjust_timer(int fd, time_t timeout_sec) = 0;

    // 删除定时器
    virtual void del_timer(int fd) = 0;

    // 检查并处理超时定时器
    virtual void tick() = 0;

    // 设置超时单位（可选）
    virtual void set_timeout_unit(time_t unit) = 0;
};
#pragma once

#include <iostream>
#include <stdlib.h>
#include <sys/time.h>
#include <mutex>
#include <condition_variable>
#include <vector>

template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
    {
        if (max_size <= 0)
        {
            exit(-1);
        }
        m_max_size = max_size;
        m_array.resize(max_size);
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue() = default;

    bool full()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_size >= m_max_size;
    }

    bool empty()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_size == 0;
    }

    bool front(T &value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_size == 0)
        {
            return false;
        }
        value = m_array[m_front];
        return true;
    }

    bool back(T &value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_size == 0)
        {
            return false;
        }
        value = m_array[m_back];
        return true;
    }

    int size()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_size;
    }

    int max_size()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_max_size;
    }

    bool push(const T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_size >= m_max_size)
        {
            m_cond.notify_all();  // 直接唤醒可能的等待者（即便满了也通知，兼容老逻辑）
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_cond.notify_all();
        return true;
    }

    bool pop(T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_size <= 0)
        {
            m_cond.wait(lock);
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        return true;
    }

    // 增加超时版本的pop
    bool pop(T &item, int ms_timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_size <= 0)
        {
            // 计算超时时间点
            auto now = std::chrono::system_clock::now();
            auto timeout = now + std::chrono::milliseconds(ms_timeout);

            if (m_cond.wait_until(lock, timeout) == std::cv_status::timeout && m_size <= 0)
            {
                return false;  // 超时且队列还空
            }
        }

        if (m_size <= 0)
        {
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        return true;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::vector<T> m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

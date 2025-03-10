#pragma once

#include "block_queue.h"
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

inline std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(::localtime(&time_t_now), "%F %T")
        << "." << std::setfill('0') << std::setw(3) << millis.count();
    return oss.str();
}

class Log
{
public:
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void flush_log_thread()
    {
        Log::get_instance()->async_write_log();
    }

    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush();

private:
    Log();
    virtual ~Log();

    void async_write_log();

private:
    char dir_name[128];
    char log_name[128];
    int m_split_lines;
    int m_log_buf_size;
    long long m_count;
    int m_today;
    FILE *m_fp;
    char *m_buf;
    block_queue<std::string> *m_log_queue;
    bool m_is_async;
    std::mutex m_mutex;
    std::thread m_log_thread;
};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#define LOG_INFO_T(format, ...) \
    do { \
        std::ostringstream oss; \
        oss << get_current_timestamp() << " " << format; \
        Log::get_instance()->write_log(1, oss.str().c_str(), ##__VA_ARGS__); \
    } while (0)

// TODO only for test, delete this
// #define LOG_INFO(format, ...) ;
// #define LOG_WARN(format, ...) ;
// #define LOG_ERROR(format, ...) ;
// #define LOG_INFO_T(format, ...) ;
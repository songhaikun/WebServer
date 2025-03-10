#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <thread>
#include <sstream>
#include "log.h"

Log::Log()
{
    m_count = 0;
    m_is_async = false;
    m_fp = nullptr;
    m_buf = nullptr;
    m_log_queue = nullptr;
}

Log::~Log()
{
    if (m_log_thread.joinable())
    {
        m_log_queue->push("Log thread exit"); // 强行触发线程退出（可选）
        m_log_thread.join();
    }
    if (m_fp != nullptr)
    {
        fclose(m_fp);
    }
    delete[] m_buf;
    delete m_log_queue;
}

bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    if (max_queue_size > 0)
    {
        m_is_async = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        m_log_thread = std::thread(flush_log_thread);  // 使用C++11标准线程
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == nullptr)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp == nullptr)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *level_str[] = {"[debug]:", "[info]:", "[warn]:", "[erro]:"};
    const char *s = (level >= 0 && level <= 3) ? level_str[level] : "[info]:";

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_count++;

        if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
        {
            char new_log[256] = {0};
            fflush(m_fp);
            fclose(m_fp);

            char tail[16] = {0};
            snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

            if (m_today != my_tm.tm_mday)
            {
                snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
                m_today = my_tm.tm_mday;
                m_count = 0;
            }
            else
            {
                snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
            }
            m_fp = fopen(new_log, "a");
        }
    }

    va_list valst;
    va_start(valst, format);
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    int m = vsnprintf(m_buf + n, m_log_buf_size - 1 - n, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';

    va_end(valst);

    std::string log_str = m_buf;

    if (m_is_async && m_log_queue != nullptr && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fputs(log_str.c_str(), m_fp);
    }
}

void Log::flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    fflush(m_fp);
}

void Log::async_write_log()
{
    std::string log_str;
    while (m_log_queue->pop(log_str))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fputs(log_str.c_str(), m_fp);
    }
}

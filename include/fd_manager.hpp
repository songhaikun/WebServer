#ifndef SAFE_FD_MANAGER_H
#define SAFE_FD_MANAGER_H
#include "log.h"
#include <unordered_map>
#include <mutex>
#include <cstdio>
#include <unistd.h>

class SafeFdManager {
public:
    // 获取单例
    static SafeFdManager& getInstance() {
        static SafeFdManager instance;
        return instance;
    }

    // 注册一个新的FD（通常是accept后）
    void registerFd(int fd, const char* tag = "unknown") {
        std::lock_guard<std::mutex> lock(mutex_);
        fd_map_[fd] = {false, tag};
        LOG_INFO("[SafeFdManager] Registered fd: %d (tag: %s)\n", fd, tag);
    }

    // 安全关闭FD，防止重复关闭
    void safeClose(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fd_map_.find(fd);
        if (it != fd_map_.end() && !it->second.closed) {
            close(fd);
            it->second.closed = true;
            LOG_INFO("[SafeFdManager] Closed fd: %d (tag: %s)\n", fd, it->second.tag.c_str());
        } else {
            LOG_WARN("[SafeFdManager] Warning: fd %d already closed or not registered\n", fd);
        }
    }

    // 移除FD记录（用于某些场合手动管理时）
    void removeFd(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        fd_map_.erase(fd);
        LOG_INFO("[SafeFdManager] Removed fd: %d\n", fd);
    }

    // 查询FD是否已经关闭
    bool isClosed(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fd_map_.find(fd);
        if (it != fd_map_.end()) {
            return it->second.closed;
        }
        return true;  // 未注册的fd当成已关闭
    }

private:
    struct FdInfo {
        bool closed;
        std::string tag;
    };

    SafeFdManager() = default;
    ~SafeFdManager() = default;

    std::unordered_map<int, FdInfo> fd_map_;
    std::mutex mutex_;
};

#endif // SAFE_FD_MANAGER_H

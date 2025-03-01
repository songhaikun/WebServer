#pragma once
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

class EpollUtil {
private:
    EpollUtil() = default;
    EpollUtil(const EpollUtil&) = delete;
    EpollUtil& operator=(const EpollUtil&) = delete;
public:

    static EpollUtil* getInstance();
    void addFd(int epollfd, int fd, bool one_shot);
    void removeFd(int epollfd, int fd);
    void modFd(int epollfd, int fd, int ev);
    int setNonblocking(int fd);
};
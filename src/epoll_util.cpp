#include "epoll_util.h"
#include "fd_manager.hpp"

EpollUtil* EpollUtil::getInstance() {
    static EpollUtil instance;
    return &instance;
}

void EpollUtil::addFd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
#ifdef LISTEN_ET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif
#ifdef LISTEN_LT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonblocking(fd);
}

void EpollUtil::removeFd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    // SafeFdManager::getInstance().safeClose(fd);  // 使用安全关闭
}

void EpollUtil::modFd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
#ifdef LISTEN_ET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif
#ifdef LISTEN_LT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int EpollUtil::setNonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
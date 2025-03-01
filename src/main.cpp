#include "header.h"
#include "thread_pool.h"
#include "http_conn.h"


// http解析总函数
int httpParsing() {
    //
    return 0;
}

// http请求行解析

// http请求头解析

// http请求体解析

int main() {
    
    std::cout << "Hello, webServer!" << std::endl;
    // 终止信号处理
    signal(SIGPIPE, SIG_IGN);
    
    
    // 创建socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("make socket error!");
        return -1;
    }

    // 绑定端口，设置端口重用
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind error!");
        return -1;
    }

    // 监听端口号
    if (listen(fd, 10) < 0) {
        perror("listen error!");
        return -1;
    }
    
    // TODO 创建线程池
    ThreadPool<HttpConn> pool(8);

    while(true) {

        int client_fd = accept(fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept error!");
            return -1;
        }
        std::cout << "--------------------------accept a new client-----------------" << std::endl;
        std::cout << "client_fd: " << client_fd << std::endl;
        pool.append(new HttpConn(client_fd));

    }
    close(fd);
    return 0;

}
#include "http_conn.h"

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <stdarg.h>
#ifdef __APPLE__
const char *html_root = "/Users/hksong/hksong/prjs/webServer/root";
#elif __linux__
const char *html_root = "/media/psf/Home/hksong/prjs/webServer/root";
#endif
// 辅助函数
std::string getTime() {
    time_t t = time(0);
    tm* local_time = localtime(&t);
    char time_str[128] = {0};
    strftime(time_str, 128, "%Y-%m-%d %H:%M:%S", local_time);
    return std::string(time_str);
}

HttpConn::HttpConn() {
    client_fd = -1;
    init();
    // memset(read_buffer, 0, sizeof(read_buffer));
}

HttpConn::HttpConn(int client_fd) : client_fd(client_fd) {
    memset(read_buffer, 0, sizeof(read_buffer));
}

void HttpConn::process() {
    std::cout << getTime() << std::endl;
    while (true) {
        if (-1 == client_fd) {
            std::cout << "client_fd is -1" << std::endl;
            break;
        }
        std::cout << "----process start---- fd: " << client_fd << " time: " << getTime() << std::endl;
        auto read_ret = processRead();
        if (read_ret == HTTP_CODE::NO_REQUEST) {
            continue;
        }
        bool write_ret = processWrite(read_ret);
        if (!write_ret) {
            std::cout << "write_ret is null" << std::endl;
            close(client_fd);
            client_fd = -1;
            break;
        }
        if (!linger) {
            std::cout << "not keep-alive" << std::endl;
            close(client_fd);
            client_fd = -1;
            break;
        }
        // 如果是keep-alive，保留client_id与linger状态，准备下一次读取
        read_idx = 0;
        checked_idx = 0;
        start_line = 0;
        state = PARSE_STATUS::REQUEST_LINE;
        method = HTTP_METHOD::GET;
        url = 0;
        version = 0;
        content_length = 0;
        cgi = 0;
        bytes_to_send = 0;
        bytes_have_send = 0;
        memset(read_buffer, 0, sizeof(read_buffer));
        memset(write_buffer, 0, sizeof(write_buffer));
        memset(real_file, 0, sizeof(real_file));
        memset(&file_stat, 0, sizeof(file_stat));
        iv_count = 0;
        write_idx = 0;
        // std::cout << "----ready for next---- fd: " << client_fd << std::endl;
    }
    std::cout << getTime() << std::endl;
    std::cout << "----process end---- fd: " << client_fd << std::endl;
}


HTTP_CODE HttpConn::processRead() {
    LINE_STATUS line_status = LINE_STATUS::LINE_OK;
    HTTP_CODE ret = HTTP_CODE::NO_REQUEST;

    // 没有epoll下，直接阻塞读取
    int res = recv(client_fd, read_buffer, sizeof(read_buffer), 0);
    if (res < 0) {
        perror("recv error!");
        close(client_fd);
        return HTTP_CODE::BAD_REQUEST;
    } else if (res == 0) {
        perror("client close!");
        close(client_fd);
        return HTTP_CODE::CLOSED_CONNECTION;
    }
    read_idx = res;

    // 打印buffer
    std::cout << "----recv----" << std::endl << read_buffer << std::endl;
    char *text = read_buffer;
    // 解析请求行
    while ((state == PARSE_STATUS::REQUEST_BODY && line_status == LINE_STATUS::LINE_OK) ||
           (line_status = parseLine()) == LINE_STATUS::LINE_OK) {

        text = read_buffer + start_line;
        start_line = checked_idx;
        switch(state) {
            case PARSE_STATUS::REQUEST_LINE: {
                ret = parseRequestLine(text);
                if (ret == HTTP_CODE::BAD_REQUEST) {
                    return HTTP_CODE::BAD_REQUEST;
                }
                break;
            }
            case PARSE_STATUS::REQUEST_HEADER: {
                ret = parseRequestHeader(text);
                if (ret == HTTP_CODE::BAD_REQUEST) {
                    return HTTP_CODE::BAD_REQUEST;
                } else if (ret == HTTP_CODE::GET_REQUEST) {
                    return doRequest();
                }
                break;
            }
            case PARSE_STATUS::REQUEST_BODY: {
                ret = parseRequestBody(text);
                if (ret == HTTP_CODE::GET_REQUEST) {
                    return doRequest();
                }
                line_status = LINE_STATUS::LINE_OPEN;
                break;
            }
            default: {
                return HTTP_CODE::INTERNAL_ERROR;
            }
        }
    }
    return HTTP_CODE::NO_REQUEST;
}

bool HttpConn::processWrite(HTTP_CODE read_ret) {
    switch (read_ret) {
        case HTTP_CODE::INTERNAL_ERROR: {
            break;
        }
        case HTTP_CODE::BAD_REQUEST: {
            break;
        }
        case HTTP_CODE::FORBIDDEN_REQUEST: {
            break;
        }
        case HTTP_CODE::FILE_REQUEST: {
            addStatusLine(200, "OK");
            addContentType();
            if (file_stat.st_size != 0) {
                addHeaders(file_stat.st_size);
                // print headers
                // 输出当前时间
                time_t t = time(0);
                tm* local_time = localtime(&t);
                char time_str[128] = {0};
                strftime(time_str, 128, "%Y-%m-%d %H:%M:%S", local_time);
                std::cout << "----time----" << std::endl << time_str << std::endl;
                std::cout << "----headers----" << std::endl << write_buffer << std::endl;


                iv[0].iov_base = write_buffer;
                iv[0].iov_len = write_idx;
                iv[1].iov_base = file_address;
                iv[1].iov_len = file_stat.st_size;
                iv_count = 2;
                bytes_to_send = write_idx + file_stat.st_size;
                // TODO change to epoll ctl
                this->write();

                t = time(0);
                local_time = localtime(&t);
                memset(time_str, 0, sizeof(time_str));
                strftime(time_str, 128, "%Y-%m-%d %H:%M:%S", local_time);
                std::cout << "----time----" << std::endl << time_str << std::endl;
                std::cout << "----write end----" << std::endl;
                return true;
            } else {
                // const char *ok_string = "<html><body></body></html>";
                // addHeaders(strlen(ok_string));
                // if (!addContent(ok_string))
                //     return false;
            }
            break;
        }
        default: {
            return false;
        }
        
    }
    return true;
}

void HttpConn::init() {
    client_fd = -1;
    read_idx = 0;
    checked_idx = 0;
    start_line = 0;
    state = PARSE_STATUS::REQUEST_LINE;
    method = HTTP_METHOD::GET;
    url = 0;
    version = 0;
    content_length = 0;
    linger = false;
    cgi = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    memset(read_buffer, 0, sizeof(read_buffer));
    memset(write_buffer, 0, sizeof(write_buffer));
    memset(real_file, 0, sizeof(real_file));
    // memset(file_address, 0, sizeof(file_address));
    memset(&file_stat, 0, sizeof(file_stat));
    iv_count = 0;
    write_idx = 0;
}



bool HttpConn::write() {
    int temp = 0;
    if (bytes_to_send == 0) {
        init();
        return true;
    }
    while (1) {
        temp = writev(client_fd, iv, iv_count);
        if (temp < 0) {
            if (errno == EAGAIN) {
                std::cout << "EAGAIN: write buffer full, retry later" << std::endl;
                return true;
            }
            unmap();
            return false;
        }
        std::cout << "Sent " << temp << " bytes for " << real_file << ", remaining " << bytes_to_send - temp << std::endl;
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= iv[0].iov_len) {
            iv[0].iov_len = 0;
            iv[1].iov_base = file_address + (bytes_have_send - write_idx);
            iv[1].iov_len = bytes_to_send;
        } else {
            iv[0].iov_base = write_buffer + bytes_have_send;
            iv[0].iov_len = iv[0].iov_len - bytes_have_send;
        }
        if (bytes_to_send <= 0) {
            unmap();
            return true; // 不关闭连接，支持复用
        }
    }
}

void HttpConn::unmap() {
    if (file_address) {
        munmap(file_address, file_stat.st_size);
        file_address = 0;
    }
}

bool HttpConn::addResponse(const char* format, ...) {
    if (write_idx >= MAX_HEADER_LENGTH) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(write_buffer + write_idx, MAX_HEADER_LENGTH - 1 - write_idx, format, arg_list);
    if (len >= (MAX_HEADER_LENGTH - 1 - write_idx)) {
        va_end(arg_list);
        return false;
    }
    write_idx += len;
    va_end(arg_list);
    return true;
}

bool HttpConn::addStatusLine(int status, const char* title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::addHeaders(int content_len) {
    addContentLength(content_len);
    // addContentType();
    addLinger();
    addBlankLine();
    return true;
}

bool HttpConn::addContentLength(int content_len) {
    return addResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::addLinger() {
    return addResponse("Connection: %s\r\n", (linger == true) ? "keep-alive" : "close");
}

bool HttpConn::addBlankLine() {
    return addResponse("%s", "\r\n");
}

bool HttpConn::addContentType() {
    const char* type = nullptr;
    if (strstr(real_file, ".html"))
        type = "text/html";
    else if (strstr(real_file, ".jpg"))
        type = "image/jpeg";
    else if (strstr(real_file, ".png"))
        type = "image/png";
    else if (strstr(real_file, ".gif"))
        type = "image/gif";
    else
        type = "text/plain";
    
    return addResponse("Content-Type: %s\r\n", type);
}

HttpConn::LINE_STATUS HttpConn::parseLine() {
    char temp;
    for (; checked_idx < read_idx; ++checked_idx) {
        temp = read_buffer[checked_idx];
        if (temp == '\r') {
            if ((checked_idx + 1) == read_idx) {
                return LINE_STATUS::LINE_OPEN;
            } else if (read_buffer[checked_idx + 1] == '\n') {
                read_buffer[checked_idx++] = '\0';
                read_buffer[checked_idx++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        } else if (temp == '\n') {
            if (checked_idx > 1 && read_buffer[checked_idx - 1] == '\r') {
                read_buffer[checked_idx - 1] = '\0';
                read_buffer[checked_idx++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        }
    }
    return LINE_STATUS::LINE_OPEN;
}

HTTP_CODE HttpConn::parseRequestLine(char* text) {
    url = strpbrk(text, " \t");
    if (!url) {
        return HTTP_CODE::BAD_REQUEST;
    }
    *url++ = '\0';
    char *method_str = text;
    if (strcasecmp(method_str, "GET") == 0) {
        method = HTTP_METHOD::GET;
    } else if (strcasecmp(method_str, "POST") == 0) {
        method = HTTP_METHOD::POST;
        cgi = 1;
    } else {
        return HTTP_CODE::BAD_REQUEST;
    }
    url += strspn(url, " \t");
    version = strpbrk(url, " \t");
    if (!version) {
        return HTTP_CODE::BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0) {
        return HTTP_CODE::BAD_REQUEST;
    }
    // 处理 HTTP URL 前缀：
    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;
        url = strchr(url, '/');
    }
    // 处理 HTTPS URL 前缀：
    if (strncasecmp(url, "https://", 8) == 0) {
        url += 8;
        url = strchr(url, '/');
    }
    // 验证 URL 格式的合法性：
    if (!url || url[0] != '/') {
        return HTTP_CODE::BAD_REQUEST;
    }

    if (strlen(url) == 1){
        strcat(url, "judge.html");
    }


    state = PARSE_STATUS::REQUEST_HEADER;

    return HTTP_CODE::NO_REQUEST;
}

// TODO
HTTP_CODE HttpConn::parseRequestHeader(char* text) {

    if (text[0] == '\0')
    {
        if (content_length != 0)
        {
            state = PARSE_STATUS::REQUEST_BODY;
            return HTTP_CODE::NO_REQUEST;
        }
        return HTTP_CODE::GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        host = text;
    }
    else
    {
        printf("oop!unknow header: %s\n",text);
        // LOG_INFO("oop!unknow header: %s", text);
        // Log::get_instance()->flush();

    }
    return HTTP_CODE::NO_REQUEST;
}

// TODO
HTTP_CODE HttpConn::parseRequestBody(char* text) {

    if (read_idx >= (content_length + checked_idx))
    {
        text[content_length] = '\0';
        content_str = text;
        return HTTP_CODE::GET_REQUEST;
    }
    return HTTP_CODE::NO_REQUEST;
}

HTTP_CODE HttpConn::doRequest() {
    strcpy(real_file, html_root);
    int len = strlen(html_root);
    printf("url:%s\n", url);
    const char* p = strrchr(url, '/');
    if (cgi == 1){
        // TODO 处理数据库等信息
    }
    if (*(p + 1) == '0') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");
        strncpy(real_file + len, url_real, strlen(url_real));
        free(url_real);
    } else if(*(p + 1) == '1') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/log.html");
        strncpy(real_file + len, url_real, strlen(url_real));
        free(url_real);
    } else if(*(p + 1) == '5') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/picture.html");
        strncpy(real_file + len, url_real, strlen(url_real));
        free(url_real);
    } else if(*(p + 1) == '6') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/video.html");
        strncpy(real_file + len, url_real, strlen(url_real));
        free(url_real);
    } else if(*(p + 1) == '7') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/fans.html");
        strncpy(real_file + len, url_real, strlen(url_real));
        free(url_real);
    } else {
        // 响应图片等资源请求
        strncpy(real_file + len, url, FILE_NAME_LENGTH - len - 1);
    }

    // 错误处理
    if (stat(real_file, &file_stat) < 0) {
        return HTTP_CODE::NO_RESOURCE; // 没有该资源
    }
    if (!(file_stat.st_mode & S_IROTH)) {
        return HTTP_CODE::FORBIDDEN_REQUEST; // 不是本用户，没有权限
    }
    if (S_ISDIR(file_stat.st_mode)) {
        return HTTP_CODE::BAD_REQUEST; // 请求的是目录
    }
    int file_fd = open(real_file, O_RDONLY);
    file_address = (char*)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    close(file_fd);
    return HTTP_CODE::FILE_REQUEST;

}

// bool HttpConn::write() {
//     // 输出需要发送的数据量bytes_to_send
//     std::cout << "----write----" << std::endl << "bytes_to_send: " << bytes_to_send << std::endl;
//     int temp = 0;
//     if (bytes_to_send == 0) {
//         // modfd(m_epollfd, m_sockfd, EPOLLIN);
//         init();
//         return true;
//     }
//     while (1) {
//         temp = writev(client_fd, iv, iv_count);
//         if (temp < 0) {
//             if (errno == EAGAIN) {
//                 // modfd(m_epollfd, m_sockfd, EPOLLOUT);
//                 return true;
//             }
//             unmap();
//             return false;
//         }
//         bytes_have_send += temp;
//         bytes_to_send -= temp;
//         if (bytes_have_send >= iv[0].iov_len) {
//             iv[0].iov_len = 0;
//             iv[1].iov_base = file_address + (bytes_have_send - write_idx);
//             iv[1].iov_len = bytes_to_send;
//         } else {
//             iv[0].iov_base = write_buffer + bytes_have_send;
//             iv[0].iov_len = iv[0].iov_len - bytes_have_send;
//         }
//         if (bytes_to_send <= 0) {
//             unmap();
//             // modfd(m_epollfd, m_sockfd, EPOLLIN);
//             if (linger) {
//                 init();
//                 return true;
//             } else {
//                 return false;
//             }
//         }
//     }
// }
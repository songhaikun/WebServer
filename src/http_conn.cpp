#include "epoll_util.h"
#include "fd_manager.hpp"
#include "http_conn.h"
#include "log.h"

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <stdarg.h>
#include <sys/epoll.h>
#include <sstream>
#ifdef __APPLE__
const char *html_root = "/Users/hksong/hksong/prjs/webServer/root";
#elif __linux__
const char *html_root = "/media/psf/Home/hksong/prjs/webServer/root";
#endif

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int HttpConn::epoll_fd = -1;
int HttpConn::user_count = 0;

HttpConn::HttpConn() {
    init();
    // memset(read_buffer, 0, sizeof(read_buffer));
}

HttpConn::HttpConn(int client_fd) : client_fd(client_fd) {
    memset(read_buffer, 0, sizeof(read_buffer));
}

void HttpConn::process() {
    HTTP_CODE read_ret = processRead();
    if (read_ret == HTTP_CODE::NO_REQUEST)
    {
        LOG_INFO("HttpConn::process read return NO_REQUEST");
        return;
    }
    bool write_ret = processWrite(read_ret);
    if (!write_ret)
    {
        LOG_INFO("HttpConn::process write return false");
        // closeConn();
        return;
    }
    LOG_INFO("HttpConn::process normal end");
}


HTTP_CODE HttpConn::processRead() {
    LINE_STATUS line_status = LINE_STATUS::LINE_OK;
    HTTP_CODE ret = HTTP_CODE::NO_REQUEST;

    char *text = 0;
    // 解析请求行
    while ((state == PARSE_STATUS::REQUEST_BODY && line_status == LINE_STATUS::LINE_OK) ||
           (line_status = parseLine()) == LINE_STATUS::LINE_OK) {

        text = getLine();
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
            LOG_INFO("INTERNAL ERRORFI");
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if (!addResponse("%s", error_500_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE::BAD_REQUEST: {
            LOG_INFO("BAD REQUEST");
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if (!addResponse("%s", error_404_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE::FORBIDDEN_REQUEST: {
            LOG_INFO("FORBIDDEN REQUEST");
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if (!addResponse("%s", error_403_form)) {
                return false;
            }
            break;
        }
        case HTTP_CODE::FILE_REQUEST: {
            LOG_INFO("FILE REQUEST");
            addStatusLine(200, "OK");
            LOG_INFO("real_file: %s", real_file);
            addContentType();
            if (file_stat.st_size != 0) {
                addHeaders(file_stat.st_size);
                iv[0].iov_base = write_buffer;
                iv[0].iov_len = write_idx;
                iv[1].iov_base = file_address;
                iv[1].iov_len = file_stat.st_size;
                iv_count = 2;
                bytes_to_send = write_idx + file_stat.st_size;

                return true;
            } else {
                const char *ok_string = "<html><body></body></html>";
                addHeaders(strlen(ok_string));
                if (!addResponse("%s", ok_string)){
                    return false;
                }
                return false;
            }
        }
        default: {
            return false;
        }
        
    }
    iv[0].iov_base = write_buffer;
    iv[0].iov_len = write_idx;
    iv_count = 1;
    bytes_to_send = write_idx;
    return true;
}

bool HttpConn::read() {
    if (read_idx >= MAX_HEADER_LENGTH) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(sock_fd, read_buffer + read_idx, MAX_HEADER_LENGTH - read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true; // 数据读完，等待下次 EPOLLIN
            }
            LOG_ERROR("recv failed for fd: %d, errno: %d", sock_fd, errno);
            return false; // 其他错误，关闭连接
        }
        if (bytes_read == 0) {
            LOG_INFO("client closed connection for fd: %d", sock_fd);
            return false; // 客户端关闭连接
        }
        read_idx += bytes_read;
        if (read_idx >= MAX_HEADER_LENGTH) {
            return false; // 缓冲区满
        }
    }
    return true;
}

void HttpConn::init(int sockfd, const sockaddr_in &addr) {
    sock_fd = sockfd;
    address = addr;
    EpollUtil::getInstance()->addFd(epoll_fd, sock_fd, true);
    user_count++;
    init();
}

void HttpConn::init() {
    // client_fd = -1;
    read_idx = 0;
    write_idx = 0;
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
    host = 0;

    memset(read_buffer, 0, sizeof(read_buffer));
    memset(write_buffer, 0, sizeof(write_buffer));
    memset(real_file, 0, sizeof(real_file));
}

HttpConn::WRITE_RES HttpConn::write() {
    if (bytes_to_send == 0) {
        LOG_INFO("call init in write end, fd: %d", sock_fd);
        return WRITE_RES::WRITE_END;
    }

    int temp = writev(sock_fd, iv, iv_count);
    if (temp < 0) {
        if (errno == EAGAIN) {
            LOG_INFO_T("write buf is full, fd: %d", sock_fd);
            return WRITE_RES::WRITE_AGAIN;
        }
        unmap();
        LOG_ERROR("writev failed for fd: %d, errno: %d", sock_fd, errno);
        return WRITE_RES::WRITE_FAILED;
    }

    LOG_INFO("sent %d bytes to client fd: %d", temp, sock_fd);
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
        if (linger) {
            init();
            LOG_INFO("keep-alive is true, fd: %d", sock_fd);
            return WRITE_RES::WRITE_LINGER;
        } else {
            LOG_INFO("keep-alive is false, fd: %d", sock_fd);
            return WRITE_RES::WRITE_CLOSED;
        }
    }
    return WRITE_RES::WRITE_AGAIN;
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
        type = "text/html; charset=utf-8"; // 增加utf-8声明
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
        LOG_INFO("BAD: !URL");
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
        LOG_INFO("BAD: !GET || !POST");
        return HTTP_CODE::BAD_REQUEST;
    }
    url += strspn(url, " \t");
    version = strpbrk(url, " \t");
    if (!version) {
        LOG_INFO("BAD: !VERSION");
        return HTTP_CODE::BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0) {
        LOG_INFO("BAD: !HTTP1.1");
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
        LOG_INFO("BAD: URL[0] NOT /");
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
        // std::cout << "oop!unknow header: " << text << std::endl;
        // printf("oop!unknow header: %s\n",text);
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
    const char* p = strrchr(url, '/');
    if (cgi == 1){
        // TODO 处理数据库等信息
    }
    if (*(p + 1) == '0') {
        char* url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(url_real, "/register.html");
        strncpy(real_file + len, url_real, strlen(url_real));
        free(url_real);
        // FOR TEST
        LOG_INFO("construct realfile:%s", real_file);
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
        LOG_INFO("BAD: GET DIR");
        return HTTP_CODE::BAD_REQUEST; // 请求的是目录
    }
    int file_fd = open(real_file, O_RDONLY);
    SafeFdManager::getInstance().registerFd(file_fd, "doRequest");
    file_address = (char*)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    SafeFdManager::getInstance().safeClose(file_fd);
    return HTTP_CODE::FILE_REQUEST;
}  
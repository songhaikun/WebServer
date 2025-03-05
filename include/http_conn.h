#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>

#define MAX_HEADER_LENGTH 8192
#define WRITE_BUFFER_SIZE 1024
#define FILE_NAME_LENGTH 200

enum class HTTP_CODE {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};

class HttpConn{
private:
    int client_fd{-1};
    int sock_fd{-1};
    sockaddr_in address;

    // 静态变量，用于存储全局信息

    char read_buffer[MAX_HEADER_LENGTH];
    char write_buffer[WRITE_BUFFER_SIZE];

    int read_idx{0};
    int checked_idx{0};
    int start_line{0};
    int content_length{0};
    int write_idx{0};


    bool linger{false};
    char* url;
    char* version;
    char* host;
    char* content_str;
    char real_file[FILE_NAME_LENGTH];
    char* file_address;
    struct stat file_stat;
    struct iovec iv[2]; // 缓冲区
    int iv_count{0};
    int bytes_to_send{0};
    int bytes_have_send{0};

    int cgi{0};

    // 当前状态，解析请求行，解析请求头，解析请求体
    enum class PARSE_STATUS {
        REQUEST_LINE = 0,
        REQUEST_HEADER,
        REQUEST_BODY
    };
    enum class LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    PARSE_STATUS state;
    enum class HTTP_METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    HTTP_METHOD method;
    

private:
    LINE_STATUS parseLine();

    HTTP_CODE parseRequestLine(char* text);

    HTTP_CODE parseRequestHeader(char* text);

    HTTP_CODE parseRequestBody(char* text);

    HTTP_CODE processRead();

    HTTP_CODE doRequest();

    bool processWrite(HTTP_CODE read_ret);

    bool addResponse(const char* format, ...);
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int content_len);
    bool addContentLength(int content_len);
    bool addLinger();
    bool addBlankLine();
    bool addContentType();
    void init();
    void unmap();

    char *getLine() { return read_buffer + start_line; };

public:
    static int epoll_fd;
    static int user_count;
public:
    HttpConn();
    explicit HttpConn(int client_fd);
    void process();

    // 提供外部epoll调用
    void init(int sockfd, const sockaddr_in &addr);
    void closeConn(bool real_close = true);
    bool readOnce();
    bool write();

    sockaddr_in *get_address()
    {
        return &address;
    }
};
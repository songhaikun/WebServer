#include "event_loop.h"
#include "log.h"
#include "server.h"
#include <signal.h>

#define SYNLOG
int main() {
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); // 异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0);
#endif

    EventLoop loop;
    Server server(&loop, 8080, HTTP_THREAD_NUM);
    server.start();

    return 0;
}
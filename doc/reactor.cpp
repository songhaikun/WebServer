#include <iostream>
#include <vector>
#include <thread>
#include <mutex>

// 事件处理函数类型
typedef void (*EventHandler)(void*);

// Reactor 模型
class Reactor {
private:
    // 事件队列
    std::vector<std::pair<EventHandler, void*>> eventQueue; 
    // 锁用于保护事件队列
    std::mutex mtx; 

public:
    // 添加事件到队列
    void addEvent(EventHandler handler, void* data) {
        std::lock_guard<std::mutex> lock(mtx);
        eventQueue.push_back(std::make_pair(handler, data));
    }

    // 运行 Reactor 模型
    void run() {
        while (true) {
            std::lock_guard<std::mutex> lock(mtx);
            if (eventQueue.empty()) {
                continue;
            }

            // 获取并处理第一个事件
            auto& event = eventQueue[0];
            event.first(event.second);
            // 从队列中删除处理的事件
            eventQueue.erase(eventQueue.begin());
        }
    }
};

// 示例事件处理函数
void handleEvent1(void*) {
    std::cout << "Event 1 handled" << std::endl;
}
void handleEvent2(void*) {
    std::cout << "Event 2 handled" << std::endl;
}

int main() {
    Reactor reactor;

    // 添加事件处理函数到 Reactor
    reactor.addEvent(handleEvent1, nullptr);
    reactor.addEvent(handleEvent2, nullptr);

    // 启动 Reactor 线程
    std::thread t([&]() {
        reactor.run(); 
    });

    // 等待一段时间让 Reactor 处理事件
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 停止 Reactor 线程
    reactor.addEvent(nullptr, nullptr);
    t.join();

    return 0;
}
主线程分发与网络io(非阻塞io)
其它线程池线程负责报文处理
使用epoll进行协作
首先创建epoll，监听listenfd事件。
当listenfd触发epoll_in事件后初始化连接对象信息

当收到epoll_in事件，使用recv读取对应连接fd的报文信息
把对象作为任务加入线程池中
同时通过条件变量唤醒一个等待线程（可能的惊群问题）
线程开始处理，此时分别执行读取与报文解析，response缓冲构建
最后结束后向epollfd发送写时间，epoll_fd收到写事件后send报文

# 开发进展
+ 基础服务器架构 commit1
+ linux移植 commit2
+ epoll et模式改造 commit3

# TODO
+ epoll lt模式改造 commit4
+ reactor与proactor堆改造
+ POST支持
+ 线程池优化
+ 连接定时器管理
+ 数据库连接
+ 日志改造
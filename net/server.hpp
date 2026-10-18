#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <sys/epoll.h>
#include <thread>
#include <mutex>
#include <sys/eventfd.h>
#include <memory>
#include <sys/timerfd.h>
#include <signal.h>
#include <typeinfo>
#include <condition_variable>

#define INF 0
#define DBG 1
#define ERR 2
#define DEFAULT_LOG_LEVEL DBG

// level: 日志级别 (INF/DBG/ERR)
// format: 类似 printf 的格式化字符串，如 "hello %s"
// ... : 可变参数，配合 format 使用
#define LOG(level, format, ...)                                                                                           \
    {                                                                                                                     \
        /* 检查当前日志级别是否达到输出门槛 */                                                                            \
        if (level >= DEFAULT_LOG_LEVEL)                                                                                   \
        {                                                                                                                 \
            /* --- 获取并格式化当前时间 --- */                                                                            \
            time_t t = time(nullptr);                                                                                     \
            struct tm *m = localtime(&t);                                                                                 \
            char ts[32] = {0};                                                                                            \
            strftime(ts, 31, "%H:%M:%S", m);                                                                              \
            fprintf(stdout, "[%p %s %s:%d] " format "\n", (void *)pthread_self(), ts, __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                                                                                 \
    }
#define INF_LOG(format, ...) LOG(LNF, format, ##__VA_ARGS__);
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__);
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__);

#define BUFFER_DEFAULT_SIZE 1024

class Buffer
{
private:
    std::vector<char> _buffer; // 使用vector进行内存空间管理
    uint64_t _reader_idx;      // 读偏移
    uint64_t _writer_idx;      // 写偏移
public:
    Buffer() : _reader_idx(0), _writer_idx(0), _buffer(BUFFER_DEFAULT_SIZE) {}
    // 获取底层空间的其实指针
    char *Begin() { return &*_buffer.begin(); }

    // 获取当前写入起始地址
    char *WritePosition() { return Begin() + _writer_idx; }

    // 获取当前读取起始地址
    char *ReadPosition() { return Begin() + _reader_idx; }

    // 获取缓冲区起始空闲空间大小 -- 写偏移之后的空闲空间
    uint64_t HandIdleSize() { return _reader_idx; }

    // 获取缓冲区末尾空闲空间大小 -- 读偏移之后的空闲空间
    uint64_t TailIdleSize() { return _buffer.size() - _writer_idx; }

    // 获取可读数据大小
    uint64_t ReadAbleSize() { return _writer_idx - _reader_idx; }

    // 将读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        if (len == 0)
            return;
        assert(len <= ReadAbleSize());
        _reader_idx += len;
    }

    // 将写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {
        if (len == 0)
            return;
        assert(len <= TailIdleSize());
        _writer_idx += len;
    }

    // 确保可写空间足够（整体空闲空间够了就移动数据，否则就扩容）
    void EnsureWriteSpace(uint64_t len)
    {
        // 1. 如果末尾空闲空间大小足够，直接返回
        if (len <= TailIdleSize())
            return;
        // 2. 末尾空间不够，但加上头部的废弃空间足够，则进行数据“整理”
        if (len <= HandIdleSize() + TailIdleSize())
        {
            uint64_t res = ReadAbleSize();
            std::copy(ReadPosition(), ReadPosition() + res, Begin());
            _reader_idx = 0;
            _writer_idx = res;
        }
        else
        { // 3. 总体空间都不够，直接扩容底层 vector
            DBG_LOG("RESIZE %ld", _writer_idx + len);
            _buffer.resize(_writer_idx + len);
        }
    }
    /* ================= 写入相关接口 ================= */
    // 写入数据
    void Write(const void *data, uint64_t len)
    {
        if (len == 0)
            return;
        EnsureWriteSpace(len);
        const char *d = (const char *)data;
        std::copy(d, d + len, WritePosition());
    }

    // 写入数据并更新偏移量
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }

    void WriteString(const std::string &data)
    {
        return Write(data.c_str(), data.size());
    }

    // 写入std::string 并更新偏移量
    void WriteStringAndPush(std::string &data)
    {
        WriteString(data);
        MoveWriteOffset(data.size());
    }

    // 从另一个Buffer 中读取数据并写入当前Buffer
    void WriteBuffer(Buffer &data)
    {
        Write(data.ReadPosition(), data.ReadAbleSize());
    }

    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadAbleSize());
    }

    /* ================= 读取相关接口 ================= */

    // 读取数据到buf，但不一定读偏移量
    void Read(void *buf, uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::copy(ReadPosition(), ReadPosition() + len, (char *)buf);
    }

    // 读取数据到buf，并移动偏移量
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }

    // 读取数据作为std::string 返回
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str;
        str.resize(len);
        Read(&str[0], len);
        return str;
    }

    // 读取数据作为std::string 返回，并使用读偏移量
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }

    // 查找换行符 '\n'，常用于解决 TCP 粘包/半包问题中的按行读取
    char *FindCRLF()
    {
        char *res = (char *)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

    // 获取一行数据 (包括换行符)，但不移动偏移量
    std::string GetLine()
    {
        char *pos = FindCRLF();
        if (pos == nullptr)
        {
            return "";
        }
        return ReadAsString(pos - ReadPosition() + 1);
    }

    // 获取一行数据并移除
    std::string GetLineAndPop()
    {
        std::string str = GetLine();
        MoveReadOffset(str.size());
        return str;
    }

    // 清空缓冲区
    void Clear()
    {
        _reader_idx = 0;
        _writer_idx = 0;
    }
};

#define MAX_LISTEN 2024
// Socket封装类
// 对TCP套接字常用操作进行简单封装：
// 创建、绑定、监听、连接、收发数据、关闭、设置非阻塞等

class Socket
{
private:
    int _sockfd; // 套接字文件描述符
public:
    Socket() : _sockfd(-1) {}
    Socket(int fd) : _sockfd(fd) {}
    ~Socket() { Close(); }

    // 获取套接字描述符
    int Fd() { return _sockfd; }

    // 创建套接字
    bool Create()
    {
        // int socket(int domain,int type,int protocol)
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_sockfd < 0)
        {
            ERR_LOG("CREATE SOCKET FAILED!!");
            return false;
        }
        return true;
    }

    // 绑定地址信息
    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        int st = inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        socklen_t len = sizeof(struct sockaddr_in);
        // int bind(int sockfd,const struct sockaddr *addr,socklen_t addrlen);
        int ret = bind(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            ERR_LOG("BIND ADDRESS FAILED!");
            return false;
        }
        return true;
    }

    // 开始监听
    bool Listen(int backlog = MAX_LISTEN)
    {
        // int listen(int sockfd,int backlog)
        int ret = listen(_sockfd, backlog);
        if (ret < 0)
        {
            ERR_LOG("SOCKET LISTEN FAILED!");
            return false;
        }
        return true;
    }

    // 向服务器发起连接
    bool Connect(const std::string &ip, uint16_t port)
    {
        // int connect(int sockfd,const struct sockaddr *addr,socklen_t addrlen);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        int st = inet_pton(AF_INET, ip.c_str(), &addr.sin_addr.s_addr);
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = connect(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            ERR_LOG("CONNECT SERVER FAILED");
            return false;
        }
        return true;
    }

    // 获取新连接
    int Accept()
    {
        // int accept(int sockfd,struct sockaddr *_Nullable restrict addr,socklen_t *_Nullable restrict addrlen)
        // 返回新的通信套接字
        int newfd = accept(_sockfd, nullptr, nullptr);
        if (newfd < 0)
        {
            ERR_LOG("SOCKET ACCEPT FAILED!");
            return -1;
        }
        return newfd;
    }

    // 接收数据
    ssize_t Recv(void *buf, size_t len, int flag = 0)
    {
        // ssize_t recv(int sockfd,void *buf,size_t len,int flag);
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if (ret <= 0)
        {
            // EAGAIN 当前socket的接收缓冲区中没有数据，在非阻塞情况下会有这个错误
            // EINTR 表示当前socket阻塞等待，被信号打断了
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0; // 表示这次没有收到数据
            }
            // ret == 0:对端关闭连接
            // ret<0: 发生错误
            ERR_LOG("SOCKET RECV FAILED!!");
            return -1;
        }
        return ret; // 实际接收的数据长度
    }

    // 非阻塞接收
    ssize_t NonBlockRecv(void *buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前接收为非阻塞。
    }

    // 发送
    ssize_t Send(const void *buf, size_t len, int flag = 0)
    {
        // ssize_t send(int sockfd, const void buf[.len], size_t len, int flags);
        int ret = send(_sockfd, buf, len, flag);
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            ERR_LOG("SOCKET SEND FAILED!!");
            return -1;
        }
        return ret; // 实际发送长度
    }

    // 非阻塞发送
    ssize_t NonBlockSend(const void *buf, ssize_t len)
    {
        if (len == 0)
            return 0;
        return Send(buf, len, MSG_DONTWAIT);
    }

    // 关闭套接字
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }

    // 创建一个服务端连接
    bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = false)
    {
        // 1.创建套接字 2.设置地址重用 3.绑定地址 4.开始监听 5.设置非阻塞
        if (!Create())
            return false;
        ReuseAddress();
        if (!block_flag)
            NonBlock();
        if (!Bind(ip, port))
            return false;
        if (!Listen())
            return false;
        return true;
    }

    // 创建一个客户端连接
    bool CreateClient(uint16_t port, const std::string &ip)
    {
        // 1.创建套接字 2.指向连接服务器
        if (!Create())
            return false;
        if (!Connect(ip, port))
            return false;
        return true;
    }

    // 设置套接字选项--开启地址端口重用
    void ReuseAddress()
    {
        // int setsockopt(int sockfd, int level, int optname,const void optval[.optlen],socklen_t optlen);
        int val = 1;
        // 允许TIME_WAIT状态下快速重用地址
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int));
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(int));
    }

    // 设置套接字阻塞属性为非阻塞
    void NonBlock()
    {
        // int fcntl(int fd, int op, ... /* arg */ );
        int flag = fcntl(_sockfd, F_GETFL, 0);
        // 增加O_NONBLOCK标志
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }
};

class Poller;
class EventLoop;
// Channel类：对文件描述符（fd）及其相关事件、回调函数的封装模块（通常用于Reactor模式）

class Channel
{
private:
    int _fd;           // 该Channel负责封装管理的文件描述符
    EventLoop *_loop;  // 该Channel所属的EventLoop（事件循环）实例
    uint32_t _events;  // 当前需要监控的事件
    uint32_t _revents; // 当前连接触发的事件

    // 事件回调函数类型声明
    using EventCallBack = std::function<void()>;
    EventCallBack _read_callback;  // 可读事件被触发的回调函数
    EventCallBack _write_callback; // 可写事件被触发的回调函数
    EventCallBack _error_callback; // 错误事件被触发的回调函数
    EventCallBack _close_callback; // 连接断开事件被触发的回调函数
    EventCallBack _event_callback; // 任意事件被触发的回调函数
public:
    Channel(EventLoop *loop, int fd) : _fd(fd), _events(0), _revents(0), _loop(loop) {}

    // 获取当前Channel管理的文件描述符
    int Fd() { return _fd; }

    // 获取想要监控的事件
    uint32_t Events() { return _events; }

    // 设置实际就绪的事件
    void  SetREvents(uint32_t events) { _revents = events; }

    // 注册具体的事件回调函数
    void SetReadCallBack(const EventCallBack &cb) { _read_callback = cb; }
    void SetWriteCallBack(const EventCallBack &cb) { _write_callback = cb; }
    void SetErrorCallBack(const EventCallBack &cb) { _error_callback = cb; }
    void SetCloseCallBack(const EventCallBack &cb) { _close_callback = cb; }
    void SetEventCallBack(const EventCallBack &cb) { _event_callback = cb; }

    // 当前是否监控了可读
    bool ReadAble() { return (_events & EPOLLIN); }

    // 当前是否监控了可写
    bool WriteAble() { return (_events & EPOLLOUT); }

    // 启动读事件监控
    void EnableRead()
    {
        _events |= EPOLLIN;
        Update();
    }

    // 启动写事件监控
    void EnableWrite()
    {
        _events |= EPOLLOUT;
        Update();
    }

    // 关闭读事件监控
    void DisableRead()
    {
        _events &= ~EPOLLIN;
        Update();
    }

    // 关闭写事件监控
    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Update();
    }

    // 关闭所有事件监控
    void DisableAll()
    {
        _events = 0;
        Update();
    }

    // 从底层的Poller/EventLoop中移除当前Channel的监控
    void Remove();
    // 将当前Channel的_events配置状态更新到底层的Poller/EventLoop中
    void Update();

    void HandleEvent()
    {
        // 处理读事件（包括正常数据可读、对端关闭导致的读半闭、紧急带外数据）
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            if (_read_callback)
                _read_callback();
        }

        /*有可能会释放连接的操作事件，一次只处理一个*/
        if (_revents & EPOLLOUT)
        { // 处理写事件
            if (_write_callback)
                _write_callback();
        }
        else if (_revents & EPOLLERR)
        { // 处理错误事件
            if (_error_callback)
                _error_callback();
        }
        else if (_revents & EPOLLHUP)
        { // 处理挂起事件（通常为连接断开）
            if (_close_callback)
                _close_callback();
        }
        // 只要有事件触发，最后都会调用的通用回调（可用于刷新连接活跃度等统一收尾操作）
        if (_event_callback)
            _event_callback();
    }
};

#define MAX_EPOLLEVENTS 1024
// Poller类：对 Linux epoll 机制的封装，负责底层的 I/O 多路复用事件监听
class Poller
{
private:
    int _epfd;                                    // epoll 实例的文件描述符
    struct epoll_event _evs[MAX_EPOLLEVENTS];     // 作为 epoll_wait 的传出数组，用于保存实际就绪的事件集合
    std::unordered_map<int, Channel *> _channels; // 管理当前 Poller 正在监控的所有 Channel，键为 fd，值为对应的 Channel 指针
private:
    // 对epoll的直接操作
    void Update(Channel *channel, int op)
    {
        // int epoll_ctl(int epfd,int op,int fd,struct epoll_event *ev)
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;               // 设置底层 epoll 触发时返回的携带数据为 fd
        ev.events = channel->Events(); // 将 Channel 期望监控的事件掩码传递给 epoll
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            ERR_LOG("EPOLLCTL FAILED!");
        }
        return;
    }

    // 判断一个Channel是否已经添加了事件监控 (通过在哈希表中查找)
    bool HasChannel(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it == _channels.end())
            return false;
        return true;
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            ERR_LOG("EPOLL CREATE FAILED!!");
            abort(); // 退出程序
        }
    }

    // 添加或修改监控事件(暴露给外部 EventLoop 调用的接口)
    void UpdateEvent(Channel *channel)
    {
        // 不存在则添加：先在本地哈希表中记录该 Channel，再向系统内核注册
        if (!HasChannel(channel))
        {
            _channels.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD);
        }
        // 已经存在，说明是修改原有的监听事件 (如从只读修改为读+写)
        return Update(channel, EPOLL_CTL_MOD);
    }

    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it != _channels.end())
        {
            _channels.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL);
    }

    // 开始监控，返回活跃连接 (核心事件轮询函数)
    void Poll(std::vector<Channel *> *active)
    {
        // int epoll_wait(int epfd,struct epoll_event *evs,int maxevents,int timeout)
        //  阻塞等待事件发生，timeout 为 -1 表示死等，直到有事件或信号中断
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            // 如果是被系统信号中断 (EINTR)，属于正常现象，直接返回等待下次轮询
            if (errno == EINTR)
                return;
            ERR_LOG("EPOLL WAIT ERROR:%s\n", strerror(errno));
            abort();
        }

        // 遍历所有内核返回就绪事件
        for (int i = 0; i < nfds; i++)
        {
            // 根据底层返回的 fd，在本地哈希表中找到对应的 Channel 模块
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());          // 断言一定能找到，保证内核通知的 fd 确实由我们在管理
            it->second->SetREvents(_evs[i].events); // 设置实际就绪的事件
            active->push_back(it->second);          // 将准备好的 Channel 添加到 active 列表中，交由上层统一执行回调逻辑
        }
        return;
    }
};

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;

// TimerTask: 定时任务封装类
// 核心思想：利用 RAII 机制，在对象析构时执行定时任务
class TimerTask
{
private:
    uint64_t _id;         // 定时器对象全局唯一ID
    uint32_t _timeout;    // 定时任务的延迟时间（超时时间）
    bool _canceled;       // 任务取消标志：false-正常执行，true-被取消
    TaskFunc _task_cb;    // 真正要执行的定时任务回调函
    ReleaseFunc _release; // 释放回调：用于在析构时从 TimerWheel 中清理自身记录
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb)
        : _id(id), _timeout(delay), _task_cb(cb), _canceled(false) {}
    ~TimerTask()
    {
        if (_canceled == false)
            _task_cb();
        _release();
    }
    void Cancel() { _canceled = true; }                       // 取消任务
    void SetRelease(const ReleaseFunc &cb) { _release = cb; } // 设置清理回调
    uint32_t DelayTime() { return _timeout; }                 // 获取延迟时间
};

// TimerWheel: 时间轮定时器模块
// 核心思想：环形数组 + tick秒针，利用 timerfd 实现按秒驱动
class EventLoop;
class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;
    int _tick;                                      // 当前表盘的“秒针”位置，走到哪就释放并执行哪里的任务
    int _capacity;                                  // 表盘最大刻度数量（也是最大延迟时间，如 60 秒）
    std::vector<std::vector<PtrTask>> _wheel;       // 时间轮本体：每个刻度上挂载一个数组，保存该时刻要到期的所有定时任务
    std::unordered_map<uint64_t, WeakTask> _timers; // 快速查找映射表：用于刷新或取消任务时，通过 ID 快速找到任务（使用 weak_ptr 避免影响引用计数）
    EventLoop *_loop;                               // 绑定事件循环
    int _timerfd;                                   // 定时器文件描述符，由 Linux 内核提供定时通知
    std::unique_ptr<Channel> _timer_channel;        // 定时器事件通道
private:
    // 供 TimerTask 析构时调用的回调，将自身从映射表中抹除
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }

    // 创建并配置 timerfd（每秒触发一次可读事件）
    static int CreateTimerFd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd < 0)
        {
            ERR_LOG("TIMERFD CREATE FAILED!");
            abort();
        }
        struct itimerspec itime;
        itime.it_value.tv_sec = 1; // 首次超时时间：1s后
        itime.it_value.tv_nsec = 0;
        itime.it_interval.tv_sec = 1; // 后续触发间隔：每1s触发一次
        itime.it_interval.tv_nsec = 0;
        timerfd_settime(timerfd, 0, &itime, nullptr);
        return timerfd;
    }

    // 读取timerfd，获取自上次读取后超时的次数
    int ReadTimeFd()
    {
        uint64_t times;
        int ret = read(_timerfd, &times, 8);
        if (ret < 0)
        {
            ERR_LOG("READ TIMEFD FAILED!");
            abort();
        }
        return times;
    }

    // 秒针向前走一步，并执行该刻度上的所有任务
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();
    }

    // timerfd 触发可读事件时的回调函数
    void OnTime()
    {
        int times = ReadTimeFd();
        // 根据实际超时次数（防止阻塞导致错漏），推进秒针
        for (int i = 0; i < times; i++)
        {
            RunTimerTask();
        }
    }

    // 【线程安全保证】以下 InLoop 函数必须在 EventLoop 所在线程中执行
    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity; // 计算在时间轮上的落点
        _wheel[pos].push_back(pt);             // 加入时间轮
        _timers[id] = WeakTask(pt);            // 加入快速查找表
    }

    void TimerRefreshInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
            return;                     // 任务已过期或不存在
        PtrTask pt = it->second.lock(); // 提升为 shared_ptr

        if (pt)
        {
            int delay = pt->DelayTime();
            int pos = (_tick + delay) % _capacity; // 重新放入新的刻度位置（旧位置的指针依然存在，但在析构前由于 _canceled 的机制或多次引用的存在，不会被重复执行）
            _wheel[pos].push_back(pt);
        }
    }

    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
            return;
        PtrTask pt = it->second.lock();
        if (pt)
            pt->Cancel(); // 设置取消标记，析构时便不再执行 task_cb
    }

public:
    TimerWheel(EventLoop *loop)
        : _capacity(60), _tick(0), _wheel(_capacity), _loop(loop), _timerfd(CreateTimerFd()), _timer_channel(new Channel(_loop, _timerfd))
    {
        _timer_channel->SetReadCallBack(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableRead(); // 启动读事件监控
    }

    // 外部调用的公开接口（内部通过 EventLoop 保证线程安全，需补充实现：调用 _loop->RunInLoop）
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb);
    void TimerRefresh(uint64_t id);
    void TimerCancel(uint64_t id);

    // 判断定时器是否存在（只能在 Loop 线程中调用）
    bool HasTimer(uint64_t id)
    {
        return _timers.find(id) != _timers.end();
    }
};

class EventLoop
{
private:
    using Functor = std::function<void()>;
    std::thread::id _thread_id;              // 记录当前 EventLoop 所属的线程 ID，用于判断是否在当前线程
    int _event_fd;                           // eventfd：用于跨线程唤醒阻塞在 epoll_wait 上的 EventLoop 线程
    std::unique_ptr<Channel> _event_channel; // 专门管理 eventfd 的事件通道
    Poller _poller;                          // I/O 多路复用器（如 epoll 封装），负责监听所有文件描述符的事件
    std::vector<Functor> _tasks;             // 任务池：存放其他线程要求本 EventLoop 执行的回调任务
    std::mutex _mutex;
    TimerWheel _timer_wheel;

public:
    void RunAllTask()
    {
        std::vector<Functor> functor;
        {
            // 将 _tasks 里的任务交换到本地的 functor 中，然后立刻释放锁。
            // 这样在执行任务时，不会阻塞其他线程继续向 _tasks 中投递新任务
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(functor);
        }
        for (auto &f : functor)
            f();
        return;
    }

    // 创建eventfd(内核提供事件等待与唤醒机制)
    static int CreateEventFd()
    {
        // EFD_CLOEXEC:fork子进程时关闭该fd
        // EFD_NONBLOCK:设置为非阻塞
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            ERR_LOG("CREATE EVENTFD FAILED!!");
            abort();
        }
        return efd;
    }

    // 处理 eventfd 的读事件（消费唤醒信号）
    void ReadEventFd()
    {
        uint64_t res = 0;
        // 读取 eventfd 中的值，每次读取会将内部计数器清零，从而消除可读事件
        int ret = read(_event_fd, &res, sizeof res);
        if (ret < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                return;
            ERR_LOG("READ EVENTFD FAILED!");
            abort();
        }
        return;
    }

    // 唤醒 EventLoop（向 eventfd 写数据）
    void WakeUpEventFd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof val);
        if (ret < 0)
        {
            if (errno == EINTR)
                return;
            ERR_LOG("WRITE EVENTFD FAILED!");
            abort();
        }
        return;
    }

public:
    // 构造函数：初始化各个组件，并启动对eventfd的监听
    EventLoop() : _thread_id(std::this_thread::get_id()), _event_fd(CreateEventFd()), _event_channel(new Channel(this, _event_fd)), _timer_wheel(this)
    {
        // 绑定eventfd的读回调，当有人调用WakeUpEventFd写数据时，此函数会被触发读取数据
        _event_channel->SetReadCallBack(std::bind(&EventLoop::ReadEventFd, this));
        // 将event注册到Poller中，开始监听它的可读事件
        _event_channel->EnableRead();
    }

    // 启动事件循环（核心驱动函数）
    // 经典的 Reactor 三步走：事件监控 -> 就绪事件处理 -> 执行任务
    void Start()
    {
        while (1)
        {
            // 1.事件监控：阻塞等待（或超时等待）文件描述符上有事件发生
            std::vector<Channel *> actives;
            _poller.Poll(&actives);
            // 2.事件处理：遍历所有触发事件的Channel，调用对应的读/写/错误回调
            for (auto &channel : actives)
            {
                channel->HandleEvent();
            }
            // 3. 执行任务：执行其他线程通过 QueueInLoop 投递过来的待办任务
            RunAllTask();
        }
    }

    // 判断当前代码是否运行在 EventLoop 专属的线程内
    bool IsInLoop()
    {
        return (_thread_id == std::this_thread::get_id());
    }

    // 断言检测：严格保证某段代码只能在当前 Loop 线程中执行
    void AssertInLoop()
    {
        assert(_thread_id == std::this_thread::get_id());
    }

    // 将任务压入任务池，并唤醒Loop线程
    void QueueInLoop(const Functor &cb)
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        WakeUpEventFd();
    }

    // 核心任务调度：在Loop线程中执行指定任务
    void RunInLoop(const Functor &cb)
    {
        if (IsInLoop())
            return cb();        // 如果当前就是 Loop 线程，直接同步执行
        return QueueInLoop(cb); // 如果是其他线程调用的，则将任务放入队列，等待 Loop 线程自行执行
    }

    // 添加/修改描述符的事件监控
    void UpdateEvent(Channel *channel) { return _poller.UpdateEvent(channel); }

    // 移除描述符的监控
    void RemoveEvent(Channel *channel) { return _poller.RemoveEvent(channel); }

    // 内部时间轮接口代理
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        return _timer_wheel.TimerAdd(id, delay, cb);
    }
    void TimerRefresh(uint64_t id) { return _timer_wheel.TimerRefresh(id); }
    void TimerCancel(uint64_t id) { return _timer_wheel.TimerCancel(id); }
    bool HasTimer(uint64_t id) { return _timer_wheel.HasTimer(id); }
};

class LoopThread
{
private:
    /*用于实现_loop获取的同步关系，避免线程创建了，但是_loop还没有实例化之前去获取_loop*/
    std::mutex _mutex;             // 互斥锁，配合条件变量使用，保护对_loop指针的并发访问
    std::condition_variable _cond; // 条件变量，用于在主线程和子线程间进行同步
    EventLoop *_loop;              // EventLoop指针变量，这个对象需要在线程内实例化（指向子线程局部变量）
    std::thread _thread;

private:
    /*实例化 EventLoop 对象，唤醒_cond上有可能阻塞的线程，并且开始运行EventLoop模块的功能*/
    void ThreadEntry()
    {
        EventLoop loop;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _loop = &loop;      // 给成员变量赋值，此时 EventLoop 已经创建完毕
            _cond.notify_all(); // 唤醒正在 GetLoop() 中阻塞等待的主线程
        }
        loop.Start(); // 启动事件循环，开始死循环监听事件，直到循环被停止
    }

public:
    /*创建线程，设定线程入口函数*/
    // 初始化_loop为空，并立即启动底层的std::thread执行ThreadEntry
    LoopThread() : _loop(nullptr), _thread(std::thread(&LoopThread::ThreadEntry, this)) {}

    // 返回当前线程关联的EventLoop对象指
    EventLoop *GetLoop()
    {
        EventLoop *loop = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            // 如果子线程还没来得及给_loop赋值，调用此函数的线程会释放锁并进入阻塞等待
            _cond.wait(lock, [&]()
                       { return _loop != nullptr; }); // loop为NULL就一直阻塞
            loop = _loop;
        }
        return loop;
    }
};

class LoopThreadPool
{
private:
    int _thread_count;                  // 线程池中子线程(sub reactor)的数量
    int _next_idx;                      // 用于轮询(Round-Robin)分发新连接的下一个索引
    EventLoop *_baseloop;               // 主线程的EventLoop (通常负责监听新连接，即主reactor)
    std::vector<LoopThread *> _threads; // 管理所有创建出来的 LoopThread 线程对象
    std::vector<EventLoop *> _loops;    // 缓存各个子线程对应的 EventLoop 指针，方便快速获取
public:
    // 构造函数：初始化时默认线程数为0，轮询索引为0，绑定主loop
    LoopThreadPool(EventLoop *baseloop) : _thread_count(0), _next_idx(0), _baseloop(baseloop) {}

    // 设置线程池的线程数量（需要在调用Create前设置）
    void SetThreadCount(int count)
    {
        _thread_count = count;
    }

    // 根据指定的线程数量，真正开始创建线程池
    void Create()
    {
        if (_thread_count > 0)
        {
            _threads.resize(_thread_count); // 预分配空间
            _loops.resize(_thread_count);
            for (int i = 0; i < _thread_count; i++)
            {
                _threads[i] = new LoopThread();     // 创建并启动新的子线程
                _loops[i] = _threads[i]->GetLoop(); // 这里会调用上面写好的获取逻辑，可能发生阻塞，直到第i个子线程的EventLoop创建完毕
            }
        }
        return;
    }

    // 核心负载均衡功能：获取下一个处理业务的 EventLoop
    EventLoop *NextLoop()
    {
        // 单线程模式：如果没有配置子线程，就全权交由主
        if (_thread_count == 0)
            return _baseloop;
        // 多线程模式：轮询算法，将下一个新连接均匀地分配给下一个子线程
        _next_idx = (_next_idx + 1) % _thread_count;
        return _loops[_next_idx];
    }
};

class Any
{
private:
    class holder
    {
    public:
        virtual ~holder() {}
        virtual const std::type_info &type() = 0; // 问题：为什么要const+引用
        virtual holder *clone() = 0;
    };

    template <class T>
    class placeholder : public holder
    {
    public:
        placeholder(const T &val) : _val(val) {}
        // 获取子类对象保存的数据类型
        virtual const std::type_info &type() { return typeid(T); }
        // 针对当前的对象自身，克隆出一个新的子类对象
        virtual holder *clone()
        {
            return new placeholder(_val);
        }

    public:
        T _val;
    };
    holder *_content;

public:
    Any() : _content(nullptr) {};

    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) {}
    Any(const Any &other) : _content(other._content ? other._content->clone() : nullptr) {}
    ~Any() { delete _content; }

    Any &swap(Any &other)
    {
        std::swap(_content, other._content);
        return *this;
    }

    // 获取返回子类对象保存数据的指针
    template <class T>
    T *Get()
    {
        assert(typeid(T) == _content->type());
        return &((placeholder<T> *)_content)->_val;
    }

    // 赋值运算符重载函数
    template <class T>
    Any &operator=(const T &val)
    {
        // 为val构造一个临时的通用容器，然后与当前容器进行指针交换，临时对象释放的时候，原先保存的数据也就被释放了
        Any(val).swap(*this);
        return *this;
    }
    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }
};

class Connection;
// DISCONECTED -- 连接关闭状态；   CONNECTING -- 连接建立成功-待处理状态
// CONNECTED -- 连接建立完成，各种设置已完成，可以通信的状态；  DISCONNECTING -- 待关闭状态
typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
} ConnStatu;
using PtrConnection = std::shared_ptr<Connection>;

// 继承enable_shared_from_this用于在类内部安全地获取指向自身的shared_ptr，
// 防止使用裸指针(this)传递给回调函数时导致对象被提前析构
class Connection : public std::enable_shared_from_this<Connection>
{
private:
    uint64_t _conn_id; // 连接的唯一ID，便于连接的管理和查找
    // uint64_t _timer_id;   //定时器ID，必须是唯一的，这块为了简化操作使用conn_id作为定时器ID
    int _sockfd;                   // 连接关联的文件描述符
    bool _enable_inactive_release; // 连接是否启动非活跃销毁的判断标志，默认为false
    EventLoop *_loop;              // 连接所关联的一个EventLoop
    ConnStatu _statu;              // 连接状态
    Socket _socket;                // 套接字操作管理
    Channel _channel;              // 连接的事件管理
    Buffer _in_buffer;             // 输入缓冲区---存放从socket中读取到的数据
    Buffer _out_buffer;            // 输出缓冲区---存放要发送给对端的数据
    Any _context;                  // 请求的接收处理上下文 (例如：处理HTTP协议时，这里可用来保存HTTP请求报文的解析进度或状态机对象)

    /*这四个回调函数，是让服务器模块来设置的（其实服务器模块的处理回调也是组件使用者设置的）*/
    /*换句话说，这几个回调都是组件使用者使用的*/
    using ConnectedCallBack = std::function<void(const PtrConnection &)>;
    using MessageCallBack = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallBack = std::function<void(const PtrConnection &)>;
    using AnyEventCallBack = std::function<void(const PtrConnection &)>;
    ConnectedCallBack _connected_callback;
    MessageCallBack _message_callback;
    ClosedCallBack _closed_callback;
    AnyEventCallBack _event_callback;
    /*组件内的连接关闭回调--组件内设置的，因为服务器组件内会把所有的连接管理起来，一旦某个连接要关闭*/
    /*就应该从管理的地方移除掉自己的信息*/
    ClosedCallBack _server_closed_callback;

private:
    /*五个channel的事件回调函数*/
    // 描述符可读事件触发后调用的函数，接收socket数据放到接收缓冲区中，然后调用_message_callback
    void HandleRead()
    {
        // 1.接收socket的数据，放到缓冲区
        char buf[65536];
        ssize_t ret = _socket.NonBlockRecv(buf, sizeof buf);
        if (ret < 0)
        {
            // 出错，但不可直接关闭连接
            return ShutdownInLoop();
        }
        // 将数据放入输入缓冲区,写入之后顺便将写偏移向后移动
        _in_buffer.WriteAndPush(buf, ret);
        // 2.调用message_callback进行业务处理
        if (_in_buffer.ReadAbleSize() > 0)
        {
            return _message_callback(shared_from_this(), &_in_buffer);
        }
    }

    // 描述符可写事件触发后调用的函数，将发送缓冲区中的数据进行发送
    void HandleWrite()
    {
        //_out_buffer中保存的数据就是要发送的数据
        ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadAbleSize());
        if (ret < 0)
        {
            // 发送错误就该关闭连接了，
            if (_in_buffer.ReadAbleSize() > 0)
            {
                _message_callback(shared_from_this(), &_in_buffer);
            }
            return Release(); // 这时候就是实际的关闭释放操作了。
        }
        _out_buffer.MoveReadOffset(ret);
        if (_out_buffer.ReadAbleSize() == 0)
        {
            _channel.DisableWrite(); // 没有数据待发送了，关闭写事件监控 (重要：如果不关闭，epoll的LT模式会不断触发可写事件导致CPU空转)
            if (_statu == DISCONNECTING)
            {
                return Release();
            }
        }
        return;
    }

    // 描述符触发挂断事件
    void HandleClose()
    {
        /*一旦连接挂断了，套接字就什么都干不了了，因此有数据待处理就处理一下，完毕关闭连接*/
        if (_in_buffer.ReadAbleSize() > 0)
        {
            _message_callback(shared_from_this(), &_in_buffer);
        }
        return Release();
    }

    // 描述符触发出错事件
    void HandleError()
    {
        return HandleClose();
    }

    // 描述符触发任意事件: 1. 刷新连接的活跃度--延迟定时销毁任务；  2. 调用组件使用者的任意事件回调
    void HandleEvent()
    {
        if (_enable_inactive_release == true)
        {
            _loop->TimerRefresh(_conn_id);
        }
        if (_event_callback)
        {
            _event_callback(shared_from_this());
        }
    }

    // 连接获取之后，所处的状态下要进行各种设置（启动读监控,调用回调函数）
    void EstablishedInLoop()
    {
        // 1.修改连接状态，2.启动读事件
        assert(_statu == CONNECTING); // 当前的状态必须是上层的半连接状态
        _statu = CONNECTED;           // 当前函数执行完毕，则连接进入已完成连接状态
        // 一旦启动读事件监控就有可能会立即触发读事件，如果这时候启动了非活跃连接销毁，但底层的定时销毁任务还未真正添加，
        // 就会导致在处理读事件去刷新活跃度时，因找不到对应的定时器任务而发生错误（或引发崩溃）
        _channel.EnableRead();
        if (_connected_callback)
            _connected_callback(shared_from_this());
    }

    // 这个才是实际的释放接口
    void ReleaseInLoop()
    {
        // 1.修改连接状态，将其置为DISCONNECTED
        _statu = DISCONNECTED;
        // 2.移除连接的事件监控
        _channel.Remove();
        // 3.关闭文件描述符
        _socket.Close();
        // 4.如果当前定时器队列中还有定时任务，则取消任务
        if (_loop->HasTimer(_conn_id))
            CancelInactiveReleaseInLoop();
        // 5. 调用关闭回调函数，避免先移除服务器管理的连接信息导致Connection被释放，再去处理会出错，因此先调用用户的回调函数
        if (_closed_callback)
            _closed_callback(shared_from_this());
        // 移除服务器内部管理的连接信息
        if (_server_closed_callback)
            _server_closed_callback(shared_from_this());
    }

    // 这个接口并不是实际的发送接口，而只是把数据放到了发送缓冲区，启动了可写事件监控
    void SendInLoop(Buffer &buf)
    {
        if (_statu == DISCONNECTED)
            return;
        _out_buffer.WriteBufferAndPush(buf);
        if (_channel.WriteAble() == false)
            _channel.EnableWrite(); // 只有当底层socket缓冲区有空间时，epoll才会触发写事件，随后调用HandleWrite真正将数据发送出去
    }

    // 这个关闭操作并非实际的连接释放操作，需要判断还有没有数据待处理，待发送
    void ShutdownInLoop()
    {
        _statu = DISCONNECTING; // 设置连接为半关闭状态
        if (_in_buffer.ReadAbleSize() > 0)
        {
            if (_message_callback)
                _message_callback(shared_from_this(), &_in_buffer);
        }
        // 要么就是写入数据的时候出错关闭，要么就是没有待发送数据，直接关闭
        if (_out_buffer.ReadAbleSize() > 0)
        {
            if (_channel.WriteAble() == false)
                _channel.EnableWrite();
        }
        if (_out_buffer.ReadAbleSize() == 0)
            Release();
    }

    // 启动非活跃连接超时释放规则
    void EnableInactiveReleaseInLoop(int sec)
    {
        // 1. 将判断标志 _enable_inactive_release 置为true
        _enable_inactive_release = true;
        // 2. 如果当前定时销毁任务已经存在，那就刷新延迟一下即可
        if (_loop->HasTimer(_conn_id))
        {
            return _loop->TimerRefresh(_conn_id);
        }
        // 3.如果不存在定时销毁任务则新增
        _loop->TimerAdd(_conn_id, sec, std::bind(&Connection::Release, this));
    }

    // 取消非活跃连接超时
    void CancelInactiveReleaseInLoop()
    {
        _enable_inactive_release = false;
        if (_loop->HasTimer(_conn_id))
        {
            _loop->TimerCancel(_conn_id);
        }
    }

    // 协议切换：重置上下文及事件回调（必须在EventLoop线程中同步执行，防止并发导致新数据被旧协议错误处理）
    void UpgradeInLoop(const Any &context,
                       const ConnectedCallBack &conn,
                       const MessageCallBack &msg,
                       const ClosedCallBack &closed,
                       const AnyEventCallBack &event)
    {
        _context = context;
        _connected_callback = conn;
        _message_callback = msg;
        _closed_callback = closed;
        _event_callback = event;
    }

public:
    Connection(EventLoop *loop, uint64_t conn_id, int sockfd)
        : _sockfd(sockfd),
          _enable_inactive_release(false),
          _loop(loop), _statu(CONNECTING),
          _socket(_sockfd),
          _channel(loop, _sockfd),
          _conn_id(conn_id)
    {
        _channel.SetCloseCallBack(std::bind(&Connection::HandleClose, this));
        _channel.SetEventCallBack(std::bind(&Connection::HandleEvent, this));
        _channel.SetReadCallBack(std::bind(&Connection::HandleRead, this));
        _channel.SetWriteCallBack(std::bind(&Connection::HandleWrite, this));
        _channel.SetErrorCallBack(std::bind(&Connection::HandleError, this));
    }
    ~Connection() { DBG_LOG("RELEASE CONNECTION:%p", this); }

    // 获取管理的文件描述符
    int Fd() { return _sockfd; }

    // 获取连接ID
    int Id() { return _conn_id; }

    // 是否处于CONNECTED状态
    bool Connected() { return (_statu == CONNECTED); }

    // 设置上下文--连接建立完成时进行调用
    void SetContext(const Any &context) { _context = context; }

    // 获取上下文，返回的是指针
    Any *Getcontext() { return &_context; }

    void SetConnectedCallBack(const ConnectedCallBack &cb) { _connected_callback = cb; }
    void SetMessageCallBack(const MessageCallBack &cb) { _message_callback = cb; }
    void SetClosedCallBack(const ClosedCallBack &cb) { _closed_callback = cb; }
    void SetAnyEventCallBack(const AnyEventCallBack &cb) { _event_callback = cb; }
    void SetSrvClosedCallBack(const ClosedCallBack &cb) { _server_closed_callback = cb; }

    // 连接建立就绪后，进行channel回调设置，启动读监控，调用_connected_callback
    void Established()
    {
        _loop->RunInLoop(std::bind(&Connection::EstablishedInLoop, this));
    }

    // 发送数据，将数据放到发送缓冲区，启动写事件监控
    void Send(const char *data, size_t len)
    {
        // 外界传入的data，可能是个临时的空间，我们现在只是把发送操作压入了任务池，有可能并没有被立即执行
        // 因此有可能执行的时候，data指向的空间有可能已经被释放了。
        Buffer buf;
        buf.WriteAndPush(data, len); // 使用局部Buffer拷贝数据，结合std::move保证跨线程传递的安全性和高效性
        _loop->RunInLoop(std::bind(&Connection::SendInLoop, this, std::move(buf)));
    }

    // 提供给组件使用者的关闭接口--并不实际关闭，需要判断有没有数据待处理
    void ShutDown()
    {
        _loop->RunInLoop(std::bind(&Connection::ShutdownInLoop, this));
    }

    void Release()
    {
        // 这里强制使用QueueInLoop将释放操作推迟到当前事件循环阶段结束，
        // 防止在其他EventCallback执行中途直接释放掉自身导致野指针崩溃
        _loop->QueueInLoop(std::bind(&Connection::ReleaseInLoop, this));
    }

    // 启动非活跃销毁，并定义多长时间无通信就是非活跃，添加定时任务
    void EnableInactiveRelease(int sec)
    {
        _loop->RunInLoop(std::bind(&Connection::EnableInactiveReleaseInLoop, this, sec));
    }

    // 取消非活跃销毁
    void CancelInactiveRelease()
    {
        _loop->RunInLoop(std::bind(&Connection::CancelInactiveReleaseInLoop, this));
    }

    // 切换协议---重置上下文以及阶段性回调处理函数 -- 而是这个接口必须在EventLoop线程中立即执行
    // 防备新的事件触发后，处理的时候，切换任务还没有被执行--会导致数据使用原协议处理了。
    //  （例如：常用于将普通的HTTP协议升级连接为WebSocket长链接协议）
    void Upgrade(const Any &context, const ConnectedCallBack &conn,
                 const MessageCallBack &msg,
                 const ClosedCallBack &closed,
                 const AnyEventCallBack &event)
    {
        _loop->AssertInLoop();
        _loop->RunInLoop(std::bind(&Connection::UpgradeInLoop, this, context, conn, msg, closed, event));
    }
};

class Acceptor
{
private:
    Socket _socket;   // 用于创建监听套接字
    EventLoop *_loop; // 用于对监听套接字进行事件监控
    Channel _channel; // 用于对监听套接字进行事件管理

    using AcceptCallBack = std::function<void(int)>;
    AcceptCallBack _accept_callback; // 存放处理新连接的上层回调函数

private:
    /*监听套接字的读事件回调处理函数---获取新连接，调用_accept_callback函数进行新连接处理*/
    void HandleRead()
    {
        int newfd = _socket.Accept(); // 接收新连接，获取客户端的套接字描述符
        if (newfd < 0)
        {
            return; // 接收失败，直接返回
        }
        if (_accept_callback)
            _accept_callback(newfd); // 将新连接交由上层设置的回调函数进行处理
    }

    // 创建服务器并返回监听套接字的描述符fd
    int CreateServer(int port)
    {
        bool ret = _socket.CreateServer(port);
        assert(ret == true);
        return _socket.Fd(); // 返回成功创建的监听套接字描述符
    }

public:
    /*不能将启动读事件监控，放到构造函数中，必须在设置回调函数后，再去启动*/
    /*否则有可能造成启动监控后，立即有事件，处理的时候，回调函数还没设置：新连接得不到处理，且资源泄漏*/
    Acceptor(EventLoop *loop, int port)
        : _socket(CreateServer(port)), _loop(loop), _channel(loop, _socket.Fd())
    {
        // 将 HandleRead 绑定为当前 Channel 的读事件回调
        _channel.SetReadCallBack(std::bind(&Acceptor::HandleRead, this));
    }

    // 提供给上层使用的接口：用于注册自定义的新连接处理逻辑
    void SetAcceptCallBack(const AcceptCallBack &cb) { _accept_callback = cb; }

    // 启动 Acceptor：使能监听套接字 Channel 的读事件（挂载到 epoll/poll 树上开始监听）
    void Linst() { _channel.EnableRead(); }
};

class TcpServer
{
private:
    uint64_t _next_id;                                  // 自动增长的连接ID，// 也可以借用作为定时器任务的唯一ID
    int _port;                                          // 服务器监听的端口号
    int _timeout;                                       // 非活跃连接的统计时间
    bool _enable_inactive_release;                      // 是否启动了非活跃连接超时销毁的判断标志
    EventLoop _baseloop;                                // 这是主线程的EventLoop对象，负责监听事件的处理 // 即 Main Reactor，只负责新连接的获取
    Acceptor _acceptor;                                 // 这是监听套接字的管理对象
    LoopThreadPool _pool;                               // 这是从属EventLoop线程池 // 即 Sub Reactor 池，负责已连接客户端的读写事件
    std::unordered_map<uint64_t, PtrConnection> _conns; // 保存管理所有连接对应的shared_ptr对象 // 集中管理，防止连接对象被提前销毁

    // 定义各类事件的回调函数类型，方便上层应用(业务层)注册自定义逻辑
    using ConnectedCallBack = std::function<void(const PtrConnection &)>;
    using MessageCallBack = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallBack = std::function<void(const PtrConnection &)>;
    using AnyEventCallBack = std::function<void(const PtrConnection &)>;
    using Functor = std::function<void()>;

    // 保存上层应用注册的业务回调
    ConnectedCallBack _connected_callback;
    MessageCallBack _message_callback;
    ClosedCallBack _closed_callback;
    AnyEventCallBack _event_callback;

private:
    // 在住EventLoop线程中真正执行添加定时任务的操作
    void RunAfterInLoop(const Functor &task, int delay)
    {
        _next_id++;
        _baseloop.TimerAdd(_next_id, delay, task); // 将任务挂载到主loop的定时器中
    }

    // 为新连接构造一个Connection进行管理
    void NewConnection(int fd)
    {
        _next_id++;
        // 核心调度：从线程池中轮询获取一个 Sub Reactor，将新连接 fd 交给它管理
        PtrConnection conn(new Connection(_pool.NextLoop(), _next_id, fd));
        // 将 TcpServer 收集到的上层业务回调，原封不动地传递给具体的 Connection 对象
        conn->SetMessageCallBack(_message_callback);
        conn->SetClosedCallBack(_closed_callback);
        conn->SetConnectedCallBack(_connected_callback);
        conn->SetAnyEventCallBack(_event_callback);

        // 设置内部回调：当 Connection 关闭时，通知 TcpServer 将其从 _conns 映射表中彻底移除
        conn->SetSrvClosedCallBack(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));
        if (_enable_inactive_release)
            conn->EnableInactiveRelease(_timeout);     // 启动非活跃超时销毁
        conn->Established();                           // 就绪初始化 (通常会在这里触发业务层 connected 回调，并开启读事件监听)
        _conns.insert(std::make_pair(_next_id, conn)); // 存入 map，增加 shared_ptr 的引用计数
    }

    // 真正执行从 map 中移除连接的操作（必须在主线程执行，保证对 _conns 操作的线程安全）
    void RemoveConnectionInLoop(const PtrConnection &conn)
    {
        int id = conn->Id();
        auto it = _conns.find(id);
        if (it != _conns.end())
        {
            _conns.erase(it); // 移除后，若无其他引用，Connection 对象将在此处自动析构释放
        }
    }

    // 从管理Connection的_conns中移除连接信息
    void RemoveConnection(const PtrConnection &conn)
    {
        // 重点跨线程处理：由于 Connection 的断开往往发生在 Sub Reactor 子线程，
        // 而 _conns 是属于 TcpServer(主线程) 的数据。为了防止多线程并发修改 map 引发崩溃，
        // 必须使用 RunInLoop 将“移除操作”作为一个任务投递回主线程的 _baseloop 中执行。
        _baseloop.RunInLoop(std::bind(&TcpServer::RemoveConnectionInLoop, this, conn));
    }

public:
    TcpServer(int port) : _port(port), _next_id(0), _enable_inactive_release(false), _acceptor(&_baseloop, port), _pool(&_baseloop)
    {
        // 将 TcpServer 内部的 NewConnection 方法注册给 Acceptor
        // 当 Acceptor 监听到新连接到来并 accept 成功后，会回调此方法
        _acceptor.SetAcceptCallBack(std::bind(&TcpServer::NewConnection, this, std::placeholders::_1));
        _acceptor.Linst(); // 将监听套接字挂到baseloop上 (开始监听 EPOLLIN 事件)
    }

    // 设置从属线程池的线程数量（需要在 Start 之前调用）
    void SetThreadCount(int count) { return _pool.SetThreadCount(count); }

    // 提供给上层组件调用的接口，用于注册业务处理逻辑
    void SetConnectedCallBack(const ConnectedCallBack &cb) { _connected_callback = cb; }
    void SetMessageCallBack(const MessageCallBack &cb) { _message_callback = cb; }
    void SetClosedCallBack(const ClosedCallBack &cb) { _closed_callback = cb; }
    void SetAnyEventCallBack(const AnyEventCallBack &cb) { _event_callback = cb; }

    // 开启超时踢出机制
    void EnableInactiveRelease(int timeout)
    {
        _timeout = timeout;
        _enable_inactive_release = true;
    }

    // 添加一个定时任务
    void RunAfter(const Functor &task, int delay)
    {
        // 同样为了线程安全，将定时任务投递给主循环所在线程去真正添加
        _baseloop.RunInLoop(std::bind(&TcpServer::RunAfterInLoop, this, task, delay));
    }

    void Start()
    {
        _pool.Create();
        _baseloop.Start();
    }
};

// Channel 并不直接操作底层的 epoll/poll，而是将“移除事件监听”的任务委托给它所绑定的 EventLoop 处理
void Channel::Remove() { return _loop->RemoveEvent(this); }

// 同理，当 Channel 关注的读写事件发生变化时，交由 EventLoop 去更新底层的监听状态
void Channel::Update() { return _loop->UpdateEvent(this); }

// TimerWheel 内部的定时器容器（如数组、哈希表）通常是非线程安全的。
// 为了允许其他线程（比如业务线程或主线程）安全地操作定时器，
// 必须将实际的添加/刷新/取消操作（InLoop系列函数）打包成任务，投递到 TimerWheel 绑定的 EventLoop 线程中串行执行

// 添加定时任务
void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}

// 刷新/延迟定时任务
void TimerWheel::TimerRefresh(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}

// 取消定时任务
void TimerWheel::TimerCancel(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}

class NetWork
{
public:
    // 构造函数：用于在网络模块启动前进行一些全局的初始化工作
    NetWork()
    {
        DBG_LOG("SIGPIPE INIT"); // 打印调试日志，表示开始初始化信号处理

        // 忽略 SIGPIPE 信号 (SIG_IGN 表示 Ignore)
        // 核心原因：在 TCP 网络编程中，如果对端已经关闭了连接，而本端依然尝试向该套接字写入数据，
        // 操作系统默认会向本端进程发送一个 SIGPIPE 信号。该信号的默认行为是终止整个进程。
        // 为了防止服务器因此意外崩溃，必须忽略此信号。忽略后，写入操作只会返回 -1 并设置 errno 为 EPIPE。
        signal(SIGPIPE, SIG_IGN);
    }
};

// 实例化一个全局静态对象 nw
// 原理：C++ 中全局静态对象的构造函数会在主函数 main() 运行之前被系统自动调用。
// 作用：利用这一特性，确保程序一启动就自动完成了对 SIGPIPE 信号的屏蔽，无需开发者在 main 中手动调用初始化。
static NetWork nw;
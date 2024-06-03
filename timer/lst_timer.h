#ifndef _LST_TIMER_H_
#define _LST_TIMER_H_

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <cstring>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <cstdarg>
#include <cerrno>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include "../log/log.h"

class util_timer;

struct client_data { // 描述客户端连接数据
    // 客户端socket地址
    sockaddr_in address;
    // Socket文件描述符
    int sockfd;
    // 客户端连接对应的定时器
    util_timer *timer;
};

class util_timer { // 定时器类
public:
    util_timer() : prev(NULL), next(NULL) {}
public:
    time_t expire; // 超时时间
    void (*cb_func)(client_data *); // 定时器回调函数，用于释放客户连接资源
    client_data *user_data; // 定时器对应的客户端数据
    util_timer *prev;       // 前驱定时器
    util_timer *next;       // 后继定时器
};

class sort_timer_list { // 定时器升序链表类
public:
    sort_timer_list();
    ~sort_timer_list();

    void add_timer(util_timer *timer); // 插入定时器
    void adjust_timer(util_timer *timer); // 定时任务发生变化，调整定时器
    void del_timer(util_timer *timer); // 删除超时的定时任务
    void tick(); // SIGALRM信号每次被触发，就在其信号处理函数（如果使用统一事件源，则是主函数）中执行一次tick函数，以处理链表上到期的任务

private:
    // 私有成员函数，被add_timer和adjust_timer调用
    void add_timer(util_timer *timer, util_timer *lst_head);
    // 构造升序定时器的双向链表，按timer -> expire的顺序调整
    util_timer *head;
    util_timer *tail;
};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);
    // 设置文件描述符为非阻塞
    int setnonblocking(int fd);
    // 为内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);
    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();
    void show_error(int connfd, const char *info);
public:
    static int *u_pipefd;
    sort_timer_list m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
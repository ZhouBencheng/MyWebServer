#ifndef _WEBSERVER_H
#define _WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"
#include "../timer/lst_timer.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer {

public:
    WebServer();
    ~WebServer();

    // webserver初始化函数，参考config类型的参数
    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();     // 初始化线程池
    void sql_pool();        // 初始化数据库连接池
    void log_write();       // 初始化日志器
    void trig_mode();       // 初始化触发方式
    void eventListen();     // 创建服务器套接字
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    //基础
    int m_port;
    char *m_root;       // 保存root文件夹路径
    int m_log_write;    // 当前日志器写入方式——异步或同步
    int m_close_log;    // 是否打开日志器
    int m_actormodel;   // proactor 或 reactor

    int m_pipefd[2];
    int m_epollfd;      // epoll方式IO多路复用
    http_conn *users;   // HTTP请求数组

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;     // 服务器端套接字
    int m_OPT_LINGER;
    int m_TRIGMode;     // 组合触发模式LT\ET
    int m_LISTENTrigmode;// 监听套接字触发模式LT\ET
    int m_CONNTrigmode; // 连接触发模式LT\ET

    //定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif

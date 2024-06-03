#ifndef _HTTP_CONNECTION_H
#define _HTTP_CONNECTION_H
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
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <mysql/mysql.h>
#include <fstream>
#include <map>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn {// HTTP请求类型

public:
    //设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    //报文的请求方法，本项目只用到GET和POST
    enum METHOD { // HTTP请求方法
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
    //主状态机的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,// 解析请求行
        CHECK_STATE_HEADER,         // 解析请求头
        CHECK_STATE_CONTENT         // 解析消息体
    };
    //报文解析的结果
    enum HTTP_CODE {
        NO_REQUEST,     // 请求不完整，需要继续读取请求报文数据
        GET_REQUEST,    // 获取完整的HTTP报文请求
        BAD_REQUEST,    // HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR, // 服务器内部错误
        CLOSED_CONNECTION
    };
    //从状态机的状态
    enum LINE_STATUS {
        LINE_OK = 0,// 完整读取一行
        LINE_BAD,   // 报文语法错误
        LINE_OPEN   // 读取行不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

    //初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);
    void process();
    //读取浏览器端发来的全部数据
    bool read_once();
    //响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();
    //从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    //向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    //主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    //主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    //主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();
    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
    //从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();
    // 释放内存中映射的服务器请求文件所占空间
    void unmap();
    //根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    //存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    long m_read_idx;
    //m_read_buf读取的位置m_checked_idx
    //m_checked_idx总是指向第一个为解析的字符
    //m_checkde_idx有parse_line()方法更新
    long m_checked_idx;
    //m_read_buf中已经解析的字符个数
    //m_read_buf+m_start_line为第一个未解析字符
    int m_start_line;
    //存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    //指示buffer中的长度
    int m_write_idx;
    //主状态机的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //以下为解析请求报文中对应的6个变量
    //从请求行和请求头中获取
    char m_real_file[FILENAME_LEN]; //存储读取文件的路径
    char *m_url;     // 请求资源地址
    char *m_version; // 请求报文的HTTP版本
    char *m_host;
    long m_content_length;
    bool m_linger;  //connection字段，决定长连接还是短连接

    char *m_file_address; //存储服务器请求文件在内存中的映射
    struct stat m_file_stat;
    //io向量机制iovec
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求数据
    int bytes_to_send;  //剩余发送字节数
    int bytes_have_send;//剩余发送字节数
    char *doc_root;     // 记录当前服务器所有提供文件的路径

    map<string, string> m_users;
    int m_TRIGMode; // 触发模式
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif

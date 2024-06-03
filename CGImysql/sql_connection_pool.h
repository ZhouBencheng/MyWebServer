#ifndef _SQL_CONNECTION_POOL_H_
#define _SQL_CONNECTION_POOL_H_

#include <mysql/mysql.h>
#include <cstdio>
#include <list>
#include <string>
#include <cerrno>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool { // 定义数据库连接池类型
public: // 公有成员变量
    string m_url;           // 主机地址
    string m_Port;          // 数据库端口号
    string m_User;          // 数据库用户名
    string m_Password;      // 数据库密码
    string m_DatabaseName;  // 使用数据库名
    int m_close_log;        // 日志开关

private:
    connection_pool();
    ~connection_pool();
    
    int m_MaxConn;          // 最大连接数
    int m_CurConn;          // 当前已使用的连接数
    int m_FreeConn;         // 当前未使用连接数
    locker lock;
    list<MYSQL *> connList; // 连接池
    sem reserve;            // 使用信号量同步连接池中剩余连接数的管理

public:
    MYSQL *GetConnection(); // 获取数据库连接
    bool ReleaseConnection(MYSQL *conn); // 释放连接
    int GetFreeConn();      // 获取连接
    void DestroyPool();     // 销毁所有连接

    static connection_pool *GetInstance(); // 单例模式
    
    // 初始化数据库连接池
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);
};

// 使用RAII机制管理一个数据库连接
// 注意该类型封装的数据库连接同时也包含指向连接池的指针
class connectionRAII {

public: 
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif
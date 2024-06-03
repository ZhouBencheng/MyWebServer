#ifndef THREAD_H_
#define THREAD_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template<typename T>
class threadpool {
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列，泛型T一般为请求类型
    locker m_queuelocker;       //访问请求队列的互斥锁
    sem  m_queuestat;            //与等待任务数量同步的信号量
    connection_pool *m_connPool;  //数据库连接池指针
    int m_actor_model;          //模型切换
};

#include "threadpool.cpp"

#endif
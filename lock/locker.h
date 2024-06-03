#ifndef _LOCKER_H_
#define _LOCKER_H_

#include <exception>
#include <semaphore.h>
#include <pthread.h>

/// 定义信号量类型
class sem {

private: // 内部封装一个信号量，注意此处采用RAII机制
    sem_t m_sem;

public: // 信号量类型的成员方法
    sem();          // 默认零初始化
    sem(int num);   // 参量初始化
    ~sem();
    bool wait();    // P操作
    bool post();    // V操作

};

/// 定义互斥锁类型
class locker { // 定义互斥锁类型

private: // 内部封装一个互斥锁，RAII
    pthread_mutex_t m_mutex;

public:
    locker();       // 构造函数
    ~locker();      // 析构函数
    bool lock();    // 加锁
    bool unlock();  // 解锁
    pthread_mutex_t *get() { return &m_mutex; } // 获取互斥锁指针
};

/// 定义条件变量类型
class cond {

private: // 内部封装一个条件变量，RAII
    pthread_cond_t m_cond;

public:
    cond();
    ~cond();
    bool wait(pthread_mutex_t *m_mutex);// 等待条件变量
    bool signal();                      // 唤醒等待条件变量的线程
    bool broadcast();                   // 广播唤醒所有等待条件变量的线程
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t); // 超时等待条件变量  
};

#endif _LOCKER_H_
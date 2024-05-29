#include <locker.h>

/// 信号量零初始化方法 
sem::sem() {
    if(sem_init(&m_sem, 0, 0) != 0) {
        throw std::exception();
    }
}

/// 信号量含参初始化
sem::sem(int num) {
    if(sem_init(&m_sem, 0, num) != 0) {
        throw std::exception();
    }
}

/// 信号量销毁方法
sem::~sem() {
    sem_destroy(&m_sem);
}

/// 信号量P操作
bool sem::wait() {
    return sem_wait(&m_sem) == 0;
}

/// 信号量V操作
bool sem::post() {
    return sem_post(&m_sem) == 0;
}

/// 互斥锁初始化
locker::locker() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0) {
        throw std::exception();
    }
}

/// 互斥锁销毁
locker::~locker() {
    pthread_mutex_destroy(&m_mutex);
}

/// 互斥锁加锁，原子操作
bool locker::lock() {
    return pthread_mutex_lock(&m_mutex) == 0;
}

/// 互斥锁解锁，原子操作
bool locker::unlock() {
    return pthread_mutex_unlock(&m_mutex) == 0;
}

/// 条件变量初始化
cond::cond() {
    if (pthread_cond_init(&m_cond, NULL) != 0) {
        throw std::exception();
    }
}

/// 条件变量销毁
cond::~cond() {
    pthread_cond_destroy(&m_cond);
}

/// 等待条件变量
bool cond::wait(pthread_mutex_t *m_mutex) {
    int ret = 0;
    ret = pthread_cond_wait(&m_cond, m_mutex);
    return ret == 0;
}

/// 带超时的等待
bool cond::timewait(pthread_mutex_t *m_mutex, struct timespec t) {
    int ret = 0;
    ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
    return ret == 0;
}

/// 唤醒一个等待线程
bool cond::signal() {
    return pthread_cond_signal(&m_cond) == 0;
}

/// 唤醒所有等待线程
bool cond::broadcast() {
    return pthread_cond_broadcast(&m_cond) == 0;
}

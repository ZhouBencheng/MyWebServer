#ifndef _THREADPOOL_CPP_
#define _THREADPOOL_CPP_

#include "threadpool.h"

/// @brief 线程池构造函数
/// @tparam T 一般用于代指HTTP请求
/// @param actor_model 指定处理模式，分别为reactor和proactor
/// @param connPool 指定数据库连接池指针
/// @param thread_number 当前线程池中线程数量（等待队列长度）
/// @param max_requests 线程池允许的最大请求数
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests)
: m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 线程id初始化
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i) {// 根据worker线程函数创建线程
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) { // 创建线程失败
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) { // 将线程设置为分离线程，线程结束后自动释放资源
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) { // 已超出最大请求数量
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

/// 向list容器创建的请求队列中添加请求任务
template <typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg) { // 线程函数
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait(); // 等待信号量，该信号量代表正在等待的任务数量
        m_queuelocker.lock(); // 互斥访问等待队列
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        //从请求队列中取出第一个任务
        //将任务从请求队列删除
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model) {
            if (0 == request->m_state) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif
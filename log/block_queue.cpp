#ifndef _BLOCK_QUEUE_CPP_
#define _BLOCK_QUEUE_CPP_

#include "block_queue.h"

template<typename T>
block_queue<T>::block_queue(int max_size) {
    if (max_size <= 0) {
        exit(-1);
    }
    m_max_size = max_size;
    m_array = new T[max_size];
    m_size = 0;
    m_front = -1;
    m_back = -1;
}

template<typename T>
void block_queue<T>::clear() {
    m_mutex.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
}

template<typename T>
block_queue<T>::~block_queue() {
    m_mutex.lock();
    if (m_array != nullptr) {
        delete [] m_array;
    }
    m_mutex.unlock();
}

template<typename T>
bool block_queue<T>::full() {
    m_mutex.lock();
    if (m_size >= m_max_size) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<typename T>
bool block_queue<T>::empty() {
    m_mutex.lock();
    if (m_size == 0) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template<typename T>
bool block_queue<T>::front(T &value) {
    m_mutex.lock();
    if (0 == m_size) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
}

template<typename T>
int block_queue<T>::size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_size;
    m_mutex.unlock();
    return tmp;
}

template<typename T>
int block_queue<T>::max_size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_max_size;
    m_mutex.unlock();
    return tmp;
}

template<typename T>
bool block_queue<T>::push(const T &item) {
    m_mutex.lock();
    if (m_size >= m_max_size) {
        m_cond.broadcast();
        return false;
    }
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size++;
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}

template<typename T>
bool block_queue<T>::pop(T &item) {
    m_mutex.lock();
    while (m_size <= 0) {
        if (!m_cond.wait(m_mutex.get())) {
            m_mutex.unlock();
            return false;
        }
    }
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front]; // 注意此处m_front指向队首元素的前一个，因此是左开右闭区间
    m_size--;
    m_mutex.unlock();
    return true;
}

template<typename T>
bool block_queue<T>::pop(T &item, int ms_timeout) {
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    m_mutex.lock();
    if (m_size <= 0) {
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000;
        if (!m_cond.timewait(m_mutex.get(), t)) {
            m_mutex.unlock();
            return false;
        }
    }

    if (m_size <= 0) {
        m_mutex.unlock();
        return false;
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}

#endif
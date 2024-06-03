#ifndef _BLOCK_QUEUE_H_
#define _BLOCK_QUEUE_H_

#include <iostream>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

template <typename T>
class block_queue { // 定义阻塞队列类型(实际为循环数组)

private:
    locker m_mutex;
    cond m_cond;

    T* m_array;     // 用于构造队列的循环数组
    int m_size;     // 当前队列长度
    int m_max_size; // 队列最大长度
    int m_front;    // 队头下标
    int m_back;     // 队尾下标

public:
    block_queue(int max_size = 1000); // 默认队列长度为1000
    ~block_queue();
    void clear();           // 清空队列
    bool full();            // 判断队列是否已满
    bool empty();           // 判断队列是否为空
    bool front(T& value);   // 获取队头元素
    bool back(T& value);    // 获取队尾元素
    int size();             // 获取队列长度
    int max_size();         // 获取队列最大长度
    bool push(const T &item);// 向队列添加元素
    bool pop(T &item);      // 弹出队列元素，队列为空时会阻塞并等待
    bool pop(T &item, int ms_timeout); // 弹出队列元素，但限时等待
};

#include "block_queue.cpp"

#endif
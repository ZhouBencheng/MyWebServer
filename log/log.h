#ifndef _LOG_H_
#define _LOG_H_

#include <cstdio>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log {

public: // 公有方法
    static Log* get_instance() { // 懒惰式&单例模式返回日志器
        static Log instance;
        return &instance;
    }
    static void *flush_log_thread(void *args) {
        Log::get_instance() -> async_write_log();
    }
    // 初始化日志器
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    // 按照输出等级将标准内容输出
    void write_log(int level, const char *format, ...);
    // 强制刷新当前日志器中目标文件的写入
    void flush(void);

private:// 私有方法
    Log();
    virtual ~Log();

    void* async_write_log() {
        string single_log;
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数，但文件中日志行数超过时另起新日志文件
    int m_log_buf_size; //日志缓冲区大小

    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;        //写日志缓冲区，该缓冲区仅存储一行日志
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;
    int m_close_log; //关闭日志
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
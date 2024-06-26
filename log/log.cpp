#include "log.h"
#include <cstring>


Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    if (m_fp != nullptr) {
        fclose(m_fp);
    }
}

/// @param file_name 日志文件
/// @param close_log 是否关闭日志
/// @param log_buf_size 日志缓冲区大小
/// @param split_lines 日志最大行数
/// @param max_queue_size 日志队列最大长度
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    if(max_queue_size > 0) { 
        // 当阻塞队列长度大于0时，开启异步写入
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        // 将flush_log_thread作为回调函数，开启异步写线程
        pthread_t tid;
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    // 若输入的文件名不带路径，即'/'符号，则直接将文件名拷贝到log_full_name中
    if(p == nullptr) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name +1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    // 使用‘a’方法打开文件时，文件不存在会自动创建
    m_fp = fopen(log_full_name, "a");

    return m_fp ? true : false;
}

void Log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 设置日志等级前缀
    char s[16] = {0};
    switch (level) {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    m_mutex.lock();
    m_count++;

    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};    // 存储新文件名
        fflush(m_fp);               
        fclose(m_fp);               // 放弃旧日志文件
        char tail[16] = {0};        // 存储日知名中日期信息

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);


        if(m_today != my_tm.tm_mday) {
            // 当前时间不是当天，则用新时间开启新日志文件
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        } else {
            // 若日志行数达到上限，则新建日志文件，末尾标志行数分块
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a"); // 打开新日志文件
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str; // 存储一行日志内容
    m_mutex.lock();
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf; // 使用char*类型构造string类型，用于推入阻塞队列

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full()) {
        m_log_queue->push(log_str);
    } else {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}

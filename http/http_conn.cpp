#include "http_conn.h"

//定义http响应的状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users; // user表中所有用户名和密码的映射集合

// 初始化静态成员变量
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 获取数据库中的用户名和密码，保存在map中
void http_conn::initmysql_result(connection_pool *connPool) {
    // 从数据库连接池中取一个连接
    MYSQL *mysql = nullptr;
    connectionRAII mysqlcon(&mysql, connPool);
    // 在user数据表中检索username,password数据
    if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    // 获取表中的完整检索结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        string u(row[0]);
        string p(row[1]);
        users[u] = p;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 为内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)  // 边缘触发ET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else                // LT模式
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot) // 选择是否开启EPOLLONESHOT模式
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化HTTP连接,外部调用初始化套接字地址
// 调用内部私有init()函数
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// http_conn私有初始化函数，简单的零初始化成员变量
void http_conn::init() {
    mysql = NULL;                           // 无mysql连接
    bytes_to_send = 0;                      // 无字节待发送
    bytes_have_send = 0;                    // 无字节已发送
    m_check_state = CHECK_STATE_REQUESTLINE; // 主状态机先解析请求行
    m_linger = false;                       // 是否为长连接，connection字段
    m_method = GET;                         // 默认为GET方法
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;                                //默认未启用POST
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 读取客户端发送的数据，注意ET模式下要一次性读取所有数据
bool http_conn::read_once() {
    if(m_read_idx >= READ_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;   // 记录本次读取的字节数
    if(0 == m_TRIGMode) { // LT模式读取数据
        // 从客户端socket描述符读取数据到m_read_buf指针指向的缓冲区中
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        // 判断本次读取是否成功
        return bytes_read <= 0 ? false : true;
    } else { // ET模式读取数据
        while(true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1) {
                // 以下两个错误码表示资源暂不可用，意味着操作会阻塞执行
                if(errno = EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            } else if(bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 从请求报文中解析出一行
// 将'\r\n'结尾的一行内容改为'\0\0'结尾的可识别字符串
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            if(m_checked_idx + 1 == m_read_idx)
                return LINE_OPEN; // 当前最后一个字符是'\r'，读取尚未结束
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD; // 当前解析行的语法错误
        } else if(temp == '\n') {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD; // 当前解析行的语法错误
        }
    }
    return LINE_OPEN;
}

// HTTP请求行解析方法，获取请求方法、HTTP版本号、目标URL
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    // 将m_url指向text字符串中第一个空格或制表符位置
    m_url = strpbrk(text, " \t");
    if(!m_url) return BAD_REQUEST;

    // 判断当前请求行中的请求方法，本项目仅处理GET和POST方法
    *m_url++ = '\0';
    char *method = text;
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {    // 请求方法解析失败，语法错误
        return BAD_REQUEST;
    }
    
    // 让m_url越过连续的空格或制表符，让其指向第一个有效的字符
    m_url += strspn(m_url, " \t");
    
    // 让m_version指向第三个字段——HTTP版本的位置
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    // 当前仅支持HTTP/1.1版本
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;

    // 处理某些报文的请求资源中有http://，将其忽略
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    //同样对请求报文中包含https://的情况进行处理
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // 一般请求报文中不包含上述两种符号，直接是单独的'/'或者‘/’后面带访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    //当url为‘/’时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求行处理结束，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析HTTP请求头和空行，请求头只取connection和content_length字段
// 请求头中各个字段换行分隔，因此请求头会被parse_headers()解析多次
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if(text[0] == '\0') {
        if(m_content_length != 0) { // 当前为POST请求，要继续解析消息体
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;         // GET请求，解析结束
    } else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += strlen("Connection:");
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true; // 如果是长连接则设置m_linger
        }
    } else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += strlen("Content-Length:");
        text += strspn(text, " \t");
        m_content_length = atol(text); // atol函数用于将字符串形式的数字转化为long整数型
    } else if(strncasecmp(text, "Host:", 5) == 0) {
        text += strlen("Host:");
        text += strspn(text, " \t");
        m_host = text;
    } else {
        LOG_INFO("oop! unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text ) {
    // 判断当前m_read_buf是否已经接收完请求数据
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机模型，用于从buffer中取出所有完整的行
http_conn::HTTP_CODE http_conn::process_read() {
    // 设置从状态机初始状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 循环条件一：主状态机处于CHECK_STATE_CONTENT状态，表示当前正在解析消息体
    // 但消息体最后没有\r\n结尾，因此整个消息体作为一行解析出来，但parse_line()返回LINE_OPEN
    // 因此采用将line_status设置为LINE_OPEN的方式强制跳出循环
    // 循环条件二：parse_line()函数解析出一行内容，且该行内容是完整的
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text); // 将当前解析出的一行请求内容记入日志器中
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default:
                return NO_REQUEST;
        }
    }
    return NO_REQUEST;
}

// 处理当前HTTP请求
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);          // 先保存当前服务器服务文件的路径
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');    // 在url中找到最后一个'/'字符
    
    // 处理CGI请求，响应注册登录
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 判断是注册还是登录
        char flag = m_url[1];
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file +len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 提取用户名和密码
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3') { // 注册处理，防止当前注册的用户名和密码在数据库中重复
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        } else if(*(p +1) == '2') { // 处理登录请求
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 处理其他请求
    if (*(p + 1) == '0') { // 注册请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '1') { // 登录请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '5') { // 图片请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '6') { // 视频请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 判断目标文件状态
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    // 打开文件
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 释放服务器请求文件的内存空间
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write() { // 将响应报文发送到客户端
    int temp = 0;

    // 发送的数据长度为0，响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    // 循环发送响应报文数据
    while (true) {
        temp = writev(m_sockfd, m_iv, m_iv_count); // 获取成功发送的字节数

        if (temp < 0) {
            //
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            // 发送响应报文失败，不可恢复
            unmap();
            return false;
        }

        // 更新已发送字节数
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len) { 
            // 当前已发送字节超出iovec[0]，即发送完响应报文状态行和状态头，一部分消息体
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            // 当前响应行和响应头尚未发送完
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 数据发送完成
        if (bytes_to_send <= 0) {
            unmap();
            // 在epoll树上重新注册EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger){ // 浏览器请求为长连接
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

/// @brief 根据ret状态设置状态行、状态头、响应信息
/// @param ret do_request()的返回状态
/// @return 表示本次响应报文写操作是否成功
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret) {
        case INTERNAL_ERROR: { // 服务器内部错误
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST: { // HTTP请求语法错误
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: { // 无权限访问
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST: { // 成功访问请求文件
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                // 状态行和状态头已经写入m_write_buf中，由iovec[0]指代
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                // 消息体mmap在m_file_address地址，由iovec[1]指代
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else { // 请求资源大小为0
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    // 除了FILE_REQUEST外，其他状态只需要申请一个iovec返回响应报文
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 添加响应内容，依次被下面的具体信息的写操作函数调用
bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) return false;

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

// 添加状态行，包裹HTTP版本，状态码，状态信息
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
// 添加状态头，包括消息体长度，是否为长连接，添加空行
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
// 状态头添加消息体长度
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
// 添加content-type字段
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 添加连接状态信息
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
// 添加空行
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
// 添加消息体
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

// 整个HTTP请求处理中的顶层处理函数，以上一切函数皆直接或间接为此函数提供服务
void http_conn::process() {
    // 读取并解析HTTP请求信息
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) { //尚未读取完成
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) { //响应报文写操作失败
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

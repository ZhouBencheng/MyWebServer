#include "sql_connection_pool.h"

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log) {
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_Password = PassWord;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;
    // 创建数据库连接
    for(int i = 0; i < MaxConn; i++) {
        MYSQL *con = nullptr;
        con = mysql_init(con); // 获取一个MYSQL结构体指针
        if(con == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 初始化该结构体
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);
        if(con == nullptr) {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        // 更新连接池
        connList.push_back(con);
        // 更新空闲连接数量
        m_FreeConn++;
    }
    // 将信号量初始化为最大连接数
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

/// 获取数据库连接，若无剩余则返回空指针，或者多线程等待连接
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = nullptr;
    if(0 == connList.size()) {
        return nullptr;
    }
    reserve.wait();
    lock.lock();
    
    con = connList.front();
    connList.pop_front();
    
    m_FreeConn--;
    m_CurConn++;

    lock.unlock();
    return con;
}

/// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

/*******************RAII类型的成员方法********************/

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}

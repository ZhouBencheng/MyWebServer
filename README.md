# 实现一个TinyWebServer

## 项目结构介绍
    .  
    ├── CGImysql----------------------（数据库连接池类型） 
    ├── config ------------------------(Webserver配置类型)
    ├── http---------------------------(kernel)  
    ├── lock---------------------------(互斥/同步类型)
    ├── log----------------------------(日志器类型)
    ├── root---------------------------(服务器资源目录)
    ├── threadpool---------------------(线程池类型)
    ├── timer--------------------------(定时器类型)
    └── main.cpp-----------------------(程序入口)
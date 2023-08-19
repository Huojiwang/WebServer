#ifndef _SQL_CONNECTION_POOL_
#define _SQL_CONNECTION_POOL_

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include "../lock/locker.h"
#include "../Log/log.h"

class connection_pool{
    public:
        MYSQL *GetConnection();
        bool ReleaseConnection(MYSQL *conn);
        int GetFreeConn();
        void DestoryPool();

        static connection_pool *GetInstance();
        void init(std::string url,std::string User,std::string PassWord,string DataBaseName,int port, int MaxConn,int close_log);
    private:
        connection_pool();
        ~connection_pool();
        
        int m_MaxConn;  //最大连接数
        int m_CurConn;  //当前已使用的连接数
        int m_FreeConn; //当前空闲的连接数
        locker lock;
        std::list<MYSQL *> connlist;    //连接池
        sem reserve;
    public:
        std::string m_url;  //主机地址
        std::string m_port; //数据库登录端口号
        std::string m_User; //登录数据库用户名
        std::string m_Password; //登录数据库密码
        std::string m_DataBaseName; //数据库名字
        int m_close_log;    //日志开关
        
};

class connectionRAII{
    public :
        connectionRAII(MYSQL **conn,connection_pool *connPool);
        ~connectionRAII();
    private:    
        MYSQL *connRAII;
        connection_pool *poolRAII;
};

#endif
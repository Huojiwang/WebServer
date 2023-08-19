#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//创建多个连接数据库的链接，放入connlist中，用到时不需要再次创建一个链接
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log){
	m_url = url;
	m_port = Port;
	m_User = User;
	m_Password = PassWord;
	m_DataBaseName = DBName;
	m_close_log = close_log;

	for(int i = 0; i < MaxConn; ++i){
		MYSQL *con = NULL;
		con = mysql_init(con);
		if(con == NULL){
			LOG_ERROR("MYSQL ERROR");
			exit(1);
		}
		con = mysql_real_connect(con,url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str(),Port,NULL,0);
		if(con == NULL){
			LOG_ERROR("MYSQL ERROR");
			exit(1);
		}
		connlist.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn);
	m_MaxConn = m_FreeConn;
}

//有请求时，从数据库连接池中返回一个可用的链接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection(){
	MYSQL *con = NULL;
	if(0 == connlist.size()){
		return NULL;
	}
	reserve.wait();
	lock.lock();

	con = connlist.front();
	connlist.pop_front();

	--m_FreeConn;
	++m_CurConn;

	lock.unlock();
	return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con){
	if(NULL == con){
		return false;
	}
	lock.lock();
	connlist.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();
	reserve.post();
	return true;
}

void  connection_pool::DestoryPool(){
	lock.lock();
	if(connlist.size() > 0){
		list<MYSQL *>::iterator iter;
		for(iter = connlist.begin(); iter != connlist.end(); ++iter){
			MYSQL *con = *iter;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connlist.clear();
	}
	lock.unlock();
}

int connection_pool::GetFreeConn(){
	return this->m_FreeConn;
}

connection_pool::~connection_pool(){
	DestoryPool();
}

//通过使用connectionRAII对象可以避免手动的管理数据库连接的释放，提高代码的可维护性和安全性，资源获取即初始化。资源管理技术
connectionRAII::connectionRAII(MYSQL **SQL,connection_pool *connpool){
	*SQL = connpool->GetConnection();

	connRAII = *SQL;
	poolRAII = connpool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(connRAII);
}
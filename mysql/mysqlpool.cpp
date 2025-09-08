#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <iostream>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include "mysqlpool.h"

using namespace std;

connection_pool::connection_pool(){
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance(){
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log){
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++){
		MYSQL *con = mysql_init(nullptr);

		if (con == NULL) {
			spdlog::error("mysql_init failed: {}", mysql_error(nullptr)); 
			exit(1);
		}

		MYSQL *conn_result = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
		if (conn_result == NULL) {
			spdlog::error("mysql_real_connect failed: {}", mysql_error(con));
			exit(1);
		}
			connList.emplace_back(std::move(con), mysql_deleter);
			++m_FreeConn;
	}

	m_MaxConn = m_FreeConn;
}


// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
connPtr connection_pool::GetConnection(){
    std::unique_lock<std::mutex> lock(m_mutex);  // 自动加锁，支持条件变量wait

	m_cond.wait(lock, [this](){
		return m_FreeConn > 0;
	} );

	connPtr conn = std::move(connList.front());
	connList.pop_front();
	MYSQL* raw_conn = conn.release();
	--m_FreeConn;
	++m_CurConn;

	 return connPtr(raw_conn, [this](MYSQL* conn) {
        this->ReleaseConnection(conn);
    });
}
 
// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL* conn) {
    if (!conn) return false;

    std::lock_guard<std::mutex> lock(m_mutex); 

    connList.emplace_back(conn, mysql_deleter);
    ++m_FreeConn;
    --m_CurConn;

    // 通知一个等待的线程（有新连接可用）
    m_cond.notify_one();
    return true;
}

void connection_pool::DestroyPool() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 清空列表时，智能指针会自动调用mysql_deleter关闭连接
    connList.clear();
    m_CurConn = 0;
    m_FreeConn = 0;
}

// 获取当前空闲连接数
int connection_pool::GetFreeConn() {
    return m_FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}
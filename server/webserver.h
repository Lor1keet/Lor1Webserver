#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "asio.hpp"
#include <memory>
#include <vector>
#include <string>
#include "http_conn.h"
#include "../mysql/mysqlpool.h"

class Connection;
using asio::ip::tcp;

class WebServer {
public:
    // 初始化服务器核心参数
    WebServer(asio::io_context& io_context,  
              connection_pool* conn_pool, int thread_num,
              const std::string& root, const std::string& user, 
              const std::string& passwd, const std::string& dbname);

    // 绑定IP和端口并开始监听
    bool listen(const std::string& ip, const std::string& port);
    
    // 启动服务器：运行事件循环线程池
    void run();

    ~WebServer() = default;

private:
    // 异步接受新连接
    void accept();

private:
    asio::io_context& io_context_;  // Asio事件循环上下文（外部管理生命周期）
    tcp::acceptor acceptor_;        // TCP连接监听器
    connection_pool* conn_pool_;    // 数据库连接池指针
    int thread_num_;                // 线程池大小
    asio::signal_set signals_;      // 信号处理器（处理终止信号）
    std::string root_;              // 网页根目录
    std::string user_;              // 数据库用户名
    std::string passwd_;            // 数据库密码
    std::string dbname_;            // 数据库名
};

// 客户端连接类：封装TCP连接的I/O和超时管理
class Connection : public std::enable_shared_from_this<Connection> {
public:
    // 初始化连接参数
    Connection(tcp::socket socket, connection_pool* conn_pool, 
               const std::string& root, const std::string& user, 
               const std::string& passwd, const std::string& dbname);

    // 启动连接处理：开始读取请求并设置超时
    void start();

private:
    // 异步读取HTTP请求数据
    void do_read();

    // 异步发送HTTP响应数据
    void do_write();

    // 重置超时定时器（延长超时时间）
    void reset_timer();

    // 关闭连接（释放资源）
    void close();

private:
    tcp::socket socket_;            // 客户端TCP套接字
    connection_pool* conn_pool_;    // 数据库连接池指针
    asio::steady_timer timer_;      // 连接超时定时器（绑定到socket执行器）
    http_conn http_;                // HTTP请求处理对象
    char buffer_[4096];             // 数据读取缓冲区
    std::string m_root;             // 网页根目录
    std::string m_user;             // 数据库用户名
    std::string m_passWord;         // 数据库密码
    std::string m_databaseName;     // 数据库名
};

#endif

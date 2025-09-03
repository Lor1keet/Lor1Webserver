#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstring>
#include <mysql/mysql.h>
#include <asio.hpp>

#include "spdlog/spdlog.h"
#include "../mysql/mysqlpool.h"

// HTTP请求方法
enum class METHOD {
    GET, POST, HEAD, PUT, DELETE, 
    TRACE, OPTIONS, CONNECT, PATH
};

enum class CHECK_STATE {
    CHECK_STATE_REQUESTLINE,  // 解析请求行
    CHECK_STATE_HEADER,       // 解析请求头
    CHECK_STATE_CONTENT       // 解析请求体
};

enum HTTP_CODE {
    NO_REQUEST,           // 未完成解析
    GET_REQUEST,          // 解析完成
    BAD_REQUEST,          // 错误请求
    NO_RESOURCE,          // 资源不存在
    FORBIDDEN_REQUEST,    // 禁止访问
    FILE_REQUEST,         // 文件请求（成功）
    INTERNAL_ERROR,       // 服务器内部错误
    CLOSED_CONNECTION,     // 连接关闭,
    REDIRECT
};

enum class LINE_STATUS {
    OK,   // 完整行
    BAD,  // 格式错误
    OPEN  // 未完成
};

class http_conn {
public:
    // 常量定义
    static constexpr int FILENAME_LEN = 200;      // 文件名最大长度
    static constexpr int READ_BUFFER_SIZE = 2048;  // 读缓冲区大小
    static constexpr int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小

public:
    http_conn() = default;
    ~http_conn() = default;

public:
    void init(int sockfd, const sockaddr_in& addr, const std::string& root,               
              const std::string& user, const std::string& passwd, const std::string& sqlname);
    
    void init();

    void close_conn(bool real_close = true);
       
    HTTP_CODE process_read();
    
    bool process_write(HTTP_CODE ret);
    
    bool read_once();
    
    bool write();
    
    const sockaddr_in* get_address() { return &m_address; }
    
    void initmysql_result(connection_pool* connPool);
    
    void unmap();

    void append_read_data(const char* data, size_t length) {
        read_buf.append(data, length);
        read_idx = read_buf.size();  // 内部维护read_idx，外部无需关心
    }

    const std::string& get_write_buffers() const { return write_buf; }

    bool has_attachment() const { 
        return (file_address != nullptr) && (file_stat.st_size > 0); 
    }

    // 获取附加文件的内存地址
    const char* get_attachment_data() const { return file_address; }

    // 获取附加文件的大小
    size_t get_attachment_size() const { return file_stat.st_size; }

    // 判断是否为长连接
    bool is_keep_alive() const { return linger; }

    // 长连接复用重置
    void reset_connection() { init(); }

    int timer_flag;

public:
    static int m_user_count;    // 在线用户数
    MYSQL* mysql;               // MySQL连接

private:
    // 解析请求行
    HTTP_CODE parse_request_line(const std::string& text);
    
    // 解析请求头
    HTTP_CODE parse_headers(const std::string& text);
    
    HTTP_CODE do_request();
    
    // 从read_buf提取一行数据
    std::string get_line() const { 
        return read_buf.substr(start_line, checked_idx - start_line); 
    }
    
    std::string get_file_extension();

    LINE_STATUS parse_line();

    bool add_response(const char* format, ...);
    bool add_content(const std::string& content);
    bool add_status_line(int status, const std::string& title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_location(const std::string& location);


private:
    int m_sockfd = -1;                  // 客户端socket
    sockaddr_in m_address;              // 客户端地址
    std::string read_buf;               // 读缓冲区
    std::string write_buf;              // 写缓冲区
    std::string real_file;              // 实际请求的文件路径

    CHECK_STATE check_state;            // 当前解析状态
    METHOD method;                      // 请求方法
    std::string url; 
    std::string requested_file_path;                  
    std::string version;                // HTTP版本
    std::string host;                   // Host头
    size_t content_length;              // 请求体长度
    bool linger;                        // 是否长连接
    char* file_address;                 // 内存映射的文件地址
    struct stat file_stat;              // 文件状态信息
    struct iovec iv[2];                 // 分散/聚集IO向量
    int iv_count;                       // 向量数量
    bool cgi;                           // 是否为CGI请求（POST）
    std::string request_content;        // 请求体内容

    // 缓冲区索引
    size_t read_idx = 0;                // 读缓冲区已读位置
    size_t checked_idx = 0;             // 已解析位置
    size_t start_line = 0;              // 当前行起始位置
    size_t write_idx = 0;               // 写缓冲区已写位置

    // 发送计数
    size_t bytes_to_send = 0;           // 待发送字节数
    size_t bytes_have_send = 0;         // 已发送字节数

    // 配置信息
    std::string doc_root;               // 网站根目录
    int trig_mode = 0;                  // 触发模式
    bool close_log = false;             // 是否关闭日志

    // 数据库信息
    std::string sql_user;               // 数据库用户名
    std::string sql_passwd;             // 数据库密码
    std::string sql_name;               // 数据库名
    std::string sql_string;             // SQL查询字符串

    static std::mutex mtx;              // 线程安全锁（用户表操作）
    std::map<std::string, std::string> users;  // 用户名-密码映射表
    static std::unordered_map<std::string, std::string> file_types;
};




#endif

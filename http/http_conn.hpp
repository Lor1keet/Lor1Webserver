#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/mman.h>
#include <string>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include "http_parser.hpp"
#include "http_responser.hpp"

using asio::ip::tcp;

class Router;
class HttpRequest;

// HTTP处理结果
enum class HTTP_CODE {
    NO_REQUEST,           // 未完成解析
    GET_REQUEST,          // 解析完成
    BAD_REQUEST,          // 错误请求
    NO_RESOURCE,          // 资源不存在
    FORBIDDEN_REQUEST,    // 禁止访问
    FILE_REQUEST,         // 文件请求（成功）
    INTERNAL_ERROR,       // 服务器内部错误
    CLOSED_CONNECTION,    // 连接关闭
    REDIRECT              // 重定向
};

class http_conn {
public:
    http_conn() = default;
    http_conn(tcp::socket* socket_, const tcp::endpoint& endpoint, const std::string& root)
        : socket(socket_), m_endpoint(endpoint), doc_root(root) {};
    ~http_conn() = default;

public:
    void init(tcp::socket* socket_, const tcp::endpoint& endpoint, const std::string& root, Router& router);
    void init();
    void close_conn(bool real_close = true);

    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);

    const tcp::endpoint* get_endpoint() const { return &m_endpoint; }
    void unmap();

    void append_read_data(const char* data, size_t length) {
        read_buf.append(data, length);
        read_idx = read_buf.size();
    }

    const std::string& get_write_buffers() const { return write_buf; }
    bool has_attachment() const { 
        return (file_address != nullptr) && (file_stat.st_size > 0); 
    }
    const char* get_attachment_data() const { return file_address; }
    size_t get_attachment_size() const { return file_stat.st_size; }
    
    bool is_keep_alive() const { return request.is_keep_alive(); }
    void reset_connection() { init(); }
    const std::string& get_url() const { return request.get_url(); }
    bool is_cgi() const { return request.is_cgi(); }
    const std::string& get_request_content() const { return request.get_content(); }
    void set_requested_file(const std::string& path) { requested_file_path = path; }
    void set_url(const std::string& new_url) { request.set_url(new_url); }
    HttpRequest::METHOD get_method() const { return request.get_method(); }
    const HttpRequest& get_request() const { return request; }
    const std::string& get_version() const { return request.get_version(); }

public:
    static int m_user_count;

private:
    HTTP_CODE do_request(); 

private:
    tcp::socket* socket;
    tcp::endpoint m_endpoint;
    std::string read_buf;     // 读缓冲区
    std::string write_buf;    // 写缓冲区
    
    HttpRequest request;      // 请求对象
    Router* m_router;         // 路由对象
    
    CHECK_STATE check_state;  // 解析状态
    std::string requested_file_path;  // 请求文件路径
    char* file_address;       // 文件映射地址
    struct stat file_stat;    // 文件状态
    struct iovec iv[2];       // 分散写向量
    int iv_count;             // 向量数量
    size_t read_idx = 0;      // 读缓冲区索引
    size_t checked_idx = 0;   // 已解析索引
    size_t start_line = 0;    // 解析行起始索引
    size_t write_idx = 0;     // 写缓冲区索引
    size_t bytes_to_send = 0; // 待发送字节数
    size_t bytes_have_send = 0; // 已发送字节数 
    std::string doc_root;     // 文档根目录
};

#endif
#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include <string>
#include <unordered_map>

// HTTP请求数据结构
class HttpRequest {
public:
    enum class METHOD {
        GET, POST, HEAD, PUT, DELETE, 
        TRACE, OPTIONS, CONNECT, PATH, UNKNOWN
    };
    
    HttpRequest() : method(METHOD::UNKNOWN), content_length(0), cgi(false), linger(false) {}

    METHOD get_method() const { return method; }
    void set_method(METHOD m) { method = m; }

    const std::string& get_url() const { return url; }
    void set_url(const std::string& u) { url = u; }

    const std::string& get_version() const { return version; }
    void set_version(const std::string& v) { version = v; }

    const std::string& get_content() const { return content; }
    void set_content(const std::string& c) { content = c; }

    size_t get_content_length() const { return content_length; }
    void set_content_length(size_t len) { content_length = len; }

    bool is_cgi() const { return cgi; }
    void set_cgi(bool c) { cgi = c; }
    
    bool is_keep_alive() const { return linger; }
    void set_keep_alive(bool l) { linger = l; }

    void add_header(const std::string& key, const std::string& value) {
        headers[key] = value;
    }

    const std::unordered_map<std::string, std::string>& get_headers() const {
        return headers;
    }
    
private:
    METHOD method;
    std::string url;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string content;
    size_t content_length;
    bool cgi;
    bool linger;
};

class HttpResponse {
public:
    HttpResponse() = default;

    const std::string& get_required_file_path() const { return required_file_path; }
    void set_required_file_path(std::string file_path) { required_file_path = file_path; }

    const std::string& get_redirect_url() const { return redirect_url; }
    void set_redirect_url(std::string url) { redirect_url = url; }

private:
    std::string required_file_path;
    std::string redirect_url;

};

// HTTP请求解析状态
enum class PARSE_STATUS {
    SUCCESS,    // 成功解析一个完整的HTTP请求
    INCOMPLETE, // 数据不完整，需要继续读取
    ERROR       // 解析出错
};

// HTTP解析状态机
enum class CHECK_STATE {
    CHECK_STATE_REQUESTLINE,  // 解析请求行
    CHECK_STATE_HEADER,       // 解析请求头
    CHECK_STATE_CONTENT       // 解析请求体
};

// 当前行状态
enum class LINE_STATUS {
    OK,     // 完整行
    BAD,    // 格式错误
    OPEN    // 未完成
};

// HttpParser 类，专门负责 HTTP 请求的解析
class HttpParser {
public:
    // 静态成员函数，实现无状态解析
    static PARSE_STATUS parse(const std::string& buf, HttpRequest& req, CHECK_STATE& check_state, size_t& checked_idx, size_t& start_line);

private:
    static LINE_STATUS parse_line(const std::string& buf, size_t& checked_idx);
    static std::string get_line(const std::string& buf, size_t start_line, size_t end_line);
    static PARSE_STATUS parse_request_line(const std::string& text, HttpRequest& req);
    static PARSE_STATUS parse_headers(const std::string& text, HttpRequest& req);
};

#endif
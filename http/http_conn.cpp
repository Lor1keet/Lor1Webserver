#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>
#include <ostream>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

std::unordered_map<std::string, std::string> http_conn::file_types = { // 请求体类型对照
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".png", "image/png"},       
    {".jpg", "image/jpeg"},      
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".ico", "image/x-icon"},       
};

std::mutex http_conn::mtx;
int http_conn::m_user_count = 0;

// 去除字符串首尾空白字符（空格/制表符/回车/换行）
std::string trim(const std::string& s) {
    if (s.empty()) return "";
    const std::string whitespace = " \t\r\n";
    size_t start = s.find_first_not_of(whitespace);
    size_t end = s.find_last_not_of(whitespace);
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// 初始化MySQL查询结果：从连接池加载用户账号密码到内存（users映射表）
void http_conn::initmysql_result(connection_pool* connPool) {
    connPtr mysql_conn = connPool->GetConnection();
    MYSQL* mysql = mysql_conn.get();

    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        spdlog::error("MySQL SELECT error: {}", mysql_error(mysql));
        return;
    }

    // 提取查询结果，存入users映射表
    MYSQL_RES* result = mysql_store_result(mysql);
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        std::string username(row[0]);
        std::string passwd(row[1]);
        users[username] = passwd;
    }
}

void http_conn::close_conn(bool real_close) {
    if (real_close && m_sockfd != -1) {
        spdlog::info("Close client socket: {}", m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 在线用户数减1
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr, const std::string& root, 
                     const std::string& user, const std::string& passwd, const std::string& sqlname) {
    m_sockfd = sockfd;
    m_address = addr;
    m_user_count++;

    doc_root = root;
    sql_user = user;
    sql_passwd = passwd;
    sql_name = sqlname;

    init(); 
}

void http_conn::init() {
    read_buf.clear();
    write_buf.clear();
    real_file.clear();
    request_content.clear();
    requested_file_path.clear();

    check_state = CHECK_STATE::CHECK_STATE_REQUESTLINE;
    method = METHOD::GET;
    content_length = 0;
    cgi = false;
    file_address = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;

    read_idx = 0;
    checked_idx = 0;
    start_line = 0;
    write_idx = 0;
}

// 从状态机按行解析HTTP数据（分隔符\r\n）
LINE_STATUS http_conn::parse_line() {
    for (; checked_idx < read_idx; ++checked_idx) {
        char temp = read_buf[checked_idx];
        if (temp == '\r') {
            // \r后无字符（数据未读完）
            if ((checked_idx + 1) == read_idx) return LINE_STATUS::OPEN;
            // \r\n（完整行）
            else if (read_buf[checked_idx + 1] == '\n') {
                checked_idx += 2;
                return LINE_STATUS::OK;
            }
            // \r后非\n（格式错误）
            return LINE_STATUS::BAD;
        }
        // 单独\n（格式错误，需配合前面的\r）
        else if (temp == '\n') {
            if (checked_idx > 1 && read_buf[checked_idx - 1] == '\r') {
                checked_idx += 2;
                return LINE_STATUS::OK;
            }
            return LINE_STATUS::BAD;
        }
    }
    // 数据未读完继续读取
    return LINE_STATUS::OPEN;
}

// 解析HTTP请求行：提取请求方法、URL、HTTP版本
HTTP_CODE http_conn::parse_request_line(const std::string& text) {
    spdlog::info("Request line: [{}]", text);

    // 提取请求方法
    size_t method_space = text.find_first_of(" \t");
    if (method_space == std::string::npos) {
        spdlog::error("BAD_REQUEST: No separator after method");
        return HTTP_CODE::BAD_REQUEST;
    }
    std::string method_str = text.substr(0, method_space);
    spdlog::info("Parsed method: [{}]", method_str);

    if (method_str == "GET") {
        method = METHOD::GET;
    } else if (method_str == "POST") {
        method = METHOD::POST;
        cgi = true; // 表示为POST请求
    } else {
        spdlog::error("BAD_REQUEST: Unsupported method: [{}]", method_str);
        return HTTP_CODE::BAD_REQUEST;
    }

    // 提取客户端URL
    size_t url_start = text.find_first_not_of(" \t", method_space);
    if (url_start == std::string::npos) {
        spdlog::error("BAD_REQUEST: No URL after method");
        return HTTP_CODE::BAD_REQUEST;
    }
    size_t url_end = text.find_first_of(" \t", url_start);
    if (url_end == std::string::npos) {
        spdlog::error("BAD_REQUEST: No separator after URL");
        return HTTP_CODE::BAD_REQUEST;
    }
    url = text.substr(url_start, url_end - url_start);

    requested_file_path = url; 
    // 处理URL中的http/https前缀
    if (requested_file_path.substr(0, 7) == "http://") {
        requested_file_path = requested_file_path.substr(7);
        size_t slash_pos = requested_file_path.find('/');
        requested_file_path = (slash_pos != std::string::npos) ? requested_file_path.substr(slash_pos) : "/";
    } else if (requested_file_path.substr(0, 8) == "https://") {
        requested_file_path = requested_file_path.substr(8);
        size_t slash_pos = requested_file_path.find('/');
        requested_file_path = (slash_pos != std::string::npos) ? requested_file_path.substr(slash_pos) : "/";
    }

    // 重定向逻辑
    if (size_t query_pos = url.find("?") != std::string::npos){
        requested_file_path = url.substr(0, query_pos); 
    }

    // 根路径映射到main.html
    if (requested_file_path == "/") {
        requested_file_path += "main.html";
    }

    spdlog::info("Parsed URL: [{}]", url);

    // HTTP版本
    size_t version_start = text.find_first_not_of(" \t", url_end);
    if (version_start == std::string::npos) {
        spdlog::error("BAD_REQUEST: No HTTP version after URL");
        return HTTP_CODE::BAD_REQUEST;
    }
    size_t version_end = text.find_first_of(" \t", version_start);
    if (version_end == std::string::npos) version_end = text.length();
    version = trim(text.substr(version_start, version_end - version_start));

    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        spdlog::error("BAD_REQUEST: Unsupported HTTP version: [{}]", version);
        return HTTP_CODE::BAD_REQUEST;
    }

    // 下一轮解析请求头
    check_state = CHECK_STATE::CHECK_STATE_HEADER;
    return HTTP_CODE::NO_REQUEST;
}

// 解析HTTP请求头
HTTP_CODE http_conn::parse_headers(const std::string& text) {
    std::string trimmed = text;
    trimmed.erase(trimmed.find_last_not_of("\r\n \t") + 1);

    // 空行表示请求头结束，判断是否需要解析请求体
    if (trimmed.empty()) {
        if (content_length != 0) {
            spdlog::info("Switch to request content parsing");
            check_state = CHECK_STATE::CHECK_STATE_CONTENT;
        }
        return (content_length == 0) ? HTTP_CODE::GET_REQUEST : HTTP_CODE::NO_REQUEST;
    }

    // 解析Connection头（判断是否为长连接）
    if (text.substr(0, 11) == "Connection:") {
        size_t start = text.find_first_not_of(" \t", 11);
        if (start == std::string::npos) return HTTP_CODE::BAD_REQUEST;
        
        std::string conn_val = text.substr(start);
        linger = (strcasecmp(conn_val.c_str(), "keep-alive") == 0);
    }
    // 解析Content-Length头（请求体长度）
    else if (strncasecmp(text.c_str(), "Content-Length:", 15) == 0) {
        size_t start = text.find_first_not_of(" \t", 15);
        if (start == std::string::npos) return HTTP_CODE::BAD_REQUEST;
        
        content_length = atol(text.substr(start).c_str());
    }
    // 解析Host头
    else if (text.substr(0, 5) == "Host:") {
        size_t start = text.find_first_not_of(" \t", 5);
        host = text.substr(start);
    }
    // 未知头字段
    else {
        spdlog::warn("Unknown header: [{}]", text);
    }

    return HTTP_CODE::NO_REQUEST;
}

// 解析请求行、请求头、请求体
HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_STATUS::OK;
    HTTP_CODE ret = HTTP_CODE::NO_REQUEST;
    std::string text;

    // 解析请求行和请求头
    while (check_state != CHECK_STATE::CHECK_STATE_CONTENT && (line_status = parse_line()) == LINE_STATUS::OK) {
        text = get_line(); // 从read_buf提取当前行
        start_line = checked_idx; // 更新下一行起始位置

        switch (check_state) {
            case CHECK_STATE::CHECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if (ret == HTTP_CODE::BAD_REQUEST) return ret;
                break;
            case CHECK_STATE::CHECK_STATE_HEADER:
                ret = parse_headers(text);
                if (ret == HTTP_CODE::BAD_REQUEST) return ret;
                // 无请求体时，直接进入请求处理
                else if (ret == HTTP_CODE::GET_REQUEST) {
                    spdlog::info("No request content, start processing request");
                    return do_request();
                }
                break;
            default:
                return HTTP_CODE::INTERNAL_ERROR;
        }
    }

    // 解析POST发送的请求体
    if (check_state == CHECK_STATE::CHECK_STATE_CONTENT) {
        if (read_buf.size() >= checked_idx + content_length) {
            sql_string = read_buf.substr(checked_idx, content_length);
            spdlog::info("Successfully read request content: [{}]", sql_string);
            return do_request(); // 进入业务处理
        } else {
            spdlog::info("Insufficient request content, wait for more data");
            return HTTP_CODE::NO_REQUEST;
        }
    }

    return HTTP_CODE::NO_REQUEST;
}

// 业务处理
HTTP_CODE http_conn::do_request() {
    real_file = doc_root;
    
    // size_t pos = url.find_last_of("/");

    if (cgi && (url.find("welcome")) != std::string::npos){
        // user=xxx&password=xxx&op=xxx
        size_t user_pos = sql_string.find("user=");
        size_t passwd_pos = sql_string.find("password=");
        size_t op_pos = sql_string.find("op=");
        if (user_pos == std::string::npos || passwd_pos == std::string::npos) {
            return BAD_REQUEST;
        }
        
        std::string username = sql_string.substr(user_pos + 5, passwd_pos - (user_pos + 6));
        std::string passwd = sql_string.substr(passwd_pos + 9, op_pos - (passwd_pos + 10));
        std::string op = sql_string.substr(op_pos + 3);
        spdlog::info("CGI request - Username: [{}], Password: [***], [{}]", username, op);

        if (op == "login"){
            std::lock_guard<std::mutex> lock(mtx);
            if (users.find(username) != users.end() && users[username] == passwd) {
                requested_file_path = "/welcome.jpg";
            }
            else {
                url = "/?error=1";
                return REDIRECT;
            }
        }

        else if (op == "register"){
            std::lock_guard<std::mutex> lock(mtx);
            if (users.find(username) == users.end()) {
                char sql_insert[200];
                snprintf(sql_insert, sizeof(sql_insert), 
                         "INSERT INTO user(username, passwd) VALUES('%s', '%s')",
                         username.c_str(), passwd.c_str()); 
                if (mysql_query(mysql, sql_insert) == 0) {
                    users[username] = passwd; // 同步内存映射表
                    url = "/?registerok=1"; 
                    spdlog::info("User registered successfully: [{}]", username);
                    return REDIRECT;
                } else {
                    url = "/?error=1";
                    return REDIRECT;
                }
            }
            else {
                url = "/?user_exists=1";
                return REDIRECT;
            }
        }

    }

    if (real_file.size() + requested_file_path.size() >= FILENAME_LEN) {
        return HTTP_CODE::BAD_REQUEST;
    }
    real_file += requested_file_path;
    
    // 检查文件状态（是否存在、是否可读、是否为目录）
    if (stat(real_file.c_str(), &file_stat) < 0) {
        spdlog::error("File not found: [{}]", real_file);
        return HTTP_CODE::NO_RESOURCE; // 404
    }
    if (!(file_stat.st_mode & S_IROTH)) {
        spdlog::error("File forbidden: [{}]", real_file);
        return HTTP_CODE::FORBIDDEN_REQUEST; // 403
    }
    if (S_ISDIR(file_stat.st_mode)) {
        spdlog::error("Request is a directory: [{}]", real_file);
        return HTTP_CODE::BAD_REQUEST; // 400
    }

    // 文件映射到内存
    int fd = open(real_file.c_str(), O_RDONLY);
    if (fd == -1) {
        spdlog::error("Open file failed: [{}], errno: {}", real_file, errno);
        return HTTP_CODE::NO_RESOURCE;
    }
    file_address = (char*)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // 映射后不需要了

    // 检查映射是否成功
    if (file_address == MAP_FAILED) {
        spdlog::error("mmap failed: [{}], errno: {}", real_file, errno);
        return HTTP_CODE::INTERNAL_ERROR; // 500
    }

    spdlog::info("File mapped successfully: [{}], size: {}", real_file, file_stat.st_size);
    return HTTP_CODE::FILE_REQUEST; // 成功：返回文件请求状态
}

// 解除内存映射（防止内存泄漏）
void http_conn::unmap() {
    if (file_address) {
        munmap(file_address, file_stat.st_size);
        file_address = nullptr;
    }
}

// 格式化添加响应内容
bool http_conn::add_response(const char* format, ...) {
    va_list arg_list;
    va_start(arg_list, format);

    char temp[4096];
    int len = vsnprintf(temp, sizeof(temp), format, arg_list);
    if (len < 0) {
        va_end(arg_list);
        spdlog::error("vsnprintf failed (response formatting)");
        return false;
    }

    write_buf.append(temp, len);
    write_idx = write_buf.size();

    va_end(arg_list);
    spdlog::debug("Added response content: [{}]", temp);
    return true;
}

// 添加响应状态行（HTTP/1.1 200 OK）
bool http_conn::add_status_line(int status, const std::string& title) {
    return add_response("HTTP/1.1 %d %s\r\n", status, title.c_str());
}

// 添加响应头（Content-Length/Content-Type/Connection/空行）
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) 
        && add_content_type() 
        && add_linger() 
        && add_blank_line();
}

// 添加Content-Length头（告知客户端响应体长度）
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}


std::string http_conn::get_file_extension() {
    size_t dot_pos = requested_file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return ""; // 无扩展名
    }
    return requested_file_path.substr(dot_pos); // 返回.xxx扩展名
}

// 添加Content-Type头
bool http_conn::add_content_type() {
    std::string ext = get_file_extension();
    std::string file_type = "application/octet-stream"; // 默认二进制流

    if (file_types.find(ext) != file_types.end()) {
        file_type = file_types[ext];
    }

    return add_response("Content-Type: %s\r\n", file_type.c_str());
}

// 添加Connection头（长连接/短连接）
bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", linger ? "keep-alive" : "close");
}

// 添加空行（响应头与响应体的分隔符，HTTP协议要求）
bool http_conn::add_blank_line() {
    return add_response("\r\n");
}

// 添加响应体内容（如错误提示文本）
bool http_conn::add_content(const std::string& content) {
    return add_response("%s", content.c_str());
}

bool http_conn::add_location(const std::string& location) {
    return add_response("Location: %s\r\n", location.c_str());
}


// 生成HTTP响应
bool http_conn::process_write(HTTP_CODE ret) {
    write_buf.clear();
    write_idx = 0;
    switch (ret) {
        case HTTP_CODE::INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) return false;
            break;

        case HTTP_CODE::BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) return false;
            break;

        case HTTP_CODE::FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) return false;
            break;

        // 200:返回文件（如HTML/图片）
        case HTTP_CODE::FILE_REQUEST:
            add_status_line(200, ok_200_title);
            if (file_stat.st_size != 0) {
                // 构建分散IO向量（响应头 + 映射的文件内存）
                add_headers(file_stat.st_size);
                iv[0].iov_base = const_cast<char*>(write_buf.c_str());
                iv[0].iov_len = write_buf.size();
                iv[1].iov_base = file_address;
                iv[1].iov_len = file_stat.st_size;
                iv_count = 2;
                bytes_to_send = write_buf.size() + file_stat.st_size;
                return true;
            } else {
                // 空文件
                const char* empty_html = "<html><body></body></html>";
                add_headers(strlen(empty_html));
                if (!add_content(empty_html)) return false;
            }
            break;

        case HTTP_CODE::REDIRECT:
            write_buf.clear();
            add_status_line(302, "Found");
            add_response("Location: %s\r\n", url.c_str());
            add_response("Content-Length: 0\r\n");
            add_linger();
            add_blank_line();
            iv[0].iov_base = const_cast<char*>(write_buf.data());
            iv[0].iov_len = write_buf.size();
            iv_count = 1;
            bytes_to_send = write_buf.size();
            return true;
            
        default:
            spdlog::error("Unsupported HTTP_CODE: {}", static_cast<int>(ret));
            return false;
    }

    // 非文件响应
    iv[0].iov_base = const_cast<char*>(write_buf.c_str());
    iv[0].iov_len = write_buf.size();
    iv_count = 1;
    bytes_to_send = write_buf.size();
    return true;
}
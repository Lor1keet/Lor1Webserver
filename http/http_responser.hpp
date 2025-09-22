#ifndef HTTP_RESPONSER_H
#define HTTP_RESPONSER_H

#include <string>
#include <sys/stat.h>
#include <sys/uio.h>
#include "http_conn.hpp"
#include "http_parser.hpp"

enum class HTTP_CODE;

class HttpResponser {
public:
    HttpResponser(const HttpRequest& req);

    void build_response(HTTP_CODE ret, const HttpRequest& request,
                       const struct stat& file_stat, char* file_address, const std::string& requested_file_path);

    const std::string& get_write_buf() const { return m_write_buf; }
    bool has_file() const { return m_has_file; }
    const char* get_file_address() const { return m_file_address; }
    size_t get_file_size() const { return m_file_size; }

private:
    template<typename... Args>
    bool add_response(const char* format, Args&&... args) {
        char temp[4096];
        int len = 0;
        if constexpr (sizeof...(Args) == 0) {
            len = snprintf(temp, sizeof(temp), "%s", format);
        } 
        else {
            len = snprintf(temp, sizeof(temp), format, std::forward<Args>(args)...);
        }

        if (len < 0) {
            spdlog::error("snprintf failed (response formatting)");
            return false;
        }

        m_write_buf.append(temp, len);
        m_write_idx = m_write_buf.size();

        spdlog::debug("Added response content: [{}]", temp);
        return true;
    }
    bool add_content(const std::string& content);
    bool add_status_line(int status, const std::string& title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_location(const std::string& location);
    std::string get_file_extension();
    
private:
    const HttpRequest& m_request; // 保存请求信息用于判断keep-alive等
    std::string m_write_buf;      // 响应头缓冲区
    size_t m_write_idx = 0;

    // 响应状态相关常量
    const char* ok_200_title = "OK";
    const char* error_400_title = "Bad Request";
    const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
    const char* error_403_title = "Forbidden";
    const char* error_403_form = "You do not have permission to get file from this server.\n";
    const char* error_404_title = "Not Found";
    const char* error_404_form = "The requested file was not found on this server.\n";
    const char* error_500_title = "Internal Error";
    const char* error_500_form = "There was an unusual problem serving the request file.\n";

    // 文件类型映射表
    static std::unordered_map<std::string, std::string> file_types;

    // 文件响应相关成员
    std::string m_requested_file_path; // 请求的文件路径
    char* m_file_address = nullptr;    // 映射的文件地址
    size_t m_file_size = 0;            // 文件大小
    bool m_has_file = false;           // 是否包含文件响应体
};

#endif
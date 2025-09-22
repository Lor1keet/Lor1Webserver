#include "http_responser.hpp"
#include "spdlog/spdlog.h"

std::unordered_map<std::string, std::string> HttpResponser::file_types = {
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

HttpResponser::HttpResponser(const HttpRequest& req) : m_request(req) {}

void HttpResponser::build_response(HTTP_CODE ret, const HttpRequest& request,
                                  const struct stat& file_stat, char* file_address, const std::string& requested_file_path) {
    m_write_buf.clear();
    m_write_idx = 0;
    m_requested_file_path = requested_file_path;
    m_has_file = false;
    m_file_address = nullptr;
    m_file_size = 0;

    switch (ret) {
        case HTTP_CODE::INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            add_content(error_500_form);
            break;

        case HTTP_CODE::BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            add_content(error_400_form);
            break;

        case HTTP_CODE::FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            add_content(error_403_form);
            break;

        case HTTP_CODE::NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            add_content(error_404_form);
            break;

        case HTTP_CODE::FILE_REQUEST:
            add_status_line(200, ok_200_title);
            if (file_stat.st_size != 0) {
                add_headers(file_stat.st_size);
                m_file_address = file_address;
                m_file_size = file_stat.st_size;
                m_has_file = true;
            } 
            else {
                const char* empty_html = "<html><body></body></html>";
                add_headers(strlen(empty_html));
                add_content(empty_html);
            }
            break;
        
        case HTTP_CODE::REDIRECT:
            add_status_line(302, "Found");
            add_location(request.get_url());
            add_content_length(0);
            add_linger();
            add_blank_line();
            break;
        
        default:
            spdlog::error("Unsupported HTTP_CODE: {}", static_cast<int>(ret));
            break;
    }
}

bool HttpResponser::add_status_line(int status, const std::string& title) {
    return add_response("HTTP/1.1 %d %s\r\n", status, title.c_str());
}

bool HttpResponser::add_headers(int content_len) {
    return add_content_length(content_len) 
        && add_content_type() 
        && add_linger() 
        && add_blank_line();
}

bool HttpResponser::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

std::string HttpResponser::get_file_extension() {
    size_t dot_pos = m_requested_file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "";
    }
    return m_requested_file_path.substr(dot_pos);
}

bool HttpResponser::add_content_type() {
    std::string ext = get_file_extension();
    std::string file_type = "application/octet-stream";

    if (file_types.find(ext) != file_types.end()) {
        file_type = file_types[ext];
    }

    return add_response("Content-Type: %s\r\n", file_type.c_str());
}

bool HttpResponser::add_linger() {
    return add_response("Connection: %s\r\n", m_request.is_keep_alive() ? "keep-alive" : "close");
}

bool HttpResponser::add_blank_line() {
    return add_response("\r\n");
}

bool HttpResponser::add_content(const std::string& content) {
    return add_response("%s", content.c_str());
}

bool HttpResponser::add_location(const std::string& location) {
    return add_response("Location: %s\r\n", location.c_str());
}
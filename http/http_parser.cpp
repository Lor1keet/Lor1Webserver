#include "http_parser.hpp"
#include <spdlog/spdlog.h>
#include <string.h>

std::string trim(const std::string& s) {
    if (s.empty()) return "";
    const std::string whitespace = " \t\r\n";
    size_t start = s.find_first_not_of(whitespace);
    size_t end = s.find_last_not_of(whitespace);
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

LINE_STATUS HttpParser::parse_line(const std::string& buf, size_t& checked_idx) {
    for (; checked_idx < buf.size(); ++checked_idx) {
        char temp = buf[checked_idx];
        if (temp == '\r') {
            if ((checked_idx + 1) >= buf.size()) return LINE_STATUS::OPEN;
            else if (buf[checked_idx + 1] == '\n') {
                checked_idx += 2;
                return LINE_STATUS::OK;
            }
            return LINE_STATUS::BAD;
        }
    }
    return LINE_STATUS::OPEN;
}

std::string HttpParser::get_line(const std::string& buf, size_t start_line, size_t end_line) {
    return buf.substr(start_line, end_line - start_line);
}

PARSE_STATUS HttpParser::parse_request_line(const std::string& text, HttpRequest& req) {
    size_t method_space = text.find_first_of(" \t");
    if (method_space == std::string::npos) {
        spdlog::error("BAD_REQUEST: No separator after method");
        return PARSE_STATUS::ERROR;
    }
    std::string method_str = text.substr(0, method_space);
    spdlog::info("Parsed method: [{}]", method_str);

    if (method_str == "GET") {
        req.set_method(HttpRequest::METHOD::GET);
    } 
    else if (method_str == "POST") {
        req.set_method(HttpRequest::METHOD::POST);
        req.set_cgi(true);
    } 
    else {
        req.set_method(HttpRequest::METHOD::UNKNOWN);
    }

    size_t url_start = text.find_first_not_of(" \t", method_space);
    if (url_start == std::string::npos) {
        spdlog::error("No URL after method");
        return PARSE_STATUS::ERROR;
    }
    size_t url_end = text.find_first_of(" \t", url_start);
    if (url_end == std::string::npos) {
        spdlog::error("No separator after URL");
        return PARSE_STATUS::ERROR;
    }
    std::string url = text.substr(url_start, url_end - url_start);
    if (url.substr(0, 7) == "http://") {
        url = url.substr(7);
        size_t slash_pos = url.find('/');
        url = (slash_pos != std::string::npos) ? url.substr(slash_pos) : "/";
    } else if (url.substr(0, 8) == "https://") {
        url = url.substr(8);
        size_t slash_pos = url.find('/');
        url = (slash_pos != std::string::npos) ? url.substr(slash_pos) : "/";
    }
    req.set_url(url);
    spdlog::info("Parsed URL: [{}]", url);

    size_t version_start = text.find_first_not_of(" \t", url_end);
    if (version_start == std::string::npos) {
        spdlog::error("No HTTP version after URL");
        return PARSE_STATUS::ERROR;
    }
    size_t version_end = text.find_first_of(" \t", version_start);
    if (version_end == std::string::npos) version_end = text.length();
    req.set_version(text.substr(version_start, version_end - version_start));

    if (req.get_version() != "HTTP/1.0" && req.get_version() != "HTTP/1.1") {
        spdlog::error("BAD_REQUEST: Unsupported HTTP version");
        return PARSE_STATUS::ERROR;
    }
    return PARSE_STATUS::INCOMPLETE;
}

PARSE_STATUS HttpParser::parse_headers(const std::string& text, HttpRequest& req) {
    std::string trimmed = trim(text);
    if (trimmed.empty()) {
        if (req.get_content_length() != 0) {
            spdlog::info("Switch to request content parsing");
            return PARSE_STATUS::INCOMPLETE;
        }
        return PARSE_STATUS::SUCCESS;
    }

    size_t colon_pos = text.find(':');
    if (colon_pos == std::string::npos) return PARSE_STATUS::ERROR;
    
    std::string key = trim(text.substr(0, colon_pos));
    std::string value = trim(text.substr(colon_pos + 1));
    req.add_header(key, value);

    if (strcasecmp(key.c_str(), "Connection") == 0) {
        req.set_keep_alive(strcasecmp(value.c_str(), "keep-alive") == 0);
    } else if (strcasecmp(key.c_str(), "Content-Length") == 0) {
        req.set_content_length(atol(value.c_str()));
    }

    return PARSE_STATUS::INCOMPLETE;
}

PARSE_STATUS HttpParser::parse(const std::string& buf, HttpRequest& req, CHECK_STATE& check_state, size_t& checked_idx, size_t& start_line) {
    while (check_state != CHECK_STATE::CHECK_STATE_CONTENT) {
        // 只调用一次 parse_line，获取当前行的状态
        LINE_STATUS line_status = parse_line(buf, checked_idx);
        if (line_status == LINE_STATUS::BAD)
            return PARSE_STATUS::ERROR;
        if (line_status == LINE_STATUS::OPEN)
            return PARSE_STATUS::INCOMPLETE;

        // 获取当前行内容（行以 "\r\n" 结尾，减2）
        std::string line = get_line(buf, start_line, checked_idx - 2);
        start_line = checked_idx;

        switch (check_state) {
            case CHECK_STATE::CHECK_STATE_REQUESTLINE:
                if (parse_request_line(line, req) == PARSE_STATUS::ERROR)
                    return PARSE_STATUS::ERROR;
                check_state = CHECK_STATE::CHECK_STATE_HEADER;
                break;
            case CHECK_STATE::CHECK_STATE_HEADER:
                if (parse_headers(line, req) == PARSE_STATUS::ERROR)
                    return PARSE_STATUS::ERROR;
                // 当遇到空行时，说明头部解析结束
                if (trim(line).empty()) {
                    // 如果 Content-Length 为 0，则整个 HTTP 请求解析完成
                    if (req.get_content_length() == 0)
                        return PARSE_STATUS::SUCCESS;
                    else {
                        // 需要解析报文体，切换状态后退出循环
                        check_state = CHECK_STATE::CHECK_STATE_CONTENT;
                    }
                }
                break;
            default:
                break;
        }

        // 当状态切换到 CONTENT 时，退出头部解析循环
        if (check_state == CHECK_STATE::CHECK_STATE_CONTENT)
            break;
    }

    // 检查缓冲区里是否已读到足够的内容
    if (check_state == CHECK_STATE::CHECK_STATE_CONTENT) {
        if (buf.size() >= start_line + req.get_content_length()) {
            req.set_content(buf.substr(start_line, req.get_content_length()));
            return PARSE_STATUS::SUCCESS;
        } else {
            return PARSE_STATUS::INCOMPLETE;
        }
    }
    return PARSE_STATUS::SUCCESS;
}
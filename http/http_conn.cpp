#include "http_conn.hpp"
#include "spdlog/spdlog.h"
#include "router.hpp" 
int http_conn::m_user_count = 0;

void http_conn::init(tcp::socket* socket_, const tcp::endpoint& endpoint, const std::string& root, Router& router) {
    socket = socket_;
    m_endpoint = endpoint;
    m_user_count++;
    doc_root = root;
    m_router = &router;
    init();
}

void http_conn::init() {
    read_buf.clear();
    write_buf.clear();
    requested_file_path.clear();
    request = HttpRequest(); // 重置HttpRequest对象

    check_state = CHECK_STATE::CHECK_STATE_REQUESTLINE;
    file_address = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;

    read_idx = 0;
    checked_idx = 0;
    start_line = 0;
    write_idx = 0;
}

void http_conn::close_conn(bool real_close) {
    if (real_close && socket != nullptr) {
        spdlog::info("Close client socket");
        std::error_code ec;
        socket->close(ec);
        if (ec) {
            spdlog::error("Socket close error: {}", ec.message());
        }
        socket = nullptr;
        m_user_count--;
    }
}

HTTP_CODE http_conn::process_read() {
    PARSE_STATUS parse_status = HttpParser::parse(read_buf, request, check_state, checked_idx, start_line);

    if (parse_status == PARSE_STATUS::INCOMPLETE) {
        return HTTP_CODE::NO_REQUEST;
    } 
    else if (parse_status == PARSE_STATUS::SUCCESS) {
        return do_request();
    } 
    else {
        return HTTP_CODE::BAD_REQUEST;
    }
}

HTTP_CODE http_conn::do_request() {
    HttpResponse res;
    
    HTTP_CODE dispatch_result = m_router->dispatch(request, res);

    switch (dispatch_result) {
        case HTTP_CODE::FILE_REQUEST:
            requested_file_path = res.get_required_file_path();
            break;
        case HTTP_CODE::REDIRECT:
            request.set_url(res.get_redirect_url()); // 保存重定向URL
            break;
        default:
            return dispatch_result;
    }

    if (dispatch_result == HTTP_CODE::FILE_REQUEST) {
        std::string full_path = doc_root + requested_file_path;
        if (stat(full_path.c_str(), &file_stat) < 0) {
            return HTTP_CODE::NO_RESOURCE;
        }
        if (!(file_stat.st_mode & S_IROTH)) {
            return HTTP_CODE::FORBIDDEN_REQUEST;
        }
        if (S_ISDIR(file_stat.st_mode)) {
            return HTTP_CODE::BAD_REQUEST;
        }

        int fd = open(full_path.c_str(), O_RDONLY);
        if (fd < 0) {
            return HTTP_CODE::INTERNAL_ERROR;
        }

        file_address = static_cast<char*>(mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
        close(fd);
        if (file_address == MAP_FAILED) {
            file_address = nullptr;
            return HTTP_CODE::INTERNAL_ERROR;
        }
    }

    return dispatch_result;
}

void http_conn::unmap() {
    if (file_address) {
        munmap(file_address, file_stat.st_size);
        file_address = nullptr;
    }
}

bool http_conn::process_write(HTTP_CODE ret) {
    HttpResponser responser(request);
    responser.build_response(ret, request, file_stat, file_address, requested_file_path);

    // 从responser获取响应数据，设置发送缓冲区
    const std::string& response_headers = responser.get_write_buf();
    this->write_buf = response_headers;
    iv[0].iov_base = const_cast<char*>(write_buf.c_str());
    iv[0].iov_len = write_buf.size();
    iv_count = 1;
    bytes_to_send = write_buf.size();
    
    // 如果有文件响应，添加到发送向量
    if (responser.has_file()) {
        iv[1].iov_base = const_cast<char*>(responser.get_file_address());
        iv[1].iov_len = responser.get_file_size();
        iv_count = 2;
        bytes_to_send += responser.get_file_size();
    }

    return true;
}
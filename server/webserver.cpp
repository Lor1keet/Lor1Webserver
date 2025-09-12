#include <thread>
#include "webserver.h"
#include "spdlog/spdlog.h"

using asio::ip::tcp;

WebServer::WebServer(asio::io_context& io_context, int thread_num, const std::string& root)
    : io_context_(io_context),
      acceptor_(io_context_),      
      thread_num_(thread_num),
      signals_(io_context, SIGINT, SIGTERM),
      root_(root){
    signals_.async_wait([this](std::error_code ec, int) {
        if (!ec) io_context_.stop();
    });
}

bool WebServer::listen(const std::string& ip, const std::string& port) {
    tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(ip, port);
    asio::error_code ec;

    acceptor_.open(endpoints->endpoint().protocol(), ec);
    if (ec) {
        spdlog::error("Open acceptor failed: {}", ec.message());
        return false;
    }

    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        spdlog::error("Set reuse address failed: {}", ec.message());
        return false;
    }

    acceptor_.bind(*endpoints, ec);
    if (ec) {
        spdlog::error("Bind address failed: {}", ec.message());
        return false;
    }

    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("Listen failed: {}", ec.message());
        return false;
    }

    accept();
    return true;
}

void WebServer::run() {
    std::vector<std::thread> threads;
    for (int i = 0; i < thread_num_; ++i) {
        threads.emplace_back([this]() {
            io_context_.run();
        });
    }
    for (auto& t : threads) {
        t.join();
    }
}

void WebServer::accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec) {
            spdlog::info("New client connection");
            std::make_shared<Connection>(std::move(socket), root_)->start();
        } else {
            spdlog::error("Accept connection failed: {}", ec.message());
        }
        accept();
    });
}

Connection::Connection(tcp::socket socket, const std::string& root)
    : socket_(std::move(socket)), timer_(socket_.get_executor()), http_(nullptr, socket_.remote_endpoint(), root), m_root(root){}

void Connection::start() {
    asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint();
    http_.init(&socket_, remote_ep, m_root);
    reset_timer();  
    do_read(); 
}

void Connection::do_read() {
    auto self(shared_from_this()); // 保持对象生命周期，避免回调中销毁
    socket_.async_read_some(asio::buffer(buffer_), // 一有数据就回调
        [this, self](std::error_code ec, std::size_t length) {
            if (ec) {
                spdlog::error("Read error: {}", ec.message());
                close();
                return;
            }
            
            http_.append_read_data(buffer_, length);
            
            HTTP_CODE read_ret = http_.process_read();
            
            if (read_ret == HTTP_CODE::NO_REQUEST) {
                // 未完成请求解析，继续读取
                do_read();
                reset_timer();
                return;
            }

            bool write_ok = http_.process_write(read_ret);
            if (!write_ok) {
                spdlog::error("Response generation failed");
                close();
                return;
            }
            spdlog::info("Response ready, starting to send");
            do_write();
            reset_timer();
        });
}

void Connection::do_write() {
    auto self(shared_from_this());
    std::vector<asio::const_buffer> buffers;
    buffers.push_back(asio::buffer(http_.get_write_buffers())); // 响应头
    
    // 响应体（如HTML、图片等）
    if (http_.has_attachment()) {
        buffers.push_back(asio::buffer(
            http_.get_attachment_data(), 
            http_.get_attachment_size()
        ));
    }

    asio::async_write(socket_, buffers,
        [this, self](std::error_code ec, std::size_t) {
            if (ec) {
                spdlog::error("Data send error");
                close();
                return;
            }
            
            http_.unmap();
            if (http_.is_keep_alive()) {
                http_.reset_connection();
                do_read(); 
            } else {
                close();
            }
        });
}

void Connection::reset_timer() {
    // 60秒超时设置
    timer_.expires_after(std::chrono::seconds(60));
    timer_.async_wait([this, self = shared_from_this()](std::error_code ec) {
        if (!ec) { 
            spdlog::info("Client connection timeout, closing");
            close();
        }
    });
}

void Connection::close() {
    std::error_code ec;
    socket_.close(ec);
    if (ec) {
        spdlog::error("Close client error: {}", ec.message());
    }
    http_.close_conn(); // 清理HTTP连接资源
}

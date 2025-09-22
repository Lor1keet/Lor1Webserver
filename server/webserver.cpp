#include <thread>
#include "webserver.hpp"
#include "spdlog/spdlog.h"

using asio::ip::tcp;

// ======================== WebServer ========================

WebServer::WebServer(asio::io_context& io_context, int thread_num, const std::string& root)
    : io_context_(io_context),
      acceptor_(io_context_),
      thread_num_(thread_num),
      signals_(io_context, SIGINT, SIGTERM),
      root_(root),
      m_service(),
      m_controller(m_service),
      m_router(m_controller) {

    // 捕获 SIGINT/SIGTERM，优雅关闭
    signals_.async_wait([this](std::error_code ec, int) {
        if (!ec) {
            spdlog::info("Signal received, stopping io_context");
            io_context_.stop();
        }
    });
}

bool WebServer::listen(const std::string& ip, const std::string& port) {
    asio::error_code ec;

    tcp::resolver resolver(io_context_);
    auto results = resolver.resolve(ip, port, ec);
    if (ec || results.begin() == results.end()) {
        spdlog::error("Resolve {}:{} failed: {}", ip, port, ec.message());
        return false;
    }

    const tcp::endpoint endpoint = results.begin()->endpoint();

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("Open acceptor failed: {}", ec.message());
        return false;
    }

    // 可复用地址
    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        spdlog::error("Set reuse_address failed: {}", ec.message());
        return false;
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        spdlog::error("Bind {}:{} failed: {}", endpoint.address().to_string(), endpoint.port(), ec.message());
        return false;
    }

    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        spdlog::error("Listen failed: {}", ec.message());
        return false;
    }

    spdlog::info("Listening on {}:{}", endpoint.address().to_string(), endpoint.port());
    accept(); // 启动 accept 循环
    return true;
}

void WebServer::run() {
    // 多线程跑 io_context
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(thread_num_));

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
    // 若 acceptor 已关闭，不再递归
    if (!acceptor_.is_open()) return;

    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec) {
            auto rep = socket.remote_endpoint(ec);
            if (!ec) spdlog::info("New client connection from {}:{}", rep.address().to_string(), rep.port());
            std::make_shared<Connection>(std::move(socket), root_, m_router)->start();
        } 
        else {
            if (ec == asio::error::operation_aborted) {
                return;
            }
            spdlog::error("Accept failed: {}", ec.message());
        }

        accept();
    });
}

// ======================== Connection ========================

Connection::Connection(tcp::socket socket, const std::string& root, Router& router)
    : socket_(std::move(socket)),
      timer_(socket_.get_executor()),
      http_(nullptr, socket_.remote_endpoint(), root),
      m_root(root),
      router(router) {}

void Connection::start() {
    // 初始化 HTTP 处理器
    const asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint();
    http_.init(&socket_, remote_ep, m_root, router);

    reset_timer();
    do_read();
}

void Connection::do_read() {
    auto self = shared_from_this();

    // 一旦有数据即回调
    socket_.async_read_some(asio::buffer(buffer_),
        [this, self](std::error_code ec, std::size_t length) {
            if (closed) return;

            if (ec) {
                if (ec == asio::error::operation_aborted) return;
                if (ec == asio::error::eof || ec == asio::error::connection_reset) {
                    spdlog::info("Client closed connection");
                } else {
                    spdlog::error("Read error: {}", ec.message());
                }
                close();
                return;
            }

            // 累积解析
            http_.append_read_data(buffer_, length);
            HTTP_CODE read_ret = http_.process_read();

            if (read_ret == HTTP_CODE::NO_REQUEST) {
                // 继续读更多数据
                reset_timer();
                do_read();
                return;
            }

            // 生成响应
            const bool write_ok = http_.process_write(read_ret);
            if (!write_ok) {
                spdlog::error("Response generation failed");
                close();
                return;
            }

            spdlog::info("Response ready, start sending");
            reset_timer();
            do_write();
        }
    );
}

void Connection::do_write() {
    auto self = shared_from_this();

    // 按顺序发送响应头 + 可选的响应体
    std::vector<asio::const_buffer> buffers;
    buffers.reserve(2);

    // 响应头
    buffers.push_back(asio::buffer(http_.get_write_buffers()));

    // 响应体
    if (http_.has_attachment()) {
        buffers.push_back(asio::buffer(http_.get_attachment_data(),
                                       http_.get_attachment_size()));
    }

    asio::async_write(socket_, buffers,
        [this, self](std::error_code ec, std::size_t) {
            if (closed) return;

            if (ec) {
                if (ec == asio::error::operation_aborted) return;
                spdlog::error("Send error: {}", ec.message());
                close();
                return;
            }

            // 释放 mmap 等资源
            http_.unmap();

            if (http_.is_keep_alive()) {
                http_.reset_connection();
                reset_timer();
                do_read();
            } else {
                close();
            }
        }
    );
}

void Connection::reset_timer() {
    asio::error_code ec;
    timer_.cancel(ec);

    timer_.expires_after(std::chrono::seconds(60));
    auto self = shared_from_this();
    timer_.async_wait([this, self](std::error_code ec2) {
        if (closed) return;
        if (ec2 == asio::error::operation_aborted) return; 
        if (!ec2) {
            spdlog::info("Client connection timeout, closing");
            close();
        } else {
            spdlog::debug("Timer wait error: {}", ec2.message());
        }
    });
}

void Connection::close() {
    if (closed) return;
    closed = true;

    asio::error_code ec;
    timer_.cancel(ec);
    socket_.cancel(ec);

    if (socket_.is_open()) {
        socket_.close(ec);
        if (!ec) {
            spdlog::info("Client socket closed");
        } else {
            spdlog::error("Close client socket error: {}", ec.message());
        }
    }

    http_.close_conn();
}
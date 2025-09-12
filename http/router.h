#ifndef ROUTER_H
#define ROUTER_H

#include <unordered_map>
#include <functional>
#include "http_conn.h"
#include "user_service.h"

enum class HTTP_CODE;
class http_conn;

using RouteHandler = std::function<HTTP_CODE(http_conn&, UserService&)>;

// 路由分发
class Router {
private:
    std::unordered_map<std::string, RouteHandler> routes; 

public:
    Router() {
        register_route("/favicon.ico", handle_favicon);
        register_route("/", handle_main);
        register_route("/welcome", handle_welcome);
    }

    void register_route(const std::string& path, RouteHandler handler) { // URL绑定处理函数映射
        routes[path] = std::move(handler);
    }

    HTTP_CODE dispatch(http_conn& conn, UserService& service) {
        std::string url = conn.get_url();
        size_t pos = url.find('?'); // 处理重定向
        if (pos != std::string::npos) {
            url = url.substr(0, pos);
        }
        auto it = routes.find(url);
        if (it != routes.end()) {
            return it->second(conn, service);
        }
        return HTTP_CODE::NO_RESOURCE;
    }

private:
    static HTTP_CODE handle_favicon(http_conn& conn, UserService& service) {
        (void)service;
        conn.set_requested_file("/favicon.ico");
        return HTTP_CODE::FILE_REQUEST;
    }

    static HTTP_CODE handle_main(http_conn& conn, UserService& service) {
        (void)service;
        conn.set_requested_file("/main.html");
        return HTTP_CODE::FILE_REQUEST;
    }

    static HTTP_CODE handle_welcome(http_conn& conn, UserService& service) {
        if (!conn.is_cgi()) {
            return HTTP_CODE::BAD_REQUEST;
        }
        const std::string& content = conn.get_request_content();
        size_t user_pos = content.find("user=");
        size_t password_pos = content.find("password=");
        size_t op_pos = content.find("op=");

        if (user_pos == std::string::npos || password_pos == std::string::npos) {
            return HTTP_CODE::BAD_REQUEST;
        }

        std::string username = content.substr(user_pos + 5, password_pos - (user_pos + 6));
        std::string password = content.substr(password_pos + 9, op_pos - (password_pos + 10));
        std::string op = content.substr(op_pos + 3);

        if (op == "login") {
            loginRequest req{username, password};
            loginResult res = service.login(req);
            if (res.success) {
                conn.set_requested_file("/welcome.jpg");
                return HTTP_CODE::FILE_REQUEST;
            } 
            else {
                conn.set_url("/?error=1");
                return HTTP_CODE::REDIRECT;
            }
        }

        else if (op == "register"){
            registerRequest req{username, password};
            registerResult res = service.registerUser(req);
            if (res.success) {
                conn.set_url("/?registerok=1");
                return HTTP_CODE::REDIRECT;
            }
            else {
                if (res.msg == "用户已存在"){
                    conn.set_url("/?user_exists=1");
                    return HTTP_CODE::REDIRECT;
                }
                else {
                    conn.set_url("/?error=1");
                    return HTTP_CODE::REDIRECT;
                }
            }
        }
        return HTTP_CODE::BAD_REQUEST;
    }
};

#endif
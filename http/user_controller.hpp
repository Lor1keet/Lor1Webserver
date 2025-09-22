#ifndef USER_CONTROLLER_H
#define USER_CONTROLLER_H

#include "user_service.hpp"
#include "http_parser.hpp"
#include "http_conn.hpp" 

class UserController {
public:
    explicit UserController(UserService& service) : m_service(service) {}

    HTTP_CODE handle_login_or_register(HttpRequest& req, HttpResponse& res);

    HTTP_CODE handle_favicon(HttpRequest& req, HttpResponse& res);

    HTTP_CODE handle_main(HttpRequest& req, HttpResponse& res);

private:
    UserService& m_service;
};

#endif
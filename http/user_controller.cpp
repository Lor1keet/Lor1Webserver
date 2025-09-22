#include "user_controller.hpp"

HTTP_CODE UserController::handle_login_or_register(HttpRequest& req, HttpResponse& res) {
    if (!req.is_cgi()) {
        return HTTP_CODE::BAD_REQUEST;
    }

    const std::string& content = req.get_content();
    size_t user_pos = content.find("user=");
    size_t password_pos = content.find("password=");
    size_t op_pos = content.find("op=");
    if (user_pos == std::string::npos || password_pos == std::string::npos || op_pos == std::string::npos) {
        return HTTP_CODE::BAD_REQUEST;
    }

    std::string username = content.substr(user_pos + 5, password_pos - (user_pos + 6));
    std::string password = content.substr(password_pos + 9, op_pos - (password_pos + 10));
    std::string op = content.substr(op_pos + 3);

    if (op == "login") {
        loginResult login_res = m_service.login({username, password});
        if (login_res.success) {
            res.set_required_file_path("/welcome.jpg");
            return HTTP_CODE::FILE_REQUEST;
        } 
        else {
            res.set_redirect_url("/?error=1");
            return HTTP_CODE::REDIRECT;
        }
    } 
    else if (op == "register") {
        registerResult reg_res = m_service.registerUser({username, password});
        if (reg_res.success) {
            res.set_redirect_url("/?registerok=1");
        } 
        else {
            res.set_redirect_url(reg_res.msg == "用户已存在" ? "/?user_exists=1" : "/?error=1");
        }
        return HTTP_CODE::REDIRECT;
    }
    return HTTP_CODE::BAD_REQUEST;
}

HTTP_CODE UserController::handle_favicon(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.set_required_file_path("/favicon.ico");
    return HTTP_CODE::FILE_REQUEST;
}

HTTP_CODE UserController::handle_main(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.set_required_file_path("/main.html");
    return HTTP_CODE::FILE_REQUEST;
}
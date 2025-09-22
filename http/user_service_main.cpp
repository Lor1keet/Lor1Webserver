#include "user_service.hpp"
#include "../mysql/mysqlpool.hpp"
#include "spdlog/spdlog.h"

loginResult UserServiceMain::login(const loginRequest& req){
    loginResult res;
    res.success = false;
    connection_pool* db_pool = connection_pool::GetInstance();
    connPtr mysql = db_pool->GetConnection();
    MYSQL* raw_mysql = mysql.get();

    MYSQL_STMT* stmt = mysql_stmt_init(raw_mysql);
    if (!stmt){
        res.msg = "数据库预处理语句初始化失败";
        return res;
    }

    const char* sql = "SELECT password FROM user WHERE username = ?";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0){
        res.msg = "数据库预处理失败";
        mysql_stmt_close(stmt);
        return res;
    }

    MYSQL_BIND param_bind;
    memset(&param_bind, 0, sizeof(param_bind));
    param_bind.buffer_type = MYSQL_TYPE_STRING;
    param_bind.buffer = (char*)req.username.c_str();
    param_bind.buffer_length = req.username.size();
    if (mysql_stmt_bind_param(stmt, &param_bind) != 0){
        res.msg = "参数绑定失败";
        mysql_stmt_close(stmt);
        return res;
    }

    if (mysql_stmt_execute(stmt) != 0){
        res.msg = "查询执行失败";
        mysql_stmt_close(stmt);
        return res;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        res.msg = "存储结果失败";
        mysql_stmt_close(stmt);
        return res;
    }

    MYSQL_RES *result = mysql_stmt_result_metadata(stmt);
    MYSQL_BIND result_bind;
    char passwd_buf[256];
    unsigned long passwd_len;
    memset(&result_bind, 0, sizeof(result_bind));
    result_bind.buffer_type = MYSQL_TYPE_STRING;
    result_bind.buffer = passwd_buf;
    result_bind.buffer_length = sizeof(passwd_buf);
    result_bind.length = &passwd_len;
    mysql_stmt_bind_result(stmt, &result_bind);
    int fetch_result = mysql_stmt_fetch(stmt);
    if (fetch_result == 0){
        std::string stored_passwd(passwd_buf, passwd_len);
        if (stored_passwd == req.password){
            res.success = true;
            res.msg = "登录成功";
        }
        else{
            res.msg = "密码错误";
        }
    }
    else if (fetch_result == MYSQL_NO_DATA){
        res.msg = "用户名不存在";
    }
    else{
        res.msg = "获取结果失败";
    }
    mysql_free_result(result);
    mysql_stmt_close(stmt);

    return res;
}

registerResult UserServiceMain::registerUser(const registerRequest& req){
    registerResult res;
    res.success = false;
    connection_pool* db_pool = connection_pool::GetInstance();
    connPtr mysql = db_pool->GetConnection();
    MYSQL* raw_mysql = mysql.get();

    MYSQL_STMT* stmt = mysql_stmt_init(raw_mysql);
    if (!stmt){
        res.msg = "数据库预处理语句初始化失败";
        return res;
    }

    const char* sql = "INSERT INTO user(username, password) VALUES(?, ?)";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0){
        res.msg = "数据库预处理失败";
        mysql_stmt_close(stmt);
        return res;
    }

    MYSQL_BIND param_bind[2];
    memset(param_bind, 0, sizeof(param_bind));

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = (char*)req.username.c_str();
    param_bind[0].buffer_length = req.username.size();

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = (char*)req.password.c_str();
    param_bind[1].buffer_length = req.password.size();

    if (mysql_stmt_bind_param(stmt, param_bind) != 0){
        res.msg = "参数绑定失败";
        mysql_stmt_close(stmt);
        return res;
    }

    if (mysql_stmt_execute(stmt) != 0){
        if (mysql_stmt_errno(stmt) == 1062){
            res.msg = "用户已存在";
        }
        else{
            res.msg = "注册失败";
        }
        mysql_stmt_close(stmt);
        return res;
    }

    if (mysql_stmt_affected_rows(stmt) == 1){
        res.success = true;
        res.msg = "注册成功";
    } 
    else{
        res.msg = "未插入数据";
    }

    mysql_stmt_close(stmt);
    return res;
}
#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include "user_data.h"

// 业务接口抽象
class UserService {
public:
    virtual ~UserService() = default;
    virtual loginResult login(const loginRequest& req) = 0;
    virtual registerResult registerUser(const registerRequest& req) = 0;
};

// 业务接口实现
class UserServiceMain : public UserService {
public:
    loginResult login(const loginRequest& req) override;
    registerResult registerUser(const registerRequest& req) override;
};

#endif
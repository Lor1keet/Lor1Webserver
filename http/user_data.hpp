#include <string>

struct loginResult {
    bool success;
    std::string msg;
};

struct loginRequest {
    std::string username;
    std::string password;
};

struct registerRequest {
    std::string username;
    std::string password;
};

struct registerResult {
    bool success;
    std::string msg;
};
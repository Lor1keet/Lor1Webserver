#include <asio.hpp>
#include <spdlog/spdlog.h>
#include "webserver.hpp"
#include "../mysql/mysqlpool.hpp"

// 服务器配置参数
const int THREAD_NUM = 4;
const std::string IP = "127.0.0.1";      
const std::string PORT = "8080";                            
const std::string WEB_ROOT = "../root";   // 根路径
const std::string DB_USER = "root";      // 数据库账户名
const std::string DB_PASS = "123456";    // 数据库密码
const std::string DB_NAME = "test";  // 使用的数据库名
const int MAX_DB_CONN = 10;             

int main() {
    try {
        spdlog::set_level(spdlog::level::info);
        spdlog::info("正在启动服务器...");

        connection_pool* db_pool = connection_pool::GetInstance();
        db_pool->init(IP, DB_USER, DB_PASS, DB_NAME, 3306, MAX_DB_CONN, 0);
        spdlog::info("Database connection pool initialized with {} connections", MAX_DB_CONN);

        asio::io_context io_context;

        WebServer server(io_context, THREAD_NUM, WEB_ROOT);
        spdlog::info("Server started on port {}", PORT);
        spdlog::info("Web root: {}", WEB_ROOT);

        server.listen(IP, PORT);

        // 运行事件循环
        server.run();
    } catch (std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        return 1;
    }

    spdlog::info("关闭服务器");
    return 0;
}
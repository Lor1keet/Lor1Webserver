#ifndef ROUTER_H
#define ROUTER_H

#include <unordered_map>
#include <functional>
#include "http_parser.hpp" 
#include "user_controller.hpp" 

using RouteHandler = std::function<HTTP_CODE(HttpRequest&, HttpResponse&)>;

class Router {
public:
    Router(UserController& userController) {
        register_route("/welcome", 
            [&](HttpRequest& req, HttpResponse& res) {
                return userController.handle_login_or_register(req, res);
            }
        );
        
        register_route("/favicon.ico", 
            [&](HttpRequest& req, HttpResponse& res) {
                return userController.handle_favicon(req, res);
            });
        register_route("/", 
            [&](HttpRequest& req, HttpResponse& res) {
                return userController.handle_main(req, res);
            });
    }

    void register_route(const std::string& path, RouteHandler handler) {
        routes[path] = std::move(handler);
    }

    HTTP_CODE dispatch(HttpRequest& req, HttpResponse& res) {
        std::string url = req.get_url();
        size_t pos = url.find('?'); 
        if (pos != std::string::npos) {
            url = url.substr(0, pos);
        }
        auto it = routes.find(url);
        if (it != routes.end()) {
            return it->second(req, res); 
        }
        return HTTP_CODE::NO_RESOURCE;
    }

private:
    std::unordered_map<std::string, RouteHandler> routes; 
};

#endif
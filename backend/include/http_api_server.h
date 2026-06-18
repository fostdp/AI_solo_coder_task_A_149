#ifndef HTTP_API_SERVER_H
#define HTTP_API_SERVER_H

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <map>
#include <vector>

namespace trebuchet {
namespace http {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> query_params;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    static HttpResponse json(int status, const std::string& json_body);
    static HttpResponse error(int status, const std::string& message);
};

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpApiServer {
public:
    struct Config {
        std::string bind_address = "0.0.0.0";
        int port = 8080;
        int max_connections = 10;
        std::string static_files_dir;
    };

    HttpApiServer(const Config& config);
    ~HttpApiServer();

    void addRoute(const std::string& method, const std::string& path, RouteHandler handler);
    void addGetRoute(const std::string& path, RouteHandler handler);
    void addPostRoute(const std::string& path, RouteHandler handler);

    bool start();
    void stop();
    bool isRunning() const;

    void setNotFoundHandler(RouteHandler handler);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void serverLoop();
    void handleClient(int client_fd);
    bool parseRequest(const std::string& raw_request, HttpRequest& out_req);
    void parseQueryString(const std::string& query, std::map<std::string, std::string>& out);
    HttpResponse routeRequest(const HttpRequest& req);

    std::atomic<bool> running_;
    std::thread server_thread_;
    std::map<std::pair<std::string, std::string>, RouteHandler> routes_;
    RouteHandler not_found_handler_;
};

}
}

#endif

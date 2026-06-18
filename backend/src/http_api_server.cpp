#include "http_api_server.h"

#include <sstream>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#define SOCKET int
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#define closesocket(s) close(s)
#endif

namespace trebuchet {
namespace http {

HttpResponse HttpResponse::json(int status, const std::string& json_body) {
    HttpResponse resp;
    resp.status_code = status;
    resp.status_text = status == 200 ? "OK" : (status == 201 ? "Created" : "Error");
    resp.headers["Content-Type"] = "application/json; charset=utf-8";
    resp.headers["Content-Length"] = std::to_string(json_body.size());
    resp.headers["Access-Control-Allow-Origin"] = "*";
    resp.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
    resp.headers["Access-Control-Allow-Headers"] = "Content-Type";
    resp.body = json_body;
    return resp;
}

HttpResponse HttpResponse::error(int status, const std::string& message) {
    std::ostringstream json_ss;
    json_ss << "{\"error\":" << status << ",\"message\":\"" << message << "\"}";
    return HttpResponse::json(status, json_ss.str());
}

struct HttpApiServer::Impl {
    Config config;
    SOCKET server_fd = INVALID_SOCKET;
};

HttpApiServer::HttpApiServer(const Config& config)
    : impl_(std::make_unique<Impl>()), running_(false) {
    impl_->config = config;
}

HttpApiServer::~HttpApiServer() {
    stop();
}

void HttpApiServer::addRoute(
    const std::string& method,
    const std::string& path,
    RouteHandler handler
) {
    routes_[{method, path}] = std::move(handler);
}

void HttpApiServer::addGetRoute(const std::string& path, RouteHandler handler) {
    addRoute("GET", path, std::move(handler));
}

void HttpApiServer::addPostRoute(const std::string& path, RouteHandler handler) {
    addRoute("POST", path, std::move(handler));
}

bool HttpApiServer::start() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
#endif

    impl_->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->server_fd == INVALID_SOCKET) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(impl_->server_fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&opt), sizeof(opt));
#else
    setsockopt(impl_->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(impl_->config.port);
    inet_pton(AF_INET, impl_->config.bind_address.c_str(), &addr.sin_addr);

    if (bind(impl_->server_fd, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) == SOCKET_ERROR) {
        closesocket(impl_->server_fd);
        impl_->server_fd = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (listen(impl_->server_fd, impl_->config.max_connections) == SOCKET_ERROR) {
        closesocket(impl_->server_fd);
        impl_->server_fd = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
#ifdef _WIN32
    setsockopt(impl_->server_fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&timeout), sizeof(timeout));
#else
    setsockopt(impl_->server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    running_ = true;

    addGetRoute("/health", [](const HttpRequest&) {
        std::ostringstream json;
        json << "{\"status\":\"ok\",\"timestamp\":"
             << std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()
             << ",\"version\":\"2.0.0\"}";
        return HttpResponse::json(200, json.str());
    });

    if (!impl_->config.static_files_dir.empty()) {
        addGetRoute("/", [this](const HttpRequest& req) {
            return serveStaticFile(req, "/index.html");
        });

        setNotFoundHandler([this](const HttpRequest& req) {
            return serveStaticFile(req, req.path);
        });
    }

    server_thread_ = std::thread(&HttpApiServer::serverLoop, this);
    return true;
}

void HttpApiServer::stop() {
    running_ = false;
    if (server_thread_.joinable()) server_thread_.join();
    if (impl_->server_fd != INVALID_SOCKET) {
        closesocket(impl_->server_fd);
        impl_->server_fd = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpApiServer::isRunning() const {
    return running_;
}

void HttpApiServer::setNotFoundHandler(RouteHandler handler) {
    not_found_handler_ = std::move(handler);
}

void HttpApiServer::serverLoop() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(impl_->server_fd,
                               reinterpret_cast<struct sockaddr*>(&client_addr),
                               &addr_len);
        if (client_fd == INVALID_SOCKET) continue;
        handleClient(client_fd);
    }
}

void HttpApiServer::handleClient(int client_fd) {
    std::vector<char> buffer(65536);
#ifdef _WIN32
    int total = recv(client_fd, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
#else
    ssize_t total = recv(client_fd, buffer.data(), buffer.size() - 1, 0);
#endif
    if (total <= 0) {
        closesocket(client_fd);
        return;
    }

    std::string raw(buffer.data(), total);
    HttpRequest req;
    HttpResponse resp;

    if (parseRequest(raw, req)) {
        if (req.method == "OPTIONS") {
            resp = HttpResponse::json(200, "{}");
        } else {
            resp = routeRequest(req);
        }
    } else {
        resp = HttpResponse::error(400, "Bad Request");
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status_code << " " << resp.status_text << "\r\n";
    for (const auto& [k, v] : resp.headers) {
        out << k << ": " << v << "\r\n";
    }
    if (resp.headers.find("Content-Length") == resp.headers.end()) {
        out << "Content-Length: " << resp.body.size() << "\r\n";
    }
    out << "Connection: close\r\n\r\n" << resp.body;

    std::string resp_str = out.str();
#ifdef _WIN32
    send(client_fd, resp_str.c_str(), static_cast<int>(resp_str.size()), 0);
    closesocket(client_fd);
#else
    send(client_fd, resp_str.c_str(), resp_str.size(), 0);
    close(client_fd);
#endif
}

bool HttpApiServer::parseRequest(
    const std::string& raw_request,
    HttpRequest& out_req
) {
    std::istringstream iss(raw_request);
    std::string line;

    if (!std::getline(iss, line)) return false;
    {
        std::istringstream ls(line);
        ls >> out_req.method;
        std::string full_path;
        ls >> full_path;
        size_t qpos = full_path.find('?');
        if (qpos != std::string::npos) {
            out_req.path = full_path.substr(0, qpos);
            out_req.query = full_path.substr(qpos + 1);
            parseQueryString(out_req.query, out_req.query_params);
        } else {
            out_req.path = full_path;
        }
    }

    while (std::getline(iss, line) && line != "\r" && !line.empty()) {
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(0, 1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
                value.pop_back();
            out_req.headers[key] = value;
        }
    }

    out_req.body.clear();
    while (std::getline(iss, line)) {
        out_req.body += line + "\n";
    }

    return true;
}

void HttpApiServer::parseQueryString(
    const std::string& query,
    std::map<std::string, std::string>& out
) {
    std::istringstream iss(query);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            out[pair.substr(0, eq)] = pair.substr(eq + 1);
        } else {
            out[pair] = "";
        }
    }
}

HttpResponse HttpApiServer::routeRequest(const HttpRequest& req) {
    auto it = routes_.find({req.method, req.path});
    if (it != routes_.end()) {
        return it->second(req);
    }
    if (not_found_handler_) return not_found_handler_(req);
    return HttpResponse::error(404, "Not Found");
}

std::string HttpApiServer::getMimeType(const std::string& path) {
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html; charset=utf-8"},
        {".htm",  "text/html; charset=utf-8"},
        {".js",   "application/javascript; charset=utf-8"},
        {".mjs",  "application/javascript; charset=utf-8"},
        {".css",  "text/css; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".svg",  "image/svg+xml"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".ico",  "image/x-icon"},
        {".map",  "application/json; charset=utf-8"},
        {".txt",  "text/plain; charset=utf-8"},
        {".woff", "font/woff"},
        {".woff2","font/woff2"},
        {".ttf",  "font/ttf"},
        {".eot",  "application/vnd.ms-fontobject"}
    };
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? it->second : "application/octet-stream";
}

bool HttpApiServer::acceptsGzip(const HttpRequest& req) {
    auto it = req.headers.find("Accept-Encoding");
    if (it == req.headers.end()) {
        it = req.headers.find("accept-encoding");
    }
    if (it == req.headers.end()) return false;
    return it->second.find("gzip") != std::string::npos;
}

HttpResponse HttpApiServer::serveStaticFile(const HttpRequest& req, const std::string& url_path) {
    if (url_path.empty() || url_path[0] != '/') {
        return HttpResponse::error(400, "Bad Request");
    }

    std::string clean_path = url_path;
    while (clean_path.size() > 1 && clean_path[1] == '/') {
        clean_path.erase(0, 1);
    }
    if (clean_path.find("..") != std::string::npos) {
        return HttpResponse::error(403, "Forbidden");
    }

    fs::path base_dir = impl_->config.static_files_dir;
    fs::path file_path = base_dir / fs::path(clean_path.substr(1));

    if (fs::is_directory(file_path)) {
        file_path /= "index.html";
    }

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        return HttpResponse::error(404, "Not Found");
    }

    bool use_gzip = acceptsGzip(req);
    fs::path gz_path = file_path;
    gz_path += ".gz";

    std::string final_path = file_path.string();
    HttpResponse resp;
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.headers["Content-Type"] = getMimeType(file_path.string());
    resp.headers["Cache-Control"] = "public, max-age=3600";

    if (use_gzip && fs::exists(gz_path) && fs::is_regular_file(gz_path)) {
        final_path = gz_path.string();
        resp.headers["Content-Encoding"] = "gzip";
        resp.headers["Vary"] = "Accept-Encoding";
    }

    std::ifstream file(final_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return HttpResponse::error(500, "Internal Server Error");
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    resp.body.resize(static_cast<size_t>(size));
    if (!file.read(resp.body.data(), size)) {
        return HttpResponse::error(500, "Internal Server Error");
    }

    resp.headers["Content-Length"] = std::to_string(resp.body.size());
    resp.headers["Last-Modified"] = "Thu, 01 Jan 1970 00:00:00 GMT";
    resp.headers["X-Content-Type-Options"] = "nosniff";
    resp.headers["X-Frame-Options"] = "DENY";

    return resp;
}

}
}

#include "udp_sensor_receiver.h"

#include <sstream>
#include <cstring>
#include <iostream>

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
namespace network {

struct UdpSensorReceiver::Impl {
    Config config;
    SOCKET sock_fd = INVALID_SOCKET;
};

UdpSensorReceiver::UdpSensorReceiver(const Config& config)
    : impl_(std::make_unique<Impl>()), running_(false) {
    impl_->config = config;
}

UdpSensorReceiver::~UdpSensorReceiver() {
    stop();
}

bool UdpSensorReceiver::start() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif

    impl_->sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->sock_fd == INVALID_SOCKET) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(impl_->config.port);
    inet_pton(AF_INET, impl_->config.bind_address.c_str(), &addr.sin_addr);

    int reuse = 1;
#ifdef _WIN32
    setsockopt(impl_->sock_fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&reuse), sizeof(reuse));
#else
    setsockopt(impl_->sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    if (bind(impl_->sock_fd, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) == SOCKET_ERROR) {
        closesocket(impl_->sock_fd);
        impl_->sock_fd = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
#ifdef _WIN32
    setsockopt(impl_->sock_fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&timeout), sizeof(timeout));
#else
    setsockopt(impl_->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    running_ = true;
    receive_thread_ = std::thread(&UdpSensorReceiver::receiveLoop, this);
    return true;
}

void UdpSensorReceiver::stop() {
    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    if (impl_->sock_fd != INVALID_SOCKET) {
        closesocket(impl_->sock_fd);
        impl_->sock_fd = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool UdpSensorReceiver::isRunning() const {
    return running_;
}

void UdpSensorReceiver::setDataCallback(SensorDataCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    data_callback_ = std::move(callback);
}

bool UdpSensorReceiver::parsePacket(
    const std::string& raw_data,
    SensorDataPacket& out_packet
) {
    out_packet.efficiency = 0.0;
    out_packet.cycle_count = 0;
    out_packet.cyclic_damage_ratio = 0.0;
    out_packet.plastic_strain = 0.0;
    out_packet.max_mach = 0.0;

    std::istringstream iss(raw_data);
    std::string field;
    std::vector<std::string> fields;
    while (std::getline(iss, field, '|')) {
        fields.push_back(field);
    }
    if (fields.size() < 7) {
        std::string key, value;
        iss.clear();
        iss.str(raw_data);
        while (std::getline(iss, field, ',')) {
            size_t eq = field.find('=');
            if (eq == std::string::npos) return false;
            key = field.substr(0, eq);
            value = field.substr(eq + 1);
            if (key == "machine_id") out_packet.machine_id = value;
            else if (key == "torsion_angle") out_packet.torsion_angle = std::stod(value);
            else if (key == "stored_energy") out_packet.stored_energy = std::stod(value);
            else if (key == "release_velocity") out_packet.release_velocity = std::stod(value);
            else if (key == "actual_range") out_packet.actual_range = std::stod(value);
            else if (key == "projectile_mass") out_packet.projectile_mass = std::stod(value);
            else if (key == "launch_angle") out_packet.launch_angle = std::stod(value);
            else if (key == "spring_status") out_packet.spring_status = value;
            else if (key == "efficiency") out_packet.efficiency = std::stod(value);
            else if (key == "cycle_count") out_packet.cycle_count = std::stoll(value);
            else if (key == "cyclic_damage_ratio") out_packet.cyclic_damage_ratio = std::stod(value);
            else if (key == "plastic_strain") out_packet.plastic_strain = std::stod(value);
            else if (key == "max_mach") out_packet.max_mach = std::stod(value);
        }
        out_packet.timestamp = std::chrono::system_clock::now();
        return !out_packet.machine_id.empty();
    }

    out_packet.machine_id = fields[0];
    try {
        out_packet.torsion_angle = std::stod(fields[1]);
        out_packet.stored_energy = std::stod(fields[2]);
        out_packet.release_velocity = std::stod(fields[3]);
        out_packet.actual_range = std::stod(fields[4]);
        out_packet.projectile_mass = std::stod(fields[5]);
        out_packet.launch_angle = std::stod(fields[6]);
        if (fields.size() > 8) out_packet.efficiency = std::stod(fields[8]);
        if (fields.size() > 9) out_packet.cycle_count = std::stoll(fields[9]);
        if (fields.size() > 10) out_packet.cyclic_damage_ratio = std::stod(fields[10]);
        if (fields.size() > 11) out_packet.plastic_strain = std::stod(fields[11]);
        if (fields.size() > 12) out_packet.max_mach = std::stod(fields[12]);
    } catch (...) {
        return false;
    }
    out_packet.spring_status = fields.size() > 7 ? fields[7] : "normal";
    out_packet.timestamp = std::chrono::system_clock::now();
    return true;
}

void UdpSensorReceiver::receiveLoop() {
    std::vector<char> buffer(impl_->config.buffer_size);
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (running_) {
#ifdef _WIN32
        int bytes = recvfrom(impl_->sock_fd, buffer.data(),
                             static_cast<int>(buffer.size()), 0,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &addr_len);
#else
        ssize_t bytes = recvfrom(impl_->sock_fd, buffer.data(), buffer.size(), 0,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
                                 &addr_len);
#endif

        if (bytes <= 0) {
#ifdef _WIN32
            if (WSAGetLastError() != WSAETIMEDOUT) continue;
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK) continue;
#endif
            continue;
        }

        std::string raw_data(buffer.data(), bytes);
        SensorDataPacket packet;
        if (parsePacket(raw_data, packet)) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (data_callback_) {
                data_callback_(packet);
            }
        }
    }
}

}
}

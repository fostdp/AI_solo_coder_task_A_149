#include "udp_receiver_module.h"

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
namespace modules {

struct UdpReceiverModule::Impl {
    SOCKET sock_fd = INVALID_SOCKET;
};

UdpReceiverModule::UdpReceiverModule(const Config& config, bus::MessageBus* bus)
    : impl_(std::make_unique<Impl>()), config_(config), bus_(bus) {}

UdpReceiverModule::~UdpReceiverModule() { stop(); }

void UdpReceiverModule::setPacketHandler(PacketHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    packet_handler_ = std::move(handler);
}

bool UdpReceiverModule::start() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
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
    addr.sin_port = htons(config_.port);
    inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr);

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
    timeout.tv_sec = config_.receive_timeout_ms / 1000;
    timeout.tv_usec = (config_.receive_timeout_ms % 1000) * 1000;
#ifdef _WIN32
    setsockopt(impl_->sock_fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&timeout), sizeof(timeout));
#else
    setsockopt(impl_->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    running_ = true;
    receive_thread_ = std::thread(&UdpReceiverModule::receiveLoop, this);
    return true;
}

void UdpReceiverModule::stop() {
    running_ = false;
    if (receive_thread_.joinable()) receive_thread_.join();
    if (impl_->sock_fd != INVALID_SOCKET) {
        closesocket(impl_->sock_fd);
        impl_->sock_fd = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool UdpReceiverModule::isRunning() const { return running_.load(); }

static std::vector<std::string> splitString(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) tokens.push_back(tok);
    return tokens;
}

static double safeStod(const std::string& s, double fallback = 0.0) {
    try { return s.empty() ? fallback : std::stod(s); }
    catch (...) { return fallback; }
}

static int64_t safeStoll(const std::string& s, int64_t fallback = 0) {
    try { return s.empty() ? fallback : std::stoll(s); }
    catch (...) { return fallback; }
}

bool UdpReceiverModule::parsePacketToMessage(const std::string& raw, bus::SensorRawMessage& out) {
    const bool hasPipe = raw.find('|') != std::string::npos;
    const bool hasKv = raw.find('=') != std::string::npos;

    if (!hasPipe && !hasKv) return false;

    out.timestamp_ms = bus::nowMs();

    if (hasPipe) {
        auto parts = splitString(raw, '|');
        if (parts.size() < 9) return false;
        out.setMachineId(parts[0]);
        if (parts.size() > 1) {
            try { out.timestamp_ms = std::stoll(parts[1]); }
            catch (...) { out.timestamp_ms = bus::nowMs(); }
        }
        out.torsion_angle_rad = safeStod(parts.size() > 2 ? parts[2] : "");
        out.stored_energy_j = safeStod(parts.size() > 3 ? parts[3] : "");
        out.release_velocity = safeStod(parts.size() > 4 ? parts[4] : "");
        out.actual_range_m = safeStod(parts.size() > 5 ? parts[5] : "");
        out.projectile_mass_kg = safeStod(parts.size() > 6 ? parts[6] : "10");
        out.launch_angle_deg = safeStod(parts.size() > 7 ? parts[7] : "45");
        out.setSpringStatus(parts.size() > 8 ? parts[8] : "ok");
        out.efficiency = safeStod(parts.size() > 9 ? parts[9] : "");
        out.cycle_count = safeStoll(parts.size() > 10 ? parts[10] : "0");
        out.cyclic_damage_ratio = safeStod(parts.size() > 11 ? parts[11] : "");
        out.plastic_strain = safeStod(parts.size() > 12 ? parts[12] : "");
        out.max_mach = safeStod(parts.size() > 13 ? parts[13] : "");
    } else {
        auto pairs = splitString(raw, ';');
        for (const auto& p : pairs) {
            auto eq = p.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = p.substr(0, eq);
            const std::string val = p.substr(eq + 1);
            if (key == "machine_id") out.setMachineId(val);
            else if (key == "timestamp_ms") out.timestamp_ms = safeStoll(val);
            else if (key == "torsion_angle") out.torsion_angle_rad = safeStod(val);
            else if (key == "stored_energy") out.stored_energy_j = safeStod(val);
            else if (key == "release_velocity") out.release_velocity = safeStod(val);
            else if (key == "actual_range") out.actual_range_m = safeStod(val);
            else if (key == "projectile_mass") out.projectile_mass_kg = safeStod(val);
            else if (key == "launch_angle") out.launch_angle_deg = safeStod(val);
            else if (key == "spring_status") out.setSpringStatus(val);
            else if (key == "efficiency") out.efficiency = safeStod(val);
            else if (key == "cycle_count") out.cycle_count = safeStoll(val);
            else if (key == "cyclic_damage_ratio") out.cyclic_damage_ratio = safeStod(val);
            else if (key == "plastic_strain") out.plastic_strain = safeStod(val);
            else if (key == "max_mach") out.max_mach = safeStod(val);
        }
    }
    return true;
}

bool UdpReceiverModule::dispatchToBus(const bus::SensorRawMessage& msg) {
    if (!bus_) return false;
    bool ok = bus_->push(bus::QueueChannel::SENSOR_TO_SPRING, msg);
    if (!ok) bus_dropped_++;
    return ok;
}

void UdpReceiverModule::receiveLoop() {
    std::vector<char> buffer(config_.buffer_size);
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
        if (bytes <= 0) continue;

        total_received_++;
        std::string raw(buffer.data(), static_cast<size_t>(bytes));
        while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r'))
            raw.pop_back();

        bus::SensorRawMessage msg;
        bool parsed = parsePacketToMessage(raw, msg);
        if (!parsed) {
            dropped_invalid_++;
            continue;
        }

        bool valid = msg.validate();
        if (!valid && config_.enable_checksum_validation) {
            msg.finalize();
            valid = msg.validate();
        }
        if (!valid && config_.drop_invalid_packets) {
            dropped_invalid_++;
            continue;
        }

        valid_ok_++;
        dispatchToBus(msg);

        if (packet_handler_) {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            if (packet_handler_) packet_handler_(msg, valid);
        }
    }
}

}
}

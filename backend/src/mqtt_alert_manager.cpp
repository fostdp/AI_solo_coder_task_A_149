#include "mqtt_alert_manager.h"

#include <sstream>
#include <cstring>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#endif

namespace trebuchet {
namespace alert {

struct MqttAlertManager::Impl {
    MqttConfig config;
    bool connected = false;
    int sock_fd = -1;
};

MqttAlertManager::MqttAlertManager(const MqttConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
}

MqttAlertManager::~MqttAlertManager() {
    disconnect();
}

static std::vector<uint8_t> encodeMqttVarLength(uint32_t length) {
    std::vector<uint8_t> encoded;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0) byte |= 0x80;
        encoded.push_back(byte);
    } while (length > 0);
    return encoded;
}

static std::vector<uint8_t> encodeMqttString(const std::string& s) {
    std::vector<uint8_t> result;
    result.push_back((s.size() >> 8) & 0xFF);
    result.push_back(s.size() & 0xFF);
    for (char c : s) result.push_back(static_cast<uint8_t>(c));
    return result;
}

static std::string formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &t);
#else
    localtime_r(&t, &tm_info);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_info, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

bool MqttAlertManager::connect() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    impl_->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->sock_fd < 0) return false;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(impl_->config.broker_port);
    inet_pton(AF_INET, impl_->config.broker_host.c_str(), &server_addr.sin_addr);

    if (::connect(impl_->sock_fd, (struct sockaddr*)&server_addr,
                  sizeof(server_addr)) < 0) {
#ifdef _WIN32
        closesocket(impl_->sock_fd);
#else
        close(impl_->sock_fd);
#endif
        impl_->sock_fd = -1;
        return false;
    }

    std::vector<uint8_t> payload;
    auto proto_name = encodeMqttString("MQTT");
    payload.insert(payload.end(), proto_name.begin(), proto_name.end());
    payload.push_back(4);
    uint8_t connect_flags = 0x02;
    if (!impl_->config.username.empty()) connect_flags |= 0x80;
    if (!impl_->config.password.empty()) connect_flags |= 0x40;
    payload.push_back(connect_flags);
    payload.push_back((impl_->config.keepalive >> 8) & 0xFF);
    payload.push_back(impl_->config.keepalive & 0xFF);
    auto client_id_bytes = encodeMqttString(impl_->config.client_id);
    payload.insert(payload.end(), client_id_bytes.begin(), client_id_bytes.end());
    if (!impl_->config.username.empty()) {
        auto user_bytes = encodeMqttString(impl_->config.username);
        payload.insert(payload.end(), user_bytes.begin(), user_bytes.end());
    }
    if (!impl_->config.password.empty()) {
        auto pass_bytes = encodeMqttString(impl_->config.password);
        payload.insert(payload.end(), pass_bytes.begin(), pass_bytes.end());
    }

    std::vector<uint8_t> packet;
    packet.push_back(0x10);
    auto var_len = encodeMqttVarLength(payload.size());
    packet.insert(packet.end(), var_len.begin(), var_len.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

#ifdef _WIN32
    send(impl_->sock_fd, reinterpret_cast<char*>(packet.data()),
         static_cast<int>(packet.size()), 0);
#else
    write(impl_->sock_fd, packet.data(), packet.size());
#endif

    uint8_t resp_buf[4] = {0};
#ifdef _WIN32
    recv(impl_->sock_fd, reinterpret_cast<char*>(resp_buf), 4, 0);
#else
    read(impl_->sock_fd, resp_buf, 4);
#endif

    if (resp_buf[0] == 0x20 && sizeof(resp_buf) >= 4 && resp_buf[3] == 0) {
        impl_->connected = true;
        return true;
    }

    impl_->connected = (resp_buf[0] == 0x20);
    return impl_->connected;
}

void MqttAlertManager::disconnect() {
    if (impl_->sock_fd >= 0 && impl_->connected) {
        uint8_t packet[] = {0xE0, 0x00};
#ifdef _WIN32
        send(impl_->sock_fd, reinterpret_cast<char*>(packet), 2, 0);
        closesocket(impl_->sock_fd);
#else
        write(impl_->sock_fd, packet, 2);
        close(impl_->sock_fd);
#endif
    }
#ifdef _WIN32
    WSACleanup();
#endif
    impl_->sock_fd = -1;
    impl_->connected = false;
}

bool MqttAlertManager::isConnected() const {
    return impl_->connected;
}

std::string MqttAlertManager::alertTypeToString(AlertType type) {
    switch (type) {
        case AlertType::SPRING_FRACTURE_RISK: return "spring_fracture_risk";
        case AlertType::INSUFFICIENT_RANGE: return "insufficient_range";
        case AlertType::EFFICIENCY_LOW: return "efficiency_low";
        case AlertType::SENSOR_TIMEOUT: return "sensor_timeout";
        case AlertType::SYSTEM_ERROR: return "system_error";
        case AlertType::CYCLIC_FATIGUE_RISK: return "cyclic_fatigue_risk";
    }
    return "unknown";
}

std::string MqttAlertManager::alertLevelToString(AlertLevel level) {
    switch (level) {
        case AlertLevel::INFO: return "info";
        case AlertLevel::WARNING: return "warning";
        case AlertLevel::CRITICAL: return "critical";
        case AlertLevel::FATAL: return "fatal";
    }
    return "unknown";
}

std::string MqttAlertManager::buildTopic(const AlertMessage& alert) {
    return impl_->config.topic_prefix + "/" + alert.machine_id + "/"
           + alertTypeToString(alert.type);
}

std::string MqttAlertManager::buildPayload(const AlertMessage& alert) {
    std::ostringstream json;
    json << "{"
         << "\"machine_id\":\"" << alert.machine_id << "\","
         << "\"timestamp\":\"" << formatTimestamp(alert.timestamp) << "\","
         << "\"alert_type\":\"" << alertTypeToString(alert.type) << "\","
         << "\"alert_level\":\"" << alertLevelToString(alert.level) << "\","
         << "\"message\":\"" << alert.message << "\","
         << "\"torsion_angle\":" << alert.torsion_angle << ","
         << "\"stored_energy\":" << alert.stored_energy << ","
         << "\"actual_range\":" << alert.actual_range << ","
         << "\"predicted_range\":" << alert.predicted_range << ","
         << "\"threshold_value\":" << alert.threshold_value
         << "}";
    return json.str();
}

bool MqttAlertManager::publishAlert(const AlertMessage& alert) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    alert_queue_.push(alert);
    if (alert_callback_) {
        alert_callback_(alert);
    }

    if (!impl_->connected) return false;

    std::string topic = buildTopic(alert);
    std::string payload = buildPayload(alert);

    std::vector<uint8_t> variable_header;
    variable_header.push_back(0x00);
    variable_header.push_back(0x01);
    auto topic_bytes = encodeMqttString(topic);
    variable_header.insert(variable_header.end(),
                           topic_bytes.begin(), topic_bytes.end());

    std::vector<uint8_t> body;
    body.insert(body.end(), variable_header.begin(), variable_header.end());
    for (char c : payload) body.push_back(static_cast<uint8_t>(c));

    std::vector<uint8_t> packet;
    packet.push_back(0x32);
    auto var_len = encodeMqttVarLength(body.size());
    packet.insert(packet.end(), var_len.begin(), var_len.end());
    packet.insert(packet.end(), body.begin(), body.end());

#ifdef _WIN32
    int sent = send(impl_->sock_fd, reinterpret_cast<char*>(packet.data()),
                    static_cast<int>(packet.size()), 0);
#else
    ssize_t sent = write(impl_->sock_fd, packet.data(), packet.size());
#endif
    return sent > 0;
}

bool MqttAlertManager::publishSpringFractureWarning(
    const std::string& machine_id,
    double torsion_angle,
    double yield_strength_ratio,
    double shear_stress
) {
    AlertMessage alert;
    alert.machine_id = machine_id;
    alert.timestamp = std::chrono::system_clock::now();
    alert.type = AlertType::SPRING_FRACTURE_RISK;
    alert.level = yield_strength_ratio > 0.95 ? AlertLevel::CRITICAL
                                              : AlertLevel::WARNING;
    std::ostringstream msg;
    msg << "弹簧断裂风险: 扭转角=" << torsion_angle << "rad, "
        << "屈服强度比=" << (yield_strength_ratio * 100) << "%, "
        << "剪应力=" << shear_stress << "Pa";
    alert.message = msg.str();
    alert.torsion_angle = torsion_angle;
    alert.threshold_value = 0.85;
    return publishAlert(alert);
}

bool MqttAlertManager::publishInsufficientRangeWarning(
    const std::string& machine_id,
    double actual_range,
    double predicted_range,
    double minimum_required
) {
    AlertMessage alert;
    alert.machine_id = machine_id;
    alert.timestamp = std::chrono::system_clock::now();
    alert.type = AlertType::INSUFFICIENT_RANGE;
    alert.level = AlertLevel::WARNING;
    std::ostringstream msg;
    msg << "射程不足: 实际=" << actual_range << "m, "
        << "预测=" << predicted_range << "m, "
        << "最低要求=" << minimum_required << "m";
    alert.message = msg.str();
    alert.actual_range = actual_range;
    alert.predicted_range = predicted_range;
    alert.threshold_value = minimum_required;
    return publishAlert(alert);
}

bool MqttAlertManager::publishEfficiencyWarning(
    const std::string& machine_id,
    double current_efficiency,
    double expected_efficiency
) {
    AlertMessage alert;
    alert.machine_id = machine_id;
    alert.timestamp = std::chrono::system_clock::now();
    alert.type = AlertType::EFFICIENCY_LOW;
    alert.level = current_efficiency < 0.6 ? AlertLevel::WARNING : AlertLevel::INFO;
    std::ostringstream msg;
    msg << "效率偏低: 当前=" << (current_efficiency * 100) << "%, "
        << "预期=" << (expected_efficiency * 100) << "%";
    alert.message = msg.str();
    alert.threshold_value = expected_efficiency;
    return publishAlert(alert);
}

bool MqttAlertManager::publishCyclicFatigueWarning(
    const std::string& machine_id,
    int64_t cycle_count,
    double cyclic_damage_ratio,
    double plastic_strain,
    int64_t remaining_life_cycles
) {
    AlertMessage alert;
    alert.machine_id = machine_id;
    alert.timestamp = std::chrono::system_clock::now();
    alert.type = AlertType::CYCLIC_FATIGUE_RISK;
    alert.level = cyclic_damage_ratio > 0.8 ? AlertLevel::CRITICAL
                : cyclic_damage_ratio > 0.5 ? AlertLevel::WARNING
                : AlertLevel::INFO;
    std::ostringstream msg;
    msg << "循环疲劳风险: 循环次数=" << cycle_count
        << ", 损伤比=" << (cyclic_damage_ratio * 100) << "%"
        << ", 塑性应变=" << plastic_strain
        << ", 剩余寿命=" << remaining_life_cycles << "次";
    alert.message = msg.str();
    alert.threshold_value = cyclic_damage_ratio;
    return publishAlert(alert);
}

void MqttAlertManager::setAlertCallback(AlertCallback callback) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    alert_callback_ = std::move(callback);
}

void MqttAlertManager::processQueuedAlerts() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!alert_queue_.empty()) {
        const auto& alert = alert_queue_.front();
        if (alert_callback_) alert_callback_(alert);
        alert_queue_.pop();
    }
}

}
}

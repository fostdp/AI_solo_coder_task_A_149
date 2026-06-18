#include "clickhouse_client.h"

#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace trebuchet {
namespace storage {

struct ClickHouseClient::Impl {
    Config config;
    bool connected = false;
};

ClickHouseClient::ClickHouseClient(const Config& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
}

ClickHouseClient::~ClickHouseClient() = default;

static std::string httpPost(
    const std::string& host,
    int port,
    const std::string& path,
    const std::string& body
) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return "";
    }
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
#ifdef _WIN32
        WSACleanup();
#endif
        return "";
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return "";
    }

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << ":" << port << "\r\n";
    request << "Content-Type: text/plain\r\n";
    request << "Content-Length: " << body.size() << "\r\n";
    request << "Connection: close\r\n\r\n";
    request << body;

    std::string req_str = request.str();
    send(sock, req_str.c_str(), static_cast<int>(req_str.size()), 0);

    std::string response;
    char buffer[4096];
    int bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        response += buffer;
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return response;
}

bool ClickHouseClient::connect() {
    std::string body = "SELECT 1";
    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        body
    );
    impl_->connected = response.find("1") != std::string::npos;
    return impl_->connected;
}

bool ClickHouseClient::isConnected() const {
    return impl_->connected;
}

std::string ClickHouseClient::formatTimestamp(
    const std::chrono::system_clock::time_point& tp
) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()
    ) % 1000;
    std::tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &t);
#else
    localtime_r(&t, &tm_info);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return ss.str();
}

static std::string escapeString(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '\'') result += "''";
        else result += c;
    }
    return result;
}

bool ClickHouseClient::insertSensorData(const SensorRecord& record) {
    std::ostringstream query;
    query << "INSERT INTO sensor_data ("
          << "machine_id, timestamp, torsion_angle, stored_energy, "
          << "release_velocity, actual_range, predicted_range, efficiency, "
          << "projectile_mass, launch_angle, spring_status, risk_level) VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << "'" << formatTimestamp(record.timestamp) << "', "
          << record.torsion_angle << ", "
          << record.stored_energy << ", "
          << record.release_velocity << ", "
          << record.actual_range << ", "
          << record.predicted_range << ", "
          << record.efficiency << ", "
          << record.projectile_mass << ", "
          << record.launch_angle << ", "
          << "'" << escapeString(record.spring_status) << "', "
          << "'" << escapeString(record.risk_level) << "')";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );
    return response.find("HTTP/1.1 200") != std::string::npos ||
           response.find("Ok.") != std::string::npos;
}

bool ClickHouseClient::insertSensorDataBatch(
    const std::vector<SensorRecord>& records
) {
    for (const auto& r : records) {
        if (!insertSensorData(r)) return false;
    }
    return true;
}

bool ClickHouseClient::insertAlert(const AlertRecord& record) {
    std::ostringstream query;
    query << "INSERT INTO alerts ("
          << "machine_id, timestamp, alert_type, alert_level, message, "
          << "torsion_angle, stored_energy, actual_range, "
          << "predicted_range, threshold_value) VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << "'" << formatTimestamp(record.timestamp) << "', "
          << "'" << escapeString(record.alert_type) << "', "
          << "'" << escapeString(record.alert_level) << "', "
          << "'" << escapeString(record.message) << "', "
          << record.torsion_angle << ", "
          << record.stored_energy << ", "
          << record.actual_range << ", "
          << record.predicted_range << ", "
          << record.threshold_value << ")";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );
    return response.find("HTTP/1.1 200") != std::string::npos ||
           response.find("Ok.") != std::string::npos;
}

bool ClickHouseClient::insertSpringEnergy(const SpringEnergyRecord& record) {
    std::ostringstream query;
    query << "INSERT INTO spring_energy_data ("
          << "machine_id, torsion_angle, stored_energy, shear_stress, "
          << "spring_constant, efficiency, yield_strength_ratio) VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << record.torsion_angle << ", "
          << record.stored_energy << ", "
          << record.shear_stress << ", "
          << record.spring_constant << ", "
          << record.efficiency << ", "
          << record.yield_strength_ratio << ")";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );
    return response.find("HTTP/1.1 200") != std::string::npos ||
           response.find("Ok.") != std::string::npos;
}

bool ClickHouseClient::insertRangePrediction(
    const RangePredictionRecord& record
) {
    std::ostringstream query;
    query << "INSERT INTO range_predictions ("
          << "machine_id, projectile_mass, launch_angle, release_velocity, "
          << "predicted_range, max_height, flight_time, air_resistance_factor) "
          << "VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << record.projectile_mass << ", "
          << record.launch_angle << ", "
          << record.release_velocity << ", "
          << record.predicted_range << ", "
          << record.max_height << ", "
          << record.flight_time << ", "
          << record.air_resistance_factor << ")";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );
    return response.find("HTTP/1.1 200") != std::string::npos ||
           response.find("Ok.") != std::string::npos;
}

std::vector<ClickHouseClient::SensorRecord> ClickHouseClient::queryRecentSensorData(
    const std::string& machine_id,
    int limit
) {
    std::vector<SensorRecord> results;
    std::ostringstream query;
    query << "SELECT machine_id, toString(timestamp), torsion_angle, "
          << "stored_energy, release_velocity, actual_range, predicted_range, "
          << "efficiency, projectile_mass, launch_angle, spring_status, risk_level "
          << "FROM sensor_data ";
    if (!machine_id.empty()) {
        query << "WHERE machine_id = '" << escapeString(machine_id) << "' ";
    }
    query << "ORDER BY timestamp DESC LIMIT " << limit
          << " FORMAT TabSeparated";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );

    size_t body_pos = response.find("\r\n\r\n");
    if (body_pos == std::string::npos) return results;
    std::string body = response.substr(body_pos + 4);

    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        SensorRecord r;
        std::string ts_str;
        std::getline(ls, r.machine_id, '\t');
        std::getline(ls, ts_str, '\t');
        ls >> r.torsion_angle; ls.ignore();
        ls >> r.stored_energy; ls.ignore();
        ls >> r.release_velocity; ls.ignore();
        ls >> r.actual_range; ls.ignore();
        ls >> r.predicted_range; ls.ignore();
        ls >> r.efficiency; ls.ignore();
        ls >> r.projectile_mass; ls.ignore();
        ls >> r.launch_angle; ls.ignore();
        std::getline(ls, r.spring_status, '\t');
        std::getline(ls, r.risk_level, '\t');
        results.push_back(r);
    }
    return results;
}

std::vector<ClickHouseClient::AlertRecord> ClickHouseClient::queryRecentAlerts(
    const std::string& machine_id,
    int limit
) {
    std::vector<AlertRecord> results;
    std::ostringstream query;
    query << "SELECT machine_id, toString(timestamp), alert_type, alert_level, "
          << "message, torsion_angle, stored_energy, actual_range, "
          << "predicted_range, threshold_value "
          << "FROM alerts ";
    if (!machine_id.empty()) {
        query << "WHERE machine_id = '" << escapeString(machine_id) << "' ";
    }
    query << "ORDER BY timestamp DESC LIMIT " << limit
          << " FORMAT TabSeparated";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );

    size_t body_pos = response.find("\r\n\r\n");
    if (body_pos == std::string::npos) return results;
    std::string body = response.substr(body_pos + 4);

    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        AlertRecord r;
        std::string ts_str;
        std::getline(ls, r.machine_id, '\t');
        std::getline(ls, ts_str, '\t');
        std::getline(ls, r.alert_type, '\t');
        std::getline(ls, r.alert_level, '\t');
        std::getline(ls, r.message, '\t');
        ls >> r.torsion_angle; ls.ignore();
        ls >> r.stored_energy; ls.ignore();
        ls >> r.actual_range; ls.ignore();
        ls >> r.predicted_range; ls.ignore();
        ls >> r.threshold_value;
        results.push_back(r);
    }
    return results;
}

std::vector<ClickHouseClient::MachineStatus> ClickHouseClient::queryAllMachineStatus() {
    std::vector<MachineStatus> results;
    std::ostringstream query;
    query << "SELECT machine_id, toString(last_report_time), last_torsion_angle, "
          << "last_stored_energy, last_release_velocity, last_actual_range, "
          << "last_predicted_range, current_risk_level, unacknowledged_alerts "
          << "FROM latest_machine_status FORMAT TabSeparated";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );

    size_t body_pos = response.find("\r\n\r\n");
    if (body_pos == std::string::npos) return results;
    std::string body = response.substr(body_pos + 4);

    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        MachineStatus r;
        std::string ts_str;
        std::getline(ls, r.machine_id, '\t');
        std::getline(ls, ts_str, '\t');
        ls >> r.last_torsion_angle; ls.ignore();
        ls >> r.last_stored_energy; ls.ignore();
        ls >> r.last_release_velocity; ls.ignore();
        ls >> r.last_actual_range; ls.ignore();
        ls >> r.last_predicted_range; ls.ignore();
        std::getline(ls, r.current_risk_level, '\t');
        ls >> r.unacknowledged_alerts;
        results.push_back(r);
    }
    return results;
}

}
}

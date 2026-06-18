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
          << "projectile_mass, launch_angle, spring_status, risk_level, "
          << "shear_stress, elastic_stress, plastic_strain, cycle_count, "
          << "cyclic_damage_ratio, modulus_reduction, max_mach, "
          << "compressibility_correction, fatigue_risk) VALUES ("
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
          << "'" << escapeString(record.risk_level) << "', "
          << record.shear_stress << ", "
          << record.elastic_stress << ", "
          << record.plastic_strain << ", "
          << record.cycle_count << ", "
          << record.cyclic_damage_ratio << ", "
          << record.modulus_reduction << ", "
          << record.max_mach << ", "
          << record.compressibility_correction << ", "
          << static_cast<int>(record.fatigue_risk) << ")";

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
          << "predicted_range, threshold_value, cyclic_damage_ratio, "
          << "cycle_count, max_mach) VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << "'" << formatTimestamp(record.timestamp) << "', "
          << "'" << escapeString(record.alert_type) << "', "
          << "'" << escapeString(record.alert_level) << "', "
          << "'" << escapeString(record.message) << "', "
          << record.torsion_angle << ", "
          << record.stored_energy << ", "
          << record.actual_range << ", "
          << record.predicted_range << ", "
          << record.threshold_value << ", "
          << record.cyclic_damage_ratio << ", "
          << record.cycle_count << ", "
          << record.max_mach << ")";

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
          << "elastic_stress, plastic_strain, spring_constant, efficiency, "
          << "yield_strength_ratio, cycle_count, cyclic_damage_ratio, "
          << "modulus_reduction, back_stress, degraded_yield_strength) VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << record.torsion_angle << ", "
          << record.stored_energy << ", "
          << record.shear_stress << ", "
          << record.elastic_stress << ", "
          << record.plastic_strain << ", "
          << record.spring_constant << ", "
          << record.efficiency << ", "
          << record.yield_strength_ratio << ", "
          << record.cycle_count << ", "
          << record.cyclic_damage_ratio << ", "
          << record.modulus_reduction << ", "
          << record.back_stress << ", "
          << record.degraded_yield_strength << ")";

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
          << "predicted_range, max_height, flight_time, air_resistance_factor, "
          << "max_mach, compressibility_correction, impact_velocity, "
          << "impact_mach, temperature_k) VALUES ("
          << "'" << escapeString(record.machine_id) << "', "
          << record.projectile_mass << ", "
          << record.launch_angle << ", "
          << record.release_velocity << ", "
          << record.predicted_range << ", "
          << record.max_height << ", "
          << record.flight_time << ", "
          << record.air_resistance_factor << ", "
          << record.max_mach << ", "
          << record.compressibility_correction << ", "
          << record.impact_velocity << ", "
          << record.impact_mach << ", "
          << record.temperature_k << ")";

    std::string response = httpPost(
        impl_->config.host,
        impl_->config.port,
        "/?database=" + impl_->config.database + "&user=" + impl_->config.username,
        query.str()
    );
    return response.find("HTTP/1.1 200") != std::string::npos ||
           response.find("Ok.") != std::string::npos;
}

bool ClickHouseClient::insertCyclicFatigueLog(
    const std::string& machine_id,
    int64_t cycle_count,
    double plastic_strain_amplitude,
    double accumulated_plastic_strain,
    double damaged_shear_modulus,
    double damaged_yield_strength,
    double damage_parameter,
    int64_t remaining_life_cycles
) {
    std::ostringstream query;
    query << "INSERT INTO cyclic_fatigue_log ("
          << "machine_id, cycle_count, plastic_strain_amplitude, "
          << "accumulated_plastic_strain, damaged_shear_modulus, "
          << "damaged_yield_strength, damage_parameter, remaining_life_cycles) "
          << "VALUES ("
          << "'" << escapeString(machine_id) << "', "
          << cycle_count << ", "
          << plastic_strain_amplitude << ", "
          << accumulated_plastic_strain << ", "
          << damaged_shear_modulus << ", "
          << damaged_yield_strength << ", "
          << damage_parameter << ", "
          << remaining_life_cycles << ")";

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
          << "efficiency, projectile_mass, launch_angle, spring_status, risk_level, "
          << "shear_stress, elastic_stress, plastic_strain, cycle_count, "
          << "cyclic_damage_ratio, modulus_reduction, max_mach, "
          << "compressibility_correction, fatigue_risk "
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
        int fatigue_int = 0;
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
        ls >> r.shear_stress; ls.ignore();
        ls >> r.elastic_stress; ls.ignore();
        ls >> r.plastic_strain; ls.ignore();
        ls >> r.cycle_count; ls.ignore();
        ls >> r.cyclic_damage_ratio; ls.ignore();
        ls >> r.modulus_reduction; ls.ignore();
        ls >> r.max_mach; ls.ignore();
        ls >> r.compressibility_correction; ls.ignore();
        ls >> fatigue_int;
        r.fatigue_risk = static_cast<uint8_t>(fatigue_int);
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
          << "predicted_range, threshold_value, cyclic_damage_ratio, "
          << "cycle_count, max_mach "
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
        ls >> r.threshold_value; ls.ignore();
        ls >> r.cyclic_damage_ratio; ls.ignore();
        ls >> r.cycle_count; ls.ignore();
        ls >> r.max_mach;
        results.push_back(r);
    }
    return results;
}

std::vector<ClickHouseClient::MachineStatus> ClickHouseClient::queryAllMachineStatus() {
    std::vector<MachineStatus> results;
    std::ostringstream query;
    query << "SELECT machine_id, toString(last_report_time), last_torsion_angle, "
          << "last_stored_energy, last_release_velocity, last_actual_range, "
          << "last_predicted_range, current_risk_level, total_cycles, "
          << "current_damage_ratio, last_max_mach, unacknowledged_alerts "
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
        ls >> r.total_cycles; ls.ignore();
        ls >> r.current_damage_ratio; ls.ignore();
        ls >> r.last_max_mach; ls.ignore();
        ls >> r.unacknowledged_alerts;
        results.push_back(r);
    }
    return results;
}

}
}

#ifndef CLICKHOUSE_CLIENT_H
#define CLICKHOUSE_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace trebuchet {
namespace storage {

struct SensorRecord {
    std::string machine_id;
    std::chrono::system_clock::time_point timestamp;
    double torsion_angle;
    double stored_energy;
    double release_velocity;
    double actual_range;
    double predicted_range;
    double efficiency;
    double projectile_mass;
    double launch_angle;
    std::string spring_status;
    std::string risk_level;
};

struct AlertRecord {
    std::string machine_id;
    std::chrono::system_clock::time_point timestamp;
    std::string alert_type;
    std::string alert_level;
    std::string message;
    double torsion_angle;
    double stored_energy;
    double actual_range;
    double predicted_range;
    double threshold_value;
};

struct RangePredictionRecord {
    std::string machine_id;
    double projectile_mass;
    double launch_angle;
    double release_velocity;
    double predicted_range;
    double max_height;
    double flight_time;
    double air_resistance_factor;
};

struct SpringEnergyRecord {
    std::string machine_id;
    double torsion_angle;
    double stored_energy;
    double shear_stress;
    double spring_constant;
    double efficiency;
    double yield_strength_ratio;
};

class ClickHouseClient {
public:
    struct Config {
        std::string host = "127.0.0.1";
        int port = 8123;
        std::string database = "trebuchet_sim";
        std::string username = "default";
        std::string password = "";
    };

    ClickHouseClient(const Config& config);
    ~ClickHouseClient();

    bool connect();
    bool isConnected() const;

    bool insertSensorData(const SensorRecord& record);
    bool insertSensorDataBatch(const std::vector<SensorRecord>& records);

    bool insertAlert(const AlertRecord& record);
    bool insertSpringEnergy(const SpringEnergyRecord& record);
    bool insertRangePrediction(const RangePredictionRecord& record);

    std::vector<SensorRecord> queryRecentSensorData(
        const std::string& machine_id,
        int limit = 100
    );

    std::vector<AlertRecord> queryRecentAlerts(
        const std::string& machine_id = "",
        int limit = 50
    );

    struct MachineStatus {
        std::string machine_id;
        std::chrono::system_clock::time_point last_report_time;
        double last_torsion_angle;
        double last_stored_energy;
        double last_release_velocity;
        double last_actual_range;
        double last_predicted_range;
        std::string current_risk_level;
        int unacknowledged_alerts;
    };

    std::vector<MachineStatus> queryAllMachineStatus();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string formatTimestamp(const std::chrono::system_clock::time_point& tp);
};

}
}

#endif

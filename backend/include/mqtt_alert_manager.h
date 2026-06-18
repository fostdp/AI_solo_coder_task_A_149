#ifndef MQTT_ALERT_MANAGER_H
#define MQTT_ALERT_MANAGER_H

#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <queue>

namespace trebuchet {
namespace alert {

struct MqttConfig {
    std::string broker_host = "127.0.0.1";
    int broker_port = 1883;
    std::string client_id = "trebuchet_backend";
    std::string username = "";
    std::string password = "";
    std::string topic_prefix = "trebuchet/alerts";
    int keepalive = 60;
};

enum class AlertLevel {
    INFO,
    WARNING,
    CRITICAL,
    FATAL
};

enum class AlertType {
    SPRING_FRACTURE_RISK,
    INSUFFICIENT_RANGE,
    EFFICIENCY_LOW,
    SENSOR_TIMEOUT,
    SYSTEM_ERROR,
    CYCLIC_FATIGUE_RISK
};

struct AlertMessage {
    std::string machine_id;
    std::chrono::system_clock::time_point timestamp;
    AlertType type;
    AlertLevel level;
    std::string message;
    double torsion_angle = 0.0;
    double stored_energy = 0.0;
    double actual_range = 0.0;
    double predicted_range = 0.0;
    double threshold_value = 0.0;
};

using AlertCallback = std::function<void(const AlertMessage&)>;

class MqttAlertManager {
public:
    MqttAlertManager(const MqttConfig& config);
    ~MqttAlertManager();

    bool connect();
    void disconnect();
    bool isConnected() const;

    bool publishAlert(const AlertMessage& alert);
    bool publishSpringFractureWarning(
        const std::string& machine_id,
        double torsion_angle,
        double yield_strength_ratio,
        double shear_stress
    );
    bool publishInsufficientRangeWarning(
        const std::string& machine_id,
        double actual_range,
        double predicted_range,
        double minimum_required
    );
    bool publishEfficiencyWarning(
        const std::string& machine_id,
        double current_efficiency,
        double expected_efficiency
    );
    bool publishCyclicFatigueWarning(
        const std::string& machine_id,
        int64_t cycle_count,
        double cyclic_damage_ratio,
        double plastic_strain,
        int64_t remaining_life_cycles
    );

    void setAlertCallback(AlertCallback callback);

    void processQueuedAlerts();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string alertTypeToString(AlertType type);
    std::string alertLevelToString(AlertLevel level);
    std::string buildTopic(const AlertMessage& alert);
    std::string buildPayload(const AlertMessage& alert);

    std::mutex queue_mutex_;
    std::queue<AlertMessage> alert_queue_;
    AlertCallback alert_callback_;
};

}
}

#endif

#include "alarm_mqtt_module.h"

#include <iostream>
#include <chrono>
#include <sstream>

namespace trebuchet {
namespace modules {

static alert::AlertLevel mapRiskToAlertLevel(bus::RiskLevel lvl) {
    using A = alert::AlertLevel;
    switch (lvl) {
        case bus::RiskLevel::INFO: return A::INFO;
        case bus::RiskLevel::WARNING: return A::WARNING;
        case bus::RiskLevel::CRITICAL: return A::CRITICAL;
        case bus::RiskLevel::FATAL: return A::FATAL;
    }
    return A::WARNING;
}

static alert::AlertType mapAlertKindToType(bus::AlertKind k) {
    using T = alert::AlertType;
    switch (k) {
        case bus::AlertKind::SPRING_FRACTURE: return T::SPRING_FRACTURE_RISK;
        case bus::AlertKind::CYCLIC_FATIGUE: return T::CYCLIC_FATIGUE_RISK;
        case bus::AlertKind::INSUFFICIENT_RANGE: return T::INSUFFICIENT_RANGE;
        case bus::AlertKind::EFFICIENCY_LOW: return T::EFFICIENCY_LOW;
        case bus::AlertKind::SENSOR_TIMEOUT: return T::SENSOR_TIMEOUT;
        case bus::AlertKind::SYSTEM_ERROR: return T::SYSTEM_ERROR;
    }
    return T::SYSTEM_ERROR;
}

struct AlarmMqttModule::Impl { bool dummy = true; };

size_t AlarmMqttModule::DedupKeyHash::operator()(const DedupKey& k) const {
    size_t h = std::hash<std::string>{}(k.machine_id);
    h ^= std::hash<int>{}(static_cast<int>(k.kind)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int64_t>{}(k.window_start) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

AlarmMqttModule::AlarmMqttModule(const Config& config, bus::MessageBus* bus)
    : impl_(std::make_unique<Impl>()), config_(config), bus_(bus) {}

AlarmMqttModule::~AlarmMqttModule() { stop(); }

void AlarmMqttModule::setPublishedHandler(OnPublishedHandler h) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    on_published_ = std::move(h);
}

bool AlarmMqttModule::start() {
    alert::MqttConfig mqtt_cfg;
    mqtt_cfg.broker_host = config_.mqtt_host;
    mqtt_cfg.broker_port = config_.mqtt_port;
    mqtt_cfg.client_id = config_.mqtt_client_id;
    mqtt_cfg.username = config_.mqtt_username;
    mqtt_cfg.password = config_.mqtt_password;
    mqtt_manager_ = std::make_unique<alert::MqttAlertManager>(mqtt_cfg);
    connected_ = mqtt_manager_->connect();
    running_ = true;
    worker_thread_ = std::thread(&AlarmMqttModule::workerLoop, this);
    return connected_;
}

void AlarmMqttModule::stop() {
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
    if (mqtt_manager_) mqtt_manager_->disconnect();
    connected_ = false;
}

bool AlarmMqttModule::isRunning() const { return running_.load(); }
bool AlarmMqttModule::isConnected() const { return connected_.load(); }

bool AlarmMqttModule::publishAlertNow(const bus::AlertTriggerMessage& alert) {
    return processAndEmitAlert(alert);
}

bool AlarmMqttModule::isDuplicate(const bus::AlertTriggerMessage& alert) {
    const int64_t window = config_.dedup_window_seconds * 1000;
    DedupKey key;
    key.machine_id = alert.getMachineId();
    key.kind = alert.kind;
    key.window_start = alert.timestamp_ms / window;
    std::lock_guard<std::mutex> lock(dedup_mutex_);
    auto it = dedup_counter_.find(key);
    if (it != dedup_counter_.end()) {
        it->second++;
        return true;
    }
    dedup_counter_[key] = 1;
    static const int64_t CLEAN_AFTER = 86400000;
    for (auto it2 = dedup_counter_.begin(); it2 != dedup_counter_.end();) {
        if (std::abs(alert.timestamp_ms - it2->first.window_start * window) > CLEAN_AFTER) {
            it2 = dedup_counter_.erase(it2);
        } else {
            ++it2;
        }
    }
    return false;
}

bool AlarmMqttModule::processAndEmitAlert(const bus::AlertTriggerMessage& alert) {
    if (isDuplicate(alert)) {
        deduped_++;
        return false;
    }
    received_++;
    bool ok = publishToMqtt(alert);
    if (ok) published_ok_++;
    else published_fail_++;

    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        if (on_published_) on_published_(alert, ok);
    }

    if (bus_) {
        bus_->push(bus::QueueChannel::ALARM_TO_STORAGE, alert);
    }
    return ok;
}

static std::string riskLevelStr(bus::RiskLevel lv) {
    switch (lv) {
        case bus::RiskLevel::FATAL: return "critical";
        case bus::RiskLevel::CRITICAL: return "critical";
        case bus::RiskLevel::WARNING: return "warning";
        default: return "info";
    }
}

static std::string alertKindTopic(bus::AlertKind k) {
    switch (k) {
        case bus::AlertKind::SPRING_FRACTURE: return "spring_fracture_risk";
        case bus::AlertKind::CYCLIC_FATIGUE: return "cyclic_fatigue_risk";
        case bus::AlertKind::INSUFFICIENT_RANGE: return "insufficient_range";
        case bus::AlertKind::EFFICIENCY_LOW: return "low_efficiency";
        case bus::AlertKind::SENSOR_TIMEOUT: return "sensor_timeout";
        default: return "system_error";
    }
}

bool AlarmMqttModule::publishToMqtt(const bus::AlertTriggerMessage& alert) {
    if (!mqtt_manager_) return false;
    bool needs_reconnect = !connected_;
    if (needs_reconnect && config_.auto_reconnect) {
        connected_ = mqtt_manager_->connect();
    }
    if (!connected_) return false;

    const std::string mid = alert.getMachineId();
    const std::string msg = alert.getMessage();
    const std::string topic_suffix = alertKindTopic(alert.kind);

    bool ok = false;
    switch (alert.kind) {
        case bus::AlertKind::SPRING_FRACTURE:
            ok = mqtt_manager_->publishSpringFractureWarning(
                mid, alert.torsion_angle_rad, alert.threshold_value,
                alert.stored_energy_j / std::max(0.01, alert.stored_energy_j));
            break;
        case bus::AlertKind::CYCLIC_FATIGUE:
            ok = mqtt_manager_->publishCyclicFatigueWarning(
                mid, alert.cycle_count, alert.cyclic_damage_ratio,
                alert.cyclic_damage_ratio * alert.threshold_value > 0.01 ? alert.cyclic_damage_ratio : 0.001,
                alert.remaining_life_cycles);
            break;
        case bus::AlertKind::INSUFFICIENT_RANGE:
            ok = mqtt_manager_->publishInsufficientRangeWarning(
                mid, alert.actual_range_m, alert.predicted_range_m,
                alert.threshold_value);
            break;
        default: {
            alert::AlertMessage m;
            m.machine_id = mid;
            m.timestamp = std::chrono::system_clock::now();
            m.type = mapAlertKindToType(alert.kind);
            m.level = mapRiskToAlertLevel(alert.level);
            m.message = alert.getMessage();
            m.threshold_value = alert.threshold_value;
            ok = mqtt_manager_->publishAlert(m);
        }
    }
    return ok;
}

bool AlarmMqttModule::consumeSpringAlerts() {
    bus::SpringResultMessage s;
    if (!bus_) return false;
    if (!bus_->pop(bus::QueueChannel::SPRING_TO_ALARM, s)) return false;

    const std::string mid = s.getMachineId();
    if (s.fracture_risk_flag) {
        bus::AlertTriggerMessage a;
        a.setMachineId(mid);
        a.timestamp_ms = s.timestamp_ms;
        a.kind = bus::AlertKind::SPRING_FRACTURE;
        a.level = s.risk;
        std::ostringstream oss;
        oss << "弹簧断裂风险: 屈服强度比=" << s.yield_strength_ratio
            << " 剪应力=" << (s.shear_stress_pa * 1e-6) << "MPa";
        a.setMessage(oss.str());
        a.torsion_angle_rad = s.torsion_angle_rad;
        a.stored_energy_j = s.stored_energy_j;
        a.cycle_count = s.cycle_count;
        a.cyclic_damage_ratio = s.cyclic_damage_ratio;
        processAndEmitAlert(a);
    }
    if (s.fatigue_risk_flag) {
        bus::AlertTriggerMessage a;
        a.setMachineId(mid);
        a.timestamp_ms = s.timestamp_ms;
        a.kind = bus::AlertKind::CYCLIC_FATIGUE;
        a.level = s.risk;
        a.cycle_count = s.cycle_count;
        a.cyclic_damage_ratio = s.cyclic_damage_ratio;
        std::ostringstream oss_fat;
        oss_fat << "循环疲劳: 损伤比=" << s.cyclic_damage_ratio
                << " 循环次数=" << s.cycle_count
                << " 塑性应变=" << s.plastic_strain;
        a.setMessage(oss_fat.str());
        processAndEmitAlert(a);
    }
    return true;
}

bool AlarmMqttModule::consumeRangeAlerts() {
    bus::RangeResultMessage r;
    if (!bus_) return false;
    if (!bus_->pop(bus::QueueChannel::RANGE_TO_ALARM, r)) return false;
    if (r.insufficient_range_flag) {
        bus::AlertTriggerMessage a;
        a.setMachineId(r.getMachineId());
        a.timestamp_ms = r.timestamp_ms;
        a.kind = bus::AlertKind::INSUFFICIENT_RANGE;
        a.level = bus::RiskLevel::WARNING;
        a.actual_range_m = r.actual_range_m;
        a.predicted_range_m = r.predicted_range_m;
        a.max_mach = r.max_mach;
        a.threshold_value = config_.insufficient_range_factor;
        std::ostringstream oss;
        oss << "射程不足: 实际=" << r.actual_range_m << " 预测=" << r.predicted_range_m;
        a.setMessage(oss.str());
        processAndEmitAlert(a);
    }
    return true;
}

void AlarmMqttModule::workerLoop() {
    while (running_) {
        bool work = consumeSpringAlerts() || consumeRangeAlerts();
        if (!work) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(config_.worker_loop_idle_us));
        }
    }
}

}
}

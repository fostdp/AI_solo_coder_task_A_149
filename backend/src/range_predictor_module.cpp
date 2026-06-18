#include "range_predictor_module.h"
#include "logger.h"
#include "metrics_collector.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace trebuchet {
namespace modules {

struct RangePredictorModule::Impl { bool dummy = true; };

RangePredictorModule::RangePredictorModule(
    const Config& config,
    const physics::ProjectileConfig& projectile_config,
    bus::MessageBus* bus)
    : impl_(std::make_unique<Impl>()),
      config_(config),
      projectile_config_(projectile_config),
      bus_(bus) {}

RangePredictorModule::~RangePredictorModule() { stop(); }

void RangePredictorModule::setProjectileConfig(const physics::ProjectileConfig& c) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    projectile_config_ = c;
}

physics::ProjectileConfig RangePredictorModule::getProjectileConfig() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return projectile_config_;
}

bool RangePredictorModule::start() {
    running_ = true;
    worker_thread_ = std::thread(&RangePredictorModule::workerLoop, this);
    return true;
}

void RangePredictorModule::stop() {
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
}

bool RangePredictorModule::isRunning() const { return running_.load(); }

RangePredictorModule::MachineStats
RangePredictorModule::getMachineStats(const std::string& machine_id) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto it = per_machine_stats_.find(machine_id);
    if (it != per_machine_stats_.end()) return it->second;
    return MachineStats{};
}

bool RangePredictorModule::loadTrajectoryParamsFromJson(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f.good()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();

    auto findNum = [&](const std::string& key, double def) -> double {
        size_t k = s.find("\"" + key + "\"");
        if (k == std::string::npos) return def;
        size_t c = s.find(':', k);
        if (c == std::string::npos) return def;
        while (c + 1 < s.size() && (s[c+1] == ' ' || s[c+1] == '\t')) c++;
        try { return std::stod(s.substr(c + 1, 20)); }
        catch (...) { return def; }
    };

    physics::ProjectileConfig cfg = projectile_config_;
    cfg.mass = findNum("massKg", cfg.mass);
    cfg.diameter = findNum("diameterM", cfg.diameter);
    cfg.cross_section_area = M_PI * (cfg.diameter / 2.0) * (cfg.diameter / 2.0);
    cfg.drag_coefficient_incompressible = findNum("dragCoefficientIncompressible",
                                                  cfg.drag_coefficient_incompressible);
    setProjectileConfig(cfg);
    return true;
}

void RangePredictorModule::workerLoop() {
    while (running_) {
        bus::SpringResultMessage spring;
        bool got = bus_ && bus_->pop(bus::QueueChannel::SPRING_TO_RANGE, spring);
        if (got) {
            consumeSpringResult(spring);
        } else {
            std::this_thread::sleep_for(
                std::chrono::microseconds(config_.worker_loop_idle_us));
        }
    }
}

bool RangePredictorModule::consumeSpringResult(bus::SpringResultMessage& spring) {
    const std::string mid = spring.getMachineId();
    if (mid.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (per_machine_stats_.find(mid) == per_machine_stats_.end()) {
            per_machine_stats_[mid] = MachineStats{};
        }
    }

    double projectile_mass_kg = 10.0;
    double launch_angle_deg = 45.0;
    double release_velocity = 0.0;
    double actual_range_m = 0.0;
    int64_t timestamp_ms = spring.timestamp_ms;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        MachineStats& ms = per_machine_stats_[mid];
        projectile_mass_kg = projectile_config_.mass;
        launch_angle_deg = 45.0;
        release_velocity = std::sqrt(2.0 * spring.stored_energy_j * spring.efficiency / std::max(0.0001, projectile_mass_kg));
        actual_range_m = ms.last_actual_m;
    }

    bus::RangeResultMessage range;
    bool ok = computeTrajectory(spring, projectile_mass_kg, launch_angle_deg,
                                 release_velocity, actual_range_m, mid, timestamp_ms, range);
    if (!ok) return false;
    range.setMachineId(mid);

    detectRangeDeficits(range, mid, timestamp_ms);
    dispatchToStorageAndAlarm(range);

    predictions_++;
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        MachineStats& ms = per_machine_stats_[mid];
        ms.last_timestamp_ms = timestamp_ms;
        ms.last_predicted_m = range.predicted_range_m;
        ms.last_actual_m = actual_range_m;
        ms.last_max_mach = range.max_mach;
        ms.last_impact_mach = range.impact_mach;
        ms.total_predictions++;
    }
    return true;
}

bool RangePredictorModule::computeTrajectory(const bus::SpringResultMessage& spring,
                                              double projectile_mass_kg,
                                              double launch_angle_deg,
                                              double release_velocity,
                                              double actual_range_m,
                                              const std::string& machine_id,
                                              int64_t,
                                              bus::RangeResultMessage& out) {
    auto t0 = std::chrono::steady_clock::now();
    physics::ProjectileConfig local_cfg = projectile_config_;
    local_cfg.mass = projectile_mass_kg;

    physics::RangePredictionResult pred = physics::predictTrajectoryRange(
        local_cfg, release_velocity, launch_angle_deg, 1.0, 288.15);
    double optimal = physics::findOptimalLaunchAngle(
        local_cfg, release_velocity, 10, 80);

    out.projectile_mass_kg = projectile_mass_kg;
    out.launch_angle_deg = launch_angle_deg;
    out.release_velocity = release_velocity;
    out.predicted_range_m = pred.predicted_range;
    out.max_height_m = pred.max_height;
    out.flight_time_s = pred.flight_time;
    out.air_resistance_factor = pred.air_resistance_factor;
    out.max_mach = pred.max_mach;
    out.compressibility_correction = pred.compressibility_correction;
    out.impact_velocity = pred.impact_velocity;
    out.impact_mach = pred.impact_mach;
    out.optimal_launch_angle_deg = pred.optimal_launch_angle_deg > 0.0
                                      ? pred.optimal_launch_angle_deg : optimal;
    out.actual_range_m = actual_range_m;
    out.insufficient_range_flag = actual_range_m > 0 &&
        actual_range_m < out.predicted_range_m * config_.insufficient_range_factor ? 1 : 0;
    out.temperature_k = pred.temperature_k > 250 ? pred.temperature_k : 288.15;

    MetricLabels labels;
    labels.machine_id = machine_id;
    labels.material = "";
    labels.active_coils = "";

    MetricsCollector::instance().incrementPredictionsMade();
    MetricsCollector::instance().setRangePredicted(pred.predicted_range, labels);
    MetricsCollector::instance().setRangeActual(actual_range_m, labels);
    MetricsCollector::instance().setMachNumber(pred.max_mach, labels);
    MetricsCollector::instance().setOptimalLaunchAngle(
        out.optimal_launch_angle_deg, labels);

    auto t1 = std::chrono::steady_clock::now();
    double latency = std::chrono::duration<double>(t1 - t0).count();
    MetricsCollector::instance().observeLatency("range_predictor.compute", latency);

    if (out.optimal_launch_angle_deg > 0.0 &&
        std::abs(out.optimal_launch_angle_deg - launch_angle_deg) > 5.0) {
        LOG_INFO("range_predictor",
                 "机器[{}] 最优角度={:.1f}°, 当前={:.1f}°, 预测射程={:.1f}m, 马赫={:.2f}",
                 machine_id, out.optimal_launch_angle_deg, launch_angle_deg,
                 pred.predicted_range, pred.max_mach);
    }

    if (predictions_ % 1000 == 0) {
        LOG_INFO("range_predictor", "已预测 {} 次, 告警 {} 次", predictions_.load(), alerts_.load());
    }

    return true;
}

bool RangePredictorModule::detectRangeDeficits(const bus::RangeResultMessage& range,
                                                const std::string& machine_id,
                                                int64_t timestamp_ms) {
    if (!range.insufficient_range_flag) return false;
    bus::AlertTriggerMessage a;
    a.setMachineId(machine_id);
    a.timestamp_ms = timestamp_ms;
    a.kind = bus::AlertKind::INSUFFICIENT_RANGE;
    a.level = bus::RiskLevel::WARNING;
    std::ostringstream oss;
    oss << "实际射程 " << range.actual_range_m << " m，预测 "
        << range.predicted_range_m << " m，低于预测值 "
        << std::fixed << (100.0 * config_.insufficient_range_factor) << "%";
    a.setMessage(oss.str());
    a.actual_range_m = range.actual_range_m;
    a.predicted_range_m = range.predicted_range_m;
    a.threshold_value = config_.insufficient_range_factor;
    a.max_mach = range.max_mach;
    if (bus_ && bus_->push(bus::QueueChannel::RANGE_TO_ALARM, a)) {
        alerts_++;
        MetricsCollector::instance().incrementAlertsEmitted(
            "insufficient_range", "warning");
        LOG_WARN("range_predictor",
                 "射程不足告警: machine_id={}, actual={:.1f}m, predicted={:.1f}m, factor={:.2f}",
                 machine_id, range.actual_range_m, range.predicted_range_m,
                 config_.insufficient_range_factor);
        return true;
    }
    return false;
}

bool RangePredictorModule::dispatchToStorageAndAlarm(const bus::RangeResultMessage& range) {
    bool ok = bus_->push(bus::QueueChannel::RANGE_TO_STORAGE, range);
    if (ok) storage_++;
    return ok;
}

}
}

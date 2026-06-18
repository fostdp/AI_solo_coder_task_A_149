#ifndef TREBUCHET_RANGE_PREDICTOR_MODULE_H
#define TREBUCHET_RANGE_PREDICTOR_MODULE_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "trebuchet_physics.h"
#include "message_bus.h"

namespace trebuchet {
namespace modules {

class RangePredictorModule {
public:
    struct Config {
        int worker_loop_idle_us = 50;
        double insufficient_range_factor = 0.85;
        double optimal_range_percent = 0.85;
        int64_t emit_metrics_every_n = 1;
    };

    struct MachineStats {
        int64_t last_timestamp_ms = 0;
        double last_predicted_m = 0.0;
        double last_actual_m = 0.0;
        double last_max_mach = 0.0;
        double last_impact_mach = 0.0;
        int64_t total_predictions = 0;
    };

    RangePredictorModule(const Config& config,
                         const physics::ProjectileConfig& projectile_config,
                         bus::MessageBus* bus);
    ~RangePredictorModule();

    bool start();
    void stop();
    bool isRunning() const;

    bool loadTrajectoryParamsFromJson(const std::string& json_path);
    void setProjectileConfig(const physics::ProjectileConfig& config);
    physics::ProjectileConfig getProjectileConfig() const;

    uint64_t predictionsMade() const { return predictions_.load(); }
    uint64_t alertsEmitted() const { return alerts_.load(); }
    uint64_t storageDispatches() const { return storage_.load(); }

    MachineStats getMachineStats(const std::string& machine_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void workerLoop();
    bool consumeSpringResult(bus::SpringResultMessage& spring_in);
    bool computeTrajectory(const bus::SpringResultMessage& spring,
                           double projectile_mass_kg,
                           double launch_angle_deg,
                           double release_velocity,
                           double actual_range_m,
                           const std::string& machine_id,
                           int64_t timestamp_ms,
                           bus::RangeResultMessage& out_range);
    bool detectRangeDeficits(const bus::RangeResultMessage& range,
                             const std::string& machine_id,
                             int64_t timestamp_ms);
    bool dispatchToStorageAndAlarm(const bus::RangeResultMessage& range);

    Config config_;
    physics::ProjectileConfig projectile_config_;
    bus::MessageBus* bus_;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    mutable std::mutex stats_mutex_;
    std::unordered_map<std::string, MachineStats> per_machine_stats_;

    std::atomic<uint64_t> predictions_{0};
    std::atomic<uint64_t> alerts_{0};
    std::atomic<uint64_t> storage_{0};
};

}
}

#endif

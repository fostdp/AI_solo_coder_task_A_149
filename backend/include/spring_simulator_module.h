#ifndef TREBUCHET_SPRING_SIMULATOR_MODULE_H
#define TREBUCHET_SPRING_SIMULATOR_MODULE_H

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

class SpringSimulatorModule {
public:
    struct Config {
        int worker_loop_idle_us = 50;
        double fracture_risk_critical_ratio = 0.85;
        double fracture_risk_warning_ratio = 0.70;
        double fatigue_damage_warning = 0.50;
        double fatigue_damage_critical = 0.80;
        int64_t emit_status_every_n_cycles = 1;
    };

    struct MachineCyclicState {
        physics::CyclicSofteningState cyclic;
        int64_t last_timestamp_ms = 0;
        double last_torsion_angle = 0.0;
        int64_t emission_counter = 0;
    };

    SpringSimulatorModule(const Config& config,
                          const physics::TorsionSpringConfig& physics_config,
                          bus::MessageBus* bus);
    ~SpringSimulatorModule();

    bool start();
    void stop();
    bool isRunning() const;

    bool loadSpringParamsFromJson(const std::string& json_path);
    void setSpringConfig(const physics::TorsionSpringConfig& config);
    physics::TorsionSpringConfig getSpringConfig() const;

    uint64_t messagesProcessed() const { return processed_.load(); }
    uint64_t alertsEmitted() const { return alerts_.load(); }
    uint64_t storageDispatches() const { return storage_.load(); }
    uint64_t rangeDispatches() const { return to_range_.load(); }

    MachineCyclicState getMachineState(const std::string& machine_id);
    void resetMachineState(const std::string& machine_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void workerLoop();
    bool consumeSensorMessage(bus::SensorRawMessage& raw_in);
    bool computeSpringEnergy(bus::SensorRawMessage& raw,
                             MachineCyclicState& state,
                             bus::SpringResultMessage& out_result);
    bool detectAndEmitAlerts(const std::string& machine_id,
                             const bus::SensorRawMessage& raw,
                             const bus::SpringResultMessage& result,
                             MachineCyclicState& state);
    bool dispatchResults(const bus::SpringResultMessage& result);

    Config config_;
    physics::TorsionSpringConfig physics_config_;
    bus::MessageBus* bus_;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    mutable std::mutex state_mutex_;
    std::unordered_map<std::string, MachineCyclicState> per_machine_states_;

    std::atomic<uint64_t> processed_{0};
    std::atomic<uint64_t> alerts_{0};
    std::atomic<uint64_t> storage_{0};
    std::atomic<uint64_t> to_range_{0};
};

}
}

#endif

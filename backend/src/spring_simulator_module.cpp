#include "spring_simulator_module.h"

#include <fstream>
#include <sstream>
#include <iostream>

#define M_PI 3.14159265358979323846

namespace trebuchet {
namespace modules {

struct SpringSimulatorModule::Impl { bool dummy = true; };

SpringSimulatorModule::SpringSimulatorModule(
    const Config& config,
    const physics::TorsionSpringConfig& physics_config,
    bus::MessageBus* bus)
    : impl_(std::make_unique<Impl>()),
      config_(config),
      physics_config_(physics_config),
      bus_(bus) {}

SpringSimulatorModule::~SpringSimulatorModule() { stop(); }

void SpringSimulatorModule::setSpringConfig(const physics::TorsionSpringConfig& c) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    physics_config_ = c;
}

physics::TorsionSpringConfig SpringSimulatorModule::getSpringConfig() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return physics_config_;
}

bool SpringSimulatorModule::start() {
    running_ = true;
    worker_thread_ = std::thread(&SpringSimulatorModule::workerLoop, this);
    return true;
}

void SpringSimulatorModule::stop() {
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
}

bool SpringSimulatorModule::isRunning() const { return running_.load(); }

SpringSimulatorModule::MachineCyclicState
SpringSimulatorModule::getMachineState(const std::string& machine_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = per_machine_states_.find(machine_id);
    if (it != per_machine_states_.end()) return it->second;
    MachineCyclicState cs;
    cs.cyclic = physics::initializeCyclicState(physics_config_.material);
    return cs;
}

void SpringSimulatorModule::resetMachineState(const std::string& machine_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = per_machine_states_.find(machine_id);
    if (it != per_machine_states_.end()) {
        it->second.cyclic = physics::initializeCyclicState(physics_config_.material);
        it->second.emission_counter = 0;
    }
}

bool SpringSimulatorModule::loadSpringParamsFromJson(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f.good()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();

    auto findStr = [&](const std::string& key, const std::string& def) -> std::string {
        size_t k = s.find("\"" + key + "\"");
        if (k == std::string::npos) return def;
        size_t c = s.find(':', k);
        if (c == std::string::npos) return def;
        size_t q1 = s.find('"', c);
        if (q1 == std::string::npos) return def;
        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) return def;
        return s.substr(q1 + 1, q2 - q1 - 1);
    };
    auto findNum = [&](const std::string& key, double def) -> double {
        size_t k = s.find("\"" + key + "\"");
        if (k == std::string::npos) return def;
        size_t c = s.find(':', k);
        if (c == std::string::npos) return def;
        while (c + 1 < s.size() && (s[c+1] == ' ' || s[c+1] == '\t')) c++;
        try {
            return std::stod(s.substr(c + 1, 20));
        } catch (...) { return def; }
    };

    physics::TorsionSpringConfig cfg = physics_config_;
    cfg.material.shear_modulus = findNum("shearModulusPa", cfg.material.shear_modulus);
    cfg.material.yield_strength = findNum("yieldStrengthPa", cfg.material.yield_strength);
    cfg.wire_diameter = findNum("wireDiameterMm", cfg.wire_diameter * 1000.0) / 1000.0;
    cfg.coil_mean_diameter = findNum("coilMeanDiameterMm", cfg.coil_mean_diameter * 1000.0) / 1000.0;
    cfg.active_coils = static_cast<int>(findNum("activeCoils", cfg.active_coils));
    cfg.material.fatigue_ductility_coefficient = findNum("fatigueDuctilityCoeff", cfg.material.fatigue_ductility_coefficient);
    cfg.material.fatigue_ductility_exponent = findNum("fatigueDuctilityExp", cfg.material.fatigue_ductility_exponent);
    cfg.material.cyclic_strength_coefficient = findNum("cyclicStrengthCoeffPa", cfg.material.cyclic_strength_coefficient);
    cfg.material.cyclic_strength_exponent = findNum("cyclicStrengthExp", cfg.material.cyclic_strength_exponent);
    setSpringConfig(cfg);
    return true;
}

void SpringSimulatorModule::workerLoop() {
    while (running_) {
        bus::SensorRawMessage raw;
        bool got = bus_ && bus_->pop(bus::QueueChannel::SENSOR_TO_SPRING, raw);
        if (got) {
            consumeSensorMessage(raw);
        } else {
            std::this_thread::sleep_for(
                std::chrono::microseconds(config_.worker_loop_idle_us));
        }
    }
}

bool SpringSimulatorModule::consumeSensorMessage(bus::SensorRawMessage& raw) {
    const std::string mid = raw.getMachineId();
    if (mid.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (per_machine_states_.find(mid) == per_machine_states_.end()) {
            MachineCyclicState state;
            state.cyclic = physics::initializeCyclicState(physics_config_.material);
            state.last_timestamp_ms = raw.timestamp_ms;
            state.last_torsion_angle = raw.torsion_angle_rad;
            per_machine_states_[mid] = std::move(state);
        }
    }

    MachineCyclicState* state_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ptr = &per_machine_states_[mid];
    }

    bus::SpringResultMessage result;
    bool ok = computeSpringEnergy(raw, *state_ptr, result);
    if (!ok) return false;

    result.setMachineId(mid);
    result.timestamp_ms = raw.timestamp_ms;
    processed_++;

    detectAndEmitAlerts(mid, raw, result, *state_ptr);
    dispatchResults(result);

    state_ptr->last_timestamp_ms = raw.timestamp_ms;
    state_ptr->last_torsion_angle = raw.torsion_angle_rad;
    state_ptr->emission_counter++;
    return true;
}

bool SpringSimulatorModule::computeSpringEnergy(bus::SensorRawMessage& raw,
                                                MachineCyclicState& state,
                                                bus::SpringResultMessage& out) {
    double shear_stress_pa = physics::calculateShearStress(physics_config_, raw.torsion_angle_rad);
    physics_config_.cyclic_state = state.cyclic;
    physics::updateCyclicSoftening(physics_config_, raw.torsion_angle_rad, shear_stress_pa);
    state.cyclic = physics_config_.cyclic_state;

    physics::SpringEnergyResult energy = physics::calculateSpringEnergy(
        physics_config_, raw.torsion_angle_rad);

    out.torsion_angle_rad = raw.torsion_angle_rad;
    out.stored_energy_j = energy.stored_energy;
    out.spring_constant = energy.spring_constant;
    out.shear_stress_pa = energy.shear_stress;
    out.elastic_stress_pa = energy.elastic_stress;
    out.plastic_strain = energy.plastic_strain;
    out.efficiency = energy.efficiency;
    out.yield_strength_ratio = energy.yield_strength_ratio;
    out.cyclic_damage_ratio = energy.cyclic_damage_ratio;
    out.modulus_reduction = energy.modulus_reduction;
    out.back_stress_pa = energy.back_stress_pa;
    out.degraded_yield_strength_pa = energy.degraded_yield_strength_pa;
    out.cycle_count = state.cyclic.cycle_count;
    out.fracture_risk_flag = energy.fracture_risk ? 1 : 0;
    out.fatigue_risk_flag = energy.fatigue_risk ? 1 : 0;
    if (energy.yield_strength_ratio > config_.fracture_risk_critical_ratio ||
        energy.cyclic_damage_ratio > config_.fatigue_damage_critical) {
        out.risk = bus::RiskLevel::CRITICAL;
    } else if (out.fracture_risk_flag || out.fatigue_risk_flag) {
        out.risk = bus::RiskLevel::WARNING;
    } else {
        out.risk = bus::RiskLevel::INFO;
    }
    return true;
}

bool SpringSimulatorModule::detectAndEmitAlerts(const std::string& machine_id,
                                                 const bus::SensorRawMessage& raw,
                                                 const bus::SpringResultMessage& result,
                                                 MachineCyclicState& state) {
    bool emitted = false;
    if (result.yield_strength_ratio > config_.fracture_risk_critical_ratio ||
        result.yield_strength_ratio > config_.fracture_risk_warning_ratio) {
        bus::AlertTriggerMessage a;
        a.setMachineId(machine_id);
        a.timestamp_ms = raw.timestamp_ms;
        a.kind = bus::AlertKind::SPRING_FRACTURE;
        a.level = result.yield_strength_ratio > config_.fracture_risk_critical_ratio
                    ? bus::RiskLevel::CRITICAL : bus::RiskLevel::WARNING;
        std::ostringstream oss;
        oss << "弹簧应力超过"
            << (a.level == bus::RiskLevel::CRITICAL ? "临界" : "预警")
            << "阈值：屈服强度比 " << std::fixed << result.yield_strength_ratio
            << "，剪应力 " << result.shear_stress_pa * 1e-6 << " MPa";
        a.setMessage(oss.str());
        a.torsion_angle_rad = raw.torsion_angle_rad;
        a.stored_energy_j = result.stored_energy_j;
        a.threshold_value = a.level == bus::RiskLevel::CRITICAL
                                ? config_.fracture_risk_critical_ratio
                                : config_.fracture_risk_warning_ratio;
        if (bus_ && bus_->push(bus::QueueChannel::SPRING_TO_ALARM, a)) {
            alerts_++;
            emitted = true;
        }
    }

    if (result.cyclic_damage_ratio > config_.fatigue_damage_critical ||
        result.cyclic_damage_ratio > config_.fatigue_damage_warning) {
        bus::AlertTriggerMessage a;
        a.setMachineId(machine_id);
        a.timestamp_ms = raw.timestamp_ms;
        a.kind = bus::AlertKind::CYCLIC_FATIGUE;
        a.level = result.cyclic_damage_ratio > config_.fatigue_damage_critical
                    ? bus::RiskLevel::CRITICAL : bus::RiskLevel::WARNING;
        int64_t remaining = static_cast<int64_t>(
            physics::calculateCoffinMansonLife(physics_config_.material,
                                               result.plastic_strain > 0 ? result.plastic_strain : 1e-6)
        );
        a.remaining_life_cycles = remaining;
        a.cycle_count = state.cyclic.cycle_count;
        a.cyclic_damage_ratio = result.cyclic_damage_ratio;
        std::ostringstream oss;
        oss << "循环疲劳损伤：累计损伤比 " << result.cyclic_damage_ratio
            << "，剩余寿命约 " << remaining << " 次";
        a.setMessage(oss.str());
        if (bus_ && bus_->push(bus::QueueChannel::SPRING_TO_ALARM, a)) {
            alerts_++;
            emitted = true;
        }
    }
    return emitted;
}

bool SpringSimulatorModule::dispatchResults(const bus::SpringResultMessage& result) {
    bool ok1 = bus_->push(bus::QueueChannel::SPRING_TO_RANGE, result);
    bool ok2 = bus_->push(bus::QueueChannel::SPRING_TO_STORAGE, result);
    if (ok1) to_range_++;
    if (ok2) storage_++;
    return ok1 && ok2;
}

}
}

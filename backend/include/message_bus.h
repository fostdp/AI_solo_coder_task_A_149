#ifndef TREBUCHET_MESSAGE_BUS_H
#define TREBUCHET_MESSAGE_BUS_H

#include <string>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <chrono>

#ifdef USE_BOOST_LOCKFREE
#include <boost/lockfree/queue.hpp>
#endif

namespace trebuchet {
namespace bus {

enum class MessageType : uint8_t {
    SENSOR_RAW = 1,
    SPRING_RESULT = 2,
    RANGE_RESULT = 3,
    ALERT_TRIGGER = 4,
    STORAGE_BATCH = 5,
    SHUTDOWN = 99
};

enum class AlertKind : uint8_t {
    SPRING_FRACTURE = 1,
    CYCLIC_FATIGUE = 2,
    INSUFFICIENT_RANGE = 3,
    EFFICIENCY_LOW = 4,
    SENSOR_TIMEOUT = 5,
    SYSTEM_ERROR = 6
};

enum class RiskLevel : uint8_t {
    INFO = 0,
    WARNING = 1,
    CRITICAL = 2,
    FATAL = 3
};

struct alignas(64) SensorRawMessage {
    static constexpr MessageType TYPE = MessageType::SENSOR_RAW;
    char machine_id[32] = {0};
    int64_t timestamp_ms = 0;
    double torsion_angle_rad = 0.0;
    double stored_energy_j = 0.0;
    double release_velocity = 0.0;
    double actual_range_m = 0.0;
    double projectile_mass_kg = 0.0;
    double launch_angle_deg = 0.0;
    char spring_status[16] = {0};
    double efficiency = 0.0;
    int64_t cycle_count = 0;
    double cyclic_damage_ratio = 0.0;
    double plastic_strain = 0.0;
    double max_mach = 0.0;
    uint8_t checksum = 0;

    void setMachineId(const std::string& id) {
        std::strncpy(machine_id, id.c_str(), sizeof(machine_id) - 1);
    }
    std::string getMachineId() const { return std::string(machine_id); }
    void setSpringStatus(const std::string& s) {
        std::strncpy(spring_status, s.c_str(), sizeof(spring_status) - 1);
    }
    std::string getSpringStatus() const { return std::string(spring_status); }
    uint8_t computeChecksum() const {
        uint8_t sum = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(this);
        for (size_t i = 0; i < offsetof(SensorRawMessage, checksum); ++i) {
            sum ^= p[i];
        }
        return sum;
    }
    bool validate() const {
        if (torsion_angle_rad < -100 || torsion_angle_rad > 100) return false;
        if (stored_energy_j < 0 || stored_energy_j > 1e9) return false;
        if (release_velocity < 0 || release_velocity > 2000) return false;
        if (actual_range_m < 0 || actual_range_m > 1e5) return false;
        if (projectile_mass_kg <= 0 || projectile_mass_kg > 1e4) return false;
        if (launch_angle_deg < -90 || launch_angle_deg > 180) return false;
        if (efficiency < 0 || efficiency > 1.5) return false;
        if (cycle_count < 0) return false;
        if (cyclic_damage_ratio < 0 || cyclic_damage_ratio > 2) return false;
        if (max_mach < 0 || max_mach > 10) return false;
        if (std::strlen(machine_id) == 0) return false;
        return computeChecksum() == checksum;
    }
    void finalize() { checksum = computeChecksum(); }
};

struct alignas(64) SpringResultMessage {
    static constexpr MessageType TYPE = MessageType::SPRING_RESULT;
    char machine_id[32] = {0};
    int64_t timestamp_ms = 0;
    int64_t cycle_count = 0;
    double torsion_angle_rad = 0.0;
    double stored_energy_j = 0.0;
    double spring_constant = 0.0;
    double shear_stress_pa = 0.0;
    double elastic_stress_pa = 0.0;
    double plastic_strain = 0.0;
    double efficiency = 0.0;
    double yield_strength_ratio = 0.0;
    double cyclic_damage_ratio = 0.0;
    double modulus_reduction = 0.0;
    double back_stress_pa = 0.0;
    double degraded_yield_strength_pa = 0.0;
    uint8_t fracture_risk_flag = 0;
    uint8_t fatigue_risk_flag = 0;
    RiskLevel risk = RiskLevel::INFO;

    void setMachineId(const std::string& id) {
        std::strncpy(machine_id, id.c_str(), sizeof(machine_id) - 1);
    }
    std::string getMachineId() const { return std::string(machine_id); }
};

struct alignas(64) RangeResultMessage {
    static constexpr MessageType TYPE = MessageType::RANGE_RESULT;
    char machine_id[32] = {0};
    int64_t timestamp_ms = 0;
    double projectile_mass_kg = 0.0;
    double launch_angle_deg = 0.0;
    double release_velocity = 0.0;
    double predicted_range_m = 0.0;
    double max_height_m = 0.0;
    double flight_time_s = 0.0;
    double air_resistance_factor = 1.0;
    double max_mach = 0.0;
    double compressibility_correction = 1.0;
    double impact_velocity = 0.0;
    double impact_mach = 0.0;
    double optimal_launch_angle_deg = 0.0;
    double actual_range_m = 0.0;
    uint8_t insufficient_range_flag = 0;
    double temperature_k = 288.15;

    void setMachineId(const std::string& id) {
        std::strncpy(machine_id, id.c_str(), sizeof(machine_id) - 1);
    }
    std::string getMachineId() const { return std::string(machine_id); }
};

struct alignas(64) AlertTriggerMessage {
    static constexpr MessageType TYPE = MessageType::ALERT_TRIGGER;
    char machine_id[32] = {0};
    int64_t timestamp_ms = 0;
    AlertKind kind = AlertKind::SYSTEM_ERROR;
    RiskLevel level = RiskLevel::WARNING;
    char message_text[256] = {0};
    double torsion_angle_rad = 0.0;
    double stored_energy_j = 0.0;
    double actual_range_m = 0.0;
    double predicted_range_m = 0.0;
    double threshold_value = 0.0;
    double cyclic_damage_ratio = 0.0;
    int64_t cycle_count = 0;
    double max_mach = 0.0;
    int64_t remaining_life_cycles = 0;

    void setMachineId(const std::string& id) {
        std::strncpy(machine_id, id.c_str(), sizeof(machine_id) - 1);
    }
    std::string getMachineId() const { return std::string(machine_id); }
    void setMessage(const std::string& s) {
        std::strncpy(message_text, s.c_str(), sizeof(message_text) - 1);
    }
    std::string getMessage() const { return std::string(message_text); }
};

enum class QueueChannel : uint8_t {
    SENSOR_TO_SPRING = 0,
    SPRING_TO_RANGE = 1,
    SPRING_TO_ALARM = 2,
    RANGE_TO_ALARM = 3,
    SPRING_TO_STORAGE = 4,
    RANGE_TO_STORAGE = 5,
    ALARM_TO_STORAGE = 6,
    MAX_CHANNELS = 7
};

#ifdef USE_BOOST_LOCKFREE

class MessageBus {
public:
    static constexpr size_t QUEUE_CAPACITY = 16384;

    MessageBus()
        : sensor_to_spring_(QUEUE_CAPACITY)
        , spring_to_range_(QUEUE_CAPACITY)
        , spring_to_alarm_(QUEUE_CAPACITY)
        , range_to_alarm_(QUEUE_CAPACITY)
        , spring_to_storage_(QUEUE_CAPACITY)
        , range_to_storage_(QUEUE_CAPACITY)
        , alarm_to_storage_(QUEUE_CAPACITY) {}

    template<typename T>
    bool push(QueueChannel channel, const T& msg) {
        T* copy = new T(msg);
        bool ok = false;
        switch (channel) {
            case QueueChannel::SENSOR_TO_SPRING:
                ok = sensor_to_spring_.push(reinterpret_cast<SensorRawMessage*>(copy));
                break;
            case QueueChannel::SPRING_TO_RANGE:
                ok = spring_to_range_.push(reinterpret_cast<SpringResultMessage*>(copy));
                break;
            case QueueChannel::SPRING_TO_ALARM:
                ok = spring_to_alarm_.push(reinterpret_cast<SpringResultMessage*>(copy));
                break;
            case QueueChannel::RANGE_TO_ALARM:
                ok = range_to_alarm_.push(reinterpret_cast<RangeResultMessage*>(copy));
                break;
            case QueueChannel::SPRING_TO_STORAGE:
                ok = spring_to_storage_.push(reinterpret_cast<SpringResultMessage*>(copy));
                break;
            case QueueChannel::RANGE_TO_STORAGE:
                ok = range_to_storage_.push(reinterpret_cast<RangeResultMessage*>(copy));
                break;
            case QueueChannel::ALARM_TO_STORAGE:
                ok = alarm_to_storage_.push(reinterpret_cast<AlertTriggerMessage*>(copy));
                break;
            default:
                ok = false;
                break;
        }
        if (!ok) delete copy;
        return ok;
    }

    template<typename T>
    bool pop(QueueChannel channel, T& out) {
        void* raw = nullptr;
        bool ok = false;
        switch (channel) {
            case QueueChannel::SENSOR_TO_SPRING: {
                SensorRawMessage* p = nullptr;
                ok = sensor_to_spring_.pop(p);
                raw = p;
                break;
            }
            case QueueChannel::SPRING_TO_RANGE: {
                SpringResultMessage* p = nullptr;
                ok = spring_to_range_.pop(p);
                raw = p;
                break;
            }
            case QueueChannel::SPRING_TO_ALARM: {
                SpringResultMessage* p = nullptr;
                ok = spring_to_alarm_.pop(p);
                raw = p;
                break;
            }
            case QueueChannel::RANGE_TO_ALARM: {
                RangeResultMessage* p = nullptr;
                ok = range_to_alarm_.pop(p);
                raw = p;
                break;
            }
            case QueueChannel::SPRING_TO_STORAGE: {
                SpringResultMessage* p = nullptr;
                ok = spring_to_storage_.pop(p);
                raw = p;
                break;
            }
            case QueueChannel::RANGE_TO_STORAGE: {
                RangeResultMessage* p = nullptr;
                ok = range_to_storage_.pop(p);
                raw = p;
                break;
            }
            case QueueChannel::ALARM_TO_STORAGE: {
                AlertTriggerMessage* p = nullptr;
                ok = alarm_to_storage_.pop(p);
                raw = p;
                break;
            }
            default:
                ok = false;
                break;
        }
        if (ok && raw) {
            T* typed = reinterpret_cast<T*>(raw);
            out = *typed;
            delete typed;
        }
        return ok;
    }

    size_t dropped() const { return dropped_.load(); }
    void incrementDropped(size_t n = 1) { dropped_ += n; }

private:
    boost::lockfree::queue<SensorRawMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> sensor_to_spring_;
    boost::lockfree::queue<SpringResultMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> spring_to_range_;
    boost::lockfree::queue<SpringResultMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> spring_to_alarm_;
    boost::lockfree::queue<RangeResultMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> range_to_alarm_;
    boost::lockfree::queue<SpringResultMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> spring_to_storage_;
    boost::lockfree::queue<RangeResultMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> range_to_storage_;
    boost::lockfree::queue<AlertTriggerMessage*,
        boost::lockfree::capacity<QUEUE_CAPACITY>> alarm_to_storage_;
    std::atomic<size_t> dropped_{0};
};

#else

class MessageBus {
public:
    MessageBus() = default;
    template<typename T>
    bool push(QueueChannel, const T&) { return true; }
    template<typename T>
    bool pop(QueueChannel, T&) { return false; }
    size_t dropped() const { return 0; }
};

#endif

inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}
}

#endif

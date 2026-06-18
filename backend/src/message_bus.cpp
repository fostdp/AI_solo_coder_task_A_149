#include "message_bus.h"

namespace trebuchet {
namespace bus {

const char* messageTypeName(MessageType t) {
    switch (t) {
        case MessageType::SENSOR_RAW: return "SensorRaw";
        case MessageType::SPRING_RESULT: return "SpringResult";
        case MessageType::RANGE_RESULT: return "RangeResult";
        case MessageType::ALERT_TRIGGER: return "AlertTrigger";
        case MessageType::STORAGE_BATCH: return "StorageBatch";
        case MessageType::SHUTDOWN: return "Shutdown";
        default: return "Unknown";
    }
}

const char* alertKindName(AlertKind k) {
    switch (k) {
        case AlertKind::SPRING_FRACTURE: return "spring_fracture";
        case AlertKind::CYCLIC_FATIGUE: return "cyclic_fatigue";
        case AlertKind::INSUFFICIENT_RANGE: return "insufficient_range";
        case AlertKind::EFFICIENCY_LOW: return "low_efficiency";
        case AlertKind::SENSOR_TIMEOUT: return "sensor_timeout";
        case AlertKind::SYSTEM_ERROR: return "system_error";
        default: return "unknown";
    }
}

const char* riskLevelName(RiskLevel lv) {
    switch (lv) {
        case RiskLevel::INFO: return "INFO";
        case RiskLevel::WARNING: return "WARNING";
        case RiskLevel::CRITICAL: return "CRITICAL";
        case RiskLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

}
}

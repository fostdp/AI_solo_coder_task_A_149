#ifndef TREBUCHET_ALARM_MQTT_MODULE_H
#define TREBUCHET_ALARM_MQTT_MODULE_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>

#include "mqtt_alert_manager.h"
#include "message_bus.h"

namespace trebuchet {
namespace modules {

class AlarmMqttModule {
public:
    struct Config {
        std::string mqtt_host = "127.0.0.1";
        int mqtt_port = 1883;
        std::string mqtt_client_id = "trebuchet_alarm";
        std::string mqtt_username = "";
        std::string mqtt_password = "";
        int mqtt_connect_timeout_ms = 5000;
        int worker_loop_idle_us = 500;
        bool persist_last_alarm = true;
        int dedup_window_seconds = 30;
        bool auto_reconnect = true;
        double insufficient_range_factor = 0.85;
    };

    using OnPublishedHandler = std::function<void(const bus::AlertTriggerMessage&, bool ok)>;

    AlarmMqttModule(const Config& config, bus::MessageBus* bus);
    ~AlarmMqttModule();

    bool start();
    void stop();
    bool isRunning() const;
    bool isConnected() const;

    bool publishAlertNow(const bus::AlertTriggerMessage& alert);

    void setPublishedHandler(OnPublishedHandler handler);

    uint64_t alertsReceived() const { return received_.load(); }
    uint64_t alertsPublishedOk() const { return published_ok_.load(); }
    uint64_t alertsPublishFailed() const { return published_fail_.load(); }
    uint64_t alertsDeduped() const { return deduped_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void workerLoop();
    bool consumeSpringAlerts();
    bool consumeRangeAlerts();
    bool processAndEmitAlert(const bus::AlertTriggerMessage& alert);
    bool isDuplicate(const bus::AlertTriggerMessage& alert);
    bool publishToMqtt(const bus::AlertTriggerMessage& alert);

    Config config_;
    bus::MessageBus* bus_;

    std::mutex dedup_mutex_;
    struct DedupKey {
        std::string machine_id;
        bus::AlertKind kind;
        int64_t window_start;
        bool operator==(const DedupKey& o) const {
            return machine_id == o.machine_id && kind == o.kind && window_start == o.window_start;
        }
    };
    struct DedupKeyHash {
        size_t operator()(const DedupKey& k) const;
    };
    std::unordered_map<DedupKey, int, DedupKeyHash> dedup_counter_;

    std::unique_ptr<alert::MqttAlertManager> mqtt_manager_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_thread_;

    std::mutex handler_mutex_;
    OnPublishedHandler on_published_;

    std::atomic<uint64_t> received_{0};
    std::atomic<uint64_t> published_ok_{0};
    std::atomic<uint64_t> published_fail_{0};
    std::atomic<uint64_t> deduped_{0};
};

}
}

#endif

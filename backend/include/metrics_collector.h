#ifndef TREBUCHET_METRICS_COLLECTOR_H
#define TREBUCHET_METRICS_COLLECTOR_H

#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace trebuchet {
namespace metrics {

struct MetricLabels {
    std::string machine_id;
    std::string material;
    int active_coils;
};

class MetricsCollector {
public:
    static MetricsCollector& instance();

    void init(int port = 8081, const std::string& bind_address = "0.0.0.0");
    void shutdown();
    bool isRunning() const;

    void incrementUdpPacketsReceived(bool valid);
    void incrementAlertsEmitted(const std::string& alert_type, const std::string& level);
    void incrementPredictionsMade();

    void setSpringStoredEnergy(double value);
    void setSpringEfficiency(double value);
    void setSpringModulusReduction(double value);
    void setRangePredicted(double value, const MetricLabels& labels);
    void setRangeActual(double value, const MetricLabels& labels);
    void setOptimalLaunchAngle(double value, const MetricLabels& labels);
    void setMachNumber(double value, const MetricLabels& labels);
    void setShearStress(double value, const MetricLabels& labels);
    void setCycleCount(int64_t value, const MetricLabels& labels);
    void setCyclicDamage(double value, const MetricLabels& labels);

    void observeLatency(const std::string& operation, double latency_seconds);

private:
    MetricsCollector();
    ~MetricsCollector();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::mutex metrics_mutex_;

    std::atomic<uint64_t> udp_packets_total_{0};
    std::atomic<uint64_t> udp_packets_invalid_{0};
    std::atomic<uint64_t> alerts_emitted_{0};
    std::atomic<uint64_t> predictions_made_{0};

    std::unordered_map<std::string, std::atomic<double>> spring_gauges_;
    std::unordered_map<std::string, std::atomic<double>> trajectory_gauges_;
};

}  // namespace metrics
}  // namespace trebuchet

#endif

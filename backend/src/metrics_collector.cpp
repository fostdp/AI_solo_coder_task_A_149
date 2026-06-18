#include "metrics_collector.h"

#ifdef USE_PROMETHEUS
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/family.h>
#endif

#include <iostream>

namespace trebuchet {
namespace metrics {

struct MetricsCollector::Impl {
#ifdef USE_PROMETHEUS
    std::unique_ptr<prometheus::Exposer> exposer;
    std::shared_ptr<prometheus::Registry> registry;

    prometheus::Family<prometheus::Counter>* udp_packets_family = nullptr;
    prometheus::Family<prometheus::Counter>* alerts_family = nullptr;
    prometheus::Family<prometheus::Counter>* predictions_family = nullptr;

    prometheus::Family<prometheus::Gauge>* spring_energy_family = nullptr;
    prometheus::Family<prometheus::Gauge>* spring_efficiency_family = nullptr;
    prometheus::Family<prometheus::Gauge>* spring_modulus_family = nullptr;

    prometheus::Family<prometheus::Gauge>* range_predicted_family = nullptr;
    prometheus::Family<prometheus::Gauge>* range_actual_family = nullptr;
    prometheus::Family<prometheus::Gauge>* optimal_angle_family = nullptr;
    prometheus::Family<prometheus::Gauge>* mach_number_family = nullptr;
    prometheus::Family<prometheus::Gauge>* shear_stress_family = nullptr;
    prometheus::Family<prometheus::Gauge>* cycle_count_family = nullptr;
    prometheus::Family<prometheus::Gauge>* cyclic_damage_family = nullptr;

    prometheus::Family<prometheus::Histogram>* latency_family = nullptr;

    prometheus::Counter* udp_valid_counter = nullptr;
    prometheus::Counter* udp_invalid_counter = nullptr;
    prometheus::Counter* predictions_counter = nullptr;

    prometheus::Gauge* spring_energy_gauge = nullptr;
    prometheus::Gauge* spring_efficiency_gauge = nullptr;
    prometheus::Gauge* spring_modulus_gauge = nullptr;
#endif

    bool initialized = false;
};

MetricsCollector& MetricsCollector::instance() {
    static MetricsCollector inst;
    return inst;
}

MetricsCollector::MetricsCollector() : impl_(std::make_unique<Impl>()) {}
MetricsCollector::~MetricsCollector() { shutdown(); }

void MetricsCollector::init(int port, const std::string& bind_address) {
    if (impl_->initialized) return;

#ifdef USE_PROMETHEUS
    try {
        impl_->registry = std::make_shared<prometheus::Registry>();

        impl_->udp_packets_family = &prometheus::BuildCounter()
            .Name("trebuchet_udp_packets_total")
            .Help("Total UDP packets received")
            .Register(*impl_->registry);
        impl_->udp_valid_counter = &impl_->udp_packets_family->Add({{"status", "valid"}});
        impl_->udp_invalid_counter = &impl_->udp_packets_family->Add({{"status", "invalid"}});

        impl_->alerts_family = &prometheus::BuildCounter()
            .Name("trebuchet_alerts_total")
            .Help("Total alerts emitted")
            .Register(*impl_->registry);

        impl_->predictions_family = &prometheus::BuildCounter()
            .Name("trebuchet_predictions_total")
            .Help("Total range predictions made")
            .Register(*impl_->registry);
        impl_->predictions_counter = &impl_->predictions_family->Add({});

        impl_->spring_energy_family = &prometheus::BuildGauge()
            .Name("trebuchet_spring_stored_energy_joules")
            .Help("Spring stored energy in joules")
            .Register(*impl_->registry);
        impl_->spring_energy_gauge = &impl_->spring_energy_family->Add({});

        impl_->spring_efficiency_family = &prometheus::BuildGauge()
            .Name("trebuchet_spring_release_efficiency")
            .Help("Spring energy release efficiency ratio")
            .Register(*impl_->registry);
        impl_->spring_efficiency_gauge = &impl_->spring_efficiency_family->Add({});

        impl_->spring_modulus_family = &prometheus::BuildGauge()
            .Name("trebuchet_spring_modulus_reduction_ratio")
            .Help("Shear modulus reduction due to cyclic softening")
            .Register(*impl_->registry);
        impl_->spring_modulus_gauge = &impl_->spring_modulus_family->Add({});

        impl_->range_predicted_family = &prometheus::BuildGauge()
            .Name("trebuchet_range_predicted_meters")
            .Help("Predicted projectile range in meters")
            .Register(*impl_->registry);

        impl_->range_actual_family = &prometheus::BuildGauge()
            .Name("trebuchet_range_actual_meters")
            .Help("Actual measured range in meters")
            .Register(*impl_->registry);

        impl_->optimal_angle_family = &prometheus::BuildGauge()
            .Name("trebuchet_optimal_launch_angle_degrees")
            .Help("Optimal launch angle for maximum range")
            .Register(*impl_->registry);

        impl_->mach_number_family = &prometheus::BuildGauge()
            .Name("trebuchet_projectile_mach_number")
            .Help("Maximum Mach number of projectile")
            .Register(*impl_->registry);

        impl_->shear_stress_family = &prometheus::BuildGauge()
            .Name("trebuchet_spring_shear_stress_pa")
            .Help("Spring wire shear stress in Pascals")
            .Register(*impl_->registry);

        impl_->cycle_count_family = &prometheus::BuildGauge()
            .Name("trebuchet_spring_cycle_count")
            .Help("Number of loading cycles")
            .Register(*impl_->registry);

        impl_->cyclic_damage_family = &prometheus::BuildGauge()
            .Name("trebuchet_spring_cyclic_damage_ratio")
            .Help("Cumulative cyclic damage parameter (Miner's rule)")
            .Register(*impl_->registry);

        impl_->latency_family = &prometheus::BuildHistogram()
            .Name("trebuchet_operation_latency_seconds")
            .Help("Latency histogram for various operations")
            .Register(*impl_->registry);

        std::string listen_addr = bind_address + ":" + std::to_string(port);
        impl_->exposer = std::make_unique<prometheus::Exposer>(listen_addr);
        impl_->exposer->RegisterCollectable(impl_->registry);

        impl_->initialized = true;
        running_ = true;
    } catch (const std::exception& e) {
        std::cerr << "[metrics] Prometheus init failed: " << e.what() << std::endl;
    }
#endif
}

void MetricsCollector::shutdown() {
    running_ = false;
}

bool MetricsCollector::isRunning() const {
    return running_.load();
}

void MetricsCollector::incrementUdpPacketsReceived(bool valid) {
    udp_packets_total_++;
    if (valid) {
#ifdef USE_PROMETHEUS
        if (impl_->udp_valid_counter) impl_->udp_valid_counter->Increment();
#endif
    } else {
        udp_packets_invalid_++;
#ifdef USE_PROMETHEUS
        if (impl_->udp_invalid_counter) impl_->udp_invalid_counter->Increment();
#endif
    }
}

void MetricsCollector::incrementAlertsEmitted(const std::string& alert_type, const std::string& level) {
    alerts_emitted_++;
#ifdef USE_PROMETHEUS
    if (impl_->alerts_family) {
        impl_->alerts_family->Add({{"type", alert_type}, {"level", level}}).Increment();
    }
#endif
}

void MetricsCollector::incrementPredictionsMade() {
    predictions_made_++;
#ifdef USE_PROMETHEUS
    if (impl_->predictions_counter) impl_->predictions_counter->Increment();
#endif
}

void MetricsCollector::setSpringStoredEnergy(double value) {
    spring_gauges_["stored_energy"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->spring_energy_gauge) impl_->spring_energy_gauge->Set(value);
#endif
}

void MetricsCollector::setSpringEfficiency(double value) {
    spring_gauges_["efficiency"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->spring_efficiency_gauge) impl_->spring_efficiency_gauge->Set(value);
#endif
}

void MetricsCollector::setSpringModulusReduction(double value) {
    spring_gauges_["modulus_reduction"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->spring_modulus_gauge) impl_->spring_modulus_gauge->Set(value);
#endif
}

void MetricsCollector::setRangePredicted(double value, const MetricLabels& labels) {
    trajectory_gauges_["predicted"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->range_predicted_family) {
        impl_->range_predicted_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(value);
    }
#endif
}

void MetricsCollector::setRangeActual(double value, const MetricLabels& labels) {
    trajectory_gauges_["actual"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->range_actual_family) {
        impl_->range_actual_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(value);
    }
#endif
}

void MetricsCollector::setOptimalLaunchAngle(double value, const MetricLabels& labels) {
    trajectory_gauges_["optimal_angle"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->optimal_angle_family) {
        impl_->optimal_angle_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(value);
    }
#endif
}

void MetricsCollector::setMachNumber(double value, const MetricLabels& labels) {
    trajectory_gauges_["mach_number"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->mach_number_family) {
        impl_->mach_number_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(value);
    }
#endif
}

void MetricsCollector::setShearStress(double value, const MetricLabels& labels) {
    spring_gauges_["shear_stress"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->shear_stress_family) {
        impl_->shear_stress_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(value);
    }
#endif
}

void MetricsCollector::setCycleCount(int64_t value, const MetricLabels& labels) {
    trajectory_gauges_["cycle_count"].store(static_cast<double>(value));
#ifdef USE_PROMETHEUS
    if (impl_->cycle_count_family) {
        impl_->cycle_count_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(static_cast<double>(value));
    }
#endif
}

void MetricsCollector::setCyclicDamage(double value, const MetricLabels& labels) {
    spring_gauges_["cyclic_damage"].store(value);
#ifdef USE_PROMETHEUS
    if (impl_->cyclic_damage_family) {
        impl_->cyclic_damage_family->Add({
            {"machine_id", labels.machine_id},
            {"material", labels.material},
            {"active_coils", std::to_string(labels.active_coils)}
        }).Set(value);
    }
#endif
}

void MetricsCollector::observeLatency(const std::string& operation, double latency_seconds) {
#ifdef USE_PROMETHEUS
    if (impl_->latency_family) {
        impl_->latency_family->Add(
            {{"operation", operation}},
            prometheus::Histogram::BucketBoundaries{0.0001, 0.001, 0.01, 0.1, 1.0, 10.0}
        ).Observe(latency_seconds);
    }
#endif
}

}  // namespace metrics
}  // namespace trebuchet

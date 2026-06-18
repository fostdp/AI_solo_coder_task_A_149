#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <mutex>

#include "message_bus.h"
#include "udp_receiver_module.h"
#include "spring_simulator_module.h"
#include "range_predictor_module.h"
#include "alarm_mqtt_module.h"
#include "trebuchet_physics.h"
#include "clickhouse_client.h"
#include "udp_sensor_receiver.h"
#include "mqtt_alert_manager.h"
#include "http_api_server.h"
#include "logger.h"
#include "metrics_collector.h"

namespace physics_ns = trebuchet::physics;
namespace alert_ns = trebuchet::alert;
namespace http_ns = trebuchet::http;
namespace storage_ns = trebuchet::storage;

using trebuchet::physics::TorsionSpringConfig;
using trebuchet::physics::ProjectileConfig;
using trebuchet::physics::SpringMaterial;
using trebuchet::physics::CyclicSofteningState;
using trebuchet::physics::STEEL_65MN;
using trebuchet::physics::initializeCyclicState;

static std::atomic<bool> g_running{true};
static std::mutex g_stdout_mutex;

static void signalHandler(int) { g_running = false; }

static constexpr double PI_VAL = 3.14159265358979323846;

static void printBanner() {
    std::lock_guard<std::mutex> lock(g_stdout_mutex);
    std::cout << "\n"
              << "  _______________________________  \n"
              << " / __/__  ____  ____ ___  _____  /__\n"
              << "/ _// _ \\/ __ `/ __ `/ _ \\/ ___/ __/  \n"
              << "\\__/\\___/_/ /_/\\__,_/\\___/_/    \\__/ \n"
              << "  Torque Spring Trebuchet Engine v2  \n"
              << std::endl;
}

static TorsionSpringConfig makeDefaultSpringConfig() {
    TorsionSpringConfig cfg;
    cfg.wire_diameter = 0.020;
    cfg.coil_mean_diameter = 0.150;
    cfg.active_coils = 12;
    cfg.material = STEEL_65MN;
    cfg.cyclic_state = initializeCyclicState(cfg.material);
    return cfg;
}

static ProjectileConfig makeDefaultProjectileConfig() {
    ProjectileConfig cfg;
    cfg.mass = 10.0;
    cfg.drag_coefficient_incompressible = 0.47;
    cfg.diameter = 0.2;
    cfg.cross_section_area = PI_VAL * (cfg.diameter / 2.0) * (cfg.diameter / 2.0);
    return cfg;
}

static void periodicStatusLogger(
    trebuchet::modules::UdpReceiverModule* udp,
    trebuchet::modules::SpringSimulatorModule* spr,
    trebuchet::modules::RangePredictorModule* rng,
    trebuchet::modules::AlarmMqttModule* alm,
    trebuchet::bus::MessageBus* bus) {
    while (g_running) {
        for (int i = 0; i < 600; i++) {
            if (!g_running) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LOG_INFO("status", "udp recv={} ok={} drop={} | spring proc={} alerts={} to_storage={} | range preds={} | mqtt ok={} fail={} dedup={} | bus_dropped={}",
                 udp->totalReceived(), udp->totalValidatedOk(), udp->totalDroppedInvalid(),
                 spr->messagesProcessed(), spr->alertsEmitted(), spr->storageDispatches(),
                 rng->predictionsMade(),
                 alm->alertsPublishedOk(), alm->alertsPublishFailed(), alm->alertsDeduped(),
                 udp->totalBusDropped() + bus->dropped());
    }
}

int main(int argc, char* argv[]) {
    using namespace trebuchet::modules;
    using namespace trebuchet::bus;
    using namespace trebuchet::metrics;
    using namespace trebuchet;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    Logger::init("logs", "trebuchet.log",
                 LogLevel::INFO, LogLevel::DEBUG);
    MetricsCollector::instance().init(8081, "0.0.0.0");

    printBanner();

    int udp_port = 9000;
    int http_port = 8080;
    std::string mqtt_host = "127.0.0.1";
    int mqtt_port = 1883;
    std::string clickhouse_host = "127.0.0.1";
    int clickhouse_port = 8123;
    std::string spring_config_path = "";
    std::string traj_config_path = "";

    for (int i = 1; i < argc - 1; i += 2) {
        const std::string k = argv[i];
        const std::string v = argv[i + 1];
        if (k == "--udp-port") udp_port = std::atoi(v.c_str());
        else if (k == "--http-port") http_port = std::atoi(v.c_str());
        else if (k == "--mqtt-host") mqtt_host = v;
        else if (k == "--mqtt-port") mqtt_port = std::atoi(v.c_str());
        else if (k == "--ch-host") clickhouse_host = v;
        else if (k == "--ch-port") clickhouse_port = std::atoi(v.c_str());
        else if (k == "--spring-config") spring_config_path = v;
        else if (k == "--traj-config") traj_config_path = v;
    }

    LOG_INFO("main", "Starting Trebuchet Sim Backend v2");
    LOG_INFO("main", "UDP port={}, HTTP port={}, Metrics port=8081", udp_port, http_port);
    LOG_INFO("main", "MQTT broker={}:{}, ClickHouse={}:{}",
             mqtt_host, mqtt_port, clickhouse_host, clickhouse_port);

    auto spring_cfg = makeDefaultSpringConfig();
    auto proj_cfg = makeDefaultProjectileConfig();

    MessageBus bus;

    UdpReceiverModule::Config udp_cfg;
    udp_cfg.port = udp_port;
    UdpReceiverModule udp_mod(udp_cfg, &bus);
    if (!spring_config_path.empty()) {
        spring_cfg = makeDefaultSpringConfig();
        LOG_INFO("main", "--spring-config file provided; loading via module API");
    }
    SpringSimulatorModule::Config spring_cfg_mod;
    SpringSimulatorModule spring_mod(spring_cfg_mod, spring_cfg, &bus);
    if (!spring_config_path.empty()) {
        bool ok = spring_mod.loadSpringParamsFromJson(spring_config_path);
        if (ok) {
            LOG_INFO("main", "spring config load OK: {}", spring_config_path);
        } else {
            LOG_ERROR("main", "spring config load FAILED: {}", spring_config_path);
        }
    }

    RangePredictorModule::Config range_cfg;
    RangePredictorModule range_mod(range_cfg, proj_cfg, &bus);
    if (!traj_config_path.empty()) {
        bool ok = range_mod.loadTrajectoryParamsFromJson(traj_config_path);
        if (ok) {
            LOG_INFO("main", "trajectory config load OK: {}", traj_config_path);
        } else {
            LOG_ERROR("main", "trajectory config load FAILED: {}", traj_config_path);
        }
    }

    AlarmMqttModule::Config alarm_cfg;
    alarm_cfg.mqtt_host = mqtt_host;
    alarm_cfg.mqtt_port = mqtt_port;
    AlarmMqttModule alarm_mod(alarm_cfg, &bus);

    LOG_INFO("main", "starting modules...");
    if (!udp_mod.start()) {
        LOG_CRITICAL("main", "UDP receiver failed to bind port {}", udp_port);
        return 1;
    }
    LOG_INFO("main", "  UDP receiver on : {} OK", udp_port);

    spring_mod.start();
    LOG_INFO("main", "  Spring simulator OK");

    range_mod.start();
    LOG_INFO("main", "  Range predictor OK");

    bool mqtt_ok = alarm_mod.start();
    LOG_INFO("main", "  MQTT alarm module: {}", (mqtt_ok ? "connected" : "standalone (no broker)"));

    auto status_cfg = spring_mod.getSpringConfig();
    auto proj_cfg_out = range_mod.getProjectileConfig();
    LOG_INFO("main", "spring: d={}mm D={}mm Na={} G={}GPa",
             status_cfg.wire_diameter * 1000,
             status_cfg.coil_mean_diameter * 1000,
             status_cfg.active_coils,
             status_cfg.material.shear_modulus * 1e-9);
    LOG_INFO("main", "projectile: m={}kg Cd0={} A={}m2",
             proj_cfg_out.mass,
             proj_cfg_out.drag_coefficient_incompressible,
             proj_cfg_out.cross_section_area);

    std::thread status_thread(periodicStatusLogger,
                              &udp_mod, &spring_mod, &range_mod, &alarm_mod, &bus);

    LOG_INFO("main", "all modules online. press Ctrl+C to stop.");

    udp_mod.setPacketHandler([](const SensorRawMessage& msg, bool valid) {
        if (!valid) return;
        if (msg.cycle_count > 0 && (msg.cycle_count % 50 == 0)) {
            LOG_INFO("sensor", "{} cycle={} theta={} rad, E={} J, damage={}, Ma_max={}",
                     msg.getMachineId(), msg.cycle_count, msg.torsion_angle_rad,
                     msg.stored_energy_j, msg.cyclic_damage_ratio, msg.max_mach);
        }
    });

    http_ns::HttpApiServer::Config http_cfg;
    http_cfg.port = http_port;
    http_cfg.static_files_dir = "frontend";
    http_ns::HttpApiServer http_srv(http_cfg);
    if (http_srv.start()) {
        LOG_INFO("main", "HTTP API server on : {}", http_port);
    } else {
        LOG_WARN("main", "HTTP API server failed to start");
    }

    try {
        storage_ns::ClickHouseClient::Config ch_cfg;
        ch_cfg.host = clickhouse_host;
        ch_cfg.port = clickhouse_port;
        storage_ns::ClickHouseClient ch(ch_cfg);
        if (ch.connect()) {
            LOG_INFO("main", "ClickHouse OK at {}:{}", clickhouse_host, clickhouse_port);
        } else {
            LOG_WARN("main", "ClickHouse unavailable at {}:{}", clickhouse_host, clickhouse_port);
        }
    } catch (...) {
        LOG_WARN("main", "ClickHouse client exception");
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LOG_INFO("main", "signal received, stopping modules...");

    udp_mod.stop();
    LOG_INFO("main", "  UDP stopped");
    spring_mod.stop();
    LOG_INFO("main", "  Spring stopped");
    range_mod.stop();
    LOG_INFO("main", "  Range stopped");
    alarm_mod.stop();
    LOG_INFO("main", "  Alarm stopped");
    http_srv.stop();
    LOG_INFO("main", "  HTTP stopped");
    MetricsCollector::instance().shutdown();
    Logger::flush();

    if (status_thread.joinable()) status_thread.join();

    LOG_INFO("main", "good-bye.");
    return 0;
}

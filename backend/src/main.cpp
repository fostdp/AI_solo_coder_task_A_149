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
        std::lock_guard<std::mutex> lock(g_stdout_mutex);
        std::cout << "[status] udp recv=" << udp->totalReceived()
                  << " ok=" << udp->totalValidatedOk()
                  << " drop=" << udp->totalDroppedInvalid()
                  << " | spring proc=" << spr->messagesProcessed()
                  << " alerts=" << spr->alertsEmitted()
                  << " to_storage=" << spr->storageDispatches()
                  << " | range preds=" << rng->predictionsMade()
                  << " | mqtt ok=" << alm->alertsPublishedOk()
                  << " fail=" << alm->alertsPublishFailed()
                  << " dedup=" << alm->alertsDeduped()
                  << " | bus_dropped=" << udp->totalBusDropped() + bus->dropped()
                  << std::endl;
    }
}

int main(int argc, char* argv[]) {
    using namespace trebuchet::modules;
    using namespace trebuchet::bus;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

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

    auto spring_cfg = makeDefaultSpringConfig();
    auto proj_cfg = makeDefaultProjectileConfig();

    MessageBus bus;

    UdpReceiverModule::Config udp_cfg;
    udp_cfg.port = udp_port;
    UdpReceiverModule udp_mod(udp_cfg, &bus);
    if (!spring_config_path.empty()) {
        spring_cfg = makeDefaultSpringConfig();
        std::cout << "[init] --spring-config file provided; loading via module API" << std::endl;
    }
    SpringSimulatorModule::Config spring_cfg_mod;
    SpringSimulatorModule spring_mod(spring_cfg_mod, spring_cfg, &bus);
    if (!spring_config_path.empty()) {
        bool ok = spring_mod.loadSpringParamsFromJson(spring_config_path);
        std::cout << "[init] spring config load " << (ok ? "OK" : "FAILED")
                  << " : " << spring_config_path << std::endl;
    }

    RangePredictorModule::Config range_cfg;
    RangePredictorModule range_mod(range_cfg, proj_cfg, &bus);
    if (!traj_config_path.empty()) {
        bool ok = range_mod.loadTrajectoryParamsFromJson(traj_config_path);
        std::cout << "[init] trajectory config load " << (ok ? "OK" : "FAILED")
                  << " : " << traj_config_path << std::endl;
    }

    AlarmMqttModule::Config alarm_cfg;
    alarm_cfg.mqtt_host = mqtt_host;
    alarm_cfg.mqtt_port = mqtt_port;
    AlarmMqttModule alarm_mod(alarm_cfg, &bus);

    std::cout << "[init] starting modules..." << std::endl;
    if (!udp_mod.start()) {
        std::cerr << "[fatal] UDP receiver failed to bind port " << udp_port << std::endl;
        return 1;
    }
    std::cout << "[init]   UDP receiver on : " << udp_port << " OK" << std::endl;

    spring_mod.start();
    std::cout << "[init]   Spring simulator OK" << std::endl;

    range_mod.start();
    std::cout << "[init]   Range predictor OK" << std::endl;

    bool mqtt_ok = alarm_mod.start();
    std::cout << "[init]   MQTT alarm module: "
              << (mqtt_ok ? "connected" : "standalone (no broker)") << std::endl;

    auto status_cfg = spring_mod.getSpringConfig();
    auto proj_cfg_out = range_mod.getProjectileConfig();
    std::cout << "[init] spring: d=" << status_cfg.wire_diameter * 1000 << "mm"
              << " D=" << status_cfg.coil_mean_diameter * 1000 << "mm"
              << " Na=" << status_cfg.active_coils
              << " G=" << status_cfg.material.shear_modulus * 1e-9 << "GPa"
              << std::endl;
    std::cout << "[init] projectile: m=" << proj_cfg_out.mass << "kg"
              << " Cd0=" << proj_cfg_out.drag_coefficient_incompressible
              << " A=" << proj_cfg_out.cross_section_area << "m2"
              << std::endl;

    std::thread status_thread(periodicStatusLogger,
                              &udp_mod, &spring_mod, &range_mod, &alarm_mod, &bus);

    std::cout << "[init] all modules online. press Ctrl+C to stop." << std::endl;

    udp_mod.setPacketHandler([](const SensorRawMessage& msg, bool valid) {
        if (!valid) return;
        std::lock_guard<std::mutex> l(g_stdout_mutex);
        if (msg.cycle_count > 0 && (msg.cycle_count % 50 == 0)) {
            std::cout << "[sensor] " << msg.getMachineId()
                      << " cycle=" << msg.cycle_count
                      << " theta=" << msg.torsion_angle_rad
                      << " rad, E=" << msg.stored_energy_j
                      << " J, damage=" << msg.cyclic_damage_ratio
                      << " Ma_max=" << msg.max_mach
                      << std::endl;
        }
    });

    http_ns::HttpApiServer::Config http_cfg;
    http_cfg.port = http_port;
    http_cfg.static_files_dir = "frontend";
    http_ns::HttpApiServer http_srv(http_cfg);
    if (http_srv.start()) {
        std::cout << "[init] HTTP API server on : " << http_port << std::endl;
    } else {
        std::cerr << "[warn] HTTP API server failed to start" << std::endl;
    }

    try {
        storage_ns::ClickHouseClient::Config ch_cfg;
        ch_cfg.host = clickhouse_host;
        ch_cfg.port = clickhouse_port;
        storage_ns::ClickHouseClient ch(ch_cfg);
        if (ch.connect()) {
            std::cout << "[init] ClickHouse OK at "
                      << clickhouse_host << ":" << clickhouse_port << std::endl;
        } else {
            std::cerr << "[warn] ClickHouse unavailable" << std::endl;
        }
    } catch (...) {
        std::cerr << "[warn] ClickHouse client exception" << std::endl;
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[shutdown] signal received, stopping modules..." << std::endl;

    udp_mod.stop();
    std::cout << "[shutdown]   UDP stopped" << std::endl;
    spring_mod.stop();
    std::cout << "[shutdown]   Spring stopped" << std::endl;
    range_mod.stop();
    std::cout << "[shutdown]   Range stopped" << std::endl;
    alarm_mod.stop();
    std::cout << "[shutdown]   Alarm stopped" << std::endl;
    http_srv.stop();
    std::cout << "[shutdown]   HTTP stopped" << std::endl;

    if (status_thread.joinable()) status_thread.join();

    std::cout << "[shutdown] good-bye." << std::endl;
    return 0;
}

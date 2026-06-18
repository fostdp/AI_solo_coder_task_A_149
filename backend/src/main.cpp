#include "trebuchet_physics.h"
#include "clickhouse_client.h"
#include "mqtt_alert_manager.h"
#include "udp_sensor_receiver.h"
#include "http_api_server.h"

#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

using namespace trebuchet;

std::atomic<bool> g_running{true};
std::mutex g_storage_mutex;

physics::TorsionSpringConfig g_spring_config;
physics::ProjectileConfig g_projectile_config;
std::mutex g_cyclic_state_mutex;

struct ServerContext {
    std::unique_ptr<storage::ClickHouseClient> clickhouse;
    std::unique_ptr<alert::MqttAlertManager> alert_manager;
    std::unique_ptr<network::UdpSensorReceiver> udp_receiver;
    std::unique_ptr<http::HttpApiServer> http_server;
};

std::unique_ptr<ServerContext> g_ctx;

void signalHandler(int signal) {
    std::cout << "\n收到信号 " << signal << "，正在关闭服务..." << std::endl;
    g_running = false;
}

std::string urlDecode(const std::string& encoded) {
    std::string result;
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            std::string hex = encoded.substr(i + 1, 2);
            char c = static_cast<char>(std::stoi(hex, nullptr, 16));
            result += c;
            i += 2;
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    return result;
}

void onSensorDataReceived(const network::SensorDataPacket& packet) {
    if (!g_ctx || !g_ctx->clickhouse) return;

    physics::SpringEnergyResult spring_result;
    physics::RangePredictionResult range_pred;

    {
        std::lock_guard<std::mutex> lock(g_cyclic_state_mutex);
        physics::updateCyclicSoftening(
            g_spring_config,
            packet.torsion_angle,
            physics::calculateShearStress(g_spring_config, packet.torsion_angle)
        );
        spring_result = physics::calculateSpringEnergy(
            g_spring_config, packet.torsion_angle
        );
    }

    physics::ProjectileConfig proj = g_projectile_config;
    proj.mass = packet.projectile_mass;
    range_pred = physics::predictTrajectoryRange(
        proj, packet.release_velocity, packet.launch_angle
    );

    std::string risk_level = "normal";
    if (spring_result.yield_strength_ratio > 0.85 || spring_result.fatigue_risk) {
        risk_level = "critical";
    } else if (spring_result.yield_strength_ratio > 0.70
            || range_pred.insufficient_range
            || spring_result.cyclic_damage_ratio > 0.5) {
        risk_level = "warning";
    }

    double modulus_reduction = g_spring_config.cyclic_state.degraded_shear_modulus
                             / g_spring_config.material.shear_modulus;

    storage::SensorRecord record;
    record.machine_id = packet.machine_id;
    record.timestamp = packet.timestamp;
    record.torsion_angle = packet.torsion_angle;
    record.stored_energy = packet.stored_energy > 0 ? packet.stored_energy
                                                    : spring_result.stored_energy;
    record.release_velocity = packet.release_velocity;
    record.actual_range = packet.actual_range;
    record.predicted_range = range_pred.predicted_range;
    record.efficiency = spring_result.efficiency;
    record.projectile_mass = packet.projectile_mass;
    record.launch_angle = packet.launch_angle;
    record.spring_status = packet.spring_status;
    record.risk_level = risk_level;
    record.shear_stress = spring_result.shear_stress;
    record.elastic_stress = spring_result.elastic_stress;
    record.plastic_strain = packet.plastic_strain > 0 ? packet.plastic_strain
                                                       : spring_result.plastic_strain;
    record.cycle_count = packet.cycle_count > 0 ? packet.cycle_count
                                                 : spring_result.cycle_count;
    record.cyclic_damage_ratio = packet.cyclic_damage_ratio > 0
                                     ? packet.cyclic_damage_ratio
                                     : spring_result.cyclic_damage_ratio;
    record.modulus_reduction = modulus_reduction;
    record.max_mach = packet.max_mach > 0 ? packet.max_mach : range_pred.max_mach;
    record.compressibility_correction = range_pred.compressibility_correction;
    record.fatigue_risk = spring_result.fatigue_risk ? 1 : 0;

    {
        std::lock_guard<std::mutex> lock(g_storage_mutex);
        g_ctx->clickhouse->insertSensorData(record);

        storage::SpringEnergyRecord ser;
        ser.machine_id = packet.machine_id;
        ser.torsion_angle = packet.torsion_angle;
        ser.stored_energy = spring_result.stored_energy;
        ser.shear_stress = spring_result.shear_stress;
        ser.elastic_stress = spring_result.elastic_stress;
        ser.plastic_strain = spring_result.plastic_strain;
        ser.spring_constant = spring_result.spring_constant;
        ser.efficiency = spring_result.efficiency;
        ser.yield_strength_ratio = spring_result.yield_strength_ratio;
        ser.cycle_count = spring_result.cycle_count;
        ser.cyclic_damage_ratio = spring_result.cyclic_damage_ratio;
        ser.modulus_reduction = modulus_reduction;
        ser.back_stress = g_spring_config.cyclic_state.back_stress;
        ser.degraded_yield_strength = g_spring_config.cyclic_state.degraded_yield_strength;
        g_ctx->clickhouse->insertSpringEnergy(ser);

        storage::RangePredictionRecord rpr;
        rpr.machine_id = packet.machine_id;
        rpr.projectile_mass = packet.projectile_mass;
        rpr.launch_angle = packet.launch_angle;
        rpr.release_velocity = packet.release_velocity;
        rpr.predicted_range = range_pred.predicted_range;
        rpr.max_height = range_pred.max_height;
        rpr.flight_time = range_pred.flight_time;
        rpr.air_resistance_factor = range_pred.air_resistance_factor;
        rpr.max_mach = range_pred.max_mach;
        rpr.compressibility_correction = range_pred.compressibility_correction;
        rpr.impact_velocity = 0.0;
        rpr.impact_mach = 0.0;
        rpr.temperature_k = 288.15;
        g_ctx->clickhouse->insertRangePrediction(rpr);

        int64_t remaining_life = 0;
        if (spring_result.plastic_strain > 1e-9) {
            double life = physics::calculateCoffinMansonLife(
                g_spring_config.material, spring_result.plastic_strain
            );
            remaining_life = static_cast<int64_t>(
                std::max(0.0, life * (1.0 - spring_result.cyclic_damage_ratio))
            );
        }

        g_ctx->clickhouse->insertCyclicFatigueLog(
            packet.machine_id,
            spring_result.cycle_count,
            spring_result.plastic_strain,
            g_spring_config.cyclic_state.accumulated_plastic_strain,
            g_spring_config.cyclic_state.degraded_shear_modulus,
            g_spring_config.cyclic_state.degraded_yield_strength,
            spring_result.cyclic_damage_ratio,
            remaining_life
        );

        if (spring_result.fracture_risk && g_ctx->alert_manager) {
            g_ctx->alert_manager->publishSpringFractureWarning(
                packet.machine_id,
                packet.torsion_angle,
                spring_result.yield_strength_ratio,
                spring_result.shear_stress
            );
            storage::AlertRecord ar;
            ar.machine_id = packet.machine_id;
            ar.timestamp = packet.timestamp;
            ar.alert_type = "spring_fracture_risk";
            ar.alert_level = spring_result.yield_strength_ratio > 0.95 ? "critical" : "warning";
            ar.message = "弹簧断裂风险告警";
            ar.torsion_angle = packet.torsion_angle;
            ar.stored_energy = spring_result.stored_energy;
            ar.actual_range = packet.actual_range;
            ar.predicted_range = range_pred.predicted_range;
            ar.threshold_value = 0.85;
            ar.cyclic_damage_ratio = spring_result.cyclic_damage_ratio;
            ar.cycle_count = spring_result.cycle_count;
            ar.max_mach = range_pred.max_mach;
            g_ctx->clickhouse->insertAlert(ar);
        }

        if (spring_result.fatigue_risk && g_ctx->alert_manager) {
            g_ctx->alert_manager->publishCyclicFatigueWarning(
                packet.machine_id,
                spring_result.cycle_count,
                spring_result.cyclic_damage_ratio,
                spring_result.plastic_strain,
                remaining_life
            );
            storage::AlertRecord ar;
            ar.machine_id = packet.machine_id;
            ar.timestamp = packet.timestamp;
            ar.alert_type = "cyclic_fatigue_risk";
            ar.alert_level = spring_result.cyclic_damage_ratio > 0.8 ? "critical" : "warning";
            ar.message = "循环疲劳风险告警: 累积损伤比超过阈值";
            ar.torsion_angle = packet.torsion_angle;
            ar.stored_energy = spring_result.stored_energy;
            ar.actual_range = packet.actual_range;
            ar.predicted_range = range_pred.predicted_range;
            ar.threshold_value = 0.5;
            ar.cyclic_damage_ratio = spring_result.cyclic_damage_ratio;
            ar.cycle_count = spring_result.cycle_count;
            ar.max_mach = range_pred.max_mach;
            g_ctx->clickhouse->insertAlert(ar);
        }

        if (range_pred.insufficient_range && packet.actual_range > 0 && g_ctx->alert_manager) {
            double min_required = range_pred.predicted_range * physics::OPTIMAL_RANGE_PERCENT;
            if (packet.actual_range < min_required) {
                g_ctx->alert_manager->publishInsufficientRangeWarning(
                    packet.machine_id,
                    packet.actual_range,
                    range_pred.predicted_range,
                    min_required
                );
                storage::AlertRecord ar;
                ar.machine_id = packet.machine_id;
                ar.timestamp = packet.timestamp;
                ar.alert_type = "insufficient_range";
                ar.alert_level = "warning";
                ar.message = "射程不足告警";
                ar.torsion_angle = packet.torsion_angle;
                ar.stored_energy = spring_result.stored_energy;
                ar.actual_range = packet.actual_range;
                ar.predicted_range = range_pred.predicted_range;
                ar.threshold_value = min_required;
                ar.cyclic_damage_ratio = spring_result.cyclic_damage_ratio;
                ar.cycle_count = spring_result.cycle_count;
                ar.max_mach = range_pred.max_mach;
                g_ctx->clickhouse->insertAlert(ar);
            }
        }
    }

    std::cout << "[" << packet.machine_id << "] "
              << "循环#" << spring_result.cycle_count << " "
              << "扭转角=" << packet.torsion_angle << "rad, "
              << "储能=" << spring_result.stored_energy << "J, "
              << "损伤比=" << (spring_result.cyclic_damage_ratio * 100) << "%, "
              << "最大Ma=" << range_pred.max_mach << ", "
              << "预测射程=" << range_pred.predicted_range << "m, "
              << "实际射程=" << packet.actual_range << "m, "
              << "风险=" << risk_level << std::endl;
}

http::HttpResponse handleHealth(const http::HttpRequest&) {
    std::ostringstream json;
    json << "{\"status\":\"ok\",\"service\":\"trebuchet-sim-backend\"}";
    return http::HttpResponse::json(200, json.str());
}

http::HttpResponse handlePredictRange(const http::HttpRequest& req) {
    try {
        double velocity = 30.0, angle = 45.0, mass = 10.0, drag = 1.0;
        auto it = req.query_params.find("velocity");
        if (it != req.query_params.end()) velocity = std::stod(urlDecode(it->second));
        it = req.query_params.find("angle");
        if (it != req.query_params.end()) angle = std::stod(urlDecode(it->second));
        it = req.query_params.find("mass");
        if (it != req.query_params.end()) mass = std::stod(urlDecode(it->second));
        it = req.query_params.find("drag");
        if (it != req.query_params.end()) drag = std::stod(urlDecode(it->second));

        physics::ProjectileConfig proj = g_projectile_config;
        proj.mass = mass;
        auto result = physics::calculateFullTrajectory(proj, velocity, angle, drag);

        std::ostringstream json;
        json << "{";
        json << "\"release_velocity\":" << velocity << ",";
        json << "\"launch_angle\":" << angle << ",";
        json << "\"projectile_mass\":" << mass << ",";
        json << "\"predicted_range\":" << result.predicted_range << ",";
        json << "\"max_height\":" << result.max_height << ",";
        json << "\"flight_time\":" << result.flight_time << ",";
        json << "\"impact_velocity\":" << result.impact_velocity << ",";
        json << "\"impact_mach\":" << result.impact_mach << ",";
        json << "\"max_mach\":" << result.max_mach << ",";
        json << "\"optimal_angle\":" << result.launch_angle_optimal << ",";
        json << "\"trajectory\":[";
        for (size_t i = 0; i < result.trajectory_points.size(); ++i) {
            if (i > 0) json << ",";
            json << "[" << result.trajectory_points[i].first << ","
                 << result.trajectory_points[i].second << "]";
        }
        json << "],\"mach_profile\":[";
        for (size_t i = 0; i < result.mach_profile.size(); ++i) {
            if (i > 0) json << ",";
            json << "[" << result.mach_profile[i].first << ","
                 << result.mach_profile[i].second << "]";
        }
        json << "]}";
        return http::HttpResponse::json(200, json.str());
    } catch (const std::exception& e) {
        return http::HttpResponse::error(400, e.what());
    }
}

http::HttpResponse handleCalculateSpringEnergy(const http::HttpRequest& req) {
    try {
        double torsion_angle = 1.0;
        auto it = req.query_params.find("angle");
        if (it != req.query_params.end()) torsion_angle = std::stod(urlDecode(it->second));

        physics::SpringEnergyResult result;
        {
            std::lock_guard<std::mutex> lock(g_cyclic_state_mutex);
            result = physics::calculateSpringEnergy(g_spring_config, torsion_angle);
        }

        double modulus_reduction = g_spring_config.cyclic_state.degraded_shear_modulus
                                 / g_spring_config.material.shear_modulus;

        std::ostringstream json;
        json << "{";
        json << "\"torsion_angle\":" << torsion_angle << ",";
        json << "\"stored_energy\":" << result.stored_energy << ",";
        json << "\"spring_constant\":" << result.spring_constant << ",";
        json << "\"shear_stress\":" << result.shear_stress << ",";
        json << "\"elastic_stress\":" << result.elastic_stress << ",";
        json << "\"plastic_strain\":" << result.plastic_strain << ",";
        json << "\"efficiency\":" << result.efficiency << ",";
        json << "\"yield_strength_ratio\":" << result.yield_strength_ratio << ",";
        json << "\"cycle_count\":" << result.cycle_count << ",";
        json << "\"cyclic_damage_ratio\":" << result.cyclic_damage_ratio << ",";
        json << "\"modulus_reduction\":" << modulus_reduction << ",";
        json << "\"back_stress\":" << g_spring_config.cyclic_state.back_stress << ",";
        json << "\"degraded_yield_strength\":" << g_spring_config.cyclic_state.degraded_yield_strength << ",";
        json << "\"fracture_risk\":" << (result.fracture_risk ? "true" : "false") << ",";
        json << "\"fatigue_risk\":" << (result.fatigue_risk ? "true" : "false");
        json << "}";
        return http::HttpResponse::json(200, json.str());
    } catch (const std::exception& e) {
        return http::HttpResponse::error(400, e.what());
    }
}

http::HttpResponse handleGetSensorData(const http::HttpRequest& req) {
    if (!g_ctx || !g_ctx->clickhouse) {
        return http::HttpResponse::error(503, "Database not connected");
    }
    std::string machine_id;
    int limit = 100;
    auto it = req.query_params.find("machine_id");
    if (it != req.query_params.end()) machine_id = urlDecode(it->second);
    it = req.query_params.find("limit");
    if (it != req.query_params.end()) limit = std::stoi(urlDecode(it->second));

    auto data = g_ctx->clickhouse->queryRecentSensorData(machine_id, limit);
    std::ostringstream json;
    json << "{\"data\":[";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << "\"machine_id\":\"" << data[i].machine_id << "\","
             << "\"torsion_angle\":" << data[i].torsion_angle << ","
             << "\"stored_energy\":" << data[i].stored_energy << ","
             << "\"release_velocity\":" << data[i].release_velocity << ","
             << "\"actual_range\":" << data[i].actual_range << ","
             << "\"predicted_range\":" << data[i].predicted_range << ","
             << "\"efficiency\":" << data[i].efficiency << ","
             << "\"projectile_mass\":" << data[i].projectile_mass << ","
             << "\"launch_angle\":" << data[i].launch_angle << ","
             << "\"spring_status\":\"" << data[i].spring_status << "\","
             << "\"risk_level\":\"" << data[i].risk_level << "\","
             << "\"shear_stress\":" << data[i].shear_stress << ","
             << "\"elastic_stress\":" << data[i].elastic_stress << ","
             << "\"plastic_strain\":" << data[i].plastic_strain << ","
             << "\"cycle_count\":" << data[i].cycle_count << ","
             << "\"cyclic_damage_ratio\":" << data[i].cyclic_damage_ratio << ","
             << "\"modulus_reduction\":" << data[i].modulus_reduction << ","
             << "\"max_mach\":" << data[i].max_mach << ","
             << "\"compressibility_correction\":" << data[i].compressibility_correction << ","
             << "\"fatigue_risk\":" << static_cast<int>(data[i].fatigue_risk)
             << "}";
    }
    json << "]}";
    return http::HttpResponse::json(200, json.str());
}

http::HttpResponse handleGetAlerts(const http::HttpRequest& req) {
    if (!g_ctx || !g_ctx->clickhouse) {
        return http::HttpResponse::error(503, "Database not connected");
    }
    std::string machine_id;
    int limit = 50;
    auto it = req.query_params.find("machine_id");
    if (it != req.query_params.end()) machine_id = urlDecode(it->second);
    it = req.query_params.find("limit");
    if (it != req.query_params.end()) limit = std::stoi(urlDecode(it->second));

    auto alerts = g_ctx->clickhouse->queryRecentAlerts(machine_id, limit);
    std::ostringstream json;
    json << "{\"data\":[";
    for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << "\"machine_id\":\"" << alerts[i].machine_id << "\","
             << "\"alert_type\":\"" << alerts[i].alert_type << "\","
             << "\"alert_level\":\"" << alerts[i].alert_level << "\","
             << "\"message\":\"" << alerts[i].message << "\","
             << "\"torsion_angle\":" << alerts[i].torsion_angle << ","
             << "\"stored_energy\":" << alerts[i].stored_energy << ","
             << "\"actual_range\":" << alerts[i].actual_range << ","
             << "\"predicted_range\":" << alerts[i].predicted_range << ","
             << "\"threshold_value\":" << alerts[i].threshold_value << ","
             << "\"cyclic_damage_ratio\":" << alerts[i].cyclic_damage_ratio << ","
             << "\"cycle_count\":" << alerts[i].cycle_count << ","
             << "\"max_mach\":" << alerts[i].max_mach
             << "}";
    }
    json << "]}";
    return http::HttpResponse::json(200, json.str());
}

http::HttpResponse handleGetMachineStatus(const http::HttpRequest&) {
    if (!g_ctx || !g_ctx->clickhouse) {
        return http::HttpResponse::error(503, "Database not connected");
    }
    auto statuses = g_ctx->clickhouse->queryAllMachineStatus();
    std::ostringstream json;
    json << "{\"data\":[";
    for (size_t i = 0; i < statuses.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << "\"machine_id\":\"" << statuses[i].machine_id << "\","
             << "\"last_torsion_angle\":" << statuses[i].last_torsion_angle << ","
             << "\"last_stored_energy\":" << statuses[i].last_stored_energy << ","
             << "\"last_release_velocity\":" << statuses[i].last_release_velocity << ","
             << "\"last_actual_range\":" << statuses[i].last_actual_range << ","
             << "\"last_predicted_range\":" << statuses[i].last_predicted_range << ","
             << "\"current_risk_level\":\"" << statuses[i].current_risk_level << "\","
             << "\"total_cycles\":" << statuses[i].total_cycles << ","
             << "\"current_damage_ratio\":" << statuses[i].current_damage_ratio << ","
             << "\"last_max_mach\":" << statuses[i].last_max_mach << ","
             << "\"unacknowledged_alerts\":" << statuses[i].unacknowledged_alerts
             << "}";
    }
    json << "]}";
    return http::HttpResponse::json(200, json.str());
}

int main(int argc, char* argv[]) {
    int udp_port = 9000;
    int http_port = 8080;
    std::string ch_host = "127.0.0.1";
    int ch_port = 8123;
    std::string mqtt_host = "127.0.0.1";
    int mqtt_port = 1883;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--udp-port" && i + 1 < argc) udp_port = std::stoi(argv[++i]);
        else if (arg == "--http-port" && i + 1 < argc) http_port = std::stoi(argv[++i]);
        else if (arg == "--ch-host" && i + 1 < argc) ch_host = argv[++i];
        else if (arg == "--ch-port" && i + 1 < argc) ch_port = std::stoi(argv[++i]);
        else if (arg == "--mqtt-host" && i + 1 < argc) mqtt_host = argv[++i];
        else if (arg == "--mqtt-port" && i + 1 < argc) mqtt_port = std::stoi(argv[++i]);
    }

    g_spring_config.wire_diameter = 0.02;
    g_spring_config.coil_mean_diameter = 0.15;
    g_spring_config.active_coils = 12;
    g_spring_config.material = physics::STEEL_65MN;
    g_spring_config.cyclic_state = physics::initializeCyclicState(g_spring_config.material);

    g_projectile_config.mass = 10.0;
    g_projectile_config.drag_coefficient_incompressible = 0.47;
    g_projectile_config.cross_section_area = 0.0314;
    g_projectile_config.diameter = 0.2;

    signal(SIGINT, signalHandler);
#ifdef SIGTERM
    signal(SIGTERM, signalHandler);
#endif

    std::cout << "================================================" << std::endl;
    std::cout << " 古代霹雳车扭力弹簧储能仿真与射程预测系统 后端" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "UDP传感器端口: " << udp_port << std::endl;
    std::cout << "HTTP API端口: " << http_port << std::endl;
    std::cout << "ClickHouse: " << ch_host << ":" << ch_port << std::endl;
    std::cout << "MQTT Broker: " << mqtt_host << ":" << mqtt_port << std::endl;
    std::cout << std::endl;

    g_ctx = std::make_unique<ServerContext>();

    storage::ClickHouseClient::Config ch_cfg;
    ch_cfg.host = ch_host;
    ch_cfg.port = ch_port;
    g_ctx->clickhouse = std::make_unique<storage::ClickHouseClient>(ch_cfg);
    std::cout << "[ClickHouse] 连接中... ";
    if (g_ctx->clickhouse->connect()) {
        std::cout << "成功" << std::endl;
    } else {
        std::cout << "失败 (将继续运行，数据不会持久化)" << std::endl;
    }

    alert::MqttConfig mqtt_cfg;
    mqtt_cfg.broker_host = mqtt_host;
    mqtt_cfg.broker_port = mqtt_port;
    g_ctx->alert_manager = std::make_unique<alert::MqttAlertManager>(mqtt_cfg);
    std::cout << "[MQTT] 连接中... ";
    if (g_ctx->alert_manager->connect()) {
        std::cout << "成功" << std::endl;
    } else {
        std::cout << "失败 (告警将仅打印到日志)" << std::endl;
    }

    network::UdpSensorReceiver::Config udp_cfg;
    udp_cfg.port = udp_port;
    g_ctx->udp_receiver = std::make_unique<network::UdpSensorReceiver>(udp_cfg);
    g_ctx->udp_receiver->setDataCallback(onSensorDataReceived);
    std::cout << "[UDP] 启动监听... ";
    if (g_ctx->udp_receiver->start()) {
        std::cout << "成功 (端口 " << udp_port << ")" << std::endl;
    } else {
        std::cout << "失败!" << std::endl;
        return 1;
    }

    http::HttpApiServer::Config http_cfg;
    http_cfg.port = http_port;
    g_ctx->http_server = std::make_unique<http::HttpApiServer>(http_cfg);

    g_ctx->http_server->addGetRoute("/api/health", handleHealth);
    g_ctx->http_server->addGetRoute("/api/predict-range", handlePredictRange);
    g_ctx->http_server->addGetRoute("/api/spring-energy", handleCalculateSpringEnergy);
    g_ctx->http_server->addGetRoute("/api/sensor-data", handleGetSensorData);
    g_ctx->http_server->addGetRoute("/api/alerts", handleGetAlerts);
    g_ctx->http_server->addGetRoute("/api/machine-status", handleGetMachineStatus);

    g_ctx->http_server->setNotFoundHandler([](const http::HttpRequest& req) {
        std::ostringstream json;
        json << "{\"error\":404,\"message\":\"Not Found: " << req.path << "\"}";
        return http::HttpResponse::error(404, json.str());
    });

    std::cout << "[HTTP] 启动API服务... ";
    if (g_ctx->http_server->start()) {
        std::cout << "成功 (端口 " << http_port << ")" << std::endl;
    } else {
        std::cout << "失败!" << std::endl;
        return 1;
    }

    std::cout << std::endl << "系统启动完成。按 Ctrl+C 停止。" << std::endl << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (g_ctx->alert_manager) {
            g_ctx->alert_manager->processQueuedAlerts();
        }
    }

    std::cout << "正在关闭服务..." << std::endl;
    g_ctx->udp_receiver->stop();
    g_ctx->http_server->stop();
    g_ctx->alert_manager->disconnect();

    std::cout << "服务已停止。" << std::endl;
    return 0;
}

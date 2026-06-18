#ifndef UDP_SENSOR_RECEIVER_H
#define UDP_SENSOR_RECEIVER_H

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>

namespace trebuchet {
namespace network {

struct SensorDataPacket {
    std::string machine_id;
    std::chrono::system_clock::time_point timestamp;
    double torsion_angle;
    double stored_energy;
    double release_velocity;
    double actual_range;
    double projectile_mass;
    double launch_angle;
    std::string spring_status;
};

using SensorDataCallback = std::function<void(const SensorDataPacket&)>;

class UdpSensorReceiver {
public:
    struct Config {
        std::string bind_address = "0.0.0.0";
        int port = 9000;
        int buffer_size = 65536;
    };

    UdpSensorReceiver(const Config& config);
    ~UdpSensorReceiver();

    bool start();
    void stop();
    bool isRunning() const;

    void setDataCallback(SensorDataCallback callback);

    bool parsePacket(
        const std::string& raw_data,
        SensorDataPacket& out_packet
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void receiveLoop();

    std::atomic<bool> running_;
    std::thread receive_thread_;
    std::mutex callback_mutex_;
    SensorDataCallback data_callback_;
};

}
}

#endif

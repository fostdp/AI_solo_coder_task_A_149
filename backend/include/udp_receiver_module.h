#ifndef TREBUCHET_UDP_RECEIVER_MODULE_H
#define TREBUCHET_UDP_RECEIVER_MODULE_H

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

#include "message_bus.h"

namespace trebuchet {
namespace modules {

class UdpReceiverModule {
public:
    struct Config {
        std::string bind_address = "0.0.0.0";
        int port = 9000;
        int buffer_size = 65536;
        int receive_timeout_ms = 500;
        bool enable_checksum_validation = true;
        bool drop_invalid_packets = true;
    };

    using PacketHandler = std::function<void(const bus::SensorRawMessage&, bool valid)>;

    UdpReceiverModule(const Config& config, bus::MessageBus* bus);
    ~UdpReceiverModule();

    bool start();
    void stop();
    bool isRunning() const;

    void setPacketHandler(PacketHandler handler);

    uint64_t totalReceived() const { return total_received_.load(); }
    uint64_t totalValidatedOk() const { return valid_ok_.load(); }
    uint64_t totalDroppedInvalid() const { return dropped_invalid_.load(); }
    uint64_t totalBusDropped() const { return bus_dropped_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void receiveLoop();
    bool parsePacketToMessage(const std::string& raw, bus::SensorRawMessage& out);
    bool dispatchToBus(const bus::SensorRawMessage& msg);

    Config config_;
    bus::MessageBus* bus_;

    std::atomic<bool> running_{false};
    std::thread receive_thread_;

    std::mutex handler_mutex_;
    PacketHandler packet_handler_;

    std::atomic<uint64_t> total_received_{0};
    std::atomic<uint64_t> valid_ok_{0};
    std::atomic<uint64_t> dropped_invalid_{0};
    std::atomic<uint64_t> bus_dropped_{0};
};

}
}

#endif

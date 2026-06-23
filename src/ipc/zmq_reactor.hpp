#pragma once

#include <zmq.hpp>
#include <iostream>
#include <string>
#include <atomic>
#include "../core/messages.hpp"

namespace berkshire::ipc {

/**
 * @brief Command payload from Python AlphaEvolve Orchestrator
 */
struct alignas(64) ZMQCommand {
    uint8_t command_type; // 0: UPDATE_RISK, 1: SWITCH_STRATEGY, 2: HALT
    uint8_t strategy_id;
    char    reserved[6];
    double  new_threshold;
    char    padding[48];
};
static_assert(sizeof(ZMQCommand) == 64, "ZMQCommand must be 64 bytes");

class ZMQReactor {
public:
    ZMQReactor(const std::string& endpoint)
        : context_(1), socket_(context_, ZMQ_PAIR), endpoint_(endpoint) {}

    void start() {
        try {
            socket_.bind(endpoint_);
            std::cout << "[ZMQ] Reactor listening on " << endpoint_ << std::endl;
        } catch (const zmq::error_t& e) {
            std::cerr << "[ZMQ] Bind error: " << e.what() << std::endl;
        }
    }

    /**
     * @brief Non-blocking poll for incoming commands from Python
     */
    void poll_commands() {
        zmq::message_t msg;
        // Non-blocking receive
        if (socket_.recv(msg, zmq::recv_flags::dontwait)) {
            if (msg.size() == sizeof(ZMQCommand)) {
                auto* cmd = reinterpret_cast<ZMQCommand*>(msg.data());
                execute_command(cmd);
            }
        }
    }

private:
    void execute_command(const ZMQCommand* cmd) {
        if (cmd->command_type == 0) {
            std::cout << "[ZMQ] AlphaEvolve: Updating risk threshold to " << cmd->new_threshold << std::endl;
        } else if (cmd->command_type == 1) {
            std::cout << "[ZMQ] AlphaEvolve: Switching to Strategy ID " << (int)cmd->strategy_id << std::endl;
        } else if (cmd->command_type == 2) {
            std::cout << "[ZMQ] AlphaEvolve: HALT Trading!" << std::endl;
        }
    }

    zmq::context_t context_;
    zmq::socket_t socket_;
    std::string endpoint_;
};

} // namespace berkshire::ipc
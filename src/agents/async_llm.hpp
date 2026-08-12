#pragma once
#include <string>
#include <optional>
#include <chrono>
#include <zmq.hpp>

namespace effata::agents {

class AsyncLLM {
public:
    AsyncLLM() : ctx_(1), sock_(ctx_, zmq::socket_type::dealer) {
        sock_.set(zmq::sockopt::routing_id, "quantum_engine");
        sock_.set(zmq::sockopt::linger, 0);
        sock_.connect("tcp://localhost:5555");
    }

    void send_async(const std::string& prompt) {
        zmq::message_t msg(prompt.data(), prompt.size());
        sock_.send(msg, zmq::send_flags::dontwait);
        pending_ = true;
        sent_at_ = std::chrono::steady_clock::now();
    }

    std::optional<std::string> try_recv() {
        if (!pending_) return std::nullopt;
        zmq::message_t resp;
        auto result = sock_.recv(resp, zmq::recv_flags::dontwait);
        if (result) {
            pending_ = false;
            return std::string(static_cast<char*>(resp.data()), resp.size());
        }
        auto elapsed = std::chrono::steady_clock::now() - sent_at_;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > 50) {
            pending_ = false;
        }
        return std::nullopt;
    }

private:
    zmq::context_t ctx_;
    zmq::socket_t sock_;
    bool pending_ = false;
    std::chrono::steady_clock::time_point sent_at_;
};

} // namespace effata::agents

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <optional>

namespace emmy {

using json = nlohmann::json;

// A single LSP message received from the client
struct IncomingMessage {
    json body;
    bool isRequest;  // true = has "id" field (request), false = notification
    std::optional<int> id;
    std::string method;
};

// JSON-RPC 2.0 over stdio with Content-Length framing
class Transport {
public:
    Transport();
    ~Transport();

    // Start reading from stdin (spawns reader thread)
    void start();

    // Stop reading and clean up
    void stop();

    // Blocking: get next message from the queue (returns false if stopped)
    bool receive(IncomingMessage& msg);

    // Send a response to a request (has "id")
    void sendResponse(int id, const json& result);

    // Send an error response to a request
    void sendError(int id, int code, const json& message);

    // Send a server-initiated notification (no "id")
    void sendNotification(const std::string& method, const json& params);

    bool isRunning() const { return running_.load(); }

private:
    void readerLoop();
    void writeMessage(const json& msg);

    std::atomic<bool> running_{false};
    std::thread readerThread_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<IncomingMessage> queue_;

    std::mutex writeMutex_;
};

}  // namespace emmy

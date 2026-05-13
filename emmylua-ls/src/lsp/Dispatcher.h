#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace emmy {

using json = nlohmann::json;

// Handler function types
using RequestHandler = std::function<json(int id, const json& params)>;
using NotificationHandler = std::function<void(const json& params)>;

class Transport;

// Routes LSP method names to handler functions
class Dispatcher {
public:
    explicit Dispatcher(Transport& transport);

    // Register a handler for a request method (has response)
    void onRequest(const std::string& method, RequestHandler handler);

    // Register a handler for a notification method (no response)
    void onNotification(const std::string& method, NotificationHandler handler);

    // Dispatch an incoming message to the appropriate handler
    void dispatch(const std::string& method, int id, const json& params, bool isRequest);

private:
    Transport& transport_;
    std::unordered_map<std::string, RequestHandler> requestHandlers_;
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
};

}  // namespace emmy

#include "lsp/Dispatcher.h"
#include "lsp/Transport.h"
#include <spdlog/spdlog.h>

namespace emmy {

Dispatcher::Dispatcher(Transport& transport) : transport_(transport) {}

void Dispatcher::onRequest(const std::string& method, RequestHandler handler) {
    requestHandlers_[method] = std::move(handler);
}

void Dispatcher::onNotification(const std::string& method, NotificationHandler handler) {
    notificationHandlers_[method] = std::move(handler);
}

void Dispatcher::dispatch(const std::string& method, int id, const json& params, bool isRequest) {
    if (isRequest) {
        auto it = requestHandlers_.find(method);
        if (it != requestHandlers_.end()) {
            try {
                json result = it->second(id, params);
                transport_.sendResponse(id, result);
            } catch (const json::exception& e) {
                spdlog::error("JSON error in handler for '{}': {}", method, e.what());
                transport_.sendError(id, -32603, std::string("Internal error in '") + method + "': " + e.what());
            } catch (const std::exception& e) {
                spdlog::error("Error in handler for '{}': {}", method, e.what());
                transport_.sendError(id, -32603, std::string("Internal error in '") + method + "': " + e.what());
            }
        } else {
            spdlog::warn("No handler for request method: {}", method);
            transport_.sendError(id, -32601, "Method not found: " + method);
        }
    } else {
        auto it = notificationHandlers_.find(method);
        if (it != notificationHandlers_.end()) {
            try {
                it->second(params);
            } catch (const std::exception& e) {
                spdlog::error("Error in notification handler for '{}': {}", method, e.what());
            }
        } else {
            spdlog::debug("No handler for notification method: {}", method);
        }
    }
}

}  // namespace emmy

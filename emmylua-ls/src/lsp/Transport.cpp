#include "lsp/Transport.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace emmy {

Transport::Transport() {}

Transport::~Transport() {
    stop();
}

void Transport::start() {
#ifdef _WIN32
    // CRITICAL: set stdout to binary mode on Windows
    // Otherwise \n is translated to \r\n, breaking Content-Length framing
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    running_.store(true);
    readerThread_ = std::thread(&Transport::readerLoop, this);
}

void Transport::stop() {
    if (!running_.exchange(false)) return;

    // Wake up the reader thread and the receive() caller
    queueCv_.notify_all();

    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}

bool Transport::receive(IncomingMessage& msg) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    queueCv_.wait(lock, [this] {
        return !queue_.empty() || !running_.load();
    });

    if (!running_.load() && queue_.empty()) {
        return false;
    }

    msg = std::move(queue_.front());
    queue_.pop();
    return true;
}

void Transport::sendResponse(int id, const json& result) {
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    writeMessage(msg);
}

void Transport::sendError(int id, int code, const json& message) {
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
    writeMessage(msg);
}

void Transport::sendNotification(const std::string& method, const json& params) {
    json msg = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    writeMessage(msg);
}

void Transport::writeMessage(const json& msg) {
    std::string body = msg.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";

    std::lock_guard<std::mutex> lock(writeMutex_);
    std::cout.write(header.data(), header.size());
    std::cout.write(body.data(), body.size());
    std::cout.flush();
}

void Transport::readerLoop() {
    while (running_.load()) {
        // Read Content-Length header
        std::string headerLine;
        int contentLength = -1;

        while (running_.load()) {
            int ch = std::cin.get();
            if (ch == EOF) {
                running_.store(false);
                queueCv_.notify_all();
                return;
            }
            headerLine += static_cast<char>(ch);

            // Check if we have a complete header line ending with \r\n
            if (headerLine.size() >= 2 &&
                headerLine[headerLine.size() - 2] == '\r' &&
                headerLine[headerLine.size() - 1] == '\n') {

                std::string line = headerLine.substr(0, headerLine.size() - 2);

                // Empty line = end of headers
                if (line.empty()) {
                    break;
                }

                // Parse Content-Length
                const std::string prefix = "Content-Length: ";
                if (line.find(prefix) == 0) {
                    contentLength = std::stoi(line.substr(prefix.size()));
                }

                headerLine.clear();
            }
        }

        if (contentLength <= 0) {
            if (!running_.load()) return;
            spdlog::warn("Invalid Content-Length: {}", contentLength);
            continue;
        }

        // Read the JSON body
        std::string body;
        body.resize(contentLength);
        std::cin.read(body.data(), contentLength);

        if (std::cin.eof() || std::cin.fail()) {
            running_.store(false);
            queueCv_.notify_all();
            return;
        }

        // Parse JSON
        try {
            json parsed = json::parse(body);

            IncomingMessage msg;
            msg.body = parsed;

            if (parsed.contains("id")) {
                msg.isRequest = true;
                msg.id = parsed["id"].get<int>();
            } else {
                msg.isRequest = false;
            }

            if (parsed.contains("method")) {
                msg.method = parsed["method"].get<std::string>();
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queue_.push(std::move(msg));
            }
            queueCv_.notify_one();

        } catch (const json::parse_error& e) {
            spdlog::error("JSON parse error: {}", e.what());
        } catch (const std::exception& e) {
            spdlog::error("Error processing message: {}", e.what());
        }
    }
}

}  // namespace emmy

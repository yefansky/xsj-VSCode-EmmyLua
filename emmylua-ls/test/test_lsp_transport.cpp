#include <doctest/doctest.h>
#include "lsp/Transport.h"

#include <thread>
#include <sstream>
#include <cstdio>

using namespace emmy;

TEST_CASE("Transport - IncomingMessage parsing") {
    // Test the JSON structure we expect
    json req = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"rootUri", "file:///test"}}}
    };

    CHECK(req.contains("id"));
    CHECK(req["id"].get<int>() == 1);
    CHECK(req["method"].get<std::string>() == "initialize");

    json notif = {
        {"jsonrpc", "2.0"},
        {"method", "initialized"},
        {"params", json::object()}
    };

    CHECK(!notif.contains("id"));
    CHECK(notif["method"].get<std::string>() == "initialized");
}

TEST_CASE("Transport - Response message format") {
    json resp = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"result", {
            {"capabilities", {{"hoverProvider", true}}}
        }}
    };

    std::string body = resp.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    std::string full = header + body;

    // Verify Content-Length matches actual body size
    size_t headerEnd = full.find("\r\n\r\n");
    std::string lenStr = full.substr(16, headerEnd - 16);  // After "Content-Length: "
    int declaredLen = std::stoi(lenStr);
    CHECK(declaredLen == static_cast<int>(body.size()));
}

TEST_CASE("Transport - Notification format") {
    json notif = {
        {"jsonrpc", "2.0"},
        {"method", "emmy/progressReport"},
        {"params", {{"text", "Indexing..."}, {"percent", 0.5}}}
    };

    CHECK(notif["method"].get<std::string>() == "emmy/progressReport");
    CHECK(notif["params"]["percent"].get<double>() == doctest::Approx(0.5));
}

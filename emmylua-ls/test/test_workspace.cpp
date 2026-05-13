#include <doctest/doctest.h>
#include "parser/Lexer.h"
#include "parser/Parser.h"
#include "parser/AnnotationParser.h"
#include "index/SymbolIndex.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <thread>
#include <vector>
#include <atomic>

using namespace emmy;

// Helper: find lua files recursively
static std::vector<std::string> findLuaFiles(const std::string& root, int maxFiles = 0) {
    std::vector<std::string> files;
    try {
        for (auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                auto path = entry.path().string();
                if (path.size() >= 4) {
                    auto ext = path.substr(path.size() - 4);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".lua") {
                        files.push_back(path);
                        if (maxFiles > 0 && static_cast<int>(files.size()) >= maxFiles) break;
                    }
                }
            }
        }
    } catch (...) {}
    return files;
}

TEST_CASE("Workspace - parse specific files") {
    // Test files from Sword3 workspace
    std::vector<std::string> testFiles = {
        "K:/Sword3-products/trunk/client/ui/Config/Default/GlobalEventHandler.lua",
        "K:/Sword3-products/trunk/client/ui/Config/Default/ACC_JJCInfo.lua"
    };

    int totalErrors = 0;

    for (auto& filePath : testFiles) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            INFO("File not found: " << filePath);
            continue;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        Parser parser(source);
        auto chunk = parser.parse();

        if (!parser.errors().empty()) {
            INFO("Errors in " << filePath << ":");
            for (auto& err : parser.errors()) {
                INFO("  Line " << err.position.line << ": " << err.message);
                totalErrors++;
            }
        }
    }

    INFO("Total errors: " << totalErrors);
}

TEST_CASE("Workspace - test specific patterns") {
    // Test patterns that might cause errors

    SUBCASE("or expression") {
        std::string source = "local x = a == 1 or b == 2";
        Parser parser(source);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("table with member access") {
        std::string source = "local t = {obj.field, obj.method()}";
        Parser parser(source);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("complex if condition") {
        std::string source = "if a == 1 or b == 2 or c == 3 then\n  x = 1\nend";
        Parser parser(source);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("function with default params") {
        std::string source = "function foo(a, b)\n  a = a or 1\n  return a\nend";
        Parser parser(source);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }
}

TEST_CASE("Workspace - real client indexing benchmark") {
    std::string clientRoot = "K:/Sword3-products/trunk/client";
    if (!std::filesystem::exists(clientRoot)) {
        INFO("Client directory not found, skipping");
        return;
    }

    // Find ALL lua files
    auto luaFiles = findLuaFiles(clientRoot);
    spdlog::info("Benchmark: found {} lua files to index", luaFiles.size());

    SymbolIndex index;

    int indexed = 0, errors = 0, skipped = 0;
    auto totalStart = std::chrono::steady_clock::now();

    for (auto& filePath : luaFiles) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) { skipped++; continue; }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        if (content.empty()) { skipped++; continue; }

        // Skip very large files (>500KB)
        if (content.size() > 500000) { skipped++; continue; }

        std::string uri = "file:///" + filePath;
        // Normalize slashes
        for (auto& c : uri) if (c == '\\') c = '/';

        auto fileStart = std::chrono::steady_clock::now();
        try {
            index.updateDocument(uri, content);
            indexed++;
        } catch (...) {
            errors++;
        }
        auto fileEnd = std::chrono::steady_clock::now();
        auto fileMs = std::chrono::duration_cast<std::chrono::milliseconds>(fileEnd - fileStart).count();

        // Warn about slow files
        if (fileMs > 50) {
            MESSAGE("Slow parse: " << fileMs << "ms for " << filePath);
        }

        if (indexed % 5000 == 0 && indexed > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - totalStart).count();
            spdlog::info("Benchmark: progress {}/{} in {}ms", indexed, luaFiles.size(), elapsed);
        }
    }

    auto totalEnd = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();

    spdlog::info("=== Indexing Results ===");
    spdlog::info("Files indexed: {}", indexed);
    spdlog::info("Files skipped: {}", skipped);
    spdlog::info("Files with errors: {}", errors);
    spdlog::info("Total time: {}ms", totalMs);
    if (totalMs > 0) {
        spdlog::info("Rate: {:.0f} files/sec", indexed * 1000.0 / totalMs);
    }

    // Correctness checks
    auto uris = index.allUris();
    CHECK(uris.size() == indexed);

    // Symbol search should work
    auto results = index.searchSymbols("function", false);
    spdlog::info("Symbol search for 'function': {} results", results.size());

    // Performance check: should index all files in under 5 minutes
    CHECK(totalMs < 300000);
}

TEST_CASE("Workspace - parallel vs single-thread indexing") {
    std::string clientRoot = "K:/Sword3-products/trunk/client";
    if (!std::filesystem::exists(clientRoot)) {
        INFO("Client directory not found, skipping");
        return;
    }

    // Use first 5000 files for a fair comparison
    auto luaFiles = findLuaFiles(clientRoot, 5000);
    spdlog::info("Parallel benchmark: {} files", luaFiles.size());

    // --- Single-threaded ---
    SymbolIndex indexSingle;
    auto start = std::chrono::steady_clock::now();
    int singleIndexed = 0;
    for (auto& filePath : luaFiles) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) continue;
        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        if (content.empty() || content.size() > 500000) continue;
        std::string uri = "file:///" + filePath;
        for (auto& c : uri) if (c == '\\') c = '/';
        indexSingle.updateDocument(uri, content);
        singleIndexed++;
    }
    auto end = std::chrono::steady_clock::now();
    auto singleMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    spdlog::info("Single-thread: {} files in {}ms ({:.0f} files/sec)",
                  singleIndexed, singleMs, singleIndexed * 1000.0 / singleMs);

    // --- Multi-threaded (parse outside lock, store inside lock) ---
    SymbolIndex indexMulti;
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    if (numThreads > 8) numThreads = 8;

    start = std::chrono::steady_clock::now();
    std::atomic<int> multiIndexed{0};

    auto worker = [&](int threadIdx) {
        for (int idx = threadIdx; idx < static_cast<int>(luaFiles.size()); idx += static_cast<int>(numThreads)) {
            auto& filePath = luaFiles[idx];
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open()) continue;
            std::stringstream ss;
            ss << file.rdbuf();
            std::string content = ss.str();
            if (content.empty() || content.size() > 500000) continue;
            std::string uri = "file:///" + filePath;
            for (auto& c : uri) if (c == '\\') c = '/';

            // Parse without lock
            auto doc = indexMulti.parseDocument(uri, content);
            if (doc) {
                // Store with brief lock
                indexMulti.storeDocument(uri, std::move(doc));
                multiIndexed++;
            }
        }
    };

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < numThreads; i++) {
        threads.emplace_back(worker, static_cast<int>(i));
    }
    for (auto& t : threads) t.join();

    end = std::chrono::steady_clock::now();
    auto multiMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    spdlog::info("Multi-thread ({} threads): {} files in {}ms ({:.0f} files/sec)",
                  numThreads, multiIndexed.load(), multiMs, multiIndexed.load() * 1000.0 / multiMs);

    if (singleMs > 0 && multiMs > 0) {
        spdlog::info("Speedup: {:.2f}x", static_cast<double>(singleMs) / multiMs);
    }

    // Correctness: both should have same document count
    CHECK(indexMulti.allUris().size() == indexSingle.allUris().size());
}
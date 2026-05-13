#include "index/WorkspaceManager.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace emmy {

#ifdef _WIN32
// Convert UTF-8 to wide string for Windows filesystem API
static std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) return L"";
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wideLen);
    wide.pop_back();  // Remove null terminator
    return wide;
}

// Check if file exists using Windows API (handles UTF-8 paths with Chinese chars)
static bool fileExistsUtf8(const std::string& path) {
    std::wstring widePath = utf8ToWide(path);
    DWORD attrs = GetFileAttributesW(widePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}
#endif

WorkspaceManager::WorkspaceManager(SymbolIndex& index, Config& config, Transport& transport)
    : index_(index), config_(config), transport_(transport) {}

void WorkspaceManager::initialize(const std::string& stdFolder,
                                   const std::string& rootUri,
                                   const std::vector<std::string>& configFiles) {

    // Store workspace root
    if (!rootUri.empty()) {
        workspaceRoot_ = uriToPath(rootUri);
    } else {
    }

    // Load standard library definitions
    if (!stdFolder.empty()) {
        loadStdLibrary(stdFolder);
    }

    // Parse config files to get source roots
    for (auto& configFile : configFiles) {
        try {
            std::string path = uriToPath(configFile);
            std::string content = readFile(path);
            if (!content.empty()) {
                auto j = json::parse(content);
                auto emmyCfg = EmmyConfig::parse(j);
                for (auto& src : emmyCfg.source) {
                    if (!src.dir.empty()) {
                        // Make path absolute relative to config file location
                        auto configDir = std::filesystem::path(path).parent_path();
                        auto srcPath = configDir / src.dir;
                        sourceRoots_.push_back(srcPath.string());
                        for (auto& ex : src.exclude) {
                            excludePatterns_.push_back(ex);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse config file {}: {}", configFile, e.what());
        }
    }

    // Also add source roots from settings
    for (auto& root : config_.source_roots) {
        sourceRoots_.push_back(root);
    }

    // Always index workspace
    indexWorkspace(sourceRoots_);
}

void WorkspaceManager::loadStdLibrary(const std::string& /*stdFolder*/) {
    // TODO: std::filesystem hangs in background thread on network drives
    // For now, skip std library loading
    reportProgress("Ready", 1.0f);
}

void WorkspaceManager::indexFile(const std::string& uri, const std::string& source) {
    if (source.empty()) {
        return;  // Keep existing index for empty content
    }
    index_.updateDocument(uri, source);
}

void WorkspaceManager::removeFile(const std::string& uri) {
    index_.removeDocument(uri);
}

#ifdef _WIN32
// Recursively find .lua files using Windows API (with loop detection)
static void findLuaFiles(const std::string& dir, std::vector<std::string>& files, std::unordered_set<std::string>& visited) {
    // Normalize path to detect loops
    std::string normalizedDir = dir;
    std::transform(normalizedDir.begin(), normalizedDir.end(), normalizedDir.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Normalize slashes
    for (char& c : normalizedDir) {
        if (c == '/') c = '\\';
    }

    // Check if we've already visited this directory (loop detection)
    if (visited.count(normalizedDir)) {
        return;
    }
    visited.insert(normalizedDir);

    std::wstring wideDir = utf8ToWide(dir + "\\*");
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(wideDir.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring name = findData.cFileName;
        if (name == L"." || name == L"..") continue;

        // Skip reparse points (symbolic links, junctions)
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        int len = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8Name(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, &utf8Name[0], len, nullptr, nullptr);

        std::string fullPath = dir + "\\" + utf8Name;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            findLuaFiles(fullPath, files, visited);
        } else {
            std::string ext = utf8Name.size() >= 4 ? utf8Name.substr(utf8Name.size() - 4) : "";
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".lua") {
                files.push_back(fullPath);
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}
#endif

void WorkspaceManager::indexWorkspace(const std::vector<std::string>& /*sourceRoots*/) {
    if (workspaceRoot_.empty()) {
        spdlog::warn("indexWorkspace: workspaceRoot_ is empty");
        return;
    }

    auto totalStart = std::chrono::steady_clock::now();

    reportProgress("Indexing workspace...", 0.0f);

    std::vector<std::string> luaFiles;

#ifdef _WIN32
    std::unordered_set<std::string> visited;
    auto discoverStart = std::chrono::steady_clock::now();
    findLuaFiles(workspaceRoot_, luaFiles, visited);
    auto discoverEnd = std::chrono::steady_clock::now();
    auto discoverMs = std::chrono::duration_cast<std::chrono::milliseconds>(discoverEnd - discoverStart).count();
    spdlog::info("indexWorkspace: file discovery took {}ms, found {} files", discoverMs, luaFiles.size());
#else
    auto discoverStart = std::chrono::steady_clock::now();
    try {
        for (auto& entry : std::filesystem::recursive_directory_iterator(workspaceRoot_)) {
            if (entry.is_regular_file() && shouldIndex(entry.path().string())) {
                luaFiles.push_back(entry.path().string());
            }
        }
    } catch (...) {}
    auto discoverEnd = std::chrono::steady_clock::now();
    auto discoverMs = std::chrono::duration_cast<std::chrono::milliseconds>(discoverEnd - discoverStart).count();
    spdlog::info("indexWorkspace: file discovery took {}ms, found {} files", discoverMs, luaFiles.size());
#endif

    int totalFiles = static_cast<int>(luaFiles.size());

    // Determine thread count
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    // Cap at 8 threads to avoid excessive overhead
    if (numThreads > 8) numThreads = 8;

    spdlog::info("indexWorkspace: using {} threads", numThreads);

    std::atomic<int> indexedFiles{0};
    std::atomic<int> skippedFiles{0};
    std::atomic<int> errorFiles{0};

    // Worker function: each thread processes a slice of files
    auto worker = [&](int threadIdx) {
        for (int idx = threadIdx; idx < totalFiles; idx += static_cast<int>(numThreads)) {
            auto& filePath = luaFiles[idx];

            std::string content = readFile(filePath);
            if (content.empty()) {
                skippedFiles++;
                continue;
            }

            // Skip files larger than 500KB
            if (content.size() > 500000) {
                skippedFiles++;
                continue;
            }

            std::string uri = pathToUri(filePath);
            try {
                // Parse without lock
                auto doc = index_.parseDocument(uri, content);
                if (doc) {
                    // Store with brief lock
                    index_.storeDocument(uri, std::move(doc));
                    int count = ++indexedFiles;

                    // Progress reporting every 1000 files (only one thread will hit this)
                    if (count % 1000 == 0) {
                        float percent = static_cast<float>(count) / totalFiles;
                        reportProgress("Indexing: " + std::to_string(count) + "/" + std::to_string(totalFiles), percent);
                    }
                }
            } catch (...) {
                errorFiles++;
            }
        }
    };

    // Launch threads
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < numThreads; i++) {
        threads.emplace_back(worker, static_cast<int>(i));
    }
    for (auto& t : threads) {
        t.join();
    }

    auto totalEnd = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();
    spdlog::info("indexWorkspace: {} files indexed, {} skipped, {}ms",
                  indexedFiles.load(), skippedFiles.load(), totalMs);

    reportProgress("Ready", 1.0f);

    // Notify that workspace indexing is complete
    transport_.sendNotification("emmy/indexingDone", json::object());
}

bool WorkspaceManager::shouldIndex(const std::string& path) const {
    // Check if it's a .lua file
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".lua") return false;

    // Skip compiled Lua files (e.g., src.20260109080957-a14b2b64.lua)
    std::string filename = path.substr(path.find_last_of("/\\") + 1);
    if (filename.find(".lua.") != std::string::npos && filename.find(".lua") != filename.size() - 4) {
        // Pattern like "src.HASH.lua" - likely compiled
        return false;
    }

    // Check exclude patterns
    for (auto& pattern : excludePatterns_) {
        if (path.find(pattern) != std::string::npos) {
            return false;
        }
    }

    return true;
}

std::string WorkspaceManager::readFile(const std::string& path) {
#ifdef _WIN32
    // Use wide-char API for Windows to handle non-ASCII paths
    std::wstring widePath = utf8ToWide(path);
    HANDLE hFile = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return {};
    }

    std::string content(fileSize, '\0');
    DWORD bytesRead = 0;
    ReadFile(hFile, &content[0], fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    content.resize(bytesRead);
    return content;
#else
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
#endif
}

std::string WorkspaceManager::pathToUri(const std::string& path) {
    std::string uri = "file:///";
    for (char c : path) {
        if (c == '\\') {
            uri += '/';
        } else {
            uri += c;
        }
    }
    return uri;
}

std::string WorkspaceManager::uriToPath(const std::string& uri) {
    std::string path = uri;

    // Remove file:// prefix
    if (path.find("file:///") == 0) {
        path = path.substr(8);  // Remove "file:///"
    } else if (path.find("file://") == 0) {
        path = path.substr(7);  // Remove "file://"
    }

    // Decode percent-encoded characters (e.g., %3A -> :)
    std::string decoded;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '%' && i + 2 < path.size()) {
            char hex[3] = { path[i + 1], path[i + 2], 0 };
            char* end = nullptr;
            long val = std::strtol(hex, &end, 16);
            if (end == hex + 2) {
                decoded += static_cast<char>(val);
                i += 2;
            } else {
                decoded += path[i];
            }
        } else {
            decoded += path[i];
        }
    }
    path = decoded;

    // Convert forward slashes to backslash on Windows
#ifdef _WIN32
    for (char& c : path) {
        if (c == '/') c = '\\';
    }
    // Uppercase drive letter (k: -> K:)
    if (path.size() >= 2 && path[1] == ':' && path[0] >= 'a' && path[0] <= 'z') {
        path[0] = static_cast<char>(path[0] - 'a' + 'A');
    }
#endif

    return path;
}

std::string WorkspaceManager::resolveModulePath(const std::string& modulePath) const {
    if (modulePath.empty() || workspaceRoot_.empty()) return {};

    // Normalize path (replace dots with slashes for module notation)
    std::string normalizedPath = modulePath;
    if (normalizedPath.find(".lua") == std::string::npos) {
        for (char& c : normalizedPath) {
            if (c == '.') c = '/';
        }
    }

    // Normalize separators to match workspace root
#ifdef _WIN32
    for (char& c : normalizedPath) {
        if (c == '/') c = '\\';
    }
    std::string root = workspaceRoot_;
    for (char& c : root) {
        if (c == '/') c = '\\';
    }
#else
    std::string root = workspaceRoot_;
#endif

    // File extensions to try
    std::vector<std::string> extensions = {"", ".lua", ".lua.txt", ".lua.bytes"};

    // Build list of search roots
    std::vector<std::string> roots = {root};
    for (auto& sr : sourceRoots_) {
        std::string normalized = sr;
#ifdef _WIN32
        for (char& c : normalized) if (c == '/') c = '\\';
#endif
        roots.push_back(normalized);
    }

    // Also search in immediate subdirectories of workspace root
    // (e.g., client/, server/ without hardcoding names)
    try {
        for (auto& entry : std::filesystem::directory_iterator(root)) {
            if (entry.is_directory()) {
                roots.push_back(entry.path().string());
            }
        }
    } catch (...) {}

    // Try each root with the normalized path
    for (auto& searchRoot : roots) {
        for (auto& ext : extensions) {
#ifdef _WIN32
            std::string fullPath = searchRoot + "\\" + normalizedPath + ext;
#else
            std::string fullPath = searchRoot + "/" + normalizedPath + ext;
#endif

#ifdef _WIN32
            if (fileExistsUtf8(fullPath)) {
                return fullPath;
            }
#else
            try {
                if (std::filesystem::exists(fullPath)) {
                    return std::filesystem::absolute(fullPath).string();
                }
            } catch (...) {}
#endif
        }

        // Try stripping path components
        std::string remaining = normalizedPath;
        while (true) {
            size_t pos = remaining.find_first_of("/\\");
            if (pos == std::string::npos) break;
            remaining = remaining.substr(pos + 1);

            for (auto& ext : extensions) {
#ifdef _WIN32
                std::string fullPath = searchRoot + "\\" + remaining + ext;
                if (fileExistsUtf8(fullPath)) {
                    return fullPath;
                }
#else
                std::string fullPath = searchRoot + "/" + remaining + ext;
                try {
                    if (std::filesystem::exists(fullPath)) {
                        return std::filesystem::absolute(fullPath).string();
                    }
                } catch (...) {}
#endif
            }
        }
    }

    return {};
}

void WorkspaceManager::reportProgress(const std::string& text, float percent) {
    if (progressCb_) {
        progressCb_(text, percent);
    }

    // Also send via LSP notification
    transport_.sendNotification("emmy/progressReport", {
        {"text", text},
        {"percent", percent}
    });
}

}  // namespace emmy

#pragma once

#include "index/SymbolIndex.h"
#include "config/Config.h"
#include "lsp/Transport.h"
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <thread>
#include <atomic>

namespace emmy {

class WorkspaceManager {
public:
    using ProgressCallback = std::function<void(const std::string& text, float percent)>;

    WorkspaceManager(SymbolIndex& index, Config& config, Transport& transport);

    // Initialize with stdFolder, rootUri and config files from initializationOptions
    void initialize(const std::string& stdFolder,
                    const std::string& rootUri,
                    const std::vector<std::string>& configFiles);

    // Index a single file
    void indexFile(const std::string& uri, const std::string& source);

    // Remove a file from index
    void removeFile(const std::string& uri);

    // Index all Lua files in the workspace
    void indexWorkspace(const std::vector<std::string>& sourceRoots);

    // Check if a file should be indexed
    bool shouldIndex(const std::string& path) const;

    // Set progress callback
    void setProgressCallback(ProgressCallback cb) { progressCb_ = std::move(cb); }

    // Read file content
    static std::string readFile(const std::string& path);

    // Convert file path to URI
    static std::string pathToUri(const std::string& path);

    // Convert URI to file path
    static std::string uriToPath(const std::string& uri);

    // Resolve a module path (like "scripts/Map/file.lua") to absolute path
    // Searches in workspace roots and source roots
    std::string resolveModulePath(const std::string& modulePath) const;

    // Get workspace root path
    const std::string& workspaceRoot() const { return workspaceRoot_; }

private:
    void indexDirectory(const std::string& dir, const std::vector<std::string>& excludes);
    void reportProgress(const std::string& text, float percent);
    void loadStdLibrary(const std::string& stdFolder);

    SymbolIndex& index_;
    Config& config_;
    Transport& transport_;
    ProgressCallback progressCb_;

    std::string workspaceRoot_;
    std::vector<std::string> sourceRoots_;
    std::vector<std::string> excludePatterns_;
};

}  // namespace emmy

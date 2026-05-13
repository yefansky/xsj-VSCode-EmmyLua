#pragma once

#include "index/DocumentIndex.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <shared_mutex>

namespace emmy {

// Workspace-wide symbol index
class SymbolIndex {
public:
    // Add or update a document
    void updateDocument(const std::string& uri, const std::string& source);

    // Parse a document without storing (for parallel pre-processing)
    // Returns a parsed DocumentIndex that can be stored later with storeDocument()
    std::unique_ptr<DocumentIndex> parseDocument(const std::string& uri, const std::string& source);

    // Store a pre-parsed document (fast, holds lock briefly)
    void storeDocument(const std::string& uri, std::unique_ptr<DocumentIndex> doc);

    // Remove a document
    void removeDocument(const std::string& uri);

    // Find symbol definition across workspace
    struct SymbolLocation {
        std::string uri;
        std::string name;
        SymbolScope scope;
        SourcePosition position;
        bool isFunction;
        bool isClass;
    };

    // Find definition of symbol by name
    std::vector<SymbolLocation> findDefinition(const std::string& name, bool caseSensitive = true);

    // Find all references to a symbol
    std::vector<SymbolLocation> findReferences(const std::string& name, bool caseSensitive = true);

    // Search symbols by name pattern (for workspace symbol)
    std::vector<SymbolLocation> searchSymbols(const std::string& query, bool caseSensitive = false);

    // Find class by name
    ClassDef* findClass(const std::string& name);

    // Get all classes
    std::vector<ClassDef> allClasses();

    // Get document index for URI
    DocumentIndex* getDocument(const std::string& uri);

    // Get all URIs
    std::vector<std::string> allUris() const;

    // Get cached function params map (function name -> parameter names)
    std::unordered_map<std::string, std::vector<std::string>> getFuncParams() const;

    // Clear all
    void clear();

    // Set case sensitivity for searches
    void setCaseSensitive(bool sensitive) { caseSensitive_ = sensitive; }
    bool isCaseSensitive() const { return caseSensitive_; }

    // Set module pattern support
    void setSupportModulePattern(bool enable) { supportModulePattern_ = enable; }

private:
    // Normalize URI for consistent lookups (decode %xx, uppercase drive letter)
    static std::string normalizeUri(const std::string& uri);

    std::string normalizeName(const std::string& name) const;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<DocumentIndex>> documents_;
    bool caseSensitive_ = false;
    bool supportModulePattern_ = false;
};

}  // namespace emmy

#include "index/SymbolIndex.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace emmy {

static size_t quickHash(const std::string& str) {
    size_t hash = 0;
    for (unsigned char c : str) {
        hash = hash * 31 + c;
    }
    return hash;
}

// Normalize URI: decode %xx, uppercase drive letter
std::string SymbolIndex::normalizeUri(const std::string& uri) {
    std::string result;

    // Decode percent-encoded characters
    for (size_t i = 0; i < uri.size(); i++) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            char hex[3] = {uri[i + 1], uri[i + 2], 0};
            char* end = nullptr;
            long val = std::strtol(hex, &end, 16);
            if (end == hex + 2) {
                result += static_cast<char>(val);
                i += 2;
            } else {
                result += uri[i];
            }
        } else {
            result += uri[i];
        }
    }

    // Uppercase drive letter: file:///x: → file:///X:
    if (result.size() >= 10 && result.substr(0, 8) == "file:///" &&
        result[9] == ':' && result[8] >= 'a' && result[8] <= 'z') {
        result[8] = static_cast<char>(result[8] - 'a' + 'A');
    }

    return result;
}

void SymbolIndex::updateDocument(const std::string& uri, const std::string& source) {
    std::string key = normalizeUri(uri);

    // Skip if content is identical to what's already indexed
    {
        std::shared_lock lock(mutex_);
        auto it = documents_.find(key);
        if (it != documents_.end() &&
            source.size() == it->second->source().size() &&
            quickHash(source) == it->second->contentHash()) {
            return;
        }
    }

    auto doc = parseDocument(key, source);
    if (doc) {
        storeDocument(key, std::move(doc));
    }
}

std::unique_ptr<DocumentIndex> SymbolIndex::parseDocument(const std::string& uri, const std::string& source) {
    if (source.empty()) return nullptr;

    auto doc = std::make_unique<DocumentIndex>();
    doc->setSupportModulePattern(supportModulePattern_);

    auto start = std::chrono::steady_clock::now();
    doc->update(uri, source);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (ms > 50) {
        spdlog::warn("updateDocument: slow parse took {}ms for {}", ms, uri);
    }

    return doc;
}

void SymbolIndex::storeDocument(const std::string& uri, std::unique_ptr<DocumentIndex> doc) {
    std::unique_lock lock(mutex_);
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        it->second = std::move(doc);
    } else {
        documents_.emplace(uri, std::move(doc));
    }
}

void SymbolIndex::removeDocument(const std::string& uri) {
    std::unique_lock lock(mutex_);
    documents_.erase(normalizeUri(uri));
}

std::vector<SymbolIndex::SymbolLocation> SymbolIndex::findDefinition(
    const std::string& name, bool caseSensitive) {

    std::shared_lock lock(mutex_);
    std::vector<SymbolLocation> results;
    std::string searchName = caseSensitive ? name : normalizeName(name);

    for (auto& [uri, doc] : documents_) {
        for (auto& sym : doc->allSymbols()) {
            std::string symName = caseSensitive ? sym.name : normalizeName(sym.name);
            if (symName == searchName) {
                results.push_back({
                    uri, sym.name, sym.scope,
                    sym.namePos, sym.isFunction, sym.isClass
                });
            }
        }
    }

    return results;
}

std::vector<SymbolIndex::SymbolLocation> SymbolIndex::findReferences(
    const std::string& name, bool caseSensitive) {

    std::shared_lock lock(mutex_);
    std::vector<SymbolLocation> results;

    // Search all documents for all occurrences of the name (definitions + usages)
    for (auto& [uri, doc] : documents_) {
        const std::string& source = doc->source();
        size_t pos = 0;

        if (caseSensitive) {
            while ((pos = source.find(name, pos)) != std::string::npos) {
                bool isStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(source[pos - 1])) && source[pos - 1] != '_'));
                bool isEnd = (pos + name.size() >= source.size() || (!std::isalnum(static_cast<unsigned char>(source[pos + name.size()])) && source[pos + name.size()] != '_'));

                if (isStart && isEnd) {
                    int refLine = 1, refCol = 1;
                    for (size_t i = 0; i < pos; i++) {
                        if (source[i] == '\n') { refLine++; refCol = 1; }
                        else { refCol++; }
                    }

                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.name = name;
                    loc.scope = SymbolScope::Global;
                    loc.position = {refLine, refCol, static_cast<int>(pos)};
                    loc.isFunction = false;
                    loc.isClass = false;
                    results.push_back(std::move(loc));
                }

                pos += name.size();
            }
        } else {
            // Case-insensitive search
            std::string normalizedSource = normalizeName(source);
            std::string normalizedName = normalizeName(name);
            while ((pos = normalizedSource.find(normalizedName, pos)) != std::string::npos) {
                bool isStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(normalizedSource[pos - 1])) && normalizedSource[pos - 1] != '_'));
                bool isEnd = (pos + normalizedName.size() >= normalizedSource.size() || (!std::isalnum(static_cast<unsigned char>(normalizedSource[pos + normalizedName.size()])) && normalizedSource[pos + normalizedName.size()] != '_'));

                if (isStart && isEnd) {
                    int refLine = 1, refCol = 1;
                    for (size_t i = 0; i < pos; i++) {
                        if (source[i] == '\n') { refLine++; refCol = 1; }
                        else { refCol++; }
                    }

                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.name = name;
                    loc.scope = SymbolScope::Global;
                    loc.position = {refLine, refCol, static_cast<int>(pos)};
                    loc.isFunction = false;
                    loc.isClass = false;
                    results.push_back(std::move(loc));
                }

                pos += normalizedName.size();
            }
        }
    }

    return results;
}

std::vector<SymbolIndex::SymbolLocation> SymbolIndex::searchSymbols(
    const std::string& query, bool caseSensitive) {

    std::shared_lock lock(mutex_);
    std::vector<SymbolLocation> results;
    std::string searchQuery = caseSensitive ? query : normalizeName(query);

    for (auto& [uri, doc] : documents_) {
        for (auto& sym : doc->allSymbols()) {
            std::string symName = caseSensitive ? sym.name : normalizeName(sym.name);

            // Check if symbol name contains the query
            if (symName.find(searchQuery) != std::string::npos) {
                results.push_back({
                    uri, sym.name, sym.scope,
                    sym.namePos, sym.isFunction, sym.isClass
                });
            }
        }

        // Also search class names
        for (auto& cls : doc->classes()) {
            std::string clsName = caseSensitive ? cls.name : normalizeName(cls.name);
            if (clsName.find(searchQuery) != std::string::npos) {
                results.push_back({
                    uri, cls.name, SymbolScope::Global,
                    {}, false, true
                });
            }
        }
    }

    // Limit results
    if (results.size() > 100) {
        results.resize(100);
    }

    return results;
}

ClassDef* SymbolIndex::findClass(const std::string& name) {
    std::shared_lock lock(mutex_);

    for (auto& [uri, doc] : documents_) {
        auto* cls = doc->findClass(name);
        if (cls) return cls;
    }
    return nullptr;
}

std::vector<ClassDef> SymbolIndex::allClasses() {
    std::shared_lock lock(mutex_);
    std::vector<ClassDef> result;

    for (auto& [uri, doc] : documents_) {
        for (auto& cls : doc->classes()) {
            result.push_back(cls);
        }
    }
    return result;
}

DocumentIndex* SymbolIndex::getDocument(const std::string& uri) {
    std::shared_lock lock(mutex_);
    auto it = documents_.find(normalizeUri(uri));
    return it != documents_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> SymbolIndex::allUris() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> uris;
    uris.reserve(documents_.size());
    for (auto& [uri, _] : documents_) {
        uris.push_back(uri);
    }
    return uris;
}

std::unordered_map<std::string, std::vector<std::string>> SymbolIndex::getFuncParams() const {
    std::shared_lock lock(mutex_);
    std::unordered_map<std::string, std::vector<std::string>> result;
    for (auto& [uri, doc] : documents_) {
        for (auto& [name, params] : doc->funcParams()) {
            result.emplace(name, params);  // First definition wins
        }
    }
    return result;
}

void SymbolIndex::clear() {
    std::unique_lock lock(mutex_);
    documents_.clear();
}

std::string SymbolIndex::normalizeName(const std::string& name) const {
    std::string result = name;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

}  // namespace emmy

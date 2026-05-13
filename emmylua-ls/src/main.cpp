#include "lsp/Transport.h"
#include "lsp/Dispatcher.h"
#include "lsp/LspTypes.h"
#include "index/SymbolIndex.h"
#include "index/WorkspaceManager.h"
#include "config/Config.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <memory>
#include <chrono>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace emmy;

static void printUsage(const char* argv0) {
    fprintf(stderr, "Usage: %s [--stdio] [--tcp <port>]\n", argv0);
}

static std::unique_ptr<Transport> createTcpTransport(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int sock = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0) {
        spdlog::error("Failed to create socket");
        return nullptr;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("Failed to connect to localhost:{}", port);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return nullptr;
    }

    spdlog::info("Connected to localhost:{}", port);
#ifdef _WIN32
    _setmode(sock, _O_BINARY);
    _dup2(sock, _fileno(stdin));
    _dup2(sock, _fileno(stdout));
#else
    dup2(sock, fileno(stdin));
    dup2(sock, fileno(stdout));
    close(sock);
#endif
    return std::make_unique<Transport>();
}

int main(int argc, char* argv[]) {
    bool useTcp = false;
    int tcpPort = 5007;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--stdio") useTcp = false;
        else if (arg == "--tcp") {
            useTcp = true;
            if (i + 1 < argc) tcpPort = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    // Create stderr-only logger (stdout is reserved for LSP protocol)
    auto stderr_sink = std::make_shared<spdlog::sinks::stderr_sink_st>();
    auto logger = std::make_shared<spdlog::logger>("emmylua-ls", stderr_sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[emmylua-ls] [%l] %v");
    spdlog::info("EmmyLua Language Server starting (mode: {})", useTcp ? "tcp" : "stdio");

    std::unique_ptr<Transport> transport;
    if (useTcp) {
        transport = createTcpTransport(tcpPort);
        if (!transport) return 1;
    } else {
        transport = std::make_unique<Transport>();
    }

    // Create global state
    Config config;
    SymbolIndex symbolIndex;
    WorkspaceManager workspaceMgr(symbolIndex, config, *transport);
    Dispatcher dispatcher(*transport);

    // Store initialization data for later use
    std::string stdFolder;
    std::string rootUri;
    std::vector<std::string> configFiles;

    // Register LSP handlers
    dispatcher.onRequest("initialize", [&](int /*id*/, const json& params) -> json {
        spdlog::info("Initialize request received");
        spdlog::info("Initialize params keys: {}", [&]() {
            std::string keys;
            if (params.is_object()) {
                for (auto& [key, _] : params.items()) {
                    if (!keys.empty()) keys += ", ";
                    keys += key;
                }
            }
            return keys;
        }());

        if (params.contains("clientInfo") && params["clientInfo"].is_object()) {
            auto ci = params["clientInfo"];
            spdlog::info("Client: {} {}", ci.value("name", "unknown"), ci.value("version", ""));
        }
        if (params.contains("rootUri") && params["rootUri"].is_string()) {
            rootUri = params["rootUri"].get<std::string>();
            spdlog::info("Root URI: {}", rootUri);
        } else {
            spdlog::info("Root URI: <null> (single file mode)");
        }
        if (params.contains("initializationOptions") && params["initializationOptions"].is_object()) {
            auto opts = params["initializationOptions"];
            if (opts.contains("stdFolder") && opts["stdFolder"].is_string()) {
                stdFolder = opts["stdFolder"].get<std::string>();
                spdlog::info("stdFolder: {}", stdFolder);
            }
            if (opts.contains("configFiles") && opts["configFiles"].is_array()) {
                for (auto& cf : opts["configFiles"]) {
                    if (cf.is_object() && cf.contains("uri") && cf["uri"].is_string()) {
                        configFiles.push_back(cf["uri"].get<std::string>());
                    }
                }
            }
        } else {
            spdlog::warn("No initializationOptions in initialize request");
        }

        return json{
            {"capabilities", {
                {"textDocumentSync", {{"openClose", true}, {"change", 1}, {"save", true}}},
                {"completionProvider", {{"triggerCharacters", {":", ".", "(", ",", "<"}}, {"resolveProvider", false}}},
                {"hoverProvider", true},
                {"definitionProvider", true},
                {"referencesProvider", true},
                {"documentSymbolProvider", true},
                {"workspaceSymbolProvider", true},
                {"codeActionProvider", true},
                {"renameProvider", true},
                {"signatureHelpProvider", {{"triggerCharacters", {"(", ",", ")"}}}},
                {"documentHighlightProvider", true},
                {"foldingRangeProvider", true},
                {"documentFormattingProvider", true},
                {"documentRangeFormattingProvider", true},
                {"codeLensProvider", {{"resolveProvider", false}}},
                {"inlayHintProvider", {{"resolveProvider", false}}}
            }},
            {"serverInfo", {{"name", "emmylua-ls"}, {"version", "0.1.0"}}}
      };
    });

    dispatcher.onNotification("initialized", [&](const json&) {
        spdlog::info("Client initialized, starting workspace indexing...");
        std::thread([&workspaceMgr, stdFolder, rootUri, configFiles]() {
            try {
                workspaceMgr.initialize(stdFolder, rootUri, configFiles);
            } catch (const std::exception& e) {
                spdlog::error("Indexing thread error: {}", e.what());
            }
        }).detach();
    });

    dispatcher.onRequest("shutdown", [](int, const json&) -> json {
        spdlog::info("Shutdown requested");
        return json();
    });

    dispatcher.onNotification("exit", [](const json&) {
        spdlog::info("Exit requested");
    });

    // Helper function to publish diagnostics for a document
    auto publishDiagnostics = [&](const std::string& uri) {
        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return;

        json diagnostics = json::array();

        // Report parse errors
        for (auto& err : doc->parseErrors()) {
            int line = err.first - 1;  // Convert to 0-based
            diagnostics.push_back({
                {"range", {
                    {"start", {{"line", line}, {"character", 0}}},
                    {"end", {{"line", line}, {"character", 1000}}}
                }},
                {"severity", 1},  // Error
                {"message", err.second},
                {"source", "emmylua"}
            });
        }

        // Limit to first 100 errors to avoid overwhelming the client
        if (diagnostics.size() > 100) {
            json limited = json::array();
            for (int i = 0; i < 100; i++) {
                limited.push_back(diagnostics[i]);
            }
            diagnostics = limited;
        }

        // Send diagnostics to client
        transport->sendNotification("textDocument/publishDiagnostics", {
            {"uri", uri},
            {"diagnostics", diagnostics}
        });
    };

    // Document lifecycle
    dispatcher.onNotification("textDocument/didOpen", [&](const json& params) {
        if (!params.contains("textDocument") || !params["textDocument"].is_object()) return;
        auto doc = params["textDocument"];
        if (!doc.contains("uri") || !doc["uri"].is_string()) return;
        if (!doc.contains("text") || !doc["text"].is_string()) return;
        std::string uri = doc["uri"].get<std::string>();
        std::string text = doc["text"].get<std::string>();

        workspaceMgr.indexFile(uri, text);
        publishDiagnostics(uri);

        // Index dependencies (Include/require calls)
        std::string source = text;
        size_t pos = 0;
        while ((pos = source.find("Include(\"", pos)) != std::string::npos ||
               (pos = source.find("require(\"", pos)) != std::string::npos) {
            // Find the function name
            size_t funcStart = pos;
            size_t funcEnd = source.find('(', pos);
            std::string funcName = source.substr(funcStart, funcEnd - funcStart);

            // Skip if not Include or require
            if (funcName != "Include" && funcName != "require") {
                pos++;
                continue;
            }

            // Find the string argument
            size_t strStart = source.find('"', funcEnd);
            if (strStart == std::string::npos) {
                strStart = source.find('\'', funcEnd);
            }
            if (strStart == std::string::npos) { pos++; continue; }

            char quote = source[strStart];
            size_t strEnd = source.find(quote, strStart + 1);
            if (strEnd == std::string::npos) { pos++; continue; }

            std::string modulePath = source.substr(strStart + 1, strEnd - strStart - 1);

            // Resolve and index the dependency
            std::string depPath = workspaceMgr.resolveModulePath(modulePath);
            if (!depPath.empty()) {
                std::string depContent = workspaceMgr.readFile(depPath);
                if (!depContent.empty()) {
                    std::string depUri = WorkspaceManager::pathToUri(depPath);
                    workspaceMgr.indexFile(depUri, depContent);
                }
            }

            pos = strEnd + 1;
        }
    });

    dispatcher.onNotification("textDocument/didChange", [&](const json& params) {
        if (!params.contains("textDocument") || !params["textDocument"].is_object()) return;
        auto doc = params["textDocument"];
        if (!doc.contains("uri") || !doc["uri"].is_string()) return;
        std::string uri = doc["uri"].get<std::string>();
        if (params.contains("contentChanges") && !params["contentChanges"].empty()) {
            auto change = params["contentChanges"][0];
            if (!change.contains("text")) return;

            std::string newText = change["text"].get<std::string>();
            std::string fullText;

            if (change.contains("range")) {
                // Incremental edit: apply range replacement to existing source
                auto* existing = symbolIndex.getDocument(uri);
                if (existing && !existing->source().empty()) {
                    std::string src = existing->source();

                    // Convert LSP range to byte offsets
                    auto startPos = change["range"]["start"];
                    auto endPos = change["range"]["end"];
                    int startLine = startPos["line"].get<int>();
                    int startChar = startPos["character"].get<int>();
                    int endLine = endPos["line"].get<int>();
                    int endChar = endPos["character"].get<int>();

                    // Find byte offset for start position
                    size_t startOffset = 0;
                    int curLine = 0, curChar = 0;
                    for (size_t i = 0; i < src.size(); i++) {
                        if (curLine == startLine && curChar == startChar) {
                            startOffset = i;
                            break;
                        }
                        if (src[i] == '\n') { curLine++; curChar = 0; }
                        else { curChar++; }
                    }

                    // Find byte offset for end position
                    size_t endOffset = src.size();
                    curLine = 0; curChar = 0;
                    for (size_t i = 0; i < src.size(); i++) {
                        if (curLine == endLine && curChar == endChar) {
                            endOffset = i;
                            break;
                        }
                        if (src[i] == '\n') { curLine++; curChar = 0; }
                        else { curChar++; }
                    }

                    // Apply edit
                    if (startOffset <= endOffset && endOffset <= src.size()) {
                        fullText = src.substr(0, startOffset) + newText + src.substr(endOffset);
                    } else {
                        fullText = src;  // Fallback: keep existing
                    }
                } else {
                    fullText = newText;  // No existing doc, use new text as-is
                }
            } else {
                // Full text sync
                fullText = newText;
            }

            workspaceMgr.indexFile(uri, fullText);
            publishDiagnostics(uri);
        }
    });

    dispatcher.onNotification("textDocument/didClose", [&](const json& params) {
        if (!params.contains("textDocument") || !params["textDocument"].is_object()) return;
        auto doc = params["textDocument"];
        if (!doc.contains("uri") || !doc["uri"].is_string()) return;
        std::string uri = doc["uri"].get<std::string>();
        workspaceMgr.removeFile(uri);

        // Clear diagnostics for closed file
        transport->sendNotification("textDocument/publishDiagnostics", {
            {"uri", uri},
            {"diagnostics", json::array()}
        });
    });

    dispatcher.onNotification("textDocument/didSave", [](const json&) {});

    // Configuration
    dispatcher.onNotification("workspace/didChangeConfiguration", [&](const json& params) {
        if (params.contains("settings")) {
            config.update(params["settings"]);
            symbolIndex.setCaseSensitive(config.completion_case_sensitive);
            symbolIndex.setSupportModulePattern(config.support_module_pattern);
        }
    });

    dispatcher.onNotification("workspace/didChangeWatchedFiles", [&](const json& params) {
        if (!params.contains("changes")) return;
        for (auto& change : params["changes"]) {
            std::string uri = change["uri"].get<std::string>();
            int type = change["type"].get<int>();  // 1=created, 2=changed, 3=deleted
            if (type == 3) {  // Deleted
                workspaceMgr.removeFile(uri);
                // Clear diagnostics for deleted file
                transport->sendNotification("textDocument/publishDiagnostics", {
                    {"uri", uri},
                    {"diagnostics", json::array()}
                });
            }
        }
    });

    // LSP feature handlers using the symbol index
    dispatcher.onRequest("textDocument/documentSymbol", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json::array();

        json result = json::array();
        for (auto& sym : doc->allSymbols()) {
            json j = {
                {"name", sym.name},
                {"kind", sym.isFunction ? 12 : 13},  // Function or Variable
                {"range", {
                    {"start", {{"line", sym.range.start.line - 1}, {"character", sym.range.start.column - 1}}},
                    {"end", {{"line", sym.range.end.line - 1}, {"character", sym.range.end.column - 1}}}
                }},
                {"selectionRange", {
                    {"start", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1}}},
                    {"end", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1 + static_cast<int>(sym.name.size())}}}
                }}
            };
            result.push_back(j);
        }
        return result;
    });

    dispatcher.onRequest("textDocument/hover", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>() + 1;
        int character = params["position"]["character"].get<int>() + 1;

        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json();

        // Extract word at cursor
        std::string source = doc->source();
        std::string word;

        // Find line start
        int currentLine = 1;
        size_t lineStart = 0;
        for (size_t i = 0; i < source.size(); i++) {
            if (source[i] == '\n') {
                currentLine++;
                if (currentLine == line) lineStart = i + 1;
                if (currentLine > line) break;
            }
        }

        // Extract word
        if (line <= currentLine) {
            size_t pos = lineStart + character - 1;
            if (pos < source.size()) {
                size_t start = pos, end = pos;
                while (start > lineStart && (std::isalnum(static_cast<unsigned char>(source[start - 1])) || source[start - 1] == '_'))
                    start--;
                while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_'))
                    end++;
                if (end > start) word = source.substr(start, end - start);
            }
        }

        if (word.empty()) return json();

        // Find symbol
        SymbolDef* sym = nullptr;
        for (auto& s : doc->allSymbols()) {
            if (s.name == word) { sym = &s; break; }
        }

        // Build hover content
        std::string content;

        if (sym) {
            if (sym->isFunction) {
                content = "function " + word;

                // Add parameter info
                std::vector<std::string> paramNames;
                for (auto& ps : doc->allSymbols()) {
                    if (ps.scope == SymbolScope::Parameter &&
                        ps.range.start.line >= sym->range.start.line &&
                        ps.range.start.line <= sym->range.start.line + 3) {
                        paramNames.push_back(ps.name);
                    }
                }
                if (!paramNames.empty()) {
                    content += "(";
                    for (size_t i = 0; i < paramNames.size(); i++) {
                        if (i > 0) content += ", ";
                        content += paramNames[i];
                    }
                    content += ")";
                }
            } else if (sym->scope == SymbolScope::Local) {
                content = "local " + word;
            } else if (sym->scope == SymbolScope::Global) {
                content = word;
            } else if (sym->scope == SymbolScope::Parameter) {
                content = "param " + word;
            } else {
                content = word;
            }
        } else {
            content = word;
        }

        return json{
            {"contents", {{"kind", "markdown"}, {"value", "```lua\n" + content + "\n```"}}}
        };
    });

    dispatcher.onRequest("textDocument/definition", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>() + 1;
        int character = params["position"]["character"].get<int>() + 1;


        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) {
            return json::array();
        }

        std::string source = doc->source();

        // Find cursor position in source
        int currentLine = 1;
        size_t lineStart = 0;
        for (size_t i = 0; i < source.size(); i++) {
            if (source[i] == '\n') {
                currentLine++;
                if (currentLine == line) lineStart = i + 1;
                if (currentLine > line) break;
            }
        }

        size_t cursorPos = lineStart + character - 1;

        // Check if cursor is inside a string literal
        if (cursorPos < source.size()) {
            // Find if we're inside a quoted string
            bool inString = false;
            char quoteChar = 0;
            size_t stringStart = 0;
            size_t stringEnd = 0;

            // Scan backwards to find the opening quote
            for (size_t i = cursorPos; i > 0; i--) {
                char c = source[i - 1];
                if ((c == '"' || c == '\'') && (i == 1 || source[i - 2] != '\\')) {
                    inString = true;
                    quoteChar = c;
                    stringStart = i;
                    break;
                }
                if (c == '\n') break;  // String can't span lines (for simple strings)
            }

            // Scan forwards to find the closing quote
            if (inString) {
                for (size_t i = cursorPos; i < source.size(); i++) {
                    if (source[i] == quoteChar && (i == cursorPos || source[i - 1] != '\\')) {
                        stringEnd = i;
                        break;
                    }
                    if (source[i] == '\n') break;
                }
            }

            if (inString && stringEnd > stringStart) {
                // Extract the string content
                std::string stringContent = source.substr(stringStart, stringEnd - stringStart);

                // Check if this string is an argument to a require-like function
                // Look backwards from stringStart for the opening quote, then for funcName(
                // stringStart is the position after the opening quote
                size_t searchPos = stringStart;
                // Skip the opening quote
                if (searchPos > 0 && (source[searchPos - 1] == '"' || source[searchPos - 1] == '\'')) {
                    searchPos--;
                }
                // Skip whitespace before the quote
                while (searchPos > 0 && (source[searchPos - 1] == ' ' || source[searchPos - 1] == '\t')) {
                    searchPos--;
                }


                if (searchPos > 0 && source[searchPos - 1] == '(') {
                    // Found '(', look for function name before it
                    size_t parenPos = searchPos - 1;
                    size_t funcEnd = parenPos;
                    size_t funcStart = funcEnd;
                    while (funcStart > 0 && (std::isalnum(static_cast<unsigned char>(source[funcStart - 1])) || source[funcStart - 1] == '_' || source[funcStart - 1] == '.')) {
                        funcStart--;
                    }

                    if (funcStart < funcEnd) {
                        std::string funcName = source.substr(funcStart, funcEnd - funcStart);

                        // Check if this function is in the require-like list
                        std::string requireFuncs = config.require_like_functions;

                        // Parse comma/semicolon-separated list
                        size_t pos = 0;
                        while (pos < requireFuncs.size()) {
                            size_t sepPos = requireFuncs.find_first_of(",;", pos);
                            std::string func = requireFuncs.substr(pos, sepPos == std::string::npos ? std::string::npos : sepPos - pos);
                            // Trim whitespace
                            while (!func.empty() && func.front() == ' ') func.erase(0, 1);
                            while (!func.empty() && func.back() == ' ') func.pop_back();


                            if (func == funcName) {
                                // This is a require-like function call
                                // Resolve the module path
                                std::string resolvedPath = workspaceMgr.resolveModulePath(stringContent);
                                if (!resolvedPath.empty()) {
                                    return json::array({
                                        {
                                            {"uri", WorkspaceManager::pathToUri(resolvedPath)},
                                            {"range", {
                                                {"start", {{"line", 0}, {"character", 0}}},
                                                {"end", {{"line", 0}, {"character", 0}}}
                                            }}
                                        }
                                    });
                                }
                                break;
                            }

                            if (sepPos == std::string::npos) break;
                            pos = sepPos + 1;
                        }
                    }
                }
            }
        }

        // Normal symbol definition lookup
        std::string word;
        std::string qualifiedName;

        // Find word at column
        if (line <= currentLine) {
            size_t pos = lineStart + character - 1;
            if (pos < source.size()) {
                size_t start = pos, end = pos;
                while (start > lineStart && (std::isalnum(static_cast<unsigned char>(source[start - 1])) || source[start - 1] == '_'))
                    start--;
                while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_'))
                    end++;
                if (end > start) word = source.substr(start, end - start);

                // Check for qualified name pattern: Module.FuncName
                // If there's a "." before the word, extract the part before it
                if (start > lineStart + 1 && source[start - 1] == '.') {
                    size_t dotPos = start - 1;
                    size_t modStart = dotPos;
                    while (modStart > lineStart && (std::isalnum(static_cast<unsigned char>(source[modStart - 1])) || source[modStart - 1] == '_'))
                        modStart--;
                    if (modStart < dotPos) {
                        std::string moduleName = source.substr(modStart, dotPos - modStart);
                        qualifiedName = moduleName + "." + word;
                    }
                }

                // Also check if there's a "." after the word (cursor on module name part)
                if (qualifiedName.empty() && end < source.size() && source[end] == '.') {
                    size_t funcStart = end + 1;
                    size_t funcEnd = funcStart;
                    while (funcEnd < source.size() && (std::isalnum(static_cast<unsigned char>(source[funcEnd])) || source[funcEnd] == '_'))
                        funcEnd++;
                    if (funcEnd > funcStart) {
                        std::string funcName = source.substr(funcStart, funcEnd - funcStart);
                        qualifiedName = word + "." + funcName;
                    }
                }
            }
        }

        if (word.empty()) {
            auto* sym = doc->findSymbolAt({line, character, 0});
            if (sym) word = sym->name;
        }

        if (word.empty()) return json::array();

        // Search for definition
        json result = json::array();

        // If we have a qualified name (Module.Func), try it first in workspace
        if (!qualifiedName.empty()) {
            auto defs = symbolIndex.findDefinition(qualifiedName, config.completion_case_sensitive);
            for (auto& def : defs) {
                return json::array({
                    {
                        {"uri", def.uri},
                        {"range", {
                            {"start", {{"line", def.position.line - 1}, {"character", def.position.column - 1}}},
                            {"end", {{"line", def.position.line - 1}, {"character", def.position.column - 1 + static_cast<int>(def.name.size())}}}
                        }}
                    }
                });
            }
        }

        // Check symbols in the current document
        for (auto& sym : doc->allSymbols()) {
            if (sym.name == word) {
                result.push_back({
                    {"uri", uri},
                    {"range", {
                        {"start", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1}}},
                        {"end", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1 + static_cast<int>(sym.name.size())}}}
                    }}
                });
                break;  // Return first match
            }
        }

        // If not found in current file, search workspace
        if (result.empty()) {
            auto defs = symbolIndex.findDefinition(word, config.completion_case_sensitive);
            for (auto& def : defs) {
                result.push_back({
                    {"uri", def.uri},
                    {"range", {
                        {"start", {{"line", def.position.line - 1}, {"character", def.position.column - 1}}},
                        {"end", {{"line", def.position.line - 1}, {"character", def.position.column - 1 + static_cast<int>(word.size())}}}
                    }}
                });
                break;  // Return first match
            }
        }

        return result;
    });

    dispatcher.onRequest("textDocument/references", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>() + 1;
        int character = params["position"]["character"].get<int>() + 1;
        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json::array();

        // Extract word at cursor
        std::string source = doc->source();
        std::string word;

        int currentLine = 1;
        size_t lineStart = 0;
        for (size_t i = 0; i < source.size(); i++) {
            if (source[i] == '\n') {
                currentLine++;
                if (currentLine == line) lineStart = i + 1;
                if (currentLine > line) break;
            }
        }

        if (line <= currentLine) {
            size_t pos = lineStart + character - 1;
            if (pos < source.size()) {
                size_t start = pos, end = pos;
                while (start > lineStart && (std::isalnum(static_cast<unsigned char>(source[start - 1])) || source[start - 1] == '_'))
                    start--;
                while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_'))
                    end++;
                if (end > start) word = source.substr(start, end - start);
            }
        }

        if (word.empty()) {
            auto* sym = doc->findSymbolAt({line, character, 0});
            if (sym) word = sym->name;
        }

        if (word.empty()) return json::array();

        // Module pattern: if current file has a module name, use qualified search
        // to avoid matching unrelated same-named functions in other files
        const std::string& moduleName = doc->moduleName();
        if (!moduleName.empty()) {
            std::string qualifiedName = moduleName + "." + word;

            json result = json::array();

            // 1. Find bare word only in the current module file (definition + local usages)
            {
                const std::string& src = doc->source();
                size_t pos = 0;
                while ((pos = src.find(word, pos)) != std::string::npos) {
                    bool isStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(src[pos - 1])) && src[pos - 1] != '_'));
                    bool isEnd = (pos + word.size() >= src.size() || (!std::isalnum(static_cast<unsigned char>(src[pos + word.size()])) && src[pos + word.size()] != '_'));
                    if (isStart && isEnd) {
                        int refLine = 1, refCol = 1;
                        for (size_t i = 0; i < pos; i++) {
                            if (src[i] == '\n') { refLine++; refCol = 1; }
                            else { refCol++; }
                        }
                        result.push_back({
                            {"uri", uri},
                            {"range", {
                                {"start", {{"line", refLine - 1}, {"character", refCol - 1}}},
                                {"end", {{"line", refLine - 1}, {"character", refCol - 1 + static_cast<int>(word.size())}}}
                            }}
                        });
                    }
                    pos += word.size();
                }
            }

            // 2. Find qualified name in other files only
            for (auto& searchUri : symbolIndex.allUris()) {
                if (searchUri == uri) continue;
                auto* searchDoc = symbolIndex.getDocument(searchUri);
                if (!searchDoc) continue;

                const std::string& src = searchDoc->source();
                size_t pos = 0;
                while ((pos = src.find(qualifiedName, pos)) != std::string::npos) {
                    bool isStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(src[pos - 1])) && src[pos - 1] != '_'));
                    bool isEnd = (pos + qualifiedName.size() >= src.size() || (!std::isalnum(static_cast<unsigned char>(src[pos + qualifiedName.size()])) && src[pos + qualifiedName.size()] != '_'));
                    if (isStart && isEnd) {
                        int refLine = 1, refCol = 1;
                        for (size_t i = 0; i < pos; i++) {
                            if (src[i] == '\n') { refLine++; refCol = 1; }
                            else { refCol++; }
                        }
                        result.push_back({
                            {"uri", searchUri},
                            {"range", {
                                {"start", {{"line", refLine - 1}, {"character", refCol - 1}}},
                                {"end", {{"line", refLine - 1}, {"character", refCol - 1 + static_cast<int>(qualifiedName.size())}}}
                            }}
                        });
                    }
                    pos += qualifiedName.size();
                }
            }

            return result;
        }

        // Non-module: find all occurrences of the word across workspace
        auto refs = symbolIndex.findReferences(word, config.completion_case_sensitive);

        json result = json::array();
        for (auto& ref : refs) {
            result.push_back({
                {"uri", ref.uri},
                {"range", {
                    {"start", {{"line", ref.position.line - 1}, {"character", ref.position.column - 1}}},
                    {"end", {{"line", ref.position.line - 1}, {"character", ref.position.column - 1 + static_cast<int>(word.size())}}}
                }}
            });
        }

        return result;
    });

    dispatcher.onRequest("textDocument/completion", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json{{"items", json::array()}, {"isIncomplete", false}};

        json items = json::array();
        for (auto& sym : doc->allSymbols()) {
            items.push_back({
                {"label", sym.name},
                {"kind", sym.isFunction ? 3 : 6}  // Function or Variable
            });
        }

        // Add symbols from other documents
        for (auto& defUri : symbolIndex.allUris()) {
            if (defUri == uri) continue;
            auto* otherDoc = symbolIndex.getDocument(defUri);
            if (!otherDoc) continue;
            for (auto& sym : otherDoc->allSymbols()) {
                if (sym.scope == SymbolScope::Global) {
                    items.push_back({
                        {"label", sym.name},
                        {"kind", sym.isFunction ? 3 : 6}
                    });
                }
            }
        }

        return {{"items", items}, {"isIncomplete", true}};
    });

    dispatcher.onRequest("workspace/symbol", [&](int, const json& params) -> json {
        std::string query = params.value("query", "");
        if (query.empty()) return json::array();

        auto results = symbolIndex.searchSymbols(query, config.completion_case_sensitive);
        json result = json::array();
        for (auto& sym : results) {
            result.push_back({
                {"name", sym.name},
                {"kind", sym.isFunction ? 12 : sym.isClass ? 5 : 13},
                {"location", {
                    {"uri", sym.uri},
                    {"range", {
                        {"start", {{"line", sym.position.line - 1}, {"character", sym.position.column - 1}}},
                        {"end", {{"line", sym.position.line - 1}, {"character", sym.position.column - 1}}}
                    }}
                }}
            });
        }
        return result;
    });

    // foldingRange - detect foldable regions
    dispatcher.onRequest("textDocument/foldingRange", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        auto* doc = symbolIndex.getDocument(uri);
        if (!doc || !doc->ast()) return json::array();

        json result = json::array();

        // Walk AST to find foldable blocks
        std::function<void(const std::shared_ptr<AstNode>&)> visit;
        visit = [&](const std::shared_ptr<AstNode>& node) {
            if (!node) return;

            auto addFolding = [&](int startLine, int endLine) {
                if (endLine > startLine) {
                    result.push_back({
                        {"startLine", startLine - 1},  // LSP is 0-based
                        {"endLine", endLine - 1}
                    });
                }
            };

            if (auto block = std::dynamic_pointer_cast<Block>(node)) {
                for (auto& stmt : block->statements) {
                    visit(stmt);
                }
            }
            else if (auto func = std::dynamic_pointer_cast<FunctionStatement>(node)) {
                if (func->body) {
                    addFolding(func->range.start.line, func->range.end.line);
                    visit(func->body);
                }
            }
            else if (auto anonFunc = std::dynamic_pointer_cast<AnonymousFunction>(node)) {
                if (anonFunc->body) {
                    addFolding(anonFunc->range.start.line, anonFunc->range.end.line);
                }
            }
            else if (auto ifStmt = std::dynamic_pointer_cast<IfStatement>(node)) {
                addFolding(ifStmt->range.start.line, ifStmt->range.end.line);
                visit(ifStmt->thenBranch);
                for (auto& branch : ifStmt->elseIfBranches) {
                    visit(branch.body);
                }
                if (ifStmt->elseBranch) visit(ifStmt->elseBranch);
            }
            else if (auto whileStmt = std::dynamic_pointer_cast<WhileStatement>(node)) {
                addFolding(whileStmt->range.start.line, whileStmt->range.end.line);
                visit(whileStmt->body);
            }
            else if (auto repeat = std::dynamic_pointer_cast<RepeatStatement>(node)) {
                addFolding(repeat->range.start.line, repeat->range.end.line);
                visit(repeat->body);
            }
            else if (auto forStmt = std::dynamic_pointer_cast<ForStatement>(node)) {
                addFolding(forStmt->range.start.line, forStmt->range.end.line);
                visit(forStmt->body);
            }
            else if (auto forIn = std::dynamic_pointer_cast<ForInStatement>(node)) {
                addFolding(forIn->range.start.line, forIn->range.end.line);
                visit(forIn->body);
            }
            else if (auto doStmt = std::dynamic_pointer_cast<DoStatement>(node)) {
                addFolding(doStmt->range.start.line, doStmt->range.end.line);
                visit(doStmt->body);
            }
            else if (auto table = std::dynamic_pointer_cast<TableConstructor>(node)) {
                addFolding(table->range.start.line, table->range.end.line);
            }
        };

        visit(doc->ast());
        return result;
    });

    // documentHighlight - highlight same symbol in document
    dispatcher.onRequest("textDocument/documentHighlight", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>() + 1;
        int character = params["position"]["character"].get<int>() + 1;

        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json::array();

        // Find symbol at cursor position
        auto* targetSym = doc->findSymbolAt({line, character, 0});
        if (!targetSym) return json::array();

        // Find all occurrences of this symbol in the document
        json result = json::array();
        for (auto& sym : doc->allSymbols()) {
            if (sym.name == targetSym->name) {
                result.push_back({
                    {"range", {
                        {"start", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1}}},
                        {"end", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1 + static_cast<int>(sym.name.size())}}}
                    }},
                    {"kind", 1}  // Text
                });
            }
        }
        return result;
    });

    // signatureHelp - show function signature with active parameter
    dispatcher.onRequest("textDocument/signatureHelp", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>() + 1;
        int character = params["position"]["character"].get<int>() + 1;

        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json();

        std::string source = doc->source();

        // Find current position in source
        int pos = 0;
        int currentLine = 1;
        for (size_t i = 0; i < source.size(); i++) {
            if (source[i] == '\n') {
                currentLine++;
                if (currentLine > line) break;
            }
            if (currentLine == line && pos == 0) {
                pos = static_cast<int>(i);
            }
        }
        pos += character - 1;

        // Look backwards for matching '(' and count commas to find active parameter
        int depth = 0;
        std::string funcName;
        int activeParameter = 0;
        int parenPos = -1;

        for (int i = pos - 1; i >= 0; i--) {
            char c = source[i];
            if (c == ')') depth++;
            else if (c == '(') {
                if (depth > 0) depth--;
                else {
                    parenPos = i;
                    // Found matching '(', look backwards for identifier
                    int end = i;
                    while (end > 0 && (std::isalnum(static_cast<unsigned char>(source[end - 1])) || source[end - 1] == '_' || source[end - 1] == '.' || source[end - 1] == ':')) {
                        end--;
                    }
                    if (end < i) {
                        funcName = source.substr(end, i - end);
                    }
                    break;
                }
            }
            else if (c == ',' && depth == 0) {
                activeParameter++;
            }
            else if (c == '\n' || c == ';') break;
        }

        if (funcName.empty() || parenPos < 0) return json();

        // Skip function declarations
        {
            int scan = parenPos - static_cast<int>(funcName.size()) - 1;
            while (scan >= 0 && (source[scan] == ' ' || source[scan] == '\t')) scan--;
            if (scan >= 7 && source.substr(scan - 7, 8) == "function") return json();
        }

        // Extract function name (last part after . or :)
        std::string lookupName = funcName;
        auto dotPos = funcName.find_last_of(".:");
        if (dotPos != std::string::npos) {
            lookupName = funcName.substr(dotPos + 1);
        }

        // Find function definition and get parameter names
        std::vector<std::string> paramNames;
        for (auto& sym : doc->allSymbols()) {
            if (sym.name == lookupName && sym.isFunction) {
                for (auto& ps : doc->allSymbols()) {
                    if (ps.scope == SymbolScope::Parameter &&
                        ps.range.start.line >= sym.range.start.line &&
                        ps.range.start.line <= sym.range.start.line + 3) {
                        paramNames.push_back(ps.name);
                    }
                }
                break;
            }
        }

        if (paramNames.empty()) return json();

        // Build signature label
        std::string label = lookupName + "(";
        for (size_t i = 0; i < paramNames.size(); i++) {
            if (i > 0) label += ", ";
            label += paramNames[i];
        }
        label += ")";

        // Build parameters array
        json parameters = json::array();
        for (auto& pname : paramNames) {
            parameters.push_back({{"label", pname}});
        }

        return {
            {"signatures", json::array({
                {
                    {"label", label},
                    {"parameters", parameters}
                }
            })},
            {"activeSignature", 0},
            {"activeParameter", std::min(activeParameter, static_cast<int>(paramNames.size()) - 1)}
        };
    });

    // rename - rename symbol across workspace
    dispatcher.onRequest("textDocument/rename", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>() + 1;
        int character = params["position"]["character"].get<int>() + 1;
        std::string newName = params["newName"].get<std::string>();

        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json();

        auto* sym = doc->findSymbolAt({line, character, 0});
        if (!sym) return json();

        std::string oldName = sym->name;

        // Find all references
        auto refs = symbolIndex.findReferences(oldName, config.completion_case_sensitive);

        // Build workspace edit
        json changes = json::object();
        for (auto& ref : refs) {
            std::string refUri = ref.uri;
            if (!changes.contains(refUri)) {
                changes[refUri] = json::array();
            }
            changes[refUri].push_back({
                {"range", {
                    {"start", {{"line", ref.position.line - 1}, {"character", ref.position.column - 1}}},
                    {"end", {{"line", ref.position.line - 1}, {"character", ref.position.column - 1 + static_cast<int>(oldName.size())}}}
                }},
                {"newText", newName}
            });
        }

        return {
            {"changes", changes}
        };
    });

    // Inlay hints - parameter names at function call sites
    dispatcher.onRequest("textDocument/inlayHint", [&](int, const json& params) -> json {
        auto totalStart = std::chrono::steady_clock::now();

        std::string uri = params["textDocument"]["uri"].get<std::string>();

        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json::array();

        std::string source = doc->source();
        json result = json::array();

        // Build map: function name -> parameter names (from cached per-document maps)
        auto funcParams = symbolIndex.getFuncParams();

        if (funcParams.empty()) return result;

        // Scan source for function calls: identifier(
        for (size_t i = 0; i < source.size(); i++) {
            // Skip whitespace and track position
            if (source[i] == '\n' || source[i] == '\r') continue;

            // Skip comments
            if (source[i] == '-' && i + 1 < source.size() && source[i+1] == '-') {
                while (i < source.size() && source[i] != '\n') i++;
                continue;
            }

            // Skip strings
            if (source[i] == '"' || source[i] == '\'') {
                char q = source[i];
                i++;
                while (i < source.size() && source[i] != q) {
                    if (source[i] == '\\') i++;
                    i++;
                }
                continue;
            }

            // Look for pattern: identifier(
            if (source[i] == '(') {
                // Find identifier before '('
                size_t end = i;
                size_t start = end;
                while (start > 0) {
                    char prev = source[start - 1];
                    if (isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == ':' || prev == '.') {
                        start--;
                    } else {
                        break;
                    }
                }

                if (start == end) continue;

                // Skip function declarations
                {
                    size_t scan = start;
                    while (scan > 0 && (source[scan - 1] == ' ' || source[scan - 1] == '\t')) scan--;
                    if (scan >= 8 && source.substr(scan - 8, 8) == "function") continue;
                }

                // Extract function name (last part after . or :)
                std::string fullName = source.substr(start, end - start);
                std::string funcName = fullName;
                auto dotPos = fullName.find_last_of(".:");
                if (dotPos != std::string::npos) {
                    funcName = fullName.substr(dotPos + 1);
                }

                // Look up function params
                auto it = funcParams.find(funcName);
                if (it == funcParams.end()) continue;

                const auto& paramNames = it->second;
                if (paramNames.empty()) continue;

                // Count lines and columns up to this point
                int callLine = 0, callCol = 0;
                for (size_t k = 0; k < i; k++) {
                    if (source[k] == '\n') { callLine++; callCol = 0; }
                    else { callCol++; }
                }

                // Add hint for first argument at position after '('
                int argLine = callLine;
                int argCol = callCol + 1;  // After '('

                // Skip whitespace after '('
                size_t argStart = i + 1;
                while (argStart < source.size() && (source[argStart] == ' ' || source[argStart] == '\t')) {
                    argStart++;
                    argCol++;
                }

                if (argStart < source.size() && source[argStart] != ')') {
                    result.push_back({
                        {"position", {{"line", argLine}, {"character", argCol}}},
                        {"label", paramNames[0] + ":"},
                        {"kind", 2},  // InlayHintKind.Parameter
                        {"paddingLeft", true}
                    });
                }

                // Process arguments and add hints for remaining params
                int argIdx = 0;
                int depth = 0;
                int curLine = argLine, curCol = argCol;

                for (size_t j = argStart; j < source.size() && argIdx + 1 < static_cast<int>(paramNames.size()); j++) {
                    char c = source[j];

                    // Track line/column
                    if (c == '\n') { curLine++; curCol = 0; continue; }
                    else { curCol++; }

                    // Track nesting
                    if (c == '(' || c == '{' || c == '[') {
                        depth++;
                    } else if (c == ')' || c == '}' || c == ']') {
                        if (depth == 0) break;  // End of call
                        depth--;
                    }

                    // Skip strings
                    if (c == '"' || c == '\'') {
                        char q = c;
                        j++; curCol++;
                        while (j < source.size() && source[j] != q && source[j] != '\n') {
                            if (source[j] == '\\') { j++; curCol++; }
                            j++; curCol++;
                        }
                        continue;
                    }

                    // At depth 0, comma separates arguments
                    if (depth == 0 && c == ',') {
                        argIdx++;

                        // Find start of next argument (skip whitespace)
                        int nextLine = curLine;
                        int nextCol = curCol;
                        size_t nextStart = j + 1;
                        while (nextStart < source.size() && (source[nextStart] == ' ' || source[nextStart] == '\t')) {
                            nextStart++;
                            nextCol++;
                        }

                        // Add hint if there's content after the comma
                        if (nextStart < source.size() && source[nextStart] != ')' && source[nextStart] != ',') {
                            result.push_back({
                                {"position", {{"line", nextLine}, {"character", nextCol}}},
                                {"label", paramNames[argIdx] + ":"},
                                {"kind", 2},
                                {"paddingLeft", true}
                            });
                        }
                    }
                }
            }
        }

        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - totalStart).count();
        if (totalMs > 50) {
            spdlog::warn("inlayHint: slow {}ms uri={} hints={}", totalMs, uri, result.size());
        }

        return result;
    });

    // Remaining stub handlers
    // Formatting - simple indentation normalization
    dispatcher.onRequest("textDocument/formatting", [&](int, const json& params) -> json {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        bool insertSpaces = params["options"]["insertSpaces"].get<bool>();
        int tabSize = params["options"]["tabSize"].get<int>();

        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) return json::array();

        std::string source = doc->source();
        std::string formatted;
        int indent = 0;
        bool lineStart = true;

        for (size_t i = 0; i < source.size(); i++) {
            char c = source[i];

            if (c == '\n') {
                formatted += c;
                lineStart = true;
                continue;
            }

            if (lineStart) {
                // Skip original whitespace
                while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) i++;
                if (i >= source.size()) break;
                c = source[i];

                // Decrease indent for end, else, elseif, until
                if ((source.compare(i, 3, "end") == 0 && (i + 3 >= source.size() || !std::isalnum(source[i+3]))) ||
                    (source.compare(i, 4, "else") == 0 && (i + 4 >= source.size() || !std::isalnum(source[i+4]))) ||
                    (source.compare(i, 6, "elseif") == 0 && (i + 6 >= source.size() || !std::isalnum(source[i+6]))) ||
                    (source.compare(i, 5, "until") == 0 && (i + 5 >= source.size() || !std::isalnum(source[i+5]))) ||
                    c == '}' || c == ')') {
                    indent = (std::max)(0, indent - 1);
                }

                // Write indentation
                if (insertSpaces) {
                    formatted.append(indent * tabSize, ' ');
                } else {
                    formatted.append(indent, '\t');
                }
                lineStart = false;
            }

            formatted += c;

            // Increase indent
            if (c == '{' || c == '(' ||
                (i >= 3 && source.compare(i-2, 3, "do ") == 0 && (i < 4 || !std::isalnum(source[i-3]))) ||
                (i >= 4 && source.compare(i-3, 4, "then ") == 0 && (i < 5 || !std::isalnum(source[i-4])))) {
                indent++;
            }
        }

        // Return full document replacement
        return json::array({
            {
                {"range", {
                    {"start", {{"line", 0}, {"character", 0}}},
                    {"end", {{"line", 999999}, {"character", 0}}}
                }},
                {"newText", formatted}
            }
        });
    });
    dispatcher.onRequest("textDocument/rangeFormatting", [](int, const json&) -> json { return json::array(); });
    dispatcher.onRequest("textDocument/codeAction", [](int, const json&) -> json { return json::array(); });
    dispatcher.onRequest("textDocument/codeLens", [](int, const json&) -> json { return json::array(); });

    // EmmyLua custom requests
    dispatcher.onRequest("emmy/annotator", [&](int, const json& params) -> json {
        if (!params.contains("uri") || !params["uri"].is_string()) return json::array();
        std::string uri = params["uri"].get<std::string>();
        auto* doc = symbolIndex.getDocument(uri);
        if (!doc) {
            return json::array();
        }

        json result = json::array();

        // Generate annotators for different symbol types
        json paramRanges = json::array();
        json globalRanges = json::array();
        json localRanges = json::array();
        json funcRanges = json::array();

        for (auto& sym : doc->allSymbols()) {
            json range = {
                {"start", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1}}},
                {"end", {{"line", sym.namePos.line - 1}, {"character", sym.namePos.column - 1 + static_cast<int>(sym.name.size())}}}
            };

            switch (sym.scope) {
                case SymbolScope::Parameter:
                    break;
                case SymbolScope::Global:
                    globalRanges.push_back({{"range", range}, {"hint", ""}});
                    break;
                case SymbolScope::Local:
                    localRanges.push_back({{"range", range}, {"hint", ""}});
                    break;
                default:
                    break;
            }
        }

        // Param (type 0)
        if (!paramRanges.empty()) {
            result.push_back({{"uri", uri}, {"ranges", paramRanges}, {"type", 0}});
        }
        // Global (type 1)
        if (!globalRanges.empty()) {
            result.push_back({{"uri", uri}, {"ranges", globalRanges}, {"type", 1}});
        }
        // Local (type 2 is DocType, we use local for now)
        if (!localRanges.empty()) {
            result.push_back({{"uri", uri}, {"ranges", localRanges}, {"type", 2}});
        }

        return result;
    });

    dispatcher.onRequest("emmy/updateConfig", [](int, const json&) -> json { return json(); });
    dispatcher.onRequest("emmy/reportAPI", [](int, const json&) -> json { return json(); });
    dispatcher.onRequest("emmy/view_syntax_tree", [](int, const json&) -> json { return json::object(); });
    dispatcher.onRequest("emmy/view_psi_select", [](int, const json&) -> json { return json::object(); });

    // Main loop
    transport->start();
    bool shouldExit = false;

    while (!shouldExit && transport->isRunning()) {
        IncomingMessage msg;
        if (!transport->receive(msg)) break;

        if (msg.method == "exit") shouldExit = true;
        dispatcher.dispatch(msg.method, msg.id.value_or(0), msg.body.value("params", json::object()), msg.isRequest);
    }

    spdlog::info("EmmyLua Language Server shutting down");
    transport->stop();
    return 0;
}

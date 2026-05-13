#pragma once

#include <nlohmann/json.hpp>

namespace emmy {

using json = nlohmann::json;

// Build the ServerCapabilities JSON that we return during initialization
inline json makeServerCapabilities() {
    return {
        {"textDocumentSync", {
            {"openClose", true},
            {"change", 2},  // Incremental
            {"save", true}
        }},
        {"completionProvider", {
            {"triggerCharacters", {":", ".", "(", ",", "<"}},
            {"resolveProvider", false}
        }},
        {"hoverProvider", true},
        {"definitionProvider", true},
        {"referencesProvider", true},
        {"documentSymbolProvider", true},
        {"workspaceSymbolProvider", true},
        {"codeActionProvider", true},
        {"renameProvider", true},
        {"signatureHelpProvider", {
            {"triggerCharacters", {"(", ",", ")"}}
        }},
        {"documentHighlightProvider", true},
        {"foldingRangeProvider", true},
        {"documentFormattingProvider", true},
        {"documentRangeFormattingProvider", true},
        {"codeLensProvider", {
            {"resolveProvider", false}
        }},
        {"inlayHintProvider", {
            {"resolveProvider", false}
        }}
    };
}

}  // namespace emmy

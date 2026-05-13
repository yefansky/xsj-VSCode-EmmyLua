#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>

namespace emmy {

using json = nlohmann::json;

// === LSP Basic Types ===

struct Position {
    int line;       // 0-based
    int character;  // 0-based

    json toJson() const {
        return {{"line", line}, {"character", character}};
    }
    static Position fromJson(const json& j) {
        return {j.at("line").get<int>(), j.at("character").get<int>()};
    }
};

struct Range {
    Position start;
    Position end;

    json toJson() const {
        return {{"start", start.toJson()}, {"end", end.toJson()}};
    }
    static Range fromJson(const json& j) {
        return {Position::fromJson(j.at("start")), Position::fromJson(j.at("end"))};
    }
};

struct Location {
    std::string uri;
    Range range;

    json toJson() const {
        return {{"uri", uri}, {"range", range.toJson()}};
    }
};

struct TextDocumentIdentifier {
    std::string uri;

    static TextDocumentIdentifier fromJson(const json& j) {
        return {j.at("uri").get<std::string>()};
    }
};

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version;
    std::string text;

    static TextDocumentItem fromJson(const json& j) {
        return {
            j.at("uri").get<std::string>(),
            j.at("languageId").get<std::string>(),
            j.at("version").get<int>(),
            j.at("text").get<std::string>()
        };
    }
};

struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
    int version;

    static VersionedTextDocumentIdentifier fromJson(const json& j) {
        return {
            j.at("uri").get<std::string>(),
            j.at("version").get<int>()
        };
    }
};

struct TextDocumentPositionParams {
    TextDocumentIdentifier textDocument;
    Position position;

    static TextDocumentPositionParams fromJson(const json& j) {
        return {
            TextDocumentIdentifier::fromJson(j.at("textDocument")),
            Position::fromJson(j.at("position"))
        };
    }
};

struct MarkupContent {
    std::string kind;  // "plaintext" or "markdown"
    std::string value;

    json toJson() const {
        return {{"kind", kind}, {"value", value}};
    }
};

// === Diagnostic ===

enum class DiagnosticSeverity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

struct Diagnostic {
    Range range;
    DiagnosticSeverity severity;
    std::string message;
    std::string source = "emmylua";

    json toJson() const {
        json j = {
            {"range", range.toJson()},
            {"severity", static_cast<int>(severity)},
            {"message", message},
            {"source", source}
        };
        return j;
    }
};

// === Completion ===

enum class CompletionItemKind {
    Text = 1, Method = 2, Function = 3, Constructor = 4, Field = 5,
    Variable = 6, Class = 7, Interface = 8, Module = 9, Property = 10,
    Unit = 11, Value = 12, Enum = 13, Keyword = 14, Snippet = 15,
    Color = 16, File = 17, Reference = 18, Folder = 19, EnumMember = 20,
    Constant = 21, Struct = 22, Event = 23, Operator = 24, TypeParameter = 25
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Text;
    std::optional<std::string> detail;
    std::optional<MarkupContent> documentation;
    std::optional<std::string> insertText;
    std::optional<int> sortText;

    json toJson() const {
        json j = {{"label", label}, {"kind", static_cast<int>(kind)}};
        if (detail) j["detail"] = *detail;
        if (documentation) j["documentation"] = documentation->toJson();
        if (insertText) j["insertText"] = *insertText;
        if (sortText) j["sortText"] = *sortText;
        return j;
    }
};

// === Symbol ===

enum class SymbolKind {
    File = 1, Module = 2, Namespace = 3, Package = 4, Class = 5,
    Method = 6, Property = 7, Field = 8, Constructor = 9, Enum = 10,
    Interface = 11, Function = 12, Variable = 13, Constant = 14,
    String = 15, Number = 16, Boolean = 17, Array = 18, Object = 19,
    Key = 20, Null = 21, EnumMember = 22, Struct = 23, Event = 24,
    Operator = 25, TypeParameter = 26
};

struct DocumentSymbol {
    std::string name;
    SymbolKind kind;
    Range range;
    Range selectionRange;
    std::optional<std::string> detail;
    std::vector<DocumentSymbol> children;

    json toJson() const {
        json j = {
            {"name", name},
            {"kind", static_cast<int>(kind)},
            {"range", range.toJson()},
            {"selectionRange", selectionRange.toJson()}
        };
        if (detail) j["detail"] = *detail;
        if (!children.empty()) {
            json arr = json::array();
            for (auto& c : children) arr.push_back(c.toJson());
            j["children"] = arr;
        }
        return j;
    }
};

struct SymbolInformation {
    std::string name;
    SymbolKind kind;
    Location location;
    std::optional<std::string> containerName;

    json toJson() const {
        json j = {
            {"name", name},
            {"kind", static_cast<int>(kind)},
            {"location", location.toJson()}
        };
        if (containerName) j["containerName"] = *containerName;
        return j;
    }
};

// === Hover ===

struct Hover {
    MarkupContent contents;
    std::optional<Range> range;

    json toJson() const {
        json j = {{"contents", contents.toJson()}};
        if (range) j["range"] = range->toJson();
        return j;
    }
};

// === SignatureHelp ===

struct ParameterInformation {
    std::string label;
    std::optional<MarkupContent> documentation;

    json toJson() const {
        json j = {{"label", label}};
        if (documentation) j["documentation"] = documentation->toJson();
        return j;
    }
};

struct SignatureInformation {
    std::string label;
    std::optional<MarkupContent> documentation;
    std::vector<ParameterInformation> parameters;
    std::optional<int> activeParameter;

    json toJson() const {
        json j = {{"label", label}};
        if (documentation) j["documentation"] = documentation->toJson();
        json params = json::array();
        for (auto& p : parameters) params.push_back(p.toJson());
        j["parameters"] = params;
        if (activeParameter) j["activeParameter"] = *activeParameter;
        return j;
    }
};

struct SignatureHelp {
    std::vector<SignatureInformation> signatures;
    std::optional<int> activeSignature;
    std::optional<int> activeParameter;

    json toJson() const {
        json j;
        json sigs = json::array();
        for (auto& s : signatures) sigs.push_back(s.toJson());
        j["signatures"] = sigs;
        if (activeSignature) j["activeSignature"] = *activeSignature;
        if (activeParameter) j["activeParameter"] = *activeParameter;
        return j;
    }
};

// === FoldingRange ===

struct FoldingRange {
    int startLine;
    int endLine;
    std::optional<int> startCharacter;
    std::optional<int> endCharacter;
    std::optional<std::string> kind;  // "comment", "imports", "region"

    json toJson() const {
        json j = {{"startLine", startLine}, {"endLine", endLine}};
        if (startCharacter) j["startCharacter"] = *startCharacter;
        if (endCharacter) j["endCharacter"] = *endCharacter;
        if (kind) j["kind"] = *kind;
        return j;
    }
};

// === CodeLens ===

struct CodeLens {
    Range range;
    std::optional<json> command;
    std::optional<json> data;

    json toJson() const {
        json j = {{"range", range.toJson()}};
        if (command) j["command"] = *command;
        if (data) j["data"] = *data;
        return j;
    }
};

// === TextEdit ===

struct TextEdit {
    Range range;
    std::string newText;

    json toJson() const {
        return {{"range", range.toJson()}, {"newText", newText}};
    }
};

// === DocumentHighlight ===

enum class DocumentHighlightKind { Text = 1, Read = 2, Write = 3 };

struct DocumentHighlight {
    Range range;
    DocumentHighlightKind kind = DocumentHighlightKind::Text;

    json toJson() const {
        return {{"range", range.toJson()}, {"kind", static_cast<int>(kind)}};
    }
};

// === CodeAction ===

struct CodeAction {
    std::string title;
    std::optional<std::string> kind;  // "quickfix", "refactor", etc.
    std::optional<std::vector<Diagnostic>> diagnostics;
    std::optional<json> edit;

    json toJson() const {
        json j = {{"title", title}};
        if (kind) j["kind"] = *kind;
        if (diagnostics) {
            json diags = json::array();
            for (auto& d : *diagnostics) diags.push_back(d.toJson());
            j["diagnostics"] = diags;
        }
        if (edit) j["edit"] = *edit;
        return j;
    }
};

}  // namespace emmy

#pragma once

#include "parser/AstNode.h"
#include "parser/Annotation.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace emmy {

// Symbol scope types
enum class SymbolScope { Local, Global, Upvalue, Parameter, Field };

// A symbol definition in a document
struct SymbolDef {
    std::string name;
    SymbolScope scope;
    SourceRange range;          // Full definition range
    SourcePosition namePos;     // Position of just the name
    TypeExprPtr type;           // Declared type (from annotations)
    std::string typeName;       // Resolved type name for quick lookup
    bool isFunction = false;
    bool isClass = false;
    bool isUsed = false;        // For unused variable detection
    int scopeDepth = 0;         // Nesting depth for scope analysis
};

// A class definition extracted from annotations
struct ClassDef {
    std::string name;
    std::vector<std::string> parents;
    bool isInterface = false;

    struct Field {
        std::string name;
        TypeExprPtr type;
        SourcePosition position;
    };
    std::vector<Field> fields;
};

// A function signature from annotations
struct FunctionSig {
    std::vector<std::string> paramNames;
    std::vector<TypeExprPtr> paramTypes;
    std::vector<bool> paramOptional;
    std::vector<TypeExprPtr> returnTypes;
    std::vector<std::string> genericParams;
    bool isVararg = false;
};

// Per-file index: AST cache + symbol table
class DocumentIndex {
public:
    // Parse source and build index
    void update(const std::string& uri, const std::string& source);

    // Clear the index
    void clear();

    // Find symbol at position
    SymbolDef* findSymbolAt(const SourcePosition& pos);

    // Find symbol by name (first match)
    SymbolDef* findSymbol(const std::string& name);

    // Find all symbols
    std::vector<SymbolDef> allSymbols() const;

    // Find all symbols that are functions
    std::vector<SymbolDef> functionSymbols() const;

    // Get all class definitions
    const std::vector<ClassDef>& classes() const { return classes_; }

    // Get class by name
    ClassDef* findClass(const std::string& name);

    // Get parsed AST
    std::shared_ptr<Chunk> ast() const { return ast_; }

    // Get source text
    const std::string& source() const { return source_; }

    // Get content hash (for change detection)
    size_t contentHash() const { return contentHash_; }

    // Get URI
    const std::string& uri() const { return uri_; }

    // Get parser errors
    const std::vector<std::pair<int, std::string>>& parseErrors() const { return parseErrors_; }

    // Get function name -> parameter names map (for this document only)
    const std::unordered_map<std::string, std::vector<std::string>>& funcParams() const { return funcParams_; }

    // Module pattern support (Lua 5.1 module() function)
    void setSupportModulePattern(bool enable) { supportModulePattern_ = enable; }
    const std::string& moduleName() const { return moduleName_; }

private:
    void extractSymbols();
    void extractAnnotations();
    void analyzeScope(const std::shared_ptr<Block>& block, int depth);

    // Find the position of a name in the source, starting from startPos
    SourcePosition findNamePosition(const std::string& name, const SourcePosition& startPos);

    std::string uri_;
    std::string source_;
    std::shared_ptr<Chunk> ast_;
    std::vector<SymbolDef> symbols_;
    std::vector<ClassDef> classes_;
    std::unordered_map<std::string, std::vector<std::string>> funcParams_;  // Cached: func name -> param names
    std::vector<DocComment> docComments_;
    std::vector<std::pair<int, std::string>> parseErrors_;  // line, message
    size_t contentHash_ = 0;  // For detecting unchanged files
    std::string moduleName_;  // From module("name") pattern
    bool supportModulePattern_ = false;
};

}  // namespace emmy

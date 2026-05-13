#include "index/DocumentIndex.h"
#include "parser/Lexer.h"
#include "parser/Parser.h"
#include "parser/AnnotationParser.h"
#include <spdlog/spdlog.h>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace emmy {

#ifdef _WIN32
// Convert ANSI encoding to UTF-8 using Windows API
static std::string ansiToUtf8(const std::string& ansi, unsigned int codePage) {
    if (ansi.empty()) return ansi;

    int wideLen = MultiByteToWideChar(codePage, 0, ansi.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) return ansi;
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(codePage, 0, ansi.c_str(), -1, &wide[0], wideLen);
    wide.pop_back();

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return ansi;
    std::string utf8(utf8Len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Len, nullptr, nullptr);

    return utf8;
}
#endif

// Hash for content comparison
static size_t quickHash(const std::string& str) {
    size_t hash = 0;
    for (unsigned char c : str) {
        hash = hash * 31 + c;
    }
    return hash;
}

// Check if string has any non-ASCII bytes
static bool hasNonAscii(const std::string& str) {
    for (unsigned char c : str) {
        if (c >= 0x80) return true;
    }
    return false;
}

// Fast UTF-8 validation (only checks first 100 bytes)
static bool isValidUtf8Fast(const std::string& str) {
    int len = (std::min)(static_cast<int>(str.size()), 100);
    for (int i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) continue;
        if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= len) return true;  // Assume valid if truncated
            i++;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= len) return true;
            i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= len) return true;
            i += 3;
        } else {
            return false;  // Invalid UTF-8
        }
    }
    return true;
}

void DocumentIndex::update(const std::string& uri, const std::string& source) {
    // Fast path: skip if content hasn't changed
    size_t newHash = quickHash(source);
    if (newHash == contentHash_ && !source_.empty()) {
        return;  // Content unchanged, skip re-parsing
    }
    contentHash_ = newHash;

    uri_ = uri;
    source_ = source;
    symbols_.clear();
    classes_.clear();
    funcParams_.clear();
    docComments_.clear();
    parseErrors_.clear();

    if (source_.empty()) return;

    // Skip files with null bytes (compiled Lua)
    if (source_.find('\0') != std::string::npos) return;

    // Convert GBK to UTF-8 if needed (fast check)
#ifdef _WIN32
    if (hasNonAscii(source_) && !isValidUtf8Fast(source_)) {
        source_ = ansiToUtf8(source_, 936);
    }
#endif

    // Skip UTF-8 BOM
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB &&
        static_cast<unsigned char>(source_[2]) == 0xBF) {
        source_ = source_.substr(3);
    }

    // Parse
    Parser parser(source_);
    ast_ = parser.parse();

    for (auto& err : parser.errors()) {
        parseErrors_.push_back({err.position.line, err.message});
    }

    // Extract symbols
    if (ast_) {
        analyzeScope(ast_, 0);
    }
}

void DocumentIndex::clear() {
    uri_.clear();
    source_.clear();
    ast_.reset();
    symbols_.clear();
    classes_.clear();
    docComments_.clear();
}

SymbolDef* DocumentIndex::findSymbolAt(const SourcePosition& pos) {
    for (auto& sym : symbols_) {
        if (pos.line >= sym.range.start.line && pos.line <= sym.range.end.line) {
            if (pos.line == sym.namePos.line &&
                pos.column >= sym.namePos.column &&
                pos.column < sym.namePos.column + static_cast<int>(sym.name.size())) {
                return &sym;
            }
        }
    }
    return nullptr;
}

SymbolDef* DocumentIndex::findSymbol(const std::string& name) {
    for (auto& sym : symbols_) {
        if (sym.name == name) return &sym;
    }
    return nullptr;
}

std::vector<SymbolDef> DocumentIndex::allSymbols() const {
    return symbols_;
}

std::vector<SymbolDef> DocumentIndex::functionSymbols() const {
    std::vector<SymbolDef> result;
    for (auto& sym : symbols_) {
        if (sym.isFunction) result.push_back(sym);
    }
    return result;
}

ClassDef* DocumentIndex::findClass(const std::string& name) {
    for (auto& cls : classes_) {
        if (cls.name == name) return &cls;
    }
    return nullptr;
}

void DocumentIndex::extractAnnotations() {
    for (auto& doc : docComments_) {
        for (auto& ann : doc.annotations) {
            if (auto cls = std::dynamic_pointer_cast<ClassAnnotation>(ann)) {
                ClassDef def;
                def.name = cls->name;
                def.parents = cls->parents;
                def.isInterface = cls->isInterface;
                classes_.push_back(std::move(def));
            }
            else if (auto field = std::dynamic_pointer_cast<FieldAnnotation>(ann)) {
                // Add field to the most recently defined class
                if (!classes_.empty() && !field->name.empty()) {
                    ClassDef::Field f;
                    f.name = field->name;
                    f.type = field->type;
                    f.position = field->position;
                    classes_.back().fields.push_back(std::move(f));
                }
            }
        }
    }
}

SourcePosition DocumentIndex::findNamePosition(const std::string& name, const SourcePosition& startPos) {
    // Convert 1-based position to 0-based offset
    int line = 1;
    int col = 1;
    size_t offset = 0;

    // Find the offset for startPos
    for (size_t i = 0; i < source_.size(); i++) {
        if (line == startPos.line && col == startPos.column) {
            offset = i;
            break;
        }
        if (source_[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }

    // Search for the name starting from offset
    for (size_t i = offset; i + name.size() <= source_.size(); i++) {
        // Check if this is the start of the name
        if (source_.substr(i, name.size()) == name) {
            // Verify it's a whole word (not part of another identifier)
            bool isStart = (i == 0 || !std::isalnum(static_cast<unsigned char>(source_[i-1])) && source_[i-1] != '_');
            bool isEnd = (i + name.size() >= source_.size() || !std::isalnum(static_cast<unsigned char>(source_[i + name.size()])) && source_[i + name.size()] != '_');

            if (isStart && isEnd) {
                // Calculate line and column for this position
                int foundLine = 1;
                int foundCol = 1;
                for (size_t j = 0; j < i; j++) {
                    if (source_[j] == '\n') {
                        foundLine++;
                        foundCol = 1;
                    } else {
                        foundCol++;
                    }
                }
                return {foundLine, foundCol, static_cast<int>(i)};
            }
        }
    }

    // Not found, return the original start position
    return startPos;
}

void DocumentIndex::analyzeScope(const std::shared_ptr<Block>& block, int depth) {
    if (!block) return;

    for (auto& stmt : block->statements) {
        // Detect module("ModuleName", ...) call (Lua 5.1 module pattern)
        if (supportModulePattern_ && moduleName_.empty()) {
            if (auto callStmt = std::dynamic_pointer_cast<CallStatement>(stmt)) {
                if (auto callExpr = std::dynamic_pointer_cast<CallExpr>(callStmt->expression)) {
                    bool isModule = false;
                    if (auto id = std::dynamic_pointer_cast<IdentifierExpr>(callExpr->function)) {
                        isModule = (id->name == "module");
                    }
                    if (isModule && !callExpr->arguments.empty()) {
                        if (auto str = std::dynamic_pointer_cast<StringLiteral>(callExpr->arguments[0])) {
                            moduleName_ = str->value;
                            continue;  // Skip the module() call itself
                        }
                    }
                }
            }
        }

        if (auto local = std::dynamic_pointer_cast<LocalStatement>(stmt)) {
            // Calculate offset for each name in the local statement
            // Names appear after "local " keyword
            SourcePosition searchStart = local->range.start;

            for (size_t i = 0; i < local->names.size(); i++) {
                SymbolDef sym;
                sym.name = local->names[i];
                sym.scope = SymbolScope::Local;
                sym.range = local->range;
                sym.namePos = findNamePosition(sym.name, searchStart);
                sym.scopeDepth = depth;

                // Update search start for next name
                searchStart = sym.namePos;
                searchStart.column += static_cast<int>(sym.name.size());
                searchStart.offset += static_cast<int>(sym.name.size());

                // Try to infer type from value
                if (i < local->values.size()) {
                    if (auto anonFunc = std::dynamic_pointer_cast<AnonymousFunction>(local->values[i])) {
                        sym.isFunction = true;
                    }
                }

                symbols_.push_back(std::move(sym));
            }
        }
        else if (auto func = std::dynamic_pointer_cast<FunctionStatement>(stmt)) {
            SymbolDef sym;
            sym.scope = func->isLocal ? SymbolScope::Local : SymbolScope::Global;
            sym.range = func->range;
            sym.isFunction = true;
            sym.scopeDepth = depth;

            // Extract function name and find its position
            if (auto id = std::dynamic_pointer_cast<IdentifierExpr>(func->name)) {
                sym.name = func->methodName.empty() ? id->name : func->methodName;
                sym.namePos = findNamePosition(sym.name, func->range.start);
            } else if (auto member = std::dynamic_pointer_cast<MemberExpr>(func->name)) {
                sym.name = func->methodName.empty() ? member->field : func->methodName;
                sym.namePos = findNamePosition(sym.name, func->range.start);
            }

            if (!sym.name.empty()) {
                bool needsQualified = !moduleName_.empty() && sym.scope != SymbolScope::Local;
                std::string funcName = sym.name;
                SourcePosition funcNamePos = sym.namePos;

                symbols_.push_back(std::move(sym));

                // Cache function -> parameter names for inlayHint
                if (!func->parameters.empty()) {
                    funcParams_[funcName] = func->parameters;
                }

                // If inside a module, also register qualified name (ModuleName.FuncName)
                if (needsQualified) {
                    SymbolDef qualifiedSym;
                    qualifiedSym.name = moduleName_ + "." + funcName;
                    qualifiedSym.scope = SymbolScope::Global;
                    qualifiedSym.range = func->range;
                    qualifiedSym.isFunction = true;
                    qualifiedSym.scopeDepth = depth;
                    qualifiedSym.namePos = funcNamePos;
                    symbols_.push_back(std::move(qualifiedSym));
                }
            }

            // Analyze function body with increased depth
            if (func->body) {
                // Add parameters as local symbols at the body scope
                SourcePosition paramSearchStart = func->range.start;
                for (auto& param : func->parameters) {
                    SymbolDef paramSym;
                    paramSym.name = param;
                    paramSym.scope = SymbolScope::Parameter;
                    paramSym.range = func->range;
                    paramSym.namePos = findNamePosition(param, paramSearchStart);
                    paramSym.scopeDepth = depth + 1;

                    paramSearchStart = paramSym.namePos;
                    paramSearchStart.column += static_cast<int>(param.size());
                    paramSearchStart.offset += static_cast<int>(param.size());

                    symbols_.push_back(std::move(paramSym));
                }
                analyzeScope(func->body, depth + 1);
            }
        }
        else if (auto forStmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
            SymbolDef sym;
            sym.name = forStmt->variable;
            sym.scope = SymbolScope::Local;
            sym.range = forStmt->range;
            sym.namePos = findNamePosition(sym.name, forStmt->range.start);
            sym.scopeDepth = depth;
            symbols_.push_back(std::move(sym));

            if (forStmt->body) analyzeScope(forStmt->body, depth + 1);
        }
        else if (auto forIn = std::dynamic_pointer_cast<ForInStatement>(stmt)) {
            SourcePosition varSearchStart = forIn->range.start;
            for (auto& var : forIn->variables) {
                SymbolDef sym;
                sym.name = var;
                sym.scope = SymbolScope::Local;
                sym.range = forIn->range;
                sym.namePos = findNamePosition(var, varSearchStart);
                sym.scopeDepth = depth;

                varSearchStart = sym.namePos;
                varSearchStart.column += static_cast<int>(var.size());
                varSearchStart.offset += static_cast<int>(var.size());

                symbols_.push_back(std::move(sym));
            }
            if (forIn->body) analyzeScope(forIn->body, depth + 1);
        }
        else if (auto ifStmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
            analyzeScope(ifStmt->thenBranch, depth + 1);
            for (auto& branch : ifStmt->elseIfBranches) {
                analyzeScope(branch.body, depth + 1);
            }
            if (ifStmt->elseBranch) analyzeScope(ifStmt->elseBranch, depth + 1);
        }
        else if (auto whileStmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
            analyzeScope(whileStmt->body, depth + 1);
        }
        else if (auto repeat = std::dynamic_pointer_cast<RepeatStatement>(stmt)) {
            analyzeScope(repeat->body, depth + 1);
        }
        else if (auto doStmt = std::dynamic_pointer_cast<DoStatement>(stmt)) {
            analyzeScope(doStmt->body, depth + 1);
        }
    }
}

}  // namespace emmy

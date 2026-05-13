#include "parser/AnnotationParser.h"
#include <sstream>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <chrono>

namespace emmy {

// ============================================================
// ParseContext implementation
// ============================================================

char AnnotationParser::ParseContext::current() const {
    if (pos >= static_cast<int>(text.size())) return '\0';
    return text[pos];
}

char AnnotationParser::ParseContext::peek(int offset) const {
    int p = pos + offset;
    if (p >= static_cast<int>(text.size())) return '\0';
    return text[p];
}

void AnnotationParser::ParseContext::advance() {
    if (pos < static_cast<int>(text.size())) pos++;
}

void AnnotationParser::ParseContext::skipWhitespace() {
    while (!atEnd() && (current() == ' ' || current() == '\t')) {
        advance();
    }
}

bool AnnotationParser::ParseContext::atEnd() const {
    return pos >= static_cast<int>(text.size());
}

std::string AnnotationParser::ParseContext::readWord() {
    int start = pos;
    while (!atEnd() && (std::isalnum(current()) || current() == '_' || current() == '.')) {
        advance();
    }
    return text.substr(start, pos - start);
}

std::string AnnotationParser::ParseContext::readLine() {
    int start = pos;
    while (!atEnd() && current() != '\n' && current() != '\r') {
        advance();
    }
    int end = pos;
    // Skip newline characters
    if (!atEnd() && current() == '\r') advance();
    if (!atEnd() && current() == '\n') advance();
    return text.substr(start, end - start);
}

std::string AnnotationParser::ParseContext::readTypeExpr() {
    int start = pos;
    int depth = 0;

    while (!atEnd()) {
        char c = current();
        if (c == '<' || c == '(') depth++;
        else if (c == '>' || c == ')') depth--;
        else if ((c == '|' || c == ',') && depth == 0) break;  // Stop at | or , at depth 0
        else if ((c == ' ' || c == '\t') && depth == 0) {
            // Check if next char is part of type or is description
            if (peek() == '|' || peek() == '<' || peek() == '(') {
                advance();
                continue;
            }
            break;
        }
        else if (c == '\n' || c == '\r') break;

        advance();
    }

    return text.substr(start, pos - start);
}

// ============================================================
// Type Expression Parser (recursive descent on string)
// ============================================================

static TypeExprPtr parseTypeString(const std::string& input);

static TypeExprPtr parseTypeUnion(const std::string& input, int& pos);
static TypeExprPtr parseTypeSimple(const std::string& input, int& pos);
static void skipSpaces(const std::string& input, int& pos);
static bool isAtEnd(const std::string& input, int pos);

static void skipSpaces(const std::string& input, int& pos) {
    while (pos < static_cast<int>(input.size()) &&
           (input[pos] == ' ' || input[pos] == '\t')) {
        pos++;
    }
}

static bool isAtEnd(const std::string& input, int pos) {
    return pos >= static_cast<int>(input.size());
}

static std::string readTypeName(const std::string& input, int& pos) {
    int start = pos;
    while (pos < static_cast<int>(input.size()) &&
           (std::isalnum(input[pos]) || input[pos] == '_' || input[pos] == '.')) {
        pos++;
    }
    return input.substr(start, pos - start);
}

// Parse quoted literal: "value" or 'value'
static std::string readQuotedString(const std::string& input, int& pos) {
    char quote = input[pos];
    pos++;  // skip opening quote
    int start = pos;
    while (pos < static_cast<int>(input.size()) && input[pos] != quote) {
        pos++;
    }
    std::string result = input.substr(start, pos - start);
    if (pos < static_cast<int>(input.size())) pos++;  // skip closing quote
    return result;
}

// Parse number literal
static std::string readNumber(const std::string& input, int& pos) {
    int start = pos;
    if (input[pos] == '-') pos++;
    while (pos < static_cast<int>(input.size()) &&
           (std::isdigit(input[pos]) || input[pos] == '.')) {
        pos++;
    }
    return input.substr(start, pos - start);
}

static TypeExprPtr parseTypeSimple(const std::string& input, int& pos) {
    skipSpaces(input, pos);
    if (isAtEnd(input, pos)) return nullptr;

    // Function type: fun(params): return
    if (input.compare(pos, 4, "fun(") == 0 || input.compare(pos, 4, "fun ") == 0) {
        auto func = std::make_shared<FuncTypeExpr>();
        pos += 3;  // skip "fun"

        // Parse parameters
        if (!isAtEnd(input, pos) && input[pos] == '(') {
            pos++;  // skip (
            while (!isAtEnd(input, pos) && input[pos] != ')') {
                skipSpaces(input, pos);
                if (input[pos] == ')') break;

                // Check for ...vararg
                if (input.compare(pos, 3, "...") == 0) {
                    pos += 3;
                    skipSpaces(input, pos);
                    auto type = parseTypeSimple(input, pos);
                    if (type) {
                        func->params.push_back({"...", std::make_shared<VarargTypeExpr>(type)});
                    }
                    break;
                }

                // Read param name
                std::string paramName = readTypeName(input, pos);
                skipSpaces(input, pos);

                TypeExprPtr paramType;
                if (!isAtEnd(input, pos) && input[pos] == ':') {
                    pos++;  // skip :
                    paramType = parseTypeUnion(input, pos);
                }

                func->params.push_back({paramName, paramType});

                skipSpaces(input, pos);
                if (!isAtEnd(input, pos) && input[pos] == ',') pos++;
            }
            if (!isAtEnd(input, pos) && input[pos] == ')') pos++;
        }

        // Parse return type(s)
        skipSpaces(input, pos);
        if (!isAtEnd(input, pos) && input[pos] == ':') {
            pos++;  // skip :
            auto retType = parseTypeUnion(input, pos);
            if (retType) func->returnTypes.push_back(retType);
        }

        return func;
    }

    // Table type: table<K, V>
    if (input.compare(pos, 6, "table<") == 0) {
        auto table = std::make_shared<TableTypeExpr>();
        pos += 6;  // skip "table<"
        table->keyType = parseTypeUnion(input, pos);
        skipSpaces(input, pos);
        if (!isAtEnd(input, pos) && input[pos] == ',') pos++;
        table->valueType = parseTypeUnion(input, pos);
        skipSpaces(input, pos);
        if (!isAtEnd(input, pos) && input[pos] == '>') pos++;
        return table;
    }

    // Literal string: "value" or 'value'
    if (!isAtEnd(input, pos) && (input[pos] == '"' || input[pos] == '\'')) {
        std::string value = readQuotedString(input, pos);
        return std::make_shared<LiteralTypeExpr>(value);
    }

    // Literal number
    if (!isAtEnd(input, pos) && (std::isdigit(input[pos]) || input[pos] == '-')) {
        std::string value = readNumber(input, pos);
        return std::make_shared<LiteralTypeExpr>(value);
    }

    // Boolean/nil literals
    if (input.compare(pos, 4, "true") == 0 && (pos + 4 >= static_cast<int>(input.size()) || !std::isalnum(input[pos + 4]))) {
        pos += 4;
        return std::make_shared<LiteralTypeExpr>("true");
    }
    if (input.compare(pos, 5, "false") == 0 && (pos + 5 >= static_cast<int>(input.size()) || !std::isalnum(input[pos + 5]))) {
        pos += 5;
        return std::make_shared<LiteralTypeExpr>("false");
    }

    // Built-in types
    static const char* builtinTypes[] = {
        "nil", "boolean", "number", "string", "table", "function",
        "any", "void", "self", "userdata", "thread"
    };

    std::string name = readTypeName(input, pos);
    if (name.empty()) return nullptr;

    // Check for built-in types
    for (const char* bt : builtinTypes) {
        if (name == bt) {
            if (name == "nil") return std::make_shared<NilTypeExpr>();
            if (name == "boolean") return std::make_shared<BooleanTypeExpr>();
            if (name == "number") return std::make_shared<NumberTypeExpr>();
            if (name == "string") return std::make_shared<StringTypeExpr>();
            return std::make_shared<NameTypeExpr>(name);
        }
    }

    // Check for generic: Name<T1, T2>
    skipSpaces(input, pos);
    if (!isAtEnd(input, pos) && input[pos] == '<') {
        auto generic = std::make_shared<GenericTypeExpr>(name);
        pos++;  // skip <
        while (!isAtEnd(input, pos) && input[pos] != '>') {
            skipSpaces(input, pos);
            if (input[pos] == '>') break;
            auto arg = parseTypeUnion(input, pos);
            if (arg) generic->typeArgs.push_back(arg);
            skipSpaces(input, pos);
            if (!isAtEnd(input, pos) && input[pos] == ',') pos++;
        }
        if (!isAtEnd(input, pos) && input[pos] == '>') pos++;

        // Check for array: Name<T>[]
        skipSpaces(input, pos);
        if (!isAtEnd(input, pos) && input[pos] == '[' &&
            (pos + 1 < static_cast<int>(input.size()) && input[pos + 1] == ']')) {
            pos += 2;
            return std::make_shared<ArrayTypeExpr>(generic);
        }

        return generic;
    }

    // Check for array: name[]
    if (!isAtEnd(input, pos) && input[pos] == '[' && (pos + 1 < static_cast<int>(input.size()) && input[pos + 1] == ']')) {
        pos += 2;
        auto nameExpr = std::make_shared<NameTypeExpr>(name);
        return std::make_shared<ArrayTypeExpr>(nameExpr);
    }

    return std::make_shared<NameTypeExpr>(name);
}

static TypeExprPtr parseTypeUnion(const std::string& input, int& pos) {
    auto first = parseTypeSimple(input, pos);
    if (!first) return nullptr;

    skipSpaces(input, pos);

    // Check for union
    if (!isAtEnd(input, pos) && input[pos] == '|') {
        auto unionType = std::make_shared<UnionTypeExpr>();
        unionType->types.push_back(first);

        while (!isAtEnd(input, pos) && input[pos] == '|') {
            pos++;  // skip |
            auto next = parseTypeSimple(input, pos);
            if (next) {
                unionType->types.push_back(next);
            }
            skipSpaces(input, pos);
        }

        return unionType;
    }

    return first;
}

static TypeExprPtr parseTypeString(const std::string& input) {
    if (input.empty()) return nullptr;
    int pos = 0;
    return parseTypeUnion(input, pos);
}

// Fix the peek helper that was incorrectly referenced
static char peek(const std::string& input, int pos, int /*unused*/) {
    int p = pos + 1;
    if (p >= static_cast<int>(input.size())) return '\0';
    return input[p];
}

TypeExprPtr AnnotationParser::ParseContext::parseTypeExpr(const std::string& text) {
    return parseTypeString(text);
}

// ============================================================
// Annotation Parser
// ============================================================

std::vector<DocComment> AnnotationParser::parse(const std::vector<Lexer::Comment>& comments) {
    std::vector<DocComment> result;

    auto startTime = std::chrono::steady_clock::now();
    int idx = 0;
    for (const auto& comment : comments) {
        // Check timeout (max 500ms for all comments)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (elapsed > 500) {
            spdlog::warn("[ANN] timeout after {} comments", idx);
            break;
        }

        if (comment.isDoc) {
            result.push_back(parseBlock(comment.text, comment.position));
        }
        idx++;
    }

    return result;
}

DocComment AnnotationParser::parseBlock(const std::string& text, const SourcePosition& position) {
    DocComment doc;
    doc.position = position;

    ParseContext ctx;
    ctx.text = text;
    ctx.pos = 0;

    std::string currentDesc;

    while (!ctx.atEnd()) {
        ctx.skipWhitespace();

        // Skip newlines
        if (!ctx.atEnd() && (ctx.current() == '\r' || ctx.current() == '\n')) {
            if (ctx.current() == '\r') ctx.advance();
            if (!ctx.atEnd() && ctx.current() == '\n') ctx.advance();
            continue;
        }

        if (ctx.atEnd()) break;

        if (ctx.current() == '@') {
            // Save description
            if (!currentDesc.empty()) {
                while (!currentDesc.empty() && (currentDesc.back() == ' ' || currentDesc.back() == '\t'))
                    currentDesc.pop_back();
                if (!doc.description.empty()) doc.description += "\n";
                doc.description += currentDesc;
                currentDesc.clear();
            }

            auto item = parseAnnotation(ctx);
            if (item) doc.annotations.push_back(item);
        } else {
            // Read description line
            std::string line = ctx.readLine();
            if (currentDesc.empty()) {
                size_t start = line.find_first_not_of(" \t");
                if (start != std::string::npos) line = line.substr(start);
            }
            if (!line.empty()) {
                if (!currentDesc.empty()) currentDesc += "\n";
                currentDesc += line;
            }
        }
    }

    // Save remaining description
    if (!currentDesc.empty()) {
        while (!currentDesc.empty() && (currentDesc.back() == ' ' || currentDesc.back() == '\t'))
            currentDesc.pop_back();
        if (!doc.description.empty()) doc.description += "\n";
        doc.description += currentDesc;
    }

    return doc;
}

AnnotationItemPtr AnnotationParser::parseAnnotation(ParseContext& ctx) {
    ctx.advance();  // skip @
    std::string tagName = ctx.readWord();
    AnnotationTag tag = parseAnnotationTag(tagName);

    ctx.skipWhitespace();

    switch (tag) {
        case AnnotationTag::Class:     return parseClass(ctx);
        case AnnotationTag::Interface: return parseClass(ctx, true);
        case AnnotationTag::Field:     return parseField(ctx);
        case AnnotationTag::Param:     return parseParam(ctx);
        case AnnotationTag::Return:    return parseReturn(ctx);
        case AnnotationTag::Type:      return parseType(ctx);
        case AnnotationTag::Overload:  return parseOverload(ctx);
        case AnnotationTag::Generic:   return parseGeneric(ctx);
        case AnnotationTag::Vararg:    return parseVararg(ctx);
        case AnnotationTag::Enum:      return parseEnum(ctx);
        case AnnotationTag::Alias:     return parseAlias(ctx);
        case AnnotationTag::See:       return parseSee(ctx);
        case AnnotationTag::Language:  return parseLanguage(ctx);
        case AnnotationTag::Deprecated: {
            auto item = std::make_shared<DeprecatedAnnotation>();
            item->tag = tag;
            return item;
        }
        case AnnotationTag::Public:
        case AnnotationTag::Protected:
        case AnnotationTag::Private:
        case AnnotationTag::Package: {
            auto item = std::make_shared<AccessAnnotation>();
            item->tag = tag;
            return item;
        }
        default: {
            // Skip unknown annotation
            ctx.readLine();
            return nullptr;
        }
    }
}

std::shared_ptr<ClassAnnotation> AnnotationParser::parseClass(ParseContext& ctx, bool isInterface) {
    auto item = std::make_shared<ClassAnnotation>();
    item->isInterface = isInterface;
    item->tag = isInterface ? AnnotationTag::Interface : AnnotationTag::Class;

    // Read class name
    item->name = ctx.readWord();

    // Read parent(s): : Parent, Interface1, Interface2
    ctx.skipWhitespace();
    if (!ctx.atEnd() && ctx.current() == ':') {
        ctx.advance();  // skip :
        ctx.skipWhitespace();

        while (!ctx.atEnd() && ctx.current() != '\n') {
            std::string parent = ctx.readWord();
            if (!parent.empty()) {
                item->parents.push_back(parent);
            }
            ctx.skipWhitespace();
            if (!ctx.atEnd() && ctx.current() == ',') {
                ctx.advance();
                ctx.skipWhitespace();
            } else {
                break;
            }
        }
    }

    return item;
}

std::shared_ptr<FieldAnnotation> AnnotationParser::parseField(ParseContext& ctx) {
    auto item = std::make_shared<FieldAnnotation>();

    // Check for numeric index: @field[1] type
    if (!ctx.atEnd() && ctx.current() == '[') {
        ctx.advance();  // skip [
        std::string idx;
        while (!ctx.atEnd() && ctx.current() != ']') {
            idx += ctx.current();
            ctx.advance();
        }
        if (!ctx.atEnd()) ctx.advance();  // skip ]
        try {
            item->numericIndex = std::stoi(idx);
        } catch (...) {}
        ctx.skipWhitespace();
    } else {
        // Read field name
        item->name = ctx.readWord();
        ctx.skipWhitespace();

        // Check for optional marker: name?
        if (!ctx.atEnd() && ctx.current() == '?') {
            item->isOptional = true;
            ctx.advance();
            ctx.skipWhitespace();
        }
    }

    // Read type
    std::string typeStr = ctx.readTypeExpr();
    item->type = ParseContext::parseTypeExpr(typeStr);

    return item;
}

std::shared_ptr<ParamAnnotation> AnnotationParser::parseParam(ParseContext& ctx) {
    auto item = std::make_shared<ParamAnnotation>();

    // Read param name
    item->name = ctx.readWord();
    ctx.skipWhitespace();

    // Check for optional marker
    if (!ctx.atEnd() && ctx.current() == '?') {
        item->isOptional = true;
        ctx.advance();
        ctx.skipWhitespace();
    }

    // Read type
    std::string typeStr = ctx.readTypeExpr();
    item->type = ParseContext::parseTypeExpr(typeStr);

    // Read description (rest of line)
    ctx.skipWhitespace();
    item->description = ctx.readLine();

    return item;
}

std::shared_ptr<ReturnAnnotation> AnnotationParser::parseReturn(ParseContext& ctx) {
    auto item = std::make_shared<ReturnAnnotation>();

    // Read type(s): @return type1, type2, ...
    while (!ctx.atEnd()) {
        ctx.skipWhitespace();
        if (ctx.atEnd()) break;

        std::string typeStr = ctx.readTypeExpr();
        auto type = ParseContext::parseTypeExpr(typeStr);
        if (type) {
            item->types.push_back(type);
        }

        ctx.skipWhitespace();
        if (!ctx.atEnd() && ctx.current() == ',') {
            ctx.advance();
        } else {
            break;
        }
    }

    // Read description
    ctx.skipWhitespace();
    item->description = ctx.readLine();

    return item;
}

std::shared_ptr<TypeAnnotation> AnnotationParser::parseType(ParseContext& ctx) {
    auto item = std::make_shared<TypeAnnotation>();

    std::string typeStr = ctx.readTypeExpr();
    item->type = ParseContext::parseTypeExpr(typeStr);

    return item;
}

std::shared_ptr<OverloadAnnotation> AnnotationParser::parseOverload(ParseContext& ctx) {
    auto item = std::make_shared<OverloadAnnotation>();

    // Read the function signature: fun(params): return
    std::string sigStr = ctx.readLine();
    item->signature = ParseContext::parseTypeExpr(sigStr);

    return item;
}

std::shared_ptr<GenericAnnotation> AnnotationParser::parseGeneric(ParseContext& ctx) {
    auto item = std::make_shared<GenericAnnotation>();

    // Read type parameters: @generic T, U, V
    while (!ctx.atEnd() && ctx.current() != '\n') {
        ctx.skipWhitespace();
        if (ctx.atEnd() || ctx.current() == '\n') break;

        std::string param = ctx.readWord();
        if (!param.empty()) {
            item->typeParams.push_back(param);
        }

        ctx.skipWhitespace();
        if (!ctx.atEnd() && ctx.current() == ',') {
            ctx.advance();
        }
    }

    return item;
}

std::shared_ptr<VarargAnnotation> AnnotationParser::parseVararg(ParseContext& ctx) {
    auto item = std::make_shared<VarargAnnotation>();

    std::string typeStr = ctx.readTypeExpr();
    item->type = ParseContext::parseTypeExpr(typeStr);

    return item;
}

std::shared_ptr<EnumAnnotation> AnnotationParser::parseEnum(ParseContext& ctx) {
    auto item = std::make_shared<EnumAnnotation>();

    item->name = ctx.readWord();
    ctx.skipWhitespace();

    // Optional base type: @enum Name: number
    if (!ctx.atEnd() && ctx.current() == ':') {
        ctx.advance();
        ctx.skipWhitespace();
        item->baseType = ctx.readWord();
    }

    return item;
}

std::shared_ptr<AliasAnnotation> AnnotationParser::parseAlias(ParseContext& ctx) {
    auto item = std::make_shared<AliasAnnotation>();

    item->name = ctx.readWord();
    ctx.skipWhitespace();

    std::string typeStr = ctx.readTypeExpr();
    item->type = ParseContext::parseTypeExpr(typeStr);

    return item;
}

std::shared_ptr<SeeAnnotation> AnnotationParser::parseSee(ParseContext& ctx) {
    auto item = std::make_shared<SeeAnnotation>();
    item->reference = ctx.readLine();
    // Trim
    while (!item->reference.empty() && item->reference.back() == ' ') {
        item->reference.pop_back();
    }
    return item;
}

std::shared_ptr<LanguageAnnotation> AnnotationParser::parseLanguage(ParseContext& ctx) {
    auto item = std::make_shared<LanguageAnnotation>();
    item->language = ctx.readWord();
    return item;
}

}  // namespace emmy

#include "parser/Lexer.h"
#include <cctype>
#include <stdexcept>
#include <sstream>

namespace emmy {

std::unordered_map<std::string, TokenType> Lexer::keywords_;

std::unordered_map<std::string, TokenType>& Lexer::getKeywords() {
    if (keywords_.empty()) {
        keywords_["and"]      = TokenType::And;
        keywords_["break"]    = TokenType::Break;
        keywords_["do"]       = TokenType::Do;
        keywords_["else"]     = TokenType::Else;
        keywords_["elseif"]   = TokenType::ElseIf;
        keywords_["end"]      = TokenType::End;
        keywords_["for"]      = TokenType::For;
        keywords_["function"] = TokenType::Function;
        keywords_["goto"]     = TokenType::Goto;
        keywords_["if"]       = TokenType::If;
        keywords_["in"]       = TokenType::In;
        keywords_["local"]    = TokenType::Local;
        keywords_["not"]      = TokenType::Not;
        keywords_["or"]       = TokenType::Or;
        keywords_["repeat"]   = TokenType::Repeat;
        keywords_["return"]   = TokenType::Return;
        keywords_["then"]     = TokenType::Then;
        keywords_["until"]    = TokenType::Until;
        keywords_["while"]    = TokenType::While;
        keywords_["true"]     = TokenType::True;
        keywords_["false"]    = TokenType::False;
        keywords_["nil"]      = TokenType::Nil;
    }
    return keywords_;
}

Lexer::Lexer(const std::string& source)
    : source_(source), pos_(0), line_(1), column_(1) {
    // Skip UTF-8 BOM if present
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB &&
        static_cast<unsigned char>(source_[2]) == 0xBF) {
        pos_ = 3;
    }
}

char Lexer::current() const {
    if (pos_ >= static_cast<int>(source_.size())) return '\0';
    return source_[pos_];
}

char Lexer::peekChar(int offset) const {
    int p = pos_ + offset;
    if (p >= static_cast<int>(source_.size())) return '\0';
    return source_[p];
}

void Lexer::advance() {
    if (pos_ < static_cast<int>(source_.size())) {
        if (source_[pos_] == '\n') {
            line_++;
            column_ = 1;
        } else {
            column_++;
        }
        pos_++;
    }
}

void Lexer::skipWhitespace() {
    while (pos_ < static_cast<int>(source_.size())) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') {
            advance();
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    // We're past "--"
    // Check if this is a doc comment (---)
    SourcePosition commentStart = {line_, column_ - 2, pos_ - 2};
    std::string text;

    bool isDoc = (current() == '-');
    if (isDoc) {
        // Consume the third dash (don't add to text)
        advance();
    }

    while (pos_ < static_cast<int>(source_.size()) && current() != '\n') {
        text += current();
        advance();
    }

    if (isDoc) {
        addDocComment(text, commentStart);
    }
}

void Lexer::skipBlockComment() {
    // We're past "--[", current is '[' or '='
    SourcePosition commentStart = {line_, column_ - 2, pos_ - 2};
    int level = 0;

    // Count '=' signs
    while (current() == '=') {
        level++;
        advance();
    }

    if (current() != '[') {
        // Not a long comment, treat as line comment
        // Go back and skip as line comment
        skipLineComment();
        return;
    }
    advance();  // skip '['

    // Check if this is a doc comment (---[=[...]=])
    bool isDoc = false;
    // Look back in source to see if there was a third dash
    if (commentStart.offset >= 0 && commentStart.offset + 2 < static_cast<int>(source_.size())) {
        // The comment started as ---[=[
        // We need to check the text between "--" and "["
    }

    std::string text;
    while (pos_ < static_cast<int>(source_.size())) {
        if (current() == ']') {
            advance();
            // Check for matching closing ]=...=]
            int closeLevel = 0;
            while (current() == '=') {
                closeLevel++;
                advance();
            }
            if (current() == ']' && closeLevel == level) {
                advance();
                break;
            } else {
                text += ']';
                for (int i = 0; i < closeLevel; i++) text += '=';
                if (current() != ']') {
                    text += current();
                    advance();
                }
            }
        } else {
            text += current();
            advance();
        }
    }

    // Determine if this was a doc comment (---[=[...]=])
    // Check back: was there a third dash before the [
    if (commentStart.offset + 2 < static_cast<int>(source_.size()) &&
        source_[commentStart.offset + 2] == '-') {
        isDoc = true;
        // Remove leading dash from text if present
        if (!text.empty() && text[0] == '-') {
            text = text.substr(1);
        }
    }

    if (isDoc) {
        addDocComment(text, commentStart);
    }
}

void Lexer::addDocComment(const std::string& text, const SourcePosition& pos) {
    comments_.push_back({text, pos, false, true});
}

Token Lexer::readToken() {
    // Skip whitespace and comments
    skipWhitespace();

    if (pos_ >= static_cast<int>(source_.size())) {
        return {TokenType::Eof, "", {line_, column_, pos_}, 0};
    }

    SourcePosition startPos = {line_, column_, pos_};
    unsigned char c = static_cast<unsigned char>(current());

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return readIdentifier();
    }

    // Numbers
    if (std::isdigit(c) || (c == '.' && std::isdigit(static_cast<unsigned char>(peekChar())))) {
        return readNumber();
    }

    // Strings
    if (c == '"' || c == '\'') {
        advance();
        return readString(static_cast<char>(c));
    }

    // Long strings [[...]] or [==[...]==]
    if (c == '[' && (peekChar() == '[' || peekChar() == '=')) {
        advance();
        return readLongString(0);
    }

    // Skip non-ASCII bytes (UTF-8 multi-byte characters, e.g., Chinese in comments)
    if (c >= 128) {
        advance();
        return readToken();  // Recursively get the next token
    }

    // Operators and delimiters
    advance();
    switch (c) {
        case '+': return {TokenType::Plus, "+", startPos, 1};
        case '*': return {TokenType::Star, "*", startPos, 1};
        case '/':
            if (current() == '/') {
                advance();
                return {TokenType::SlashSlash, "//", startPos, 2};
            }
            return {TokenType::Slash, "/", startPos, 1};
        case '%': return {TokenType::Percent, "%", startPos, 1};
        case '^': return {TokenType::Caret, "^", startPos, 1};
        case '#': return {TokenType::Hash, "#", startPos, 1};
        case '(': return {TokenType::LeftParen, "(", startPos, 1};
        case ')': return {TokenType::RightParen, ")", startPos, 1};
        case '{': return {TokenType::LeftBrace, "{", startPos, 1};
        case '}': return {TokenType::RightBrace, "}", startPos, 1};
        case '[': return {TokenType::LeftBracket, "[", startPos, 1};
        case ']': return {TokenType::RightBracket, "]", startPos, 1};
        case ';': return {TokenType::Semicolon, ";", startPos, 1};
        case ',': return {TokenType::Comma, ",", startPos, 1};

        case ':':
            if (current() == ':') {
                advance();
                return {TokenType::DoubleColon, "::", startPos, 2};
            }
            return {TokenType::Colon, ":", startPos, 1};

        case '.':
            if (current() == '.') {
                advance();
                if (current() == '.') {
                    advance();
                    return {TokenType::DotDotDot, "...", startPos, 3};
                }
                return {TokenType::DotDot, "..", startPos, 2};
            }
            return {TokenType::Dot, ".", startPos, 1};

        case '=':
            if (current() == '=') {
                advance();
                return {TokenType::Equal, "==", startPos, 2};
            }
            return {TokenType::Assign, "=", startPos, 1};

        case '<':
            if (current() == '=') {
                advance();
                return {TokenType::LessEqual, "<=", startPos, 2};
            }
            if (current() == '<') {
                advance();
                return {TokenType::LeftShift, "<<", startPos, 2};
            }
            return {TokenType::Less, "<", startPos, 1};

        case '>':
            if (current() == '=') {
                advance();
                return {TokenType::GreaterEqual, ">=", startPos, 2};
            }
            if (current() == '>') {
                advance();
                return {TokenType::RightShift, ">>", startPos, 2};
            }
            return {TokenType::Greater, ">", startPos, 1};

        case '~':
            if (current() == '=') {
                advance();
                return {TokenType::NotEqual, "~=", startPos, 2};
            }
            return {TokenType::Tilde, "~", startPos, 1};

        case '&': return {TokenType::Ampersand, "&", startPos, 1};
        case '|': return {TokenType::Pipe, "|", startPos, 1};

        case '-':
            // Check for comment
            if (current() == '-') {
                advance();
                // Check for long comment --[==[...]==]
                if (current() == '[' && (peekChar() == '[' || peekChar() == '=')) {
                    advance();
                    skipBlockComment();
                } else {
                    skipLineComment();
                }
                return readToken();  // Recursively get the next real token
            }
            return {TokenType::Minus, "-", startPos, 1};

        default:
            return {TokenType::Invalid, std::string(1, c), startPos, 1};
    }
}

Token Lexer::readIdentifier() {
    SourcePosition startPos = {line_, column_, pos_};
    int start = pos_;

    while (pos_ < static_cast<int>(source_.size()) &&
           (std::isalnum(static_cast<unsigned char>(current())) || current() == '_')) {
        advance();
    }

    std::string text = source_.substr(start, pos_ - start);

    auto& keywords = getKeywords();
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return {it->second, text, startPos, static_cast<int>(text.size())};
    }

    return {TokenType::Identifier, text, startPos, static_cast<int>(text.size())};
}

Token Lexer::readNumber() {
    SourcePosition startPos = {line_, column_, pos_};
    int start = pos_;

    // Hex numbers: 0x...
    if (current() == '0' && (peekChar() == 'x' || peekChar() == 'X')) {
        advance(); advance();  // skip 0x
        while (std::isxdigit(current()) || current() == '_') advance();
        // Hex float (Lua 5.3): 0x1p10, 0x1.5p-3
        if (current() == '.') {
            advance();
            while (std::isxdigit(current()) || current() == '_') advance();
        }
        if (current() == 'p' || current() == 'P') {
            advance();
            if (current() == '+' || current() == '-') advance();
            while (std::isdigit(current()) || current() == '_') advance();
        }
        std::string text = source_.substr(start, pos_ - start);
        return {TokenType::Number, text, startPos, static_cast<int>(text.size())};
    }

    // Decimal integers
    while (std::isdigit(current()) || current() == '_') advance();

    // Decimal point
    if (current() == '.' && std::isdigit(peekChar())) {
        advance();
        while (std::isdigit(current()) || current() == '_') advance();
    }

    // Exponent
    if (current() == 'e' || current() == 'E') {
        advance();
        if (current() == '+' || current() == '-') advance();
        while (std::isdigit(current()) || current() == '_') advance();
    }

    std::string text = source_.substr(start, pos_ - start);
    return {TokenType::Number, text, startPos, static_cast<int>(text.size())};
}

static int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Token Lexer::readString(char quote) {
    SourcePosition startPos = {line_, column_ - 1, pos_ - 1};
    std::string value;

    while (pos_ < static_cast<int>(source_.size()) && current() != quote) {
        if (current() == '\n' || current() == '\0') {
            // Unterminated string
            return {TokenType::String, value, startPos, pos_ - startPos.offset};
        }

        if (current() == '\\') {
            advance();
            switch (current()) {
                case 'a':  value += '\a'; break;
                case 'b':  value += '\b'; break;
                case 'f':  value += '\f'; break;
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case 'v':  value += '\v'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"'; break;
                case '\'': value += '\''; break;
                case '\n': value += '\n'; break;
                case 'z':
                    // Lua 5.2+: \z skips whitespace
                    advance();
                    while (std::isspace(current())) advance();
                    continue;
                case 'x': {
                    // Lua 5.3: \xHH
                    advance();
                    int hi = hexDigitValue(current());
                    advance();
                    int lo = hexDigitValue(current());
                    if (hi >= 0 && lo >= 0) {
                        value += static_cast<char>(hi * 16 + lo);
                    }
                    break;
                }
                case 'u': {
                    // Lua 5.3: \u{HHHH}
                    advance();  // skip {
                    advance();
                    int code = 0;
                    while (current() != '}' && std::isxdigit(current())) {
                        code = code * 16 + hexDigitValue(current());
                        advance();
                    }
                    // Simple UTF-8 encoding for BMP
                    if (code < 0x80) {
                        value += static_cast<char>(code);
                    } else if (code < 0x800) {
                        value += static_cast<char>(0xC0 | (code >> 6));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        value += static_cast<char>(0xE0 | (code >> 12));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default:
                    if (std::isdigit(current())) {
                        // \DDD decimal escape
                        int val = current() - '0';
                        advance();
                        for (int i = 0; i < 2 && std::isdigit(current()); i++) {
                            val = val * 10 + (current() - '0');
                            advance();
                        }
                        value += static_cast<char>(val);
                        continue;  // advance already done
                    }
                    value += current();
                    break;
            }
            advance();
        } else {
            value += current();
            advance();
        }
    }

    // Skip closing quote
    if (current() == quote) advance();

    return {TokenType::String, value, startPos, pos_ - startPos.offset};
}

Token Lexer::readLongString(int level) {
    SourcePosition startPos = {line_, column_, pos_};

    // Count '=' signs (we may have already read some)
    while (current() == '=') {
        level++;
        advance();
    }

    if (current() == '[') {
        advance();
    } else {
        // Not a long string, return what we can
        return {TokenType::Invalid, "[", startPos, 1};
    }

    // Skip leading newline
    if (current() == '\n') advance();

    std::string value;
    while (pos_ < static_cast<int>(source_.size())) {
        if (current() == ']') {
            advance();
            int closeLevel = 0;
            while (current() == '=') {
                closeLevel++;
                advance();
            }
            if (current() == ']' && closeLevel == level) {
                advance();
                break;
            } else {
                value += ']';
                for (int i = 0; i < closeLevel; i++) value += '=';
                continue;
            }
        } else {
            value += current();
            advance();
        }
    }

    return {TokenType::String, value, startPos, pos_ - startPos.offset};
}

Token Lexer::next() {
    skipWhitespace();
    if (pos_ >= static_cast<int>(source_.size())) {
        return {TokenType::Eof, "", {line_, column_, pos_}, 0};
    }
    return readToken();
}

Token Lexer::peek() {
    int savedPos = pos_;
    int savedLine = line_;
    int savedCol = column_;
    Token t = next();
    pos_ = savedPos;
    line_ = savedLine;
    column_ = savedCol;
    return t;
}

std::vector<Token> Lexer::tokenizeAll() {
    std::vector<Token> tokens;
    while (true) {
        Token t = next();
        tokens.push_back(t);
        if (t.type == TokenType::Eof || t.type == TokenType::Invalid) break;
    }
    return tokens;
}

}  // namespace emmy

#pragma once

#include <string>
#include <string_view>

namespace emmy {

enum class TokenType {
    // Literals
    Number,
    String,
    Nil,
    True,
    False,

    // Identifiers
    Identifier,

    // Keywords
    And,
    Break,
    Do,
    Else,
    ElseIf,
    End,
    For,
    Function,
    Goto,
    If,
    In,
    Local,
    Not,
    Or,
    Repeat,
    Return,
    Then,
    Until,
    While,

    // Operators
    Plus,         // +
    Minus,        // -
    Star,         // *
    Slash,        // /
    Percent,      // %
    Caret,        // ^
    Hash,         // #
    Ampersand,    // & (Lua 5.3 bitwise)
    Tilde,        // ~ (Lua 5.3 bitwise NOT, also ~=)
    Pipe,         // | (Lua 5.3 bitwise)
    LeftShift,    // << (Lua 5.3)
    RightShift,   // >> (Lua 5.3)
    SlashSlash,   // // (Lua 5.3 integer division)
    Equal,        // ==
    NotEqual,     // ~=
    LessEqual,    // <=
    GreaterEqual, // >=
    Less,         // <
    Greater,      // >
    Assign,       // =

    // Delimiters
    LeftParen,    // (
    RightParen,   // )
    LeftBrace,    // {
    RightBrace,   // }
    LeftBracket,  // [
    RightBracket, // ]
    Semicolon,    // ;
    Colon,        // :
    Comma,        // ,
    Dot,          // .
    DotDot,       // ..
    DotDotDot,    // ...

    // Special
    DoubleColon,  // ::
    Eof,
    Invalid
};

struct SourcePosition {
    int line;      // 1-based
    int column;    // 1-based
    int offset;    // 0-based byte offset in source

    bool operator==(const SourcePosition& o) const {
        return line == o.line && column == o.column && offset == o.offset;
    }
};

struct SourceRange {
    SourcePosition start;
    SourcePosition end;

    bool operator==(const SourceRange& o) const {
        return start == o.start && end == o.end;
    }
};

struct Token {
    TokenType type;
    std::string text;         // The raw text of the token
    SourcePosition position;  // Start position
    int length;               // Byte length

    SourceRange range() const {
        SourcePosition end = position;
        end.offset += length;
        // For single-line tokens, compute end column
        end.column = position.column + length;
        return {position, end};
    }

    bool isKeyword() const {
        return type >= TokenType::And && type <= TokenType::While;
    }

    bool isOperator() const {
        return type >= TokenType::Plus && type <= TokenType::Greater;
    }

    bool isLiteral() const {
        return type == TokenType::Number ||
               type == TokenType::String ||
               type == TokenType::Nil ||
               type == TokenType::True ||
               type == TokenType::False;
    }
};

// Utility: get token type name for debugging
inline const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::Number:       return "Number";
        case TokenType::String:       return "String";
        case TokenType::Nil:          return "nil";
        case TokenType::True:         return "true";
        case TokenType::False:        return "false";
        case TokenType::Identifier:   return "Identifier";
        case TokenType::And:          return "and";
        case TokenType::Break:        return "break";
        case TokenType::Do:           return "do";
        case TokenType::Else:         return "else";
        case TokenType::ElseIf:       return "elseif";
        case TokenType::End:          return "end";
        case TokenType::For:          return "for";
        case TokenType::Function:     return "function";
        case TokenType::Goto:         return "goto";
        case TokenType::If:           return "if";
        case TokenType::In:           return "in";
        case TokenType::Local:        return "local";
        case TokenType::Not:          return "not";
        case TokenType::Or:           return "or";
        case TokenType::Repeat:       return "repeat";
        case TokenType::Return:       return "return";
        case TokenType::Then:         return "then";
        case TokenType::Until:        return "until";
        case TokenType::While:        return "while";
        case TokenType::Plus:         return "+";
        case TokenType::Minus:        return "-";
        case TokenType::Star:         return "*";
        case TokenType::Slash:        return "/";
        case TokenType::Percent:      return "%";
        case TokenType::Caret:        return "^";
        case TokenType::Hash:         return "#";
        case TokenType::Ampersand:    return "&";
        case TokenType::Tilde:        return "~";
        case TokenType::Pipe:         return "|";
        case TokenType::LeftShift:    return "<<";
        case TokenType::RightShift:   return ">>";
        case TokenType::SlashSlash:   return "//";
        case TokenType::Equal:        return "==";
        case TokenType::NotEqual:     return "~=";
        case TokenType::LessEqual:    return "<=";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::Less:         return "<";
        case TokenType::Greater:      return ">";
        case TokenType::Assign:       return "=";
        case TokenType::LeftParen:    return "(";
        case TokenType::RightParen:   return ")";
        case TokenType::LeftBrace:    return "{";
        case TokenType::RightBrace:   return "}";
        case TokenType::LeftBracket:  return "[";
        case TokenType::RightBracket: return "]";
        case TokenType::Semicolon:    return ";";
        case TokenType::Colon:        return ":";
        case TokenType::Comma:        return ",";
        case TokenType::Dot:          return ".";
        case TokenType::DotDot:       return "..";
        case TokenType::DotDotDot:    return "...";
        case TokenType::DoubleColon:  return "::";
        case TokenType::Eof:          return "<eof>";
        case TokenType::Invalid:      return "<invalid>";
    }
    return "<unknown>";
}

}  // namespace emmy

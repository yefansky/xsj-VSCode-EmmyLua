#pragma once

#include "parser/Token.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace emmy {

class Lexer {
public:
    explicit Lexer(const std::string& source);

    // Get next token
    Token next();

    // Peek at next token without consuming (saves/restores state)
    Token peek();

    // Get all tokens (for debugging)
    std::vector<Token> tokenizeAll();

    // Comments collected during tokenization (for annotation parsing later)
    struct Comment {
        std::string text;
        SourcePosition position;
        bool isBlock;      // --[[...]] style
        bool isDoc;        // starts with ---
    };
    const std::vector<Comment>& comments() const { return comments_; }

private:
    char current() const;
    char peekChar(int offset = 1) const;
    void advance();
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();

    Token readToken();
    Token readIdentifier();
    Token readNumber();
    Token readString(char quote);
    Token readLongString(int level);

    void addDocComment(const std::string& text, const SourcePosition& pos);

    std::string source_;
    int pos_;
    int line_;
    int column_;
    std::vector<Comment> comments_;

    static std::unordered_map<std::string, TokenType> keywords_;
    static std::unordered_map<std::string, TokenType>& getKeywords();
};

}  // namespace emmy

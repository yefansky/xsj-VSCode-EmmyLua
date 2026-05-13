#pragma once

#include "parser/Lexer.h"
#include "parser/AstNode.h"
#include <string>
#include <vector>
#include <functional>

namespace emmy {

class Parser {
public:
    explicit Parser(const std::string& source);

    // Parse the entire source as a chunk (block)
    std::shared_ptr<Chunk> parse();

    // Get parsing errors
    struct ParseError {
        std::string message;
        SourcePosition position;
    };
    const std::vector<ParseError>& errors() const { return errors_; }

private:
    // Token management
    Token current() const;
    Token lookAhead(int offset = 1);
    void advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool expect(TokenType type, const std::string& context = "");
    Token consume(TokenType type, const std::string& context = "");

    // Error recovery
    void error(const std::string& message);
    void synchronize();

    // Parsing
    std::shared_ptr<Block> parseBlock();
    StmtPtr parseStatement();
    StmtPtr parseLocalStatement();
    StmtPtr parseIfStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseRepeatStatement();
    StmtPtr parseForStatement();
    StmtPtr parseFunctionStatement(bool isLocal = false);
    StmtPtr parseReturnStatement();
    StmtPtr parseLabelStatement();
    StmtPtr parseGotoStatement();

    // Parse function name: a.b.c or a.b:c (without consuming parens)
    ExprPtr parseFunctionName(std::string& methodName);

    // Expressions (precedence climbing)
    ExprPtr parseExpression();
    ExprPtr parseOrExpr();
    ExprPtr parseAndExpr();
    ExprPtr parseComparisonExpr();
    ExprPtr parseBitwiseOrExpr();
    ExprPtr parseBitwiseXorExpr();
    ExprPtr parseBitwiseAndExpr();
    ExprPtr parseShiftExpr();
    ExprPtr parseConcatExpr();
    ExprPtr parseAdditiveExpr();
    ExprPtr parseMultiplicativeExpr();
    ExprPtr parseUnaryExpr();
    ExprPtr parsePowerExpr();
    ExprPtr parsePrimaryExpr();
    ExprPtr parsePrefixExpr();
    ExprPtr finishCallOrMemberOrIndex(ExprPtr expr);
    ExprPtr parseExpressionFromIdentifier(ExprPtr expr);  // Continue parsing expression from identifier

    // Table and function
    std::shared_ptr<TableConstructor> parseTableConstructor();
    std::shared_ptr<AnonymousFunction> parseAnonymousFunction();
    void parseFunctionBody(std::vector<std::string>& params, bool& isVararg, std::shared_ptr<Block>& body);

    // Helpers
    std::vector<ExprPtr> parseExpressionList();
    std::vector<std::string> parseNameList();

    Lexer lexer_;
    Token current_;
    std::vector<ParseError> errors_;
    bool panicMode_ = false;
};

}  // namespace emmy

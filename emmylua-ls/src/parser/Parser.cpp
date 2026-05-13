#include "parser/Parser.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace emmy {

Parser::Parser(const std::string& source) : lexer_(source) {
    current_ = lexer_.next();
}

Token Parser::current() const { return current_; }

Token Parser::lookAhead(int offset) {
    // Save state
    int savedPos = 0; // We can't easily save lexer state, so we'll use a different approach
    // Actually, we need to just peek at the next token
    // The lexer's internal state makes this tricky. Let's use a simple approach:
    // We'll keep a small lookahead buffer.
    // For now, just use the lexer's current position
    Token t = current_;
    for (int i = 0; i < offset; i++) {
        t = lexer_.next();
    }
    // We can't restore lexer state easily, so we'll avoid lookAhead when possible
    // and rely on the current token
    return t;
}

void Parser::advance() {
    current_ = lexer_.next();
}

bool Parser::check(TokenType type) const {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenType type, const std::string& context) {
    if (check(type)) {
        advance();
        return true;
    }
    error("Expected " + std::string(tokenTypeName(type)) +
          " but got " + std::string(tokenTypeName(current_.type)) +
          (context.empty() ? "" : " in " + context));
    return false;
}

Token Parser::consume(TokenType type, const std::string& context) {
    Token t = current_;
    expect(type, context);
    return t;
}

void Parser::error(const std::string& message) {
    ParseError err;
    err.message = message;
    err.position = current_.position;
    errors_.push_back(err);
    // Error logging disabled - DocumentIndex logs with file path
    // spdlog::error("Parse error at {}:{}: {}", current_.position.line, current_.position.column, message);
}

void Parser::synchronize() {
    panicMode_ = false;
    // Skip tokens until we find a statement boundary
    while (!check(TokenType::Eof)) {
        switch (current_.type) {
            case TokenType::Semicolon:
                advance();
                return;
            case TokenType::End:
            case TokenType::Else:
            case TokenType::ElseIf:
            case TokenType::Until:
            case TokenType::Return:
            case TokenType::Break:
                return;
            default:
                advance();
                break;
        }
    }
}

std::shared_ptr<Chunk> Parser::parse() {
    auto chunk = parseBlock();
    // After parsing, there should be only Eof left
    // (but we don't error if there are extra tokens - the block parser handles it)
    return chunk;
}

std::shared_ptr<Block> Parser::parseBlock() {
    auto block = std::make_shared<Block>();

    while (!check(TokenType::Eof) &&
           !check(TokenType::End) &&
           !check(TokenType::Else) &&
           !check(TokenType::ElseIf) &&
           !check(TokenType::Until)) {

        if (panicMode_) {
            synchronize();
        }

        auto stmt = parseStatement();
        if (stmt) {
            block->statements.push_back(stmt);
        }

        // Optional semicolons
        match(TokenType::Semicolon);
    }

    return block;
}

StmtPtr Parser::parseStatement() {
    switch (current_.type) {
        case TokenType::If:     return parseIfStatement();
        case TokenType::While:  return parseWhileStatement();
        case TokenType::Repeat: return parseRepeatStatement();
        case TokenType::For:    return parseForStatement();
        case TokenType::Function: return parseFunctionStatement(false);
        case TokenType::Local:  return parseLocalStatement();
        case TokenType::Return: return parseReturnStatement();
        case TokenType::Break: {
            auto stmt = std::make_shared<BreakStatement>();
            stmt->range = current_.range();
            advance();
            return stmt;
        }
        case TokenType::Goto:   return parseGotoStatement();
        case TokenType::DoubleColon:
            // Label: ::name::
            // Actually, we need to handle ::label::
            // Let me handle it here
            return parseLabelStatement();
        case TokenType::Do: {
            auto stmt = std::make_shared<DoStatement>();
            stmt->range.start = current_.position;
            advance();  // skip 'do'
            stmt->body = parseBlock();
            expect(TokenType::End, "do statement");
            stmt->range.end = current_.position;
            return stmt;
        }
        default:
            break;
    }

    // Assignment or call statement
    // Parse prefix expression
    auto expr = parsePrefixExpr();

    if (!expr) {
        error("Expected statement");
        advance();
        return nullptr;
    }

    // Check if this is an assignment (targets = values)
    if (check(TokenType::Comma) || check(TokenType::Assign)) {
        auto assign = std::make_shared<AssignStatement>();
        assign->range.start = expr->range.start;
        assign->targets.push_back(expr);

        while (match(TokenType::Comma)) {
            assign->targets.push_back(parsePrefixExpr());
        }

        expect(TokenType::Assign, "assignment");
        assign->values = parseExpressionList();
        assign->range.end = assign->values.empty() ? current_.range().end : assign->values.back()->range.end;
        return assign;
    }

    // Otherwise it's a call statement
    auto callStmt = std::make_shared<CallStatement>();
    callStmt->expression = expr;
    callStmt->range = expr->range;
    return callStmt;
}

StmtPtr Parser::parseLocalStatement() {
    auto stmt = std::make_shared<LocalStatement>();
    stmt->range.start = current_.position;
    advance();  // skip 'local'

    // Check for local function
    if (check(TokenType::Function)) {
        return parseFunctionStatement(true);
    }

    stmt->names = parseNameList();

    if (match(TokenType::Assign)) {
        stmt->values = parseExpressionList();
    }

    stmt->range.end = stmt->values.empty()
        ? SourcePosition{current_.position.line, current_.position.column, current_.position.offset}
        : stmt->values.back()->range.end;
    return stmt;
}

StmtPtr Parser::parseIfStatement() {
    auto stmt = std::make_shared<IfStatement>();
    stmt->range.start = current_.position;
    advance();  // skip 'if'

    stmt->condition = parseExpression();
    expect(TokenType::Then, "if statement");
    stmt->thenBranch = parseBlock();

    while (match(TokenType::ElseIf)) {
        ElseIfBranch branch;
        branch.condition = parseExpression();
        expect(TokenType::Then, "elseif");
        branch.body = parseBlock();
        stmt->elseIfBranches.push_back(std::move(branch));
    }

    if (match(TokenType::Else)) {
        stmt->elseBranch = parseBlock();
    }

    expect(TokenType::End, "if statement");
    stmt->range.end = current_.position;
    return stmt;
}

StmtPtr Parser::parseWhileStatement() {
    auto stmt = std::make_shared<WhileStatement>();
    stmt->range.start = current_.position;
    advance();  // skip 'while'

    stmt->condition = parseExpression();
    expect(TokenType::Do, "while statement");
    stmt->body = parseBlock();
    expect(TokenType::End, "while statement");
    stmt->range.end = current_.position;
    return stmt;
}

StmtPtr Parser::parseRepeatStatement() {
    auto stmt = std::make_shared<RepeatStatement>();
    stmt->range.start = current_.position;
    advance();  // skip 'repeat'

    stmt->body = parseBlock();
    expect(TokenType::Until, "repeat statement");
    stmt->condition = parseExpression();
    stmt->range.end = stmt->condition->range.end;
    return stmt;
}

StmtPtr Parser::parseForStatement() {
    auto rangeStart = current_.position;
    advance();  // skip 'for'

    std::string name = consume(TokenType::Identifier, "for loop").text;

    // Check for 'in' (for-in) or '=' (numeric for)
    if (match(TokenType::Assign)) {
        // Numeric for: for i = start, stop [, step] do ... end
        auto stmt = std::make_shared<ForStatement>();
        stmt->range.start = rangeStart;
        stmt->variable = name;
        stmt->start = parseExpression();
        expect(TokenType::Comma, "for loop");
        stmt->stop = parseExpression();
        if (match(TokenType::Comma)) {
            stmt->step = parseExpression();
        }
        expect(TokenType::Do, "for loop");
        stmt->body = parseBlock();
        expect(TokenType::End, "for loop");
        stmt->range.end = current_.position;
        return stmt;
    } else {
        // For-in: for name [, name] in expr [, expr] do ... end
        auto stmt = std::make_shared<ForInStatement>();
        stmt->range.start = rangeStart;
        stmt->variables.push_back(name);

        while (match(TokenType::Comma)) {
            stmt->variables.push_back(consume(TokenType::Identifier, "for-in").text);
        }

        expect(TokenType::In, "for-in loop");
        stmt->iterators = parseExpressionList();
        expect(TokenType::Do, "for-in loop");
        stmt->body = parseBlock();
        expect(TokenType::End, "for-in loop");
        stmt->range.end = current_.position;
        return stmt;
    }
}

StmtPtr Parser::parseFunctionStatement(bool isLocal) {
    auto stmt = std::make_shared<FunctionStatement>();
    stmt->range.start = current_.position;
    stmt->isLocal = isLocal;
    advance();  // skip 'function'

    // Parse function name: name.name.name or name:name (without consuming parens)
    std::string methodName;
    stmt->name = parseFunctionName(methodName);
    stmt->methodName = methodName;

    parseFunctionBody(stmt->parameters, stmt->isVararg, stmt->body);
    stmt->range.end = current_.position;
    return stmt;
}

ExprPtr Parser::parseFunctionName(std::string& methodName) {
    // name ('.' name)* (':' name)?
    methodName.clear();

    std::string firstName = consume(TokenType::Identifier, "function name").text;
    ExprPtr expr = std::make_shared<IdentifierExpr>(firstName);

    while (check(TokenType::Dot)) {
        advance();
        std::string field = consume(TokenType::Identifier, "function name").text;
        expr = std::make_shared<MemberExpr>(expr, field);
    }

    if (check(TokenType::Colon)) {
        advance();
        methodName = consume(TokenType::Identifier, "method name").text;
    }

    return expr;
}

void Parser::parseFunctionBody(std::vector<std::string>& params, bool& isVararg, std::shared_ptr<Block>& body) {
    expect(TokenType::LeftParen, "function");

    isVararg = false;
    if (!check(TokenType::RightParen)) {
        if (check(TokenType::DotDotDot)) {
            isVararg = true;
            advance();
        } else {
            params.push_back(consume(TokenType::Identifier, "parameter").text);
            while (match(TokenType::Comma)) {
                if (check(TokenType::DotDotDot)) {
                    isVararg = true;
                    advance();
                    break;
                }
                params.push_back(consume(TokenType::Identifier, "parameter").text);
            }
        }
    }

    expect(TokenType::RightParen, "function parameters");
    body = parseBlock();
    expect(TokenType::End, "function");
}

StmtPtr Parser::parseReturnStatement() {
    auto stmt = std::make_shared<ReturnStatement>();
    stmt->range.start = current_.position;
    advance();  // skip 'return'

    // Return can have zero or more values
    if (!check(TokenType::Eof) &&
        !check(TokenType::End) &&
        !check(TokenType::Else) &&
        !check(TokenType::ElseIf) &&
        !check(TokenType::Until) &&
        !check(TokenType::Semicolon)) {
        stmt->values = parseExpressionList();
    }

    stmt->range.end = stmt->values.empty()
        ? current_.range().end
        : stmt->values.back()->range.end;
    return stmt;
}

StmtPtr Parser::parseLabelStatement() {
    // ::name::
    advance();  // skip ::
    std::string name = consume(TokenType::Identifier, "label").text;
    expect(TokenType::DoubleColon, "label");
    auto stmt = std::make_shared<LabelStatement>(name);
    stmt->range = current_.range();
    return stmt;
}

StmtPtr Parser::parseGotoStatement() {
    auto start = current_.position;
    advance();  // skip 'goto'
    std::string label = consume(TokenType::Identifier, "goto").text;
    auto stmt = std::make_shared<GotoStatement>(label);
    stmt->range.start = start;
    stmt->range.end = current_.position;
    return stmt;
}

std::vector<ExprPtr> Parser::parseExpressionList() {
    std::vector<ExprPtr> exprs;
    exprs.push_back(parseExpression());
    while (match(TokenType::Comma)) {
        exprs.push_back(parseExpression());
    }
    return exprs;
}

std::vector<std::string> Parser::parseNameList() {
    std::vector<std::string> names;
    names.push_back(consume(TokenType::Identifier, "name list").text);
    while (match(TokenType::Comma)) {
        names.push_back(consume(TokenType::Identifier, "name list").text);
    }
    return names;
}

// ============================================================
// Expression parsing (precedence climbing)
// ============================================================

ExprPtr Parser::parseExpression() {
    return parseOrExpr();
}

ExprPtr Parser::parseOrExpr() {
    auto left = parseAndExpr();
    while (check(TokenType::Or)) {
        auto op = current_.type;
        advance();
        auto right = parseAndExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseAndExpr() {
    auto left = parseComparisonExpr();
    while (check(TokenType::And)) {
        auto op = current_.type;
        advance();
        auto right = parseComparisonExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseComparisonExpr() {
    auto left = parseBitwiseOrExpr();
    while (check(TokenType::Less) || check(TokenType::Greater) ||
           check(TokenType::LessEqual) || check(TokenType::GreaterEqual) ||
           check(TokenType::NotEqual) || check(TokenType::Equal)) {
        auto op = current_.type;
        advance();
        auto right = parseBitwiseOrExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseBitwiseOrExpr() {
    auto left = parseBitwiseXorExpr();
    while (check(TokenType::Pipe)) {
        auto op = current_.type;
        advance();
        auto right = parseBitwiseXorExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseBitwiseXorExpr() {
    // We don't have a separate XOR token, use Tilde for bitwise XOR
    auto left = parseBitwiseAndExpr();
    while (check(TokenType::Tilde)) {
        auto op = current_.type;
        advance();
        auto right = parseBitwiseAndExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseBitwiseAndExpr() {
    auto left = parseShiftExpr();
    while (check(TokenType::Ampersand)) {
        auto op = current_.type;
        advance();
        auto right = parseShiftExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseShiftExpr() {
    auto left = parseConcatExpr();
    while (check(TokenType::LeftShift) || check(TokenType::RightShift)) {
        auto op = current_.type;
        advance();
        auto right = parseConcatExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseConcatExpr() {
    auto left = parseAdditiveExpr();
    if (check(TokenType::DotDot)) {
        // Right-associative: a .. b .. c = a .. (b .. c)
        auto op = current_.type;
        advance();
        auto right = parseConcatExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        return expr;
    }
    return left;
}

ExprPtr Parser::parseAdditiveExpr() {
    auto left = parseMultiplicativeExpr();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        auto op = current_.type;
        advance();
        auto right = parseMultiplicativeExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseMultiplicativeExpr() {
    auto left = parseUnaryExpr();
    while (check(TokenType::Star) || check(TokenType::Slash) ||
           check(TokenType::Percent) || check(TokenType::SlashSlash)) {
        auto op = current_.type;
        advance();
        auto right = parseUnaryExpr();
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        left = expr;
    }
    return left;
}

ExprPtr Parser::parseUnaryExpr() {
    if (check(TokenType::Not) || check(TokenType::Minus) ||
        check(TokenType::Hash) || check(TokenType::Tilde)) {
        auto op = current_.type;
        auto start = current_.position;
        advance();
        auto operand = parseUnaryExpr();
        auto expr = std::make_shared<UnaryExpr>(op, operand);
        expr->range.start = start;
        expr->range.end = operand->range.end;
        return expr;
    }
    return parsePowerExpr();
}

ExprPtr Parser::parsePowerExpr() {
    auto left = parsePrimaryExpr();
    // Power is right-associative: a^b^c = a^(b^c)
    if (check(TokenType::Caret)) {
        auto op = current_.type;
        advance();
        auto right = parseUnaryExpr();  // Right operand includes unary
        auto expr = std::make_shared<BinaryExpr>(op, left, right);
        expr->range.start = left->range.start;
        expr->range.end = right->range.end;
        return expr;
    }
    return left;
}

ExprPtr Parser::parsePrimaryExpr() {
    // Handle prefix expressions (identifiers, parenthesized expressions)
    // Then handle suffixes (.field, [index], (call), :method)

    switch (current_.type) {
        case TokenType::Nil: {
            auto expr = std::make_shared<NilLiteral>();
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::True: {
            auto expr = std::make_shared<BoolLiteral>(true);
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::False: {
            auto expr = std::make_shared<BoolLiteral>(false);
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::Number: {
            auto text = current_.text;
            bool isInt = (text.find('.') == std::string::npos &&
                          text.find('e') == std::string::npos &&
                          text.find('E') == std::string::npos &&
                          text.find('p') == std::string::npos &&
                          text.find('P') == std::string::npos);
            auto expr = std::make_shared<NumberLiteral>(text, isInt);
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::String: {
            auto expr = std::make_shared<StringLiteral>(current_.text);
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::DotDotDot: {
            auto expr = std::make_shared<VarargExpr>();
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::Identifier: {
            auto expr = std::make_shared<IdentifierExpr>(current_.text);
            expr->range = current_.range();
            advance();
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::LeftParen: {
            advance();
            auto expr = parseExpression();
            expect(TokenType::RightParen, "parenthesized expression");
            return finishCallOrMemberOrIndex(expr);
        }
        case TokenType::LeftBrace: {
            // Table constructor as function argument: func { ... }
            auto table = parseTableConstructor();
            return finishCallOrMemberOrIndex(table);
        }
        case TokenType::Function: {
            auto func = parseAnonymousFunction();
            return finishCallOrMemberOrIndex(func);
        }
        default:
            error("Unexpected token: " + std::string(tokenTypeName(current_.type)));
            advance();
            // Return a nil literal as recovery
            auto expr = std::make_shared<NilLiteral>();
            expr->range = current_.range();
            return expr;
    }
}

ExprPtr Parser::parsePrefixExpr() {
    // Similar to parsePrimaryExpr but only for prefix expressions (used in assignments)
    return parsePrimaryExpr();
}

ExprPtr Parser::finishCallOrMemberOrIndex(ExprPtr expr) {
    while (true) {
        switch (current_.type) {
            case TokenType::Dot: {
                advance();
                std::string field = consume(TokenType::Identifier, "member access").text;
                auto member = std::make_shared<MemberExpr>(expr, field);
                member->range.start = expr->range.start;
                member->range.end = current_.position;
                expr = member;
                break;
            }
            case TokenType::LeftBracket: {
                advance();
                auto index = parseExpression();
                expect(TokenType::RightBracket, "index expression");
                auto idx = std::make_shared<IndexExpr>(expr, index);
                idx->range.start = expr->range.start;
                idx->range.end = current_.position;
                expr = idx;
                break;
            }
            case TokenType::Colon: {
                // Method call: expr:method(args)
                advance();
                std::string method = consume(TokenType::Identifier, "method call").text;
                auto call = std::make_shared<CallExpr>();
                call->function = expr;
                call->method = method;
                call->range.start = expr->range.start;

                if (check(TokenType::LeftParen)) {
                    advance();
                    if (!check(TokenType::RightParen)) {
                        call->arguments = parseExpressionList();
                    }
                    expect(TokenType::RightParen, "method call");
                } else if (check(TokenType::String)) {
                    call->hasStringArg = true;
                    call->stringArg = std::make_shared<StringLiteral>(current_.text);
                    advance();
                } else if (check(TokenType::LeftBrace)) {
                    call->hasTableArg = true;
                    call->tableArg = parseTableConstructor();
                }

                call->range.end = current_.position;
                expr = call;
                break;
            }
            case TokenType::LeftParen: {
                // Function call: expr(args)
                advance();
                auto call = std::make_shared<CallExpr>();
                call->function = expr;
                call->range.start = expr->range.start;

                if (!check(TokenType::RightParen)) {
                    call->arguments = parseExpressionList();
                }
                expect(TokenType::RightParen, "function call");
                call->range.end = current_.position;
                expr = call;
                break;
            }
            case TokenType::String: {
                // String literal as function argument: expr "str"
                auto call = std::make_shared<CallExpr>();
                call->function = expr;
                call->hasStringArg = true;
                call->stringArg = std::make_shared<StringLiteral>(current_.text);
                call->stringArg->range = current_.range();
                call->range.start = expr->range.start;
                call->range.end = current_.position;
                advance();
                expr = call;
                break;
            }
            case TokenType::LeftBrace: {
                // Table constructor as function argument: expr { ... }
                auto call = std::make_shared<CallExpr>();
                call->function = expr;
                call->hasTableArg = true;
                call->tableArg = parseTableConstructor();
                call->range.start = expr->range.start;
                call->range.end = current_.position;
                expr = call;
                break;
            }
            default:
                return expr;
        }
    }
}

ExprPtr Parser::parseExpressionFromIdentifier(ExprPtr expr) {
    // First handle member access, function calls (., [, (), :)
    expr = finishCallOrMemberOrIndex(expr);

    // Then handle binary operators by delegating to the expression parsing hierarchy
    // We need to continue parsing from the additive level since we already have the left operand
    while (check(TokenType::Plus) || check(TokenType::Minus) ||
           check(TokenType::Star) || check(TokenType::Slash) ||
           check(TokenType::Percent) || check(TokenType::SlashSlash) ||
           check(TokenType::DotDot) ||
           check(TokenType::And) || check(TokenType::Or) ||
           check(TokenType::Equal) || check(TokenType::NotEqual) ||
           check(TokenType::Less) || check(TokenType::Greater) ||
           check(TokenType::LessEqual) || check(TokenType::GreaterEqual) ||
           check(TokenType::Pipe) || check(TokenType::Ampersand) ||
           check(TokenType::Tilde) || check(TokenType::LeftShift) || check(TokenType::RightShift)) {
        auto op = current_.type;
        advance();
        auto right = parseUnaryExpr();

        // Handle right-associative operators and precedence
        if (op == TokenType::Caret) {
            right = parseUnaryExpr();  // Power is right-associative
        }

        auto binaryExpr = std::make_shared<BinaryExpr>(op, expr, right);
        binaryExpr->range.start = expr->range.start;
        binaryExpr->range.end = right->range.end;
        expr = binaryExpr;
    }

    return expr;
}

std::shared_ptr<TableConstructor> Parser::parseTableConstructor() {
    auto table = std::make_shared<TableConstructor>();
    table->range.start = current_.position;
    expect(TokenType::LeftBrace, "table constructor");

    while (!check(TokenType::RightBrace) && !check(TokenType::Eof)) {
        // Skip separators
        if (check(TokenType::Comma) || check(TokenType::Semicolon)) {
            advance();
            continue;
        }

        if (check(TokenType::LeftBracket)) {
            // [expr] = expr
            advance();
            auto key = parseExpression();
            expect(TokenType::RightBracket, "table field");
            expect(TokenType::Assign, "table field");
            auto value = parseExpression();
            table->fields.emplace_back(key, value);
        } else if (check(TokenType::Identifier)) {
            // Could be: name = expr, or name (array-style), or name.field, etc.
            std::string name = current_.text;
            auto nameExpr = std::make_shared<IdentifierExpr>(name);
            nameExpr->range = current_.range();
            advance();

            if (check(TokenType::Assign)) {
                // name = expr
                advance();
                auto value = parseExpression();
                table->fields.emplace_back(nameExpr, value);
            } else if (check(TokenType::Dot) || check(TokenType::LeftBracket) || check(TokenType::LeftParen) || check(TokenType::Colon)) {
                // Complex expression: obj.field, obj[key], obj(), obj:method()
                auto expr = finishCallOrMemberOrIndex(nameExpr);
                table->fields.emplace_back(expr, true);
            } else {
                // Could be simple identifier or start of expression (y - h)
                // Parse as expression starting from this identifier
                auto expr = parseExpressionFromIdentifier(nameExpr);
                table->fields.emplace_back(expr, true);
            }
        } else {
            // Array-style: just an expression
            auto value = parseExpression();
            table->fields.emplace_back(value, true);
        }

        // Separator: comma or semicolon (optional - Lua allows missing separators)
        if (check(TokenType::Comma) || check(TokenType::Semicolon)) {
            advance();
        }
        // Continue parsing next field even without separator
    }

    expect(TokenType::RightBrace, "table constructor");
    table->range.end = current_.position;
    return table;
}

std::shared_ptr<AnonymousFunction> Parser::parseAnonymousFunction() {
    auto func = std::make_shared<AnonymousFunction>();
    func->range.start = current_.position;
    advance();  // skip 'function'
    parseFunctionBody(func->parameters, func->isVararg, func->body);
    func->range.end = current_.position;
    return func;
}

}  // namespace emmy

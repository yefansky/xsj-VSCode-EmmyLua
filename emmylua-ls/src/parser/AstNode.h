#pragma once

#include "parser/Token.h"
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace emmy {

// Forward declarations
struct AstNode;
struct Expression;
struct Statement;

using AstNodePtr = std::shared_ptr<AstNode>;
using ExprPtr = std::shared_ptr<Expression>;
using StmtPtr = std::shared_ptr<Statement>;

// ============================================================
// Base types
// ============================================================

enum class AstKind {
    // Statements
    Block,
    LocalStatement,
    AssignStatement,
    FunctionStatement,
    IfStatement,
    WhileStatement,
    RepeatStatement,
    ForStatement,
    ForInStatement,
    ReturnStatement,
    BreakStatement,
    GotoStatement,
    LabelStatement,
    DoStatement,
    CallStatement,

    // Expressions
    NilLiteral,
    BoolLiteral,
    NumberLiteral,
    StringLiteral,
    VarargExpr,
    IdentifierExpr,
    UnaryExpr,
    BinaryExpr,
    CallExpr,
    IndexExpr,
    MemberExpr,
    TableConstructor,
    AnonymousFunction,
};

struct AstNode {
    AstKind kind;
    SourceRange range;

    virtual ~AstNode() = default;

    bool isExpression() const;
    bool isStatement() const;
};

struct Expression : AstNode {};
struct Statement : AstNode {};

inline bool AstNode::isExpression() const {
    return kind >= AstKind::NilLiteral;
}
inline bool AstNode::isStatement() const {
    return kind < AstKind::NilLiteral;
}

// ============================================================
// Expressions
// ============================================================

struct NilLiteral : Expression {
    NilLiteral() { kind = AstKind::NilLiteral; }
};

struct BoolLiteral : Expression {
    bool value;
    BoolLiteral(bool v) : value(v) { kind = AstKind::BoolLiteral; }
};

struct NumberLiteral : Expression {
    std::string text;   // Original text (e.g., "0xFF", "1.5e10")
    bool isInteger;

    NumberLiteral(const std::string& t, bool integer)
        : text(t), isInteger(integer) { kind = AstKind::NumberLiteral; }
};

struct StringLiteral : Expression {
    std::string value;  // Parsed string value (escapes resolved)

    StringLiteral(const std::string& v) : value(v) { kind = AstKind::StringLiteral; }
};

struct VarargExpr : Expression {
    VarargExpr() { kind = AstKind::VarargExpr; }
};

struct IdentifierExpr : Expression {
    std::string name;

    IdentifierExpr(const std::string& n) : name(n) { kind = AstKind::IdentifierExpr; }
};

struct UnaryExpr : Expression {
    TokenType op;  // Not, Minus, Hash, Tilde (5.3)
    ExprPtr operand;

    UnaryExpr(TokenType o, ExprPtr expr) : op(o), operand(std::move(expr)) {
        kind = AstKind::UnaryExpr;
    }
};

struct BinaryExpr : Expression {
    TokenType op;  // And, Or, Plus, Minus, Star, Slash, etc.
    ExprPtr left;
    ExprPtr right;

    BinaryExpr(TokenType o, ExprPtr l, ExprPtr r)
        : op(o), left(std::move(l)), right(std::move(r)) {
        kind = AstKind::BinaryExpr;
    }
};

struct CallExpr : Expression {
    ExprPtr function;                   // The function being called
    std::vector<ExprPtr> arguments;     // Positional arguments
    std::string method;                 // If method call (a:b()), the method name
    bool hasStringArg = false;          // "func" "str" style
    ExprPtr stringArg;                  // The string argument in func "str" style
    bool hasTableArg = false;           // func {table} style
    ExprPtr tableArg;                   // The table argument

    CallExpr() { kind = AstKind::CallExpr; }
};

struct IndexExpr : Expression {
    ExprPtr object;   // t[expr]
    ExprPtr index;

    IndexExpr(ExprPtr obj, ExprPtr idx)
        : object(std::move(obj)), index(std::move(idx)) {
        kind = AstKind::IndexExpr;
    }
};

struct MemberExpr : Expression {
    ExprPtr object;     // obj.field
    std::string field;

    MemberExpr(ExprPtr obj, const std::string& f)
        : object(std::move(obj)), field(f) {
        kind = AstKind::MemberExpr;
    }
};

struct TableField {
    ExprPtr key;      // Explicit key (for [key] = value)
    ExprPtr value;
    bool arrayStyle;  // true if implicit numeric key

    TableField() : arrayStyle(false) {}
    TableField(ExprPtr v, bool arr) : value(std::move(v)), arrayStyle(arr) {}
    TableField(ExprPtr k, ExprPtr v) : key(std::move(k)), value(std::move(v)), arrayStyle(false) {}
};

struct TableConstructor : Expression {
    std::vector<TableField> fields;

    TableConstructor() { kind = AstKind::TableConstructor; }
};

struct AnonymousFunction : Expression {
    std::vector<std::string> parameters;
    bool isVararg = false;
    std::shared_ptr<struct Block> body;

    AnonymousFunction() { kind = AstKind::AnonymousFunction; }
};

// ============================================================
// Statements
// ============================================================

struct Block : Statement {
    std::vector<StmtPtr> statements;

    Block() { kind = AstKind::Block; }
};

struct LocalStatement : Statement {
    std::vector<std::string> names;
    std::vector<ExprPtr> values;

    LocalStatement() { kind = AstKind::LocalStatement; }
};

struct AssignStatement : Statement {
    std::vector<ExprPtr> targets;   // LHS (identifiers, index, member)
    std::vector<ExprPtr> values;    // RHS

    AssignStatement() { kind = AstKind::AssignStatement; }
};

struct FunctionStatement : Statement {
    ExprPtr name;                           // Function name expression (identifier or a.b.c)
    std::string methodName;                 // For function a:b() end
    std::vector<std::string> parameters;
    bool isLocal = false;                   // local function name() end
    bool isVararg = false;
    std::shared_ptr<Block> body;

    FunctionStatement() { kind = AstKind::FunctionStatement; }
};

struct ElseIfBranch {
    ExprPtr condition;
    std::shared_ptr<Block> body;
};

struct IfStatement : Statement {
    ExprPtr condition;
    std::shared_ptr<Block> thenBranch;
    std::vector<ElseIfBranch> elseIfBranches;
    std::shared_ptr<Block> elseBranch;

    IfStatement() { kind = AstKind::IfStatement; }
};

struct WhileStatement : Statement {
    ExprPtr condition;
    std::shared_ptr<Block> body;

    WhileStatement() { kind = AstKind::WhileStatement; }
};

struct RepeatStatement : Statement {
    std::shared_ptr<Block> body;
    ExprPtr condition;

    RepeatStatement() { kind = AstKind::RepeatStatement; }
};

struct ForStatement : Statement {
    std::string variable;
    ExprPtr start;
    ExprPtr stop;
    ExprPtr step;  // Optional
    std::shared_ptr<Block> body;

    ForStatement() { kind = AstKind::ForStatement; }
};

struct ForInStatement : Statement {
    std::vector<std::string> variables;
    std::vector<ExprPtr> iterators;
    std::shared_ptr<Block> body;

    ForInStatement() { kind = AstKind::ForInStatement; }
};

struct ReturnStatement : Statement {
    std::vector<ExprPtr> values;

    ReturnStatement() { kind = AstKind::ReturnStatement; }
};

struct BreakStatement : Statement {
    BreakStatement() { kind = AstKind::BreakStatement; }
};

struct GotoStatement : Statement {
    std::string label;

    GotoStatement(const std::string& l) : label(l) { kind = AstKind::GotoStatement; }
};

struct LabelStatement : Statement {
    std::string label;

    LabelStatement(const std::string& l) : label(l) { kind = AstKind::LabelStatement; }
};

struct DoStatement : Statement {
    std::shared_ptr<Block> body;

    DoStatement() { kind = AstKind::DoStatement; }
};

// Statement wrapper for call expressions (func() as a statement)
struct CallStatement : Statement {
    ExprPtr expression;

    CallStatement() { kind = AstKind::CallStatement; }
};

// ============================================================
// Root node
// ============================================================

using Chunk = Block;

}  // namespace emmy

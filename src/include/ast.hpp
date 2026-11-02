#ifndef ASCEND_AST_HPP
#define ASCEND_AST_HPP

#include "lexer.hpp"
#include "value.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ascend {

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ---------------------------------------------------------------------------
// Expressions

enum class ExprKind {
    Literal, Variable, Unary, Binary, Logical, Ternary,
    Call, Index, Slice, Member, List, Map, Function
};

struct Expr {
    Expr(ExprKind kind, int line, int column) : kind(kind), line(line), column(column) {}
    virtual ~Expr() = default;

    template <typename T>
    const T& as() const { return static_cast<const T&>(*this); }

    const ExprKind kind;
    const int line;
    const int column;
};

struct LiteralExpr final : Expr {
    LiteralExpr(int line, int column) : Expr(ExprKind::Literal, line, column) {}
    Value value;
};

struct VariableExpr final : Expr {
    VariableExpr(int line, int column) : Expr(ExprKind::Variable, line, column) {}
    std::string name;
};

struct UnaryExpr final : Expr {
    UnaryExpr(int line, int column) : Expr(ExprKind::Unary, line, column) {}
    TokenType op = TokenType::Minus;  // Minus or Not
    ExprPtr operand;
};

struct BinaryExpr final : Expr {
    BinaryExpr(int line, int column) : Expr(ExprKind::Binary, line, column) {}
    TokenType op = TokenType::Plus;
    ExprPtr left;
    ExprPtr right;
};

// `and` / `or`, which only evaluate their right side when needed.
struct LogicalExpr final : Expr {
    LogicalExpr(int line, int column) : Expr(ExprKind::Logical, line, column) {}
    TokenType op = TokenType::And;
    ExprPtr left;
    ExprPtr right;
};

struct TernaryExpr final : Expr {
    TernaryExpr(int line, int column) : Expr(ExprKind::Ternary, line, column) {}
    ExprPtr condition;
    ExprPtr thenExpr;
    ExprPtr elseExpr;
};

struct CallExpr final : Expr {
    CallExpr(int line, int column) : Expr(ExprKind::Call, line, column) {}
    ExprPtr callee;
    std::vector<ExprPtr> args;
};

struct IndexExpr final : Expr {
    IndexExpr(int line, int column) : Expr(ExprKind::Index, line, column) {}
    ExprPtr object;
    ExprPtr index;
};

// object[start:end]; either bound may be missing.
struct SliceExpr final : Expr {
    SliceExpr(int line, int column) : Expr(ExprKind::Slice, line, column) {}
    ExprPtr object;
    ExprPtr start;
    ExprPtr end;
};

// object.name
struct MemberExpr final : Expr {
    MemberExpr(int line, int column) : Expr(ExprKind::Member, line, column) {}
    ExprPtr object;
    std::string name;
};

struct ListExpr final : Expr {
    ListExpr(int line, int column) : Expr(ExprKind::List, line, column) {}
    std::vector<ExprPtr> elements;
};

struct MapExpr final : Expr {
    MapExpr(int line, int column) : Expr(ExprKind::Map, line, column) {}
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;
};

// ---------------------------------------------------------------------------
// Statements

enum class StmtKind {
    Expression, Declare, Assign, Block, If, While, For,
    Break, Continue, Return, Function, Try, Throw
};

struct Stmt {
    Stmt(StmtKind kind, int line, int column) : kind(kind), line(line), column(column) {}
    virtual ~Stmt() = default;

    template <typename T>
    const T& as() const { return static_cast<const T&>(*this); }

    const StmtKind kind;
    const int line;
    const int column;
};

// A sequence of statements with facts the interpreter uses to run it quickly.
struct Block {
    std::vector<StmtPtr> statements;
    bool declaresNames = false;   // contains `:=` or `func` directly, so it needs its own scope
    bool hasFunctions = false;    // contains `func` declarations, which are hoisted
};

struct Param {
    std::string name;
    ExprPtr defaultValue;  // may be null
};

struct FunctionExpr final : Expr {
    FunctionExpr(int line, int column) : Expr(ExprKind::Function, line, column) {}
    std::string name;  // empty for anonymous functions
    std::vector<Param> params;
    size_t requiredParams = 0;  // parameters without a default value
    Block body;
};

struct ExpressionStmt final : Stmt {
    ExpressionStmt(int line, int column) : Stmt(StmtKind::Expression, line, column) {}
    ExprPtr expr;
};

// name := value
struct DeclareStmt final : Stmt {
    DeclareStmt(int line, int column) : Stmt(StmtKind::Declare, line, column) {}
    std::string name;
    ExprPtr value;
};

// target = value, target += value, ...
struct AssignStmt final : Stmt {
    AssignStmt(int line, int column) : Stmt(StmtKind::Assign, line, column) {}
    ExprPtr target;  // VariableExpr, IndexExpr or MemberExpr
    TokenType op = TokenType::Equal;
    ExprPtr value;
};

struct BlockStmt final : Stmt {
    BlockStmt(int line, int column) : Stmt(StmtKind::Block, line, column) {}
    Block body;
};

struct IfStmt final : Stmt {
    IfStmt(int line, int column) : Stmt(StmtKind::If, line, column) {}
    ExprPtr condition;
    Block thenBody;
    StmtPtr elseBranch;  // a BlockStmt, another IfStmt (else if), or null
};

struct WhileStmt final : Stmt {
    WhileStmt(int line, int column) : Stmt(StmtKind::While, line, column) {}
    ExprPtr condition;
    Block body;
};

// for first in iterable { }   /   for first, second in iterable { }
struct ForStmt final : Stmt {
    ForStmt(int line, int column) : Stmt(StmtKind::For, line, column) {}
    std::string first;
    std::string second;  // empty when only one loop variable is given
    ExprPtr iterable;
    Block body;
};

struct BreakStmt final : Stmt {
    BreakStmt(int line, int column) : Stmt(StmtKind::Break, line, column) {}
};

struct ContinueStmt final : Stmt {
    ContinueStmt(int line, int column) : Stmt(StmtKind::Continue, line, column) {}
};

struct ReturnStmt final : Stmt {
    ReturnStmt(int line, int column) : Stmt(StmtKind::Return, line, column) {}
    ExprPtr value;  // may be null
};

struct FunctionStmt final : Stmt {
    FunctionStmt(int line, int column) : Stmt(StmtKind::Function, line, column) {}
    std::unique_ptr<FunctionExpr> function;
};

struct TryStmt final : Stmt {
    TryStmt(int line, int column) : Stmt(StmtKind::Try, line, column) {}
    Block body;
    std::string errorName;  // may be empty: `catch { ... }`
    Block handler;
};

struct ThrowStmt final : Stmt {
    ThrowStmt(int line, int column) : Stmt(StmtKind::Throw, line, column) {}
    ExprPtr value;
};

struct Program {
    Block body;
};

}  // namespace ascend

#endif  // ASCEND_AST_HPP

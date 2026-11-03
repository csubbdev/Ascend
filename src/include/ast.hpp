#ifndef ASCEND_AST_HPP
#define ASCEND_AST_HPP

#include "lexer.hpp"
#include "types.hpp"
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
    Call, Index, Slice, Member, List, Map, Function, FString, IncDec,
    Comprehension, Range, Spread
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
    TokenType op = TokenType::Minus;  // Minus, Not or Tilde
    ExprPtr operand;
};

struct BinaryExpr final : Expr {
    BinaryExpr(int line, int column) : Expr(ExprKind::Binary, line, column) {}
    TokenType op = TokenType::Plus;
    ExprPtr left;
    ExprPtr right;
};

// `and` / `or` / `??`, which only evaluate their right side when needed.
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

// f(a, b, name: c, ...rest)
struct CallExpr final : Expr {
    CallExpr(int line, int column) : Expr(ExprKind::Call, line, column) {}
    ExprPtr callee;
    std::vector<ExprPtr> args;
    std::vector<std::string> argNames;  // one per argument: "" for positional ones
    bool hasNamedArgs = false;
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

// object.name, or object?.name (null when the object is null)
struct MemberExpr final : Expr {
    MemberExpr(int line, int column) : Expr(ExprKind::Member, line, column) {}
    ExprPtr object;
    std::string name;
    bool optional = false;
};

struct ListExpr final : Expr {
    ListExpr(int line, int column) : Expr(ExprKind::List, line, column) {}
    std::vector<ExprPtr> elements;
};

struct MapExpr final : Expr {
    MapExpr(int line, int column) : Expr(ExprKind::Map, line, column) {}
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;
};

// x++, x--, ++x, --x (also on elements and fields: items[i]++, p.count--)
struct IncDecExpr final : Expr {
    IncDecExpr(int line, int column) : Expr(ExprKind::IncDec, line, column) {}
    ExprPtr target;  // VariableExpr, IndexExpr or MemberExpr
    bool increment = true;
    bool prefix = false;  // ++x gives the new value, x++ the old one
};

// [x * 2 for x in items if x > 0]   {k: v for k, v in pairs}
struct ComprehensionClause {
    bool isFor = true;       // `for a, b in iterable`, or `if condition`
    std::string first;
    std::string second;      // may be empty
    ExprPtr expr;            // the iterable or the condition
};

struct ComprehensionExpr final : Expr {
    ComprehensionExpr(int line, int column) : Expr(ExprKind::Comprehension, line, column) {}
    bool isMap = false;
    ExprPtr key;             // maps only
    ExprPtr value;           // the list element, or the map value
    std::vector<ComprehensionClause> clauses;
};

// start..end (end not included) and start..=end (included)
struct RangeExpr final : Expr {
    RangeExpr(int line, int column) : Expr(ExprKind::Range, line, column) {}
    ExprPtr start;
    ExprPtr end;
    bool inclusive = false;
};

// ...items: spreads a list into a call's arguments or a list, or a map into a map
struct SpreadExpr final : Expr {
    SpreadExpr(int line, int column) : Expr(ExprKind::Spread, line, column) {}
    ExprPtr value;
};

// f"text {expression:spec} more text"
struct FStringPart {
    std::string text;   // literal text (when expr is null)
    ExprPtr expr;       // an interpolated expression
    std::string spec;   // its format spec, e.g. ">8" or ".2f"
};

struct FStringExpr final : Expr {
    FStringExpr(int line, int column) : Expr(ExprKind::FString, line, column) {}
    std::vector<FStringPart> parts;
};

// ---------------------------------------------------------------------------
// Statements

enum class StmtKind {
    Expression, Declare, Assign, Block, If, While, For, CFor,
    Break, Continue, Return, Function, Try, Throw, Match,
    Switch, Import, Struct, Enum, Group, MultiAssign
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
    bool declaresNames = false;   // declares variables directly, so it needs its own scope
    bool hasFunctions = false;    // contains function or struct declarations, which are hoisted
};

struct Param {
    std::string name;
    TypeSpec type;         // `any` when the parameter has no type
    ExprPtr defaultValue;  // may be null
};

struct FunctionExpr final : Expr {
    FunctionExpr(int line, int column) : Expr(ExprKind::Function, line, column) {}
    std::string name;  // empty for anonymous functions
    std::vector<Param> params;
    size_t requiredParams = 0;  // parameters without a default value
    bool variadic = false;      // the last parameter (...rest) collects the remaining arguments in a list
    TypeSpec returnType;        // `any` when the function has no return type
    Block body;
};

struct ExpressionStmt final : Stmt {
    ExpressionStmt(int line, int column) : Stmt(StmtKind::Expression, line, column) {}
    ExprPtr expr;
};

// name := value      type name = value      type name      const [type] name = value
// auto name = value   (the variable gets the type of its first value)
struct DeclareStmt final : Stmt {
    DeclareStmt(int line, int column) : Stmt(StmtKind::Declare, line, column) {}
    std::string name;
    TypeSpec type;          // `any` when no type was written
    bool hasType = false;
    bool inferType = false;  // `auto`
    bool constant = false;
    ExprPtr value;          // null for `type name`, which starts at the type's zero value
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

// while (condition) { }   and   do { } while (condition)
struct WhileStmt final : Stmt {
    WhileStmt(int line, int column) : Stmt(StmtKind::While, line, column) {}
    ExprPtr condition;
    Block body;
    bool doWhile = false;  // the body runs once before the condition is checked
};

// for first in iterable { }   /   for first, second in iterable { }
// The variables may have types: for (string name : names) { }
struct ForStmt final : Stmt {
    ForStmt(int line, int column) : Stmt(StmtKind::For, line, column) {}
    std::string first;
    std::string second;  // empty when only one loop variable is given
    TypeSpec firstType;
    TypeSpec secondType;
    ExprPtr iterable;
    Block body;
};

// for (init; condition; step) { }   Each part may be empty, and init and step
// may list several statements separated by commas.
struct CForStmt final : Stmt {
    CForStmt(int line, int column) : Stmt(StmtKind::CFor, line, column) {}
    std::vector<StmtPtr> init;
    ExprPtr condition;  // null: loop until `break`
    std::vector<StmtPtr> step;
    Block body;
    // The body creates functions, which may capture the loop variables, so
    // each iteration gets its own copy of them (like `let` in JavaScript).
    bool copyPerIteration = false;
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
    bool hasCatch = false;
    std::string errorName;  // may be empty: `catch { ... }`
    Block handler;
    bool hasFinally = false;
    Block finallyBody;      // runs however the try block ends
};

// a, b := pair      a, b = b, a      x, y = point_list
struct MultiAssignStmt final : Stmt {
    MultiAssignStmt(int line, int column) : Stmt(StmtKind::MultiAssign, line, column) {}
    std::vector<ExprPtr> targets;  // VariableExpr (for :=), or anything assignable
    bool declare = false;
    std::vector<ExprPtr> values;   // one list to unpack, or one value per target
};

struct ThrowStmt final : Stmt {
    ThrowStmt(int line, int column) : Stmt(StmtKind::Throw, line, column) {}
    ExprPtr value;
};

// match value {
//     1, 2 => print("small")
//     else => { print("other") }
// }
struct MatchArm {
    std::vector<ExprPtr> patterns;  // empty for the default arm (`else` or `_`)
    Block body;                     // a { block } or a single statement
};

struct MatchStmt final : Stmt {
    MatchStmt(int line, int column) : Stmt(StmtKind::Match, line, column) {}
    ExprPtr subject;
    std::vector<MatchArm> arms;
};

// switch (value) {
// case 1, 2:
//     print("small")
//     break
// default:
//     print("other")
// }
// Like C, execution continues into the next case unless there's a `break`.
struct SwitchCase {
    std::vector<ExprPtr> values;  // empty for `default`
    size_t start = 0;             // index of its first statement in the body
};

struct SwitchStmt final : Stmt {
    SwitchStmt(int line, int column) : Stmt(StmtKind::Switch, line, column) {}
    ExprPtr subject;
    std::vector<SwitchCase> cases;
    Block body;  // every case's statements, in order
};

// import geometry                 import lib.shapes as shapes
// import "path/to/file" as name   from geometry import area, Point as P
struct ImportName {
    std::string name;     // the module path, or the exported name for `from`
    std::string binding;  // the variable it's stored in
};

struct ImportStmt final : Stmt {
    ImportStmt(int line, int column) : Stmt(StmtKind::Import, line, column) {}
    bool from = false;
    std::string fromPath;            // for `from path import ...`
    bool all = false;                // from path import *
    std::vector<ImportName> names;   // modules (import), or exported names (from)
};

// struct Point {
//     f64 x
//     f64 y = 0
// }
struct StructField {
    std::string name;
    TypeSpec type;          // `any` when no type was written
    ExprPtr defaultValue;   // may be null: then the type's zero value
    int line = 0;
    int column = 0;
};

struct StructStmt final : Stmt {
    StructStmt(int line, int column) : Stmt(StmtKind::Struct, line, column) {}
    std::string name;
    std::string parent;  // struct Dog : Animal { ... }
    std::vector<StructField> fields;
    // name -> method. Methods get the value they're called on as their first
    // parameter, `this`.
    std::vector<std::pair<std::string, std::unique_ptr<FunctionExpr>>> methods;
};

// enum Color { Red, Green = 5, Blue }  makes the constant map Color.
struct EnumMember {
    std::string name;
    ExprPtr value;  // may be null: one more than the previous member
    int line = 0;
    int column = 0;
};

struct EnumStmt final : Stmt {
    EnumStmt(int line, int column) : Stmt(StmtKind::Enum, line, column) {}
    std::string name;
    std::vector<EnumMember> members;
};

// Several statements written as one, run in the enclosing scope:
// `i32 a = 1, b = 2`, or `i = 0, j = n` in a for header.
struct GroupStmt final : Stmt {
    GroupStmt(int line, int column) : Stmt(StmtKind::Group, line, column) {}
    std::vector<StmtPtr> statements;
};

struct Program {
    Block body;
};

}  // namespace ascend

#endif  // ASCEND_AST_HPP

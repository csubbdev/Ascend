#include "lexer.hpp"
#include "parser.hpp"
#include "test_framework.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace ascend;

namespace {

struct Parsed {
    std::unique_ptr<Program> program;
    std::vector<Diagnostic> errors;
    bool incomplete = false;
};

Parsed parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Parsed result;
    result.program = parser.parseProgram();
    result.errors = parser.errors();
    result.incomplete = parser.incomplete();
    return result;
}

std::string op(TokenType type) {
    switch (type) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::StarStar: return "**";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::EqualEqual: return "==";
        case TokenType::NotEqual: return "!=";
        case TokenType::Less: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::Greater: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::In: return "in";
        case TokenType::And: return "and";
        case TokenType::Or: return "or";
        case TokenType::Not: return "not";
        default: return "?";
    }
}

// Renders an expression as an S-expression, e.g. "(+ 1 (* 2 3))".
std::string dump(const Expr* expr) {
    if (expr == nullptr) return "_";
    switch (expr->kind) {
        case ExprKind::Literal: return toRepr(expr->as<LiteralExpr>().value);
        case ExprKind::Variable: return expr->as<VariableExpr>().name;
        case ExprKind::Unary: {
            const auto& unary = expr->as<UnaryExpr>();
            return "(" + op(unary.op) + " " + dump(unary.operand.get()) + ")";
        }
        case ExprKind::Binary: {
            const auto& binary = expr->as<BinaryExpr>();
            return "(" + op(binary.op) + " " + dump(binary.left.get()) + " " + dump(binary.right.get()) + ")";
        }
        case ExprKind::Logical: {
            const auto& logical = expr->as<LogicalExpr>();
            return "(" + op(logical.op) + " " + dump(logical.left.get()) + " " + dump(logical.right.get()) + ")";
        }
        case ExprKind::Ternary: {
            const auto& ternary = expr->as<TernaryExpr>();
            return "(? " + dump(ternary.condition.get()) + " " + dump(ternary.thenExpr.get()) + " " +
                   dump(ternary.elseExpr.get()) + ")";
        }
        case ExprKind::Call: {
            const auto& call = expr->as<CallExpr>();
            std::string out = "(call " + dump(call.callee.get());
            for (const auto& arg : call.args) out += " " + dump(arg.get());
            return out + ")";
        }
        case ExprKind::Index: {
            const auto& index = expr->as<IndexExpr>();
            return "(index " + dump(index.object.get()) + " " + dump(index.index.get()) + ")";
        }
        case ExprKind::Slice: {
            const auto& slice = expr->as<SliceExpr>();
            return "(slice " + dump(slice.object.get()) + " " + dump(slice.start.get()) + " " + dump(slice.end.get()) + ")";
        }
        case ExprKind::Member: {
            const auto& member = expr->as<MemberExpr>();
            return "(. " + dump(member.object.get()) + " " + member.name + ")";
        }
        case ExprKind::List: {
            std::string out = "[";
            const auto& elements = expr->as<ListExpr>().elements;
            for (size_t i = 0; i < elements.size(); ++i) out += (i ? " " : "") + dump(elements[i].get());
            return out + "]";
        }
        case ExprKind::Map: {
            std::string out = "{";
            const auto& entries = expr->as<MapExpr>().entries;
            for (size_t i = 0; i < entries.size(); ++i) {
                out += (i ? " " : "") + dump(entries[i].first.get()) + ":" + dump(entries[i].second.get());
            }
            return out + "}";
        }
        case ExprKind::Function: {
            const auto& function = expr->as<FunctionExpr>();
            std::string out = "(func";
            for (const auto& param : function.params) out += " " + param.name;
            return out + ")";
        }
    }
    return "?";
}

// Parses `source`, which must be a single expression statement, and dumps it.
std::string expr(const std::string& source) {
    Parsed parsed = parse(source);
    if (!parsed.errors.empty()) return "error: " + parsed.errors[0].message;
    if (parsed.program->body.statements.size() != 1) return "error: expected one statement";
    const Stmt& stmt = *parsed.program->body.statements[0];
    if (stmt.kind != StmtKind::Expression) return "error: not an expression statement";
    return dump(stmt.as<ExpressionStmt>().expr.get());
}

std::string firstError(const std::string& source) {
    Parsed parsed = parse(source);
    return parsed.errors.empty() ? "" : parsed.errors[0].message;
}

const Stmt& onlyStatement(const Parsed& parsed) { return *parsed.program->body.statements.at(0); }

}  // namespace

TEST(parser_arithmetic_precedence) {
    CHECK_EQ(expr("1 + 2 * 3"), std::string("(+ 1 (* 2 3))"));
    CHECK_EQ(expr("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));
    CHECK_EQ(expr("10 - 4 - 3"), std::string("(- (- 10 4) 3)"));
    CHECK_EQ(expr("7 % 4 / 2"), std::string("(/ (% 7 4) 2)"));
}

TEST(parser_power_and_unary_minus) {
    CHECK_EQ(expr("-2 ** 2"), std::string("(- (** 2 2))"));
    CHECK_EQ(expr("2 ** 3 ** 2"), std::string("(** 2 (** 3 2))"));
    CHECK_EQ(expr("2 ** -1"), std::string("(** 2 (- 1))"));
}

TEST(parser_logic_and_comparison) {
    CHECK_EQ(expr("a > 5 and y < 10 or !c"), std::string("(or (and (> a 5) (< y 10)) (not c))"));
    CHECK_EQ(expr("a == b != c"), std::string("(!= (== a b) c)"));
    CHECK_EQ(expr("x in items"), std::string("(in x items)"));
}

TEST(parser_not_applies_to_whole_comparison) {
    CHECK_EQ(expr("not x in items"), std::string("(not (in x items))"));
    CHECK_EQ(expr("!a == b"), std::string("(not (== a b))"));
    CHECK_EQ(expr("x == !y"), std::string("(== x (not y))"));
}

TEST(parser_ternary) {
    CHECK_EQ(expr("a ? b : c"), std::string("(? a b c)"));
    CHECK_EQ(expr("a ? b : c ? d : e"), std::string("(? a b (? c d e))"));
}

TEST(parser_postfix_chains) {
    CHECK_EQ(expr("a.b(c)[0]"), std::string("(index (call (. a b) c) 0)"));
    CHECK_EQ(expr("f()()"), std::string("(call (call f))"));
    CHECK_EQ(expr("s[1:]"), std::string("(slice s 1 _)"));
    CHECK_EQ(expr("s[:2]"), std::string("(slice s _ 2)"));
    CHECK_EQ(expr("s[:]"), std::string("(slice s _ _)"));
}

TEST(parser_literals) {
    CHECK_EQ(expr("[1, \"two\", 3.5, true, null,]"), std::string("[1 \"two\" 3.5 true null]"));
    CHECK_EQ(expr("x({a: 1, \"b c\": 2, (k): 3})"), std::string("(call x {\"a\":1 \"b c\":2 k:3})"));
    CHECK_EQ(expr("func(a, b) { return a }"), std::string("(func a b)"));
}

TEST(parser_multiline_expressions) {
    CHECK_EQ(expr("f(1,\n  2)"), std::string("(call f 1 2)"));
    CHECK_EQ(expr("1 +\n  2"), std::string("(+ 1 2)"));
    CHECK_EQ(expr("x({\n  a: 1,\n  b: 2\n})"), std::string("(call x {\"a\":1 \"b\":2})"));
}

TEST(parser_declarations_and_assignments) {
    Parsed declare = parse("x := 5");
    CHECK(declare.errors.empty());
    CHECK(onlyStatement(declare).kind == StmtKind::Declare);
    CHECK_EQ(onlyStatement(declare).as<DeclareStmt>().name, std::string("x"));
    CHECK(declare.program->body.declaresNames);

    Parsed compound = parse("items[0] += 2");
    CHECK(compound.errors.empty());
    const auto& assign = onlyStatement(compound).as<AssignStmt>();
    CHECK(assign.op == TokenType::PlusEqual);
    CHECK_EQ(dump(assign.target.get()), std::string("(index items 0)"));
    CHECK(!compound.program->body.declaresNames);

    Parsed field = parse("p.name = \"x\"");
    CHECK(field.errors.empty());
    CHECK(onlyStatement(field).kind == StmtKind::Assign);
}

TEST(parser_rejects_invalid_assignment_targets) {
    CHECK_EQ(firstError("1 = 2"), std::string("can't assign to this expression"));
    CHECK(firstError("items[0] := 1").find("':=' can only declare a plain variable name") == 0);
    CHECK(firstError("f() = 1") == "can't assign to this expression");
}

TEST(parser_if_else_chains) {
    Parsed parsed = parse("if a {\n  x := 1\n}\nelse if b {\n  x := 2\n} else {\n  x := 3\n}");
    CHECK(parsed.errors.empty());
    CHECK_EQ(parsed.program->body.statements.size(), size_t(1));
    const auto& first = onlyStatement(parsed).as<IfStmt>();
    CHECK(first.elseBranch->kind == StmtKind::If);
    const auto& second = first.elseBranch->as<IfStmt>();
    CHECK(second.elseBranch->kind == StmtKind::Block);
}

TEST(parser_parenthesized_conditions_and_allman_braces) {
    CHECK(parse("if (x > 1) {\n}").errors.empty());
    CHECK(parse("while (x > 0)\n{\n  x -= 1\n}").errors.empty());
    CHECK(parse("func f(a)\n{\n  return a\n}").errors.empty());
}

TEST(parser_for_loops) {
    Parsed simple = parse("for i in range(3) { }");
    CHECK(simple.errors.empty());
    CHECK_EQ(onlyStatement(simple).as<ForStmt>().first, std::string("i"));

    Parsed pair = parse("for k, v in m { }");
    CHECK(pair.errors.empty());
    CHECK_EQ(onlyStatement(pair).as<ForStmt>().second, std::string("v"));

    CHECK(parse("for (x in xs) { }").errors.empty());
    CHECK(firstError("for in xs { }").find("expected a loop variable name") == 0);
}

TEST(parser_functions) {
    Parsed parsed = parse("func greet(name, greeting = \"hi\") { return greeting + name }");
    CHECK(parsed.errors.empty());
    CHECK(parsed.program->body.hasFunctions);
    const auto& function = *onlyStatement(parsed).as<FunctionStmt>().function;
    CHECK_EQ(function.name, std::string("greet"));
    CHECK_EQ(function.params.size(), size_t(2));
    CHECK_EQ(function.requiredParams, size_t(1));
    CHECK(firstError("func f(a = 1, b) { }").find("needs a default value") != std::string::npos);
}

TEST(parser_statement_separators) {
    Parsed parsed = parse("a := 1; b := 2;\nc := 3");
    CHECK(parsed.errors.empty());
    CHECK_EQ(parsed.program->body.statements.size(), size_t(3));
    CHECK_EQ(firstError("x := 1 y := 2"), std::string("expected a newline or ';' after the statement, found identifier 'y'"));
}

TEST(parser_reports_several_errors) {
    Parsed parsed = parse("x := )\ny := 2\nz := (\nprint(y)");
    CHECK(parsed.errors.size() >= 2);
    CHECK_EQ(parsed.errors[0].line, 1);
}

TEST(parser_error_positions) {
    Parsed parsed = parse("x := 1\nprint(\"hi\" 5)");
    CHECK_EQ(parsed.errors.size(), size_t(1));
    CHECK_EQ(parsed.errors[0].line, 2);
    CHECK_EQ(parsed.errors[0].column, 12);
}

TEST(parser_detects_incomplete_input) {
    CHECK(parse("if x {").incomplete);
    CHECK(parse("x := 1 +").incomplete);
    CHECK(parse("print(1,").incomplete);
    CHECK(parse("items := [1, 2").incomplete);
    CHECK(!parse("x := )").incomplete);
    CHECK(!parse("x := 1").incomplete);
}

TEST(parser_stray_tokens) {
    CHECK_EQ(firstError("}"), std::string("unexpected '}' (there is no open block to close)"));
    CHECK_EQ(firstError("else { }"), std::string("'else' without a matching 'if'"));
    CHECK(firstError("if { }").find("expected an expression after 'if'") == 0);
}

TEST(parser_survives_deep_nesting) {
    std::string deep(5000, '(');
    deep += "1";
    deep += std::string(5000, ')');
    CHECK_EQ(firstError(deep), std::string("code is nested too deeply"));
}

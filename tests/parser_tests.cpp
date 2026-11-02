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
        case TokenType::Tilde: return "~";
        case TokenType::Ampersand: return "&";
        case TokenType::Pipe: return "|";
        case TokenType::Caret: return "^";
        case TokenType::ShiftLeft: return "<<";
        case TokenType::ShiftRight: return ">>";
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
            for (const auto& param : function.params) {
                out += " " + param.name;
                if (!param.type.isAny()) out += ":" + param.type.name();
            }
            if (!function.returnType.isAny()) out += " -> " + function.returnType.name();
            return out + ")";
        }
        case ExprKind::IncDec: {
            const auto& incdec = expr->as<IncDecExpr>();
            std::string symbol = incdec.increment ? "++" : "--";
            return incdec.prefix ? "(" + symbol + dump(incdec.target.get()) + ")"
                                 : "(" + dump(incdec.target.get()) + symbol + ")";
        }
        case ExprKind::FString: {
            std::string out = "(f";
            for (const auto& part : expr->as<FStringExpr>().parts) {
                out += " " + (part.expr ? dump(part.expr.get()) + (part.spec.empty() ? "" : ":" + part.spec)
                                        : toRepr(Value::fromString(part.text)));
            }
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
    CHECK_EQ(expr("2 ** -1"), std::string("(** 2 -1)"));  // a minus before a number is part of the literal
    CHECK_EQ(expr("-x ** 2"), std::string("(- (** x 2))"));
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

TEST(parser_bitwise_precedence) {
    CHECK_EQ(expr("a & 1 == 0"), std::string("(== (& a 1) 0)"));  // unlike C, & binds tighter than ==
    CHECK_EQ(expr("1 << 2 + 3"), std::string("(<< 1 (+ 2 3))"));
    CHECK_EQ(expr("a | b ^ c & d"), std::string("(| a (^ b (& c d)))"));
    CHECK_EQ(expr("~x & 0xFF"), std::string("(& (~ x) 255)"));
}

TEST(parser_typed_declarations) {
    Parsed typed = parse("u8 age = 30");
    CHECK(typed.errors.empty());
    const auto& decl = onlyStatement(typed).as<DeclareStmt>();
    CHECK(decl.hasType);
    CHECK(decl.type.kind == TypeKind::Number);
    CHECK(decl.type.num == NumKind::U8);
    CHECK(decl.value != nullptr);

    Parsed zero = parse("string? name");
    CHECK(zero.errors.empty());
    const auto& noValue = onlyStatement(zero).as<DeclareStmt>();
    CHECK(noValue.type.nullable);
    CHECK(noValue.value == nullptr);

    Parsed constant = parse("const i64 MAX = 10");
    CHECK(constant.errors.empty());
    CHECK(onlyStatement(constant).as<DeclareStmt>().constant);
    CHECK(parse("const LIMIT := 3").errors.empty());

    // Other names may be structs, so unknown types are reported by the semantic analyzer.
    CHECK(parse("int32 x = 1").errors.empty());
    CHECK_EQ(firstError("void x"), std::string("'void' can only be used as a return type"));
    CHECK_EQ(firstError("i32 x := 5"), std::string("use '=' in a typed declaration, like 'i32 x = ...'"));
    CHECK_EQ(firstError("const X"), std::string("a constant needs a value: 'const X = ...'"));
}

TEST(parser_old_style_types_get_a_hint) {
    CHECK_EQ(firstError("age: u8 = 30"), std::string("write the type before the name, like 'u8 age = ...'"));
    CHECK_EQ(firstError("const MAX: i64 = 1"), std::string("write the type before the name, like 'const i64 MAX = ...'"));
    CHECK_EQ(firstError("func f(n: u8) { }"), std::string("write the parameter's type before its name, like 'u8 n'"));
}

TEST(parser_c_style_functions) {
    Parsed parsed = parse("i32 add(i32 a, i32 b = 1) {\n  return a + b\n}");
    CHECK(parsed.errors.empty());
    CHECK(parsed.program->body.hasFunctions);
    const auto& function = *onlyStatement(parsed).as<FunctionStmt>().function;
    CHECK_EQ(function.name, std::string("add"));
    CHECK_EQ(function.returnType.name(), std::string("i32"));
    CHECK_EQ(function.params[0].type.name(), std::string("i32"));
    CHECK_EQ(function.requiredParams, size_t(1));
    CHECK(parse("void log(string message) { }").errors.empty());
    CHECK(parse("string? find(list items, any target) { return null }").errors.empty());
    CHECK_EQ(firstError("i32 f() -> i32 { }"), std::string("the return type is already written before the function name"));
    // A type name followed by '(' is still a call: string(5), list(...) are conversions.
    CHECK_EQ(expr("string(5)"), std::string("(call string 5)"));
}

TEST(parser_func_typed_variables) {
    Parsed holder = parse("func handler = print");
    CHECK(holder.errors.empty());
    CHECK(onlyStatement(holder).kind == StmtKind::Declare);
    CHECK(parse("func? callback").errors.empty());
    CHECK(onlyStatement(parse("func greet() { }")).kind == StmtKind::Function);
}

TEST(parser_typed_functions) {
    CHECK_EQ(expr("func(i32 a, string? b = null) -> f64 { return 1.0 }"), std::string("(func a:i32 b:string? -> f64)"));
    CHECK_EQ(expr("func() -> void { }"), std::string("(func -> void)"));
}

TEST(parser_number_literal_ranges) {
    CHECK_EQ(expr("-128i8"), std::string("-128"));
    CHECK_EQ(expr("-9223372036854775808"), std::string("-9223372036854775808"));
    CHECK_EQ(expr("18446744073709551615u64"), std::string("18446744073709551615"));
    CHECK_EQ(firstError("300u8"), std::string("300u8 doesn't fit in u8 (0 to 255)"));
    CHECK_EQ(firstError("-1u8"), std::string("-1u8 is negative, but u8 is unsigned"));
    CHECK(firstError("9223372036854775808").find("9223372036854775808 is too large for int") == 0);
}

TEST(parser_match) {
    Parsed parsed = parse("match x {\n  1, 2 => print(1)\n  3 => {\n    y := 2\n  }\n  else => print(0)\n}");
    CHECK(parsed.errors.empty());
    const auto& match = onlyStatement(parsed).as<MatchStmt>();
    CHECK_EQ(match.arms.size(), size_t(3));
    CHECK_EQ(match.arms[0].patterns.size(), size_t(2));
    CHECK(match.arms[1].body.declaresNames);
    CHECK(match.arms[2].patterns.empty());
    CHECK(parse("match x { _ => print(0) }").errors.empty());
    CHECK_EQ(firstError("match x {\n else => 1\n _ => 2\n}"), std::string("a match can only have one default ('else') arm"));
    CHECK_EQ(firstError("match x { 1 print(1) }"), std::string("expected '=>' after the match pattern, found identifier 'print'"));
}

TEST(parser_f_strings) {
    CHECK_EQ(expr("f\"x = {x + 1:>4}!\""), std::string("(f \"x = \" (+ x 1):>4 \"!\")"));
    CHECK_EQ(expr("f\"{a}{b}\""), std::string("(f a b)"));
    CHECK(firstError("f\"{a ? b : c}\"").find("put a conditional expression in parentheses") != std::string::npos);
    CHECK(parse("f\"{(a ? b : c)}\"").errors.empty());
}

TEST(parser_increment_and_decrement) {
    CHECK_EQ(expr("i++"), "(i++)");
    CHECK_EQ(expr("--i"), "(--i)");
    CHECK_EQ(expr("items[i++]"), "(index items (i++))");
    CHECK_EQ(expr("p.count--"), "((. p count)--)");
    CHECK_EQ(expr("x + ++y"), "(+ x (++y))");
    CHECK_EQ(expr("a - -b"), "(- a (- b))");
    CHECK_EQ(firstError("5++"), "expected a newline or ';' after the statement, found '++'");
    CHECK_EQ(firstError("++f()"), "'++' needs a variable, element or field to change");
}

TEST(parser_casts) {
    CHECK_EQ(expr("(f64) total / count"), "(/ (call f64 total) count)");
    CHECK_EQ(expr("(i32) -x"), "(call i32 (- x))");
    CHECK_EQ(expr("(double) 1"), "(call double 1)");
    CHECK_EQ(expr("(x) + 1"), "(+ x 1)");        // not a type: just parentheses
    CHECK_EQ(expr("(list) + 1"), "(+ list 1)");  // only number, bool, char and string casts
}

TEST(parser_c_style_for_loops) {
    Parsed classic = parse("for (i32 i = 0; i < 10; i++) { print(i) }");
    CHECK(classic.errors.empty());
    const auto& loop = onlyStatement(classic).as<CForStmt>();
    CHECK_EQ(loop.init.size(), size_t(1));
    CHECK(loop.init[0]->kind == StmtKind::Declare);
    CHECK_EQ(dump(loop.condition.get()), "(< i 10)");
    CHECK_EQ(loop.step.size(), size_t(1));
    CHECK(!loop.copyPerIteration);

    Parsed several = parse("for (i32 a = 0, b = 9; a < b; a++, b--) {}");
    CHECK(several.errors.empty());
    const auto& pair = onlyStatement(several).as<CForStmt>();
    CHECK(pair.init[0]->kind == StmtKind::Group);
    CHECK_EQ(pair.step.size(), size_t(2));

    CHECK(onlyStatement(parse("for (;;) { break }")).as<CForStmt>().condition == nullptr);
    CHECK(onlyStatement(parse("for { break }")).kind == StmtKind::CFor);
    CHECK(onlyStatement(parse("for i := 0; i < 3; i++ { }")).kind == StmtKind::CFor);
    CHECK(onlyStatement(parse("for (i := 0; i < 3; i++) fns.push(func() { return i })")).as<CForStmt>().copyPerIteration);

    Parsed typed = parse("for (string name : names) print(name)");
    CHECK(typed.errors.empty());
    const auto& each = onlyStatement(typed).as<ForStmt>();
    CHECK_EQ(each.firstType.name(), "string");
    CHECK(onlyStatement(parse("for (i, x in items) {}")).kind == StmtKind::For);
    CHECK(onlyStatement(parse("for x in (a) {}")).kind == StmtKind::For);
    CHECK_EQ(firstError("for (i = 0; i < 3) {}"), "expected ';' after the loop condition, found ')'");
}

TEST(parser_bodies_without_braces) {
    Parsed parsed = parse("if (x > 0) print(x)\nelse if (x < 0)\n    print(-x)\nelse print(0)");
    CHECK(parsed.errors.empty());
    const auto& branch = onlyStatement(parsed).as<IfStmt>();
    CHECK_EQ(branch.thenBody.statements.size(), size_t(1));
    CHECK(branch.elseBranch->kind == StmtKind::If);

    CHECK(parse("if (a) b() else c()").errors.empty());
    CHECK(parse("while (i < 10) i++").errors.empty());
    CHECK(parse("while (busy());").errors.empty());
    CHECK(parse("for (x in xs) total += x").errors.empty());
    CHECK(parse("if (done) ++count").errors.empty());
    // When something follows the ')', the parentheses are part of the condition.
    Parsed longer = parse("if (a + b) * 2 > 3 { }");
    CHECK(longer.errors.empty());
    CHECK_EQ(dump(onlyStatement(longer).as<IfStmt>().condition.get()), "(> (* (+ a b) 2) 3)");
    CHECK_EQ(firstError("if (x)\n"), "expected '{' to start the 'if' body, found end of input");
}

TEST(parser_do_while_and_switch) {
    Parsed loop = parse("do {\n  i++\n} while (i < 3)");
    CHECK(loop.errors.empty());
    CHECK(onlyStatement(loop).as<WhileStmt>().doWhile);
    CHECK(parse("do x++; while (x < 3)").errors.empty());
    CHECK_EQ(firstError("do { }"), "expected 'while' and a condition after the 'do' body, found end of input");

    Parsed choice = parse("switch (x) {\ncase 1, 2:\n  a()\n  break\ncase 3:\ndefault:\n  b()\n}");
    CHECK(choice.errors.empty());
    const auto& cases = onlyStatement(choice).as<SwitchStmt>().cases;
    CHECK_EQ(cases.size(), size_t(3));
    CHECK_EQ(cases[0].values.size(), size_t(2));
    CHECK_EQ(cases[1].start, size_t(2));
    CHECK(cases[2].values.empty());
    CHECK_EQ(cases[2].start, size_t(2));
    CHECK(parse("switch x { case \"a\": print(1); break }").errors.empty());
    CHECK_EQ(firstError("switch x { print(1) }"), "expected 'case' or 'default' in the switch, found identifier 'print'");
    CHECK_EQ(firstError("switch x { default: a()\n default: b() }"), "a switch can only have one 'default'");
    CHECK_EQ(firstError("case 1: x()"), "'case' can only be used inside a switch");
    // These words are still ordinary names elsewhere.
    CHECK(parse("switch := 1\ndo := 2\ncase := 3\ndefault := 4\nprint(switch, do, case, default)").errors.empty());
}

TEST(parser_import_statements) {
    Parsed simple = parse("import geometry, lib.shapes as shapes, \"../x.asc\"");
    CHECK(simple.errors.empty());
    const auto& names = onlyStatement(simple).as<ImportStmt>().names;
    CHECK_EQ(names.size(), size_t(3));
    CHECK_EQ(names[0].name, "geometry");
    CHECK_EQ(names[0].binding, "geometry");
    CHECK_EQ(names[1].name, "lib/shapes");
    CHECK_EQ(names[1].binding, "shapes");
    CHECK_EQ(names[2].name, "../x.asc");
    CHECK_EQ(names[2].binding, "x");

    Parsed from = parse("from lib.shapes import area, Point as P");
    CHECK(from.errors.empty());
    const auto& fromImport = onlyStatement(from).as<ImportStmt>();
    CHECK(fromImport.from);
    CHECK_EQ(fromImport.fromPath, "lib/shapes");
    CHECK_EQ(fromImport.names[1].binding, "P");
    CHECK(onlyStatement(parse("from \"./x\" import *")).as<ImportStmt>().all);
    CHECK(parse("from x import (a, b,)").errors.empty());

    CHECK(onlyStatement(parse("m := import(\"x\")")).kind == StmtKind::Declare);  // the function still works
    CHECK(parse("from := 1\nimport := 2").errors.empty());
    CHECK(firstError("import \"my-lib\"").find("give the module a name with 'as'") != std::string::npos);
    CHECK_EQ(firstError("from x import"), "expected a name to import, found end of input");
}

TEST(parser_structs_enums_and_auto) {
    Parsed point = parse("struct Point {\n  f64 x, y\n  string label = \"p\"\n  any extra\n}");
    CHECK(point.errors.empty());
    const auto& fields = onlyStatement(point).as<StructStmt>().fields;
    CHECK_EQ(fields.size(), size_t(4));
    CHECK_EQ(fields[1].name, "y");
    CHECK_EQ(fields[1].type.name(), "f64");
    CHECK(fields[2].defaultValue != nullptr);
    CHECK(parse("struct P { i32 a; i32 b };").errors.empty());
    CHECK(firstError("struct P { f64 area() { } }").find("a struct can't contain functions") == 0);

    Parsed typed = parse("Point p = Point(1, 2)");
    CHECK(typed.errors.empty());
    CHECK_EQ(onlyStatement(typed).as<DeclareStmt>().type.name(), "Point");
    CHECK(onlyStatement(parse("Point? find(list items) { return null }")).kind == StmtKind::Function);
    CHECK(onlyStatement(parse("ready ? go() : stop()")).kind == StmtKind::Expression);

    Parsed colors = parse("enum Color {\n  Red,\n  Green = 5\n  Blue\n}");
    CHECK(colors.errors.empty());
    CHECK_EQ(onlyStatement(colors).as<EnumStmt>().members.size(), size_t(3));

    Parsed inferred = parse("auto n = 5");
    CHECK(inferred.errors.empty());
    CHECK(onlyStatement(inferred).as<DeclareStmt>().inferType);
    CHECK_EQ(firstError("auto n"), "'auto n' needs a value to take its type from, like 'auto n = 0'");
    CHECK_EQ(firstError("i32 f(auto x) { }"), "'auto' can only be used to declare a variable with a value");
    CHECK(onlyStatement(parse("i32 a = 1, b = 2")).kind == StmtKind::Group);
}

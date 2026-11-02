#include "interpreter.hpp"
#include "repl.hpp"
#include "runner.hpp"
#include "test_framework.hpp"

#include <sstream>
#include <string>

using namespace ascend;

namespace {

struct Result {
    RunStatus status;
    int exitCode;
    std::string out;
    std::string err;
};

Result run(const std::string& code, const std::string& input = "") {
    std::ostringstream out;
    std::ostringstream err;
    std::istringstream in(input);
    RunResult result;
    {
        Interpreter interpreter(out, in);
        RunOptions options;
        options.filename = "test.asc";
        result = runSource(interpreter, code, options, err);
    }
    return {result.status, result.exitCode, out.str(), err.str()};
}

// The program's output; fails the test if the program reports an error.
std::string output(const std::string& code, const std::string& input = "") {
    Result result = run(code, input);
    if (result.status != RunStatus::Ok) return "UNEXPECTED ERROR:\n" + result.err;
    return result.out;
}

// The error a program reports (empty if it ran successfully).
std::string error(const std::string& code) {
    Result result = run(code);
    return result.status == RunStatus::Ok ? "" : result.err;
}

bool contains(const std::string& text, const std::string& fragment) { return text.find(fragment) != std::string::npos; }

}  // namespace

#define CHECK_OUTPUT(code, expected) CHECK_EQ(output(code), std::string(expected))
#define CHECK_ERROR(code, fragment)                                                           \
    do {                                                                                      \
        std::string message_ = error(code);                                                   \
        if (!contains(message_, fragment)) {                                                  \
            throw ::asctest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                     ": expected an error containing \"" + (fragment) +       \
                                     "\"\n    got: " + (message_.empty() ? "(no error)" : message_)}; \
        }                                                                                     \
    } while (0)

// ---------------------------------------------------------------------------
// The original example programs

TEST(interp_original_math_example) {
    CHECK_OUTPUT(
        "x := 10 + 5\nprint(x)  // Output: 15\n\ny := x * 2\nprint(y)\n\n"
        "print(\"y value\" + y)\nz := (y + 5) * 3\nprint(z)\nprint(\"z value\", z)\n\n"
        "mod := 17 % 5\nprint(mod)\n",
        "15\n30\ny value30\n105\nz value 105\n2\n");
}

TEST(interp_original_control_flow_example) {
    CHECK_OUTPUT(
        "/*\nmulti-line comments woo hoo\n*/a := 10\ny := 6\nc := 3\n\nb := 5\nb *= 5\nprint(b)\n\nprint()\n\n"
        "if (a > 5 and y < 10 or !c) {\n    print(a)\n}\n\nprint()\n\n"
        "for i in range(3) {\n    print(i)\n}\n\nprint()\n\n"
        "x := 3\nwhile (x > 0) {\n    print(x)\n    x -= 1\n}",
        "25\n\n10\n\n0\n1\n2\n\n3\n2\n1\n");
}

TEST(interp_regressions_from_old_interpreter) {
    // `!=` was not recognized.
    CHECK_OUTPUT("x := 5\nif (x != 3) { print(\"ok\") }", "ok\n");
    // for-loop bodies skipped every other statement.
    CHECK_OUTPUT("for i in range(2) {\n  print(i)\n  print(i * 10)\n}", "0\n0\n1\n10\n");
    // while loops only ran their body once.
    CHECK_OUTPUT("y := 0\nwhile (y < 3) { y += 1 }\nprint(y)", "3\n");
    // calling a function as a statement was a syntax error that skipped the rest of the file.
    CHECK_OUTPUT("func add(a, b) { return a + b; }\nprint(add(1, 2))\nadd(3, 4)\nprint(99)", "3\n99\n");
}

// ---------------------------------------------------------------------------
// Values and operators

TEST(interp_arithmetic) {
    CHECK_OUTPUT("print(7 / 2, -7 / 2, 7 % 3, -7 % 3, 2 ** 10, -2 ** 2)", "3 -3 1 -1 1024 -4\n");
    CHECK_OUTPUT("print(7.0 / 2, 1 + 0.5, 2 ** -1, 10 / 4.0, 5.5 % 2)", "3.5 1.5 0.5 2.5 1.5\n");
    CHECK_OUTPUT("print(0.1 + 0.2, 1e20, 3.0, -0.5, 1 / 3.0)",
                 "0.30000000000000004 1e+20 3.0 -0.5 0.3333333333333333\n");
}

TEST(interp_arithmetic_errors) {
    CHECK_ERROR("print(1 / 0)", "division by zero");
    CHECK_ERROR("print(1.5 / 0.0)", "division by zero");
    CHECK_ERROR("print(1 % 0)", "modulo by zero");
    CHECK_ERROR("x := 9223372036854775807\nprint(x + 1)", "integer overflow");
    CHECK_ERROR("print(2 ** 64)", "integer overflow");
    CHECK_ERROR("print(1 + [1])", "unsupported operand types for '+': int and list");
    CHECK_ERROR("print(-\"a\")", "can't negate a string value");
    CHECK_ERROR("print(1 < \"a\")", "can't compare int and string with '<'");
}

TEST(interp_strings) {
    CHECK_OUTPUT("print(\"a\" + 1, 2 + \"b\", \"ab\" * 3, \"x\" + null)", "a1 2b ababab xnull\n");
    CHECK_OUTPUT("s := \"hello\"\nprint(s[0], s[-1], s[1:3], s[3:], len(s))", "h o el lo 5\n");
    CHECK_OUTPUT("print(\"ell\" in \"hello\", \"abc\" < \"abd\", \"b\" > \"a\")", "true true true\n");
    CHECK_ERROR("s := \"abc\"\ns[0] = \"x\"", "strings can't be changed in place");
}

TEST(interp_equality) {
    CHECK_OUTPUT("print(1 == 1.0, \"1\" == 1, null == null, [1, [2]] == [1, [2]], {a: 1} == {a: 1}, [1] != [2])",
                 "true false true true true true\n");
    CHECK_OUTPUT("f := func() { }\ng := f\nprint(f == g, f == func() { })", "true false\n");
}

TEST(interp_truthiness) {
    CHECK_OUTPUT(
        "for v in [0, 0.0, \"\", [], {}, null, false, 1, \"a\", [0], {a: 1}, true] {\n"
        "  print(v ? \"T\" : \"F\")\n}",
        "F\nF\nF\nF\nF\nF\nF\nT\nT\nT\nT\nT\n");
}

TEST(interp_logical_operators_short_circuit) {
    CHECK_OUTPUT("func boom() { throw \"evaluated\" }\nprint(false and boom(), true or boom())", "false true\n");
    CHECK_OUTPUT("print(1 and \"x\", 0 or null, not 0, !\"\")", "true false true true\n");
}

TEST(interp_ternary) {
    CHECK_OUTPUT("x := 5\nprint(x > 3 ? \"big\" : \"small\", x > 9 ? 1 : x > 4 ? 2 : 3)", "big 2\n");
}

// ---------------------------------------------------------------------------
// Variables and scope

TEST(interp_variables) {
    CHECK_OUTPUT("x := 1\nx = x + 1\nx += 10\nx -= 2\nx *= 3\nx /= 2\nx %= 7\nprint(x)", "1\n");
    CHECK_ERROR("x = 1", "undefined variable 'x' (to create it, write 'x := ...')");
    CHECK_ERROR("x += 1", "undefined variable 'x'");
    CHECK_ERROR("count := 1\nprint(cuont)", "undefined variable 'cuont' (did you mean 'count'?)");
}

TEST(interp_block_scope) {
    CHECK_OUTPUT("x := 1\nif true {\n  x := 2\n  print(x)\n}\nprint(x)", "2\n1\n");
    CHECK_OUTPUT("x := 1\nif true {\n  x = 2\n}\nprint(x)", "2\n");
    CHECK_ERROR("if true {\n  inner := 1\n}\nprint(inner)", "undefined variable 'inner'");
    CHECK_ERROR("for i in range(2) { }\nprint(i)", "undefined variable 'i'");
}

// ---------------------------------------------------------------------------
// Control flow

TEST(interp_if_else_chain) {
    std::string program =
        "func grade(n) {\n"
        "  if n >= 90 {\n    return \"A\"\n  } else if n >= 80 {\n    return \"B\"\n  }\n"
        "  else {\n    return \"C\"\n  }\n}\n"
        "print(grade(95), grade(85), grade(10))";
    CHECK_OUTPUT(program, "A B C\n");
}

TEST(interp_while_break_continue) {
    CHECK_OUTPUT(
        "i := 0\ntotal := 0\nwhile true {\n  i += 1\n  if i > 10 { break }\n  if i % 2 == 0 { continue }\n"
        "  total += i\n}\nprint(total)",
        "25\n");
}

TEST(interp_for_range_forms) {
    CHECK_OUTPUT("for i in range(3) { print(i) }", "0\n1\n2\n");
    CHECK_OUTPUT("for i in range(2, 5) { print(i) }", "2\n3\n4\n");
    CHECK_OUTPUT("for i in range(10, 0, -4) { print(i) }", "10\n6\n2\n");
    CHECK_OUTPUT("for i in range(0) { print(i) }\nprint(range(5, 0))", "[]\n");
    CHECK_OUTPUT("r := range\nfor i in r(2) { print(i) }", "0\n1\n");
    CHECK_ERROR("for i in range(1, 2, 0) { }", "range() step can't be zero");
    CHECK_ERROR("for i in range(1.5) { }", "range() expects an int as argument 1, not a float");
}

TEST(interp_for_over_collections) {
    CHECK_OUTPUT("for x in [\"a\", \"b\"] { print(x) }", "a\nb\n");
    CHECK_OUTPUT("for i, x in [\"a\", \"b\"] { print(i, x) }", "0 a\n1 b\n");
    CHECK_OUTPUT("for c in \"hi\" { print(c) }", "h\ni\n");
    CHECK_OUTPUT("m := {b: 2, a: 1}\nfor k in m { print(k) }\nfor k, v in m { print(k, v) }", "b\na\nb 2\na 1\n");
    CHECK_ERROR("for x in 5 { }", "can't loop over a int value");
}

TEST(interp_for_loop_body_can_modify_collection) {
    CHECK_OUTPUT("items := [1, 2, 3]\nfor x in items {\n  if x < 3 { items.push(x * 10) }\n}\nprint(items)",
                 "[1, 2, 3, 10, 20]\n");
    CHECK_OUTPUT("m := {a: 1}\nfor k in m { m[k + k] = 2 }\nprint(m)", "{\"a\": 1, \"aa\": 2}\n");
}

TEST(interp_nested_loops_break_inner_only) {
    CHECK_OUTPUT("for i in range(3) {\n  for j in range(3) {\n    if j == 1 { break }\n    print(i, j)\n  }\n}",
                 "0 0\n1 0\n2 0\n");
}

// ---------------------------------------------------------------------------
// Functions

TEST(interp_functions) {
    CHECK_OUTPUT("func add(a, b) { return a + b }\nprint(add(2, 3))", "5\n");
    CHECK_OUTPUT("func nothing() { }\nprint(nothing())", "null\n");
    CHECK_OUTPUT("func early(x) {\n  if x { return \"yes\" }\n  return\n}\nprint(early(1), early(0))", "yes null\n");
    CHECK_OUTPUT("func fib(n) {\n  if n < 2 { return n }\n  return fib(n - 1) + fib(n - 2)\n}\nprint(fib(20))", "6765\n");
}

TEST(interp_default_parameters) {
    CHECK_OUTPUT(
        "func greet(name, greeting = \"Hello\", mark = \"!\") { return greeting + \", \" + name + mark }\n"
        "print(greet(\"Ann\"))\nprint(greet(\"Bo\", \"Hi\"))\nprint(greet(\"Cy\", \"Yo\", \"?\"))",
        "Hello, Ann!\nHi, Bo!\nYo, Cy?\n");
    CHECK_OUTPUT("func f(a, b = a * 2) { return b }\nprint(f(4))", "8\n");
}

TEST(interp_arity_errors) {
    CHECK_ERROR("func f(a, b) { }\nf(1)", "f() expects 2 arguments but got 1");
    CHECK_ERROR("func f(a, b = 1) { }\nf()", "f() expects 1 to 2 arguments but got 0");
    CHECK_ERROR("len()", "len() expects 1 argument but got 0");
    CHECK_ERROR("push([])", "push() expects at least 2 arguments but got 1");
    CHECK_ERROR("x := 5\nx()", "a int value can't be called like a function");
}

TEST(interp_closures) {
    CHECK_OUTPUT(
        "func counter() {\n  count := 0\n  return func() {\n    count += 1\n    return count\n  }\n}\n"
        "a := counter()\nb := counter()\na()\na()\nprint(a(), b())",
        "3 1\n");
    // Each loop iteration gets its own variable.
    CHECK_OUTPUT("fs := []\nfor i in range(3) { fs.push(func() { return i }) }\nprint(fs[0](), fs[2]())", "0 2\n");
}

TEST(interp_function_hoisting) {
    CHECK_OUTPUT("print(double(4))\nfunc double(x) { return x * 2 }", "8\n");
    CHECK_OUTPUT("func isEven(n) { return n == 0 ? true : isOdd(n - 1) }\n"
                 "func isOdd(n) { return n == 0 ? false : isEven(n - 1) }\nprint(isEven(10), isOdd(7))",
                 "true true\n");
}

TEST(interp_functions_are_values) {
    CHECK_OUTPUT("square := func(x) { return x * x }\nprint(square(5), square, print)",
                 "25 <func square> <builtin print>\n");
    CHECK_OUTPUT("func apply(f, x) { return f(x) }\nprint(apply(func(n) { return n + 1 }, 1))", "2\n");
}

TEST(interp_method_call_syntax) {
    CHECK_OUTPUT("items := [3, 1, 2]\nitems.push(0)\nprint(items.sort(), items.len(), \"Hi\".upper())",
                 "[0, 1, 2, 3] 4 HI\n");
    CHECK_OUTPUT("func double(x) { return x * 2 }\nprint(21.double())", "42\n");
    // A function stored in a map is called as a method of that map.
    CHECK_OUTPUT("dog := {name: \"Rex\", speak: func() { return \"Woof\" }}\nprint(dog.speak(), dog.name)",
                 "Woof Rex\n");
    CHECK_ERROR("\"abc\".frobnicate()", "a string value has no method 'frobnicate'");
}

TEST(interp_recursion_limit_is_an_error_not_a_crash) {
    CHECK_ERROR("func forever(n) { return forever(n + 1) }\nforever(0)", "stack overflow");
    CHECK_OUTPUT("func depth(n) { if n == 0 { return 0 }\n return 1 + depth(n - 1) }\nprint(depth(500))", "500\n");
}

// ---------------------------------------------------------------------------
// Lists and maps

TEST(interp_lists) {
    CHECK_OUTPUT("a := [1, 2, 3]\na[0] = 10\na[-1] += 5\nprint(a, len(a), a[1:], a + [4], [0] * 3)",
                 "[10, 2, 8] 3 [2, 8] [10, 2, 8, 4] [0, 0, 0]\n");
    CHECK_OUTPUT("a := [1, 2]\nb := a\nb.push(3)\nc := copy(a)\nc.push(4)\nprint(a, c)", "[1, 2, 3] [1, 2, 3, 4]\n");
    CHECK_ERROR("a := [1, 2]\nprint(a[2])", "list index 2 is out of range (length 2)");
    CHECK_ERROR("a := [1]\nprint(a[\"x\"])", "list index must be an int, not string");
}

TEST(interp_list_builtins) {
    CHECK_OUTPUT(
        "a := [5, 3, 8]\nprint(pop(a), a)\ninsert(a, 0, 9)\nprint(a)\nprint(remove(a, 1), a)\n"
        "print(contains(a, 9), 9 in a, index_of(a, 3), index_of(a, 7))\n"
        "print(sort([3, 1, 2]), reverse([1, 2, 3]), sort([\"b\", \"a\"]), join([1, \"x\", 2.5], \"-\"))",
        "8 [5, 3]\n[9, 5, 3]\n5 [9, 3]\ntrue true 1 -1\n[1, 2, 3] [3, 2, 1] [\"a\", \"b\"] 1-x-2.5\n");
    CHECK_OUTPUT("words := [\"ccc\", \"a\", \"bb\"]\nprint(sort(words, len), words)",
                 "[\"a\", \"bb\", \"ccc\"] [\"ccc\", \"a\", \"bb\"]\n");
    CHECK_OUTPUT(
        "nums := [1, 2, 3, 4]\nprint(map(nums, func(x) { return x * x }), filter(nums, func(x) { return x % 2 == 0 }),"
        " reduce(nums, func(a, b) { return a + b }), reduce([], func(a, b) { return a }, 0))",
        "[1, 4, 9, 16] [2, 4] 10 0\n");
    CHECK_ERROR("pop([])", "pop() from an empty list");
    CHECK_ERROR("sort([1, \"a\"])", "can't compare string with int");
}

TEST(interp_maps) {
    CHECK_OUTPUT(
        "p := {name: \"Bob\", age: 30}\np.age += 1\np[\"city\"] = \"Paris\"\np.name = \"Rob\"\n"
        "print(p, p.name, p[\"age\"], \"city\" in p, len(p))",
        "{\"name\": \"Rob\", \"age\": 31, \"city\": \"Paris\"} Rob 31 true 3\n");
    CHECK_OUTPUT("m := {1: \"one\", true: \"yes\", null: \"nothing\"}\nprint(m[1.0], m[true], m[null])",
                 "one yes nothing\n");
    CHECK_OUTPUT("m := {a: 1, b: 2}\nprint(keys(m), values(m), get(m, \"z\", 0), get(m, \"a\"), remove(m, \"a\"), m)",
                 "[\"a\", \"b\"] [1, 2] 0 1 1 {\"b\": 2}\n");
    CHECK_ERROR("m := {}\nprint(m[\"missing\"])", "key \"missing\" not found in map");
    CHECK_ERROR("m := {}\nprint(m.missing)", "map has no key \"missing\"");
    CHECK_ERROR("m := {}\nm[[1]] = 2", "a list can't be used as a map key");
    CHECK_ERROR("x := 5\nx.field = 1", "can't set field 'field' on a int value");
}

TEST(interp_printing_nested_and_cyclic_values) {
    CHECK_OUTPUT("print([1, \"two\", [3.0, null], {k: \"v\\n\"}])", "[1, \"two\", [3.0, null], {\"k\": \"v\\n\"}]\n");
    CHECK_OUTPUT("a := [1]\na.push(a)\nprint(a)", "[1, [...]]\n");
}

// ---------------------------------------------------------------------------
// Errors

TEST(interp_try_catch) {
    CHECK_OUTPUT("try {\n  print(1 / 0)\n} catch e {\n  print(\"caught:\", e)\n}", "caught: division by zero\n");
    CHECK_OUTPUT("try { throw {code: 42} } catch (err) { print(err.code) }", "42\n");
    CHECK_OUTPUT("try { [].pop() } catch { print(\"failed\") }\nprint(\"after\")", "failed\nafter\n");
    CHECK_OUTPUT(
        "func risky() { throw \"inner\" }\n"
        "try {\n  try { risky() } catch e { throw e + \" rethrown\" }\n} catch e { print(e) }",
        "inner rethrown\n");
    CHECK_OUTPUT("func f() {\n  try { return \"from try\" } catch { }\n  return \"after\"\n}\nprint(f())", "from try\n");
}

TEST(interp_uncaught_errors_report_location) {
    Result result = run("func f(x) {\n  return x / 0\n}\nf(1)");
    CHECK(result.status == RunStatus::RuntimeError);
    CHECK_EQ(result.exitCode, kExitRuntimeError);
    CHECK(contains(result.err, "test.asc:2:12: runtime error: division by zero"));
    CHECK(contains(result.err, " 2 |   return x / 0\n"));

    Result thrown = run("print(\"before\")\nthrow \"bad input\"");
    CHECK_EQ(thrown.out, std::string("before\n"));
    CHECK(contains(thrown.err, "test.asc:2:1: runtime error: uncaught error: bad input"));
}

TEST(interp_syntax_and_semantic_errors_prevent_running) {
    Result syntax = run("print(\"should not run\")\nx := (");
    CHECK(syntax.status == RunStatus::SyntaxError);
    CHECK_EQ(syntax.exitCode, kExitSyntaxError);
    CHECK_EQ(syntax.out, std::string(""));

    CHECK_ERROR("break", "'break' can only be used inside a loop");
    CHECK_ERROR("while true { func f() { continue } }", "'continue' can only be used inside a loop");
    CHECK_ERROR("return 1", "'return' can only be used inside a function");
    CHECK_ERROR("func f(a, a) { }", "parameter 'a' is listed more than once");
}

TEST(interp_exit) {
    Result result = run("print(\"a\")\nexit(3)\nprint(\"b\")");
    CHECK(result.status == RunStatus::Exited);
    CHECK_EQ(result.exitCode, 3);
    CHECK_EQ(result.out, std::string("a\n"));
    // exit() is not an error, so try/catch doesn't stop it.
    CHECK_EQ(run("try { exit(4) } catch { print(\"caught\") }").exitCode, 4);
}

TEST(interp_assert) {
    CHECK_ERROR("assert(1 == 2, \"math is broken\")", "assertion failed: math is broken");
    CHECK_OUTPUT("assert(true)\nprint(\"fine\")", "fine\n");
}

// ---------------------------------------------------------------------------
// Builtins

TEST(interp_conversions) {
    CHECK_OUTPUT("print(int(\"42\") + 1, int(3.9), int(-3.9), int(true), float(\"2.5\"), float(2), str(12) + \"!\")",
                 "43 3 -3 1 2.5 2.0 12!\n");
    CHECK_OUTPUT("print(type(1), type(1.5), type(\"s\"), type([]), type({}), type(null), type(true), type(len))",
                 "int float string list map null bool function\n");
    CHECK_OUTPUT("print(bool(0), bool(\"x\"), repr(\"a\\tb\"), repr([1]))", "false true \"a\\tb\" [1]\n");
    CHECK_ERROR("int(\"abc\")", "int() can't convert \"abc\" to an int");
    CHECK_ERROR("float(\"1.2.3\")", "float() can't convert \"1.2.3\" to a float");
}

TEST(interp_math_builtins) {
    CHECK_OUTPUT("print(abs(-4), abs(-2.5), min(3, 1, 2), max([4, 8, 2]), sum([1, 2, 3]), sum([1, 2.5]))",
                 "4 2.5 1 8 6 3.5\n");
    CHECK_OUTPUT("print(sqrt(16), pow(2, 8), floor(2.7), ceil(2.1), round(2.5), round(3.14159, 2), floor(-1.5))",
                 "4.0 256 2 3 3 3.14 -2\n");
    CHECK_OUTPUT("seed(7)\nx := randint(1, 6)\nseed(7)\nprint(x == randint(1, 6), random() < 1)", "true true\n");
    CHECK_OUTPUT("print(PI > 3.14 and PI < 3.15)", "true\n");
    CHECK_ERROR("sqrt(-1)", "sqrt() of a negative number");
    CHECK_ERROR("min([])", "min() of an empty list");
}

TEST(interp_string_builtins) {
    CHECK_OUTPUT("print(split(\"a,b,,c\", \",\"), split(\"  one two  \"), split(\"abc\", \"\"))",
                 "[\"a\", \"b\", \"\", \"c\"] [\"one\", \"two\"] [\"a\", \"b\", \"c\"]\n");
    CHECK_OUTPUT("print(upper(\"abc\"), lower(\"ABC\"), trim(\"  x  \"), replace(\"a-b-c\", \"-\", \"+\"))",
                 "ABC abc x a+b+c\n");
    CHECK_OUTPUT("print(starts_with(\"hello\", \"he\"), ends_with(\"hello\", \"lo\"), index_of(\"hello\", \"l\"))",
                 "true true 2\n");
    CHECK_OUTPUT("print(ord(\"A\"), chr(66), reverse(\"abc\"), slice(\"hello\", 1, -1))", "65 B cba ell\n");
}

TEST(interp_input) {
    CHECK_EQ(output("name := input(\"Name? \")\nprint(\"Hi \" + name)\nprint(input())", "Ada\n"),
             std::string("Name? Hi Ada\nnull\n"));
}

// ---------------------------------------------------------------------------
// REPL

TEST(interp_repl_mode_echoes_expression_values) {
    std::ostringstream out;
    std::ostringstream err;
    std::istringstream in;
    Interpreter interpreter(out, in);
    interpreter.setReplMode(true);
    RunOptions options;
    options.repl = true;
    runSource(interpreter, "x := 2\n", options, err);
    runSource(interpreter, "x * 21\n", options, err);
    runSource(interpreter, "\"text\"\n", options, err);
    runSource(interpreter, "print(\"printed\")\n", options, err);
    CHECK_EQ(out.str(), std::string("42\n\"text\"\nprinted\n"));
    CHECK(runSource(interpreter, "if x > 1 {\n", options, err).status == RunStatus::Incomplete);
    CHECK(runSource(interpreter, "/* comment\n", options, err).status == RunStatus::Incomplete);
    CHECK(runSource(interpreter, "x := )\n", options, err).status == RunStatus::SyntaxError);
}

TEST(interp_repl_session) {
    std::istringstream in("x := 5\nfunc twice(n) {\n  return n * 2\n}\ntwice(x)\nbad +* 1\n[x, x]\nexit\n");
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interpreter(out, in);
    int code = runRepl(interpreter, in, out, err);
    CHECK_EQ(code, 0);
    CHECK(contains(out.str(), ">>> ... ... >>> 10\n"));
    CHECK(contains(out.str(), "[5, 5]\n"));
    CHECK(contains(err.str(), "syntax error"));
}

// ---------------------------------------------------------------------------
// Memory

TEST(interp_cycle_collector_frees_closures) {
    std::ostringstream out;
    std::istringstream in;
    Interpreter interpreter(out, in);
    RunOptions options;
    std::ostringstream err;
    // Each call creates a scope that holds a function closing over that same
    // scope: a reference cycle that reference counting alone never frees.
    runSource(interpreter,
              "func make() {\n  func helper() { return 1 }\n  return helper()\n}\n"
              "for i in range(50000) { make() }\n"
              "a := [1]\na.push(a)\na = null\n",
              options, err);
    CHECK_EQ(err.str(), std::string(""));
    interpreter.collectGarbage();
    CHECK(interpreter.trackedObjectCount() < 100);
}

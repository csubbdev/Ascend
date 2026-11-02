#include "interpreter.hpp"
#include "repl.hpp"
#include "runner.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <fstream>
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

// A file in the working directory that is deleted when the test ends.
struct TempFile {
    TempFile(std::string name, const std::string& contents) : path(std::move(name)) {
        std::ofstream(path, std::ios::binary) << contents;
    }
    ~TempFile() { std::remove(path.c_str()); }
    std::string path;
};

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
    CHECK_ERROR("for i in range(1.5) { }", "range() expects an integer as argument 1, not a float");
}

TEST(interp_for_over_collections) {
    CHECK_OUTPUT("for x in [\"a\", \"b\"] { print(x) }", "a\nb\n");
    CHECK_OUTPUT("for i, x in [\"a\", \"b\"] { print(i, x) }", "0 a\n1 b\n");
    CHECK_OUTPUT("for c in \"hi\" { print(c) }", "h\ni\n");
    CHECK_OUTPUT("m := {b: 2, a: 1}\nfor k in m { print(k) }\nfor k, v in m { print(k, v) }", "b\na\nb 2\na 1\n");
    CHECK_ERROR("for x in 5 { }", "can't loop over an int value");
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
    CHECK_ERROR("x := 5\nx()", "an int value can't be called like a function");
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
    CHECK_ERROR("a := [1]\nprint(a[\"x\"])", "list index must be an integer, not a string");
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
    CHECK_ERROR("x := 5\nx.field = 1", "can't set field 'field' on an int value");
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
                 "int float string list map null bool func\n");
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

// ---------------------------------------------------------------------------
// Types

TEST(interp_typed_variables) {
    CHECK_OUTPUT(
        "i8 a = -5\nu16 b = 60000\nu64 c = 18446744073709551615u64\nf32 d = 0.1\nf64 e = 0.1\n"
        "char f = 'x'\nstring g = \"s\"\nbool h = true\nbyte k = 255\n"
        "print(a, b, c, d, e, f, g, h, k)\n"
        "print(type(a), type(b), type(c), type(d), type(e), type(f), type(g), type(h), type(k), type(1), type(1.0))",
        "-5 60000 18446744073709551615 0.1 0.1 x s true 255\n"
        "i8 u16 u64 f32 f64 char string bool u8 int float\n");
}

TEST(interp_zero_values) {
    CHECK_OUTPUT("i32 a\nf64 b\nbool c\nstring d\nchar e\nlist f\nmap g\nstring? h\n"
                 "print(a, b, c, repr(d), ord(e), f, g, h)",
                 "0 0.0 false \"\" 0 [] {} null\n");
}

TEST(interp_typed_variables_keep_their_type) {
    CHECK_OUTPUT("u8 x = 10\nx = 20\nx += 5\nprint(x, type(x))", "25 u8\n");
    CHECK_OUTPUT("f64 x = 1\nprint(x, type(x))", "1.0 f64\n");  // ints convert to floats
    CHECK_OUTPUT("i32 x = 3.0\nprint(x)", "3\n");                 // whole floats convert to ints
    CHECK_OUTPUT("string s = 'c'\nprint(s, type(s))", "c string\n");
    CHECK_ERROR("u8 x = 10\nx = 256", "'x' is declared as u8, so it can't hold 256 (u8 is 0 to 255)");
    CHECK_ERROR("i32 x = 1\nx = 2.5", "can't hold 2.5 (use i32(...) to drop the fraction)");
    CHECK_ERROR("i32 x = 1\nx = \"5\"", "can't hold a string (use i32(...) to convert a string)");
    CHECK_ERROR("string x = \"a\"\nx = null", "can't hold null (declare it as string? to allow null)");
    CHECK_ERROR("char x = 'a'\nx = \"b\"", "use single quotes for a char, like 'a'");
    CHECK_OUTPUT("string? x = \"a\"\nx = null\nprint(x)", "null\n");
    CHECK_OUTPUT("u8 x = 1\nx := \"now untyped\"\nprint(x)", "now untyped\n");  // := declares a new variable
}

TEST(interp_static_type_errors) {
    Result result = run("print(\"never runs\")\nu8 x = 300");
    CHECK(result.status == RunStatus::SyntaxError);
    CHECK_EQ(result.out, std::string(""));
    CHECK(contains(result.err, "test.asc:2:8: error: 'x' is declared as u8, so it can't hold 300 (u8 is 0 to 255)"));
    CHECK_ERROR("bool x = 1", "'x' is declared as bool, so it can't hold 1");
    CHECK_ERROR("list x = {}", "'x' is declared as list, so it can't hold a map");
    CHECK_ERROR("func f", "'f' is declared as func, so it needs a value");
}

TEST(interp_integer_overflow_per_type) {
    CHECK_ERROR("u8 x = 250\nx += 10", "integer overflow: 250 + 10 doesn't fit in u8 (0 to 255)");
    CHECK_ERROR("i8 x = 100\nprint(x * 2)", "integer overflow: 100 * 2 doesn't fit in i8 (-128 to 127)");
    CHECK_ERROR("u32 x = 0\nprint(x - 1)", "integer overflow: 0 - 1 doesn't fit in u32");
    CHECK_ERROR("i8 x = -128\nprint(-x)", "integer overflow: -(-128) doesn't fit in i8");
    CHECK_ERROR("u8 x = 1\nprint(-x)", "can't negate 1: u8 is unsigned");
    CHECK_ERROR("u8 x = 1\nprint(x + 300)", "300 doesn't fit in u8 (0 to 255)");
    CHECK_OUTPUT("u64 x = 18446744073709551614u64\nx += 1\nprint(x)", "18446744073709551615\n");
    CHECK_ERROR("u64 x = 18446744073709551615u64\nx += 1", "integer overflow");
    CHECK_ERROR("print(2i32 ** 31)", "doesn't fit in i32");
    CHECK_OUTPUT("print(2i32 ** 30, 3u8 ** 5)", "1073741824 243\n");
}

TEST(interp_mixed_type_arithmetic) {
    CHECK_OUTPUT("u8 a = 1\ni32 b = 2\ni8 c = 3\nf32 d = 1.5\nf64 e = 2.0\n"
                 "print(type(a + 1), type(a + b), type(a + c), type(b + d), type(d + e), type(d * 2), type(a + 1.5))",
                 "u8 i32 i16 f32 f64 f32 float\n");
    CHECK_ERROR("u64 a = 1\ni64 b = 2\nprint(a + b)", "can't mix u64 and i64");
    CHECK_OUTPUT("print(7u8 / 2, 7i32 % 3, 7.0f32 / 2)", "3 1 3.5\n");
    CHECK_OUTPUT("u8 a = 200\ni8 b = -100\nprint(a > b, a == 200, 200 == a, 1u8 == 1.0)", "true true true true\n");
}

TEST(interp_f32_precision) {
    CHECK_OUTPUT("f32 x = 0.1\nf64 y = 0.1\nprint(x, y, f64(x), x == y)", "0.1 0.1 0.10000000149011612 false\n");
    CHECK_OUTPUT("print(f32(1) / 3, 1.0 / 3)", "0.33333334 0.3333333333333333\n");
}

TEST(interp_conversion_functions) {
    CHECK_OUTPUT("print(i8(100), u8(3.9), u8('A'), int(\"0xff\"), int(\" -42 \"), u64(\"18446744073709551615\"))",
                 "100 3 65 255 -42 18446744073709551615\n");
    CHECK_OUTPUT("print(type(i16(5)), type(byte(5)), type(f32(5)), f64(\"2.5\"), float(true), char(97), char(\"z\"))",
                 "i16 u8 f32 2.5 1.0 a z\n");
    CHECK_OUTPUT("print(hex(255), hex(-16), bin(5), string(12) + \"!\")", "0xff -0x10 0b101 12!\n");
    CHECK_ERROR("i8(300)", "300 doesn't fit in i8 (-128 to 127)");
    CHECK_ERROR("u8(-1)", "-1 doesn't fit in u8 (0 to 255)");
    CHECK_ERROR("int(\"2.5\")", "int() can't convert \"2.5\" to an integer (it has a fraction; use float() first)");
    CHECK_ERROR("char(\"ab\")", "char() needs a string with exactly one character");
    CHECK_ERROR("char(-1)", "char(): -1 is not a valid character code");
}

TEST(interp_number_literals) {
    CHECK_OUTPUT("print(0xFF, 0b1010, 0o17, 1_000_000, 255u8, type(255u8), -128i8, 1.5f32, type(2f64))",
                 "255 10 15 1000000 255 u8 -128 1.5 f64\n");
}

TEST(interp_bitwise_operators) {
    CHECK_OUTPUT("print(12 & 10, 12 | 10, 12 ^ 10, ~0, 1 << 10, -16 >> 2, 0xF0 >> 4)", "8 14 6 -1 1024 -4 15\n");
    CHECK_OUTPUT("u8 x = 0b1111_0000\nprint(~x, x << 1, x >> 4, type(x << 1))", "15 224 15 u8\n");
    CHECK_OUTPUT("i8 x = -1\nprint(x << 1, x >> 1)", "-2 -1\n");
    CHECK_OUTPUT("flags := 0\nflags |= 4\nflags |= 1\nflags &= ~4\nflags ^= 2\nflags <<= 1\nprint(flags, flags & 1 == 0)",
                 "6 true\n");
    CHECK_ERROR("print(1u8 << 8)", "shift amount 8 is out of range for u8 (0 to 7)");
    CHECK_ERROR("print(1.5 & 1)", "'&' needs integers, not float and int");
    CHECK_ERROR("print(true | false)", "for true/false use 'or'");
}

TEST(interp_chars) {
    CHECK_OUTPUT("c := 'a'\nprint(c, type(c), repr(c), c + 1, 'z' - 'a', 'a' < 'b', c == \"a\", '-' * 3, 'h' + \"i\")",
                 "a char 'a' b 25 true true --- hi\n");
    CHECK_OUTPUT("s := \"hey\"\nprint(s[0], type(s[0]), s[-1] == 'y', 'e' in s, ord('A'), chr(66), upper('q'))",
                 "h char true true 65 B Q\n");
    CHECK_OUTPUT("for c in \"ab\" { print(c, type(c)) }", "a char\nb char\n");
    CHECK_OUTPUT("print(repr('\\n'), repr('\\''), '\\u{1F600}' == \"\\u{1F600}\")", "'\\n' '\\'' true\n");
    CHECK_ERROR("print('a' + 1114112)", "is not a valid character code");
}

TEST(interp_unicode_strings) {
    CHECK_OUTPUT("s := \"héllo wörld\"\nprint(len(s), s[1], s[7:], reverse(\"añb\"), index_of(s, \"w\"), len(bytes(\"é\")))",
                 "11 é örld bña 6 2\n");
    CHECK_OUTPUT("print(chars(\"añ\"), from_bytes([72, 105]), split(\"añ\", \"\"))", "['a', 'ñ'] Hi [\"a\", \"ñ\"]\n");
}

TEST(interp_constants) {
    CHECK_OUTPUT("const MAX = 10\nconst string NAME = \"x\"\nprint(MAX, NAME)", "10 x\n");
    CHECK_ERROR("const MAX = 10\nMAX = 11", "can't change 'MAX': it's a constant");
    CHECK_ERROR("const MAX = 10\nMAX += 1", "can't change 'MAX': it's a constant");
    CHECK_ERROR("const MAX = 10\nMAX := 11", "'MAX' is a constant and can't be declared again");
    CHECK_OUTPUT("const MAX = 10\nif true {\n  MAX := 1\n  print(MAX)\n}", "1\n");  // shadowing in a block is allowed
}

TEST(interp_typed_functions) {
    CHECK_OUTPUT("func area(f64 w, f64 h) -> f64 { return w * h }\nprint(area(2, 3), type(area(2, 3)))", "6.0 f64\n");
    CHECK_OUTPUT("func half(i32 n) -> i32 { return n / 2 }\nprint(half(7))", "3\n");
    CHECK_OUTPUT("func find(list xs, x) -> i32? {\n  for i, v in xs { if v == x { return i } }\n  return null\n}\n"
                 "print(find([5, 6], 6), find([], 1))",
                 "1 null\n");
    CHECK_OUTPUT("func log(string msg) -> void { print(\"log:\", msg) }\nlog(\"hi\")", "log: hi\n");
    CHECK_ERROR("func f(u8 n) { }\nf(256)", "f() expects 'n' to be u8, but got 256 (u8 is 0 to 255)");
    CHECK_ERROR("func f(string s) { }\nf(1)", "f() expects 's' to be string, but got 1");
    CHECK_ERROR("func f(x) -> i32 { return x }\nf(\"no\")", "f() must return i32, but returned a string");
    CHECK_ERROR("func f() -> i32 { }\nf()", "f() must return i32, but it ended without returning a value");
    CHECK_ERROR("func f() -> void { return 1 }", "f() returns void, so it can't return a value");
    CHECK_ERROR("void f() { return 1 }", "f() returns void, so it can't return a value");
    CHECK_ERROR("func f(u8 n) {\n  n = 300\n}\nf(1)", "'n' is declared as u8, so it can't hold 300");
}

TEST(interp_c_style_functions) {
    CHECK_OUTPUT("i32 add(i32 a, i32 b = 1) {\n  return a + b\n}\nprint(add(2, 3), add(4), type(add(1, 1)))", "5 5 i32\n");
    CHECK_OUTPUT("void greet(string name) {\n  print(f\"hi {name}\")\n}\ngreet(\"Ada\")", "hi Ada\n");
    CHECK_OUTPUT("string? lookup(map m, string key) {\n  return get(m, key)\n}\nprint(lookup({a: \"x\"}, \"a\"), lookup({}, \"b\"))",
                 "x null\n");
    CHECK_OUTPUT("print(twice(21))\ni64 twice(i64 n) { return n * 2 }", "42\n");  // hoisted like `func`
    CHECK_OUTPUT("func handler = print\nhandler(\"called\")\nfunc? maybe\nprint(maybe)", "called\nnull\n");
    CHECK_ERROR("u8 small(i32 n) { return n }\nsmall(300)", "small() must return u8, but returned 300 (u8 is 0 to 255)");
}

TEST(interp_match) {
    std::string program =
        "func describe(x) {\n"
        "  match x {\n"
        "    0 => return \"zero\"\n"
        "    1, 2, 3 => return \"small\"\n"
        "    \"hi\" => {\n      greeting := \"a greeting\"\n      return greeting\n    }\n"
        "    else => return \"other\"\n"
        "  }\n"
        "}\n"
        "print(describe(0), describe(2), describe(\"hi\"), describe(99))";
    CHECK_OUTPUT(program, "zero small a greeting other\n");
    CHECK_OUTPUT("match 5 { 1 => print(1) }\nprint(\"no match is fine\")", "no match is fine\n");
    CHECK_OUTPUT("for i in range(5) {\n  match i {\n    1 => continue\n    3 => break\n    _ => print(i)\n  }\n}", "0\n2\n");
    CHECK_OUTPUT("c := 'b'\nmatch c {\n  'a' => print(\"A\")\n  'b' => print(\"B\")\n}", "B\n");
}

TEST(interp_f_strings) {
    CHECK_OUTPUT("name := \"Ada\"\nage := 36\nprint(f\"{name} is {age} years old, next year {age + 1}\")",
                 "Ada is 36 years old, next year 37\n");
    CHECK_OUTPUT("pi := 3.14159\nprint(f\"[{pi:.2f}] [{pi:8.3f}] [{42:>5}] [{42:<5}] [{42:^6}] [{7:03}] [{255:x}] [{255:#^8X}]\")",
                 "[3.14] [   3.142] [   42] [42   ] [  42  ] [007] [ff] [###FF###]\n");
    CHECK_OUTPUT("print(f\"{1234567:,} {0.256:.1%} {-5:+} {5:+} {3:b} {\"abc\":.2} {{literal}}\")",
                 "1,234,567 25.6% -5 +5 11 ab {literal}\n");
    CHECK_OUTPUT("m := {k: \"v\"}\nprint(f\"{m[\"k\"]} {[1, 2]} {(1 > 0 ? \"yes\" : \"no\")} {f\"in{1}ner\"}\")",
                 "v [1, 2] yes in1ner\n");
    CHECK_ERROR("print(f\"{\"x\":d}\")", "invalid format spec \"d\": 'd' needs an integer, got a string");
    CHECK_ERROR("print(f\"{1:q}\")", "invalid format spec \"q\": unknown type 'q'");
}

TEST(interp_format_function) {
    CHECK_OUTPUT("print(format(\"{} + {} = {}\", 1, 2, 3), format(\"{1} {0}\", \"a\", \"b\"), format(\"{:>6.2f}|\", 2.5))",
                 "1 + 2 = 3 b a   2.50|\n");
    CHECK_ERROR("format(\"{} {}\", 1)", "format(): the template needs more arguments than the 1 given");
}

// ---------------------------------------------------------------------------
// New builtins

TEST(interp_collection_tools) {
    CHECK_OUTPUT("print(zip([1, 2, 3], [\"a\", \"b\"]), unique([1, 2, 1, 3, 2]), flatten([[1, 2], 3, [4]]))",
                 "[[1, \"a\"], [2, \"b\"]] [1, 2, 3] [1, 2, 3, 4]\n");
    CHECK_OUTPUT("a := [1]\nextend(a, [2, 3])\nprint(a, count(a, 2), count(\"banana\", \"an\"))\nclear(a)\nprint(a)",
                 "[1, 2, 3] 1 2\n[]\n");
    CHECK_OUTPUT("evens := func(x) { return x % 2 == 0 }\n"
                 "print(find([1, 4, 6], evens), find([1], evens), any([1, 3], evens), all([2, 4], evens), any([0, \"\"]), all([]))",
                 "4 null false true false true\n");
    CHECK_OUTPUT("m := {a: 1, b: 2}\nprint(items(m), merge(m, {b: 3, c: 4}), m)",
                 "[[\"a\", 1], [\"b\", 2]] {\"a\": 1, \"b\": 3, \"c\": 4} {\"a\": 1, \"b\": 2}\n");
}

TEST(interp_string_tools) {
    CHECK_OUTPUT("print(pad_left(\"7\", 3, '0'), pad_right(\"ab\", 4, \".\") + \"|\", pad_left(\"long\", 2))",
                 "007 ab..| long\n");
    CHECK_OUTPUT("print(lines(\"a\\r\\nb\\n\"), is_digit(\"123\"), is_digit(\"12a\"), is_alpha('x'), is_space(\" \\t\"), is_upper(\"AB\"))",
                 "[\"a\", \"b\"] true false true true true\n");
}

TEST(interp_math_tools) {
    CHECK_OUTPUT("print(clamp(15, 0, 10), clamp(-1, 0, 10), sign(-3), sign(0), sign(2.5), gcd(12, 18), gcd(-4, 6))",
                 "10 0 -1 0 1 6 2\n");
    CHECK_OUTPUT("print(round(sin(PI / 2), 6), round(cos(0)), round(log(E), 6), log(8, 2), log10(1000), sqrt(2) ** 2 > 1.99)",
                 "1.0 1 1.0 3.0 3.0 true\n");
    CHECK_OUTPUT("print(is_nan(NAN), is_inf(INF), INF > 1e308, hypot(3, 4), atan2(0, 1))", "true true true 5.0 0.0\n");
    CHECK_OUTPUT("seed(1)\nxs := shuffle([1, 2, 3, 4])\nprint(len(xs), sum(xs), choice([7]) == 7)", "4 10 true\n");
    CHECK_OUTPUT("print(sum([200u8, 100u8]), type(sum([1u8])))", "300 int\n");  // sums are widened
}

TEST(interp_json) {
    CHECK_OUTPUT("d := json_parse(\"{\\\"a\\\": [1, 2.5, true, null, \\\"x\\\\u00e9\\\"], \\\"b\\\": {}}\")\n"
                 "print(d, type(d.a[0]), type(d.a[1]))",
                 "{\"a\": [1, 2.5, true, null, \"xé\"], \"b\": {}} int float\n");
    CHECK_OUTPUT("print(json_stringify({name: \"Ada\", tags: [\"a\", 'b'], n: null, ok: true, pi: 3.5}))",
                 "{\"name\":\"Ada\",\"tags\":[\"a\",\"b\"],\"n\":null,\"ok\":true,\"pi\":3.5}\n");
    CHECK_OUTPUT("print(json_stringify({a: [1, 2], b: {}}, 2))", "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": {}\n}\n");
    CHECK_OUTPUT("v := {list: [1, {x: \"y\"}]}\nprint(json_parse(json_stringify(v)) == v)", "true\n");
    CHECK_ERROR("json_parse(\"{\\\"a\\\": }\")", "json_parse(): unexpected character '}' (line 1, column 7)");
    CHECK_ERROR("json_stringify(NAN)", "JSON can't represent nan");
    CHECK_ERROR("a := []\na.push(a)\njson_stringify(a)", "the value contains itself");
}

TEST(interp_files) {
    const std::string path = "ascend_test_file.txt";
    std::remove(path.c_str());
    CHECK_OUTPUT("p := \"" + path + "\"\nprint(file_exists(p))\nwrite_file(p, \"one\\n\")\nappend_file(p, \"two\\n\")\n"
                 "print(file_exists(p), repr(read_file(p)), read_lines(p))\nprint(delete_file(p), file_exists(p))",
                 "false\ntrue \"one\\ntwo\\n\" [\"one\", \"two\"]\ntrue false\n");
    CHECK_ERROR("read_file(\"no_such_file_here.txt\")", "read_file(): can't open 'no_such_file_here.txt'");
}

TEST(interp_system_tools) {
    CHECK_OUTPUT("print(type(clock()), type(time()), len(date(\"%Y\")), env(\"ASCEND_SURELY_UNSET_VARIABLE\"))",
                 "float float 4 null\n");
}

TEST(interp_imports) {
    TempFile module("ascend_test_module.asc",
                    "func square(i32 n) -> i32 { return n * n }\n"
                    "VERSION := \"1.0\"\n_private := 1\n"
                    "func fail() { return 1 / 0 }\n");
    TempFile broken("ascend_test_broken.asc", "x := (\n");
    CHECK_OUTPUT("m := import(\"ascend_test_module\")\nprint(m.square(9), m.VERSION, keys(m))\n"
                 "again := import(\"ascend_test_module.asc\")\nprint(again == m)",
                 "81 1.0 [\"VERSION\", \"fail\", \"square\"]\ntrue\n");
    // Errors inside a module point at the module's own file.
    CHECK_ERROR("m := import(\"ascend_test_module\")\nm.fail()", "ascend_test_module.asc:4:24: runtime error: division by zero");
    CHECK_ERROR("import(\"ascend_test_broken\")", "can't import \"ascend_test_broken\" because it has errors");
    CHECK_ERROR("import(\"ascend_no_such_module\")", "file 'ascend_no_such_module.asc' not found");
}

TEST(interp_check_only_mode) {
    std::ostringstream out;
    std::ostringstream err;
    std::istringstream in;
    Interpreter interpreter(out, in);
    RunOptions options;
    options.checkOnly = true;
    CHECK(runSource(interpreter, "print(\"not run\")", options, err).status == RunStatus::Ok);
    CHECK_EQ(out.str(), std::string(""));
    CHECK(runSource(interpreter, "u8 x = 999", options, err).status == RunStatus::SyntaxError);
}

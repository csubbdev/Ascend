// Tests for C-style syntax, import statements, structs, enums and the
// builtins added with them.

#include "interpreter.hpp"
#include "runner.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ascend;
namespace fs = std::filesystem;

namespace {

struct Result {
    RunStatus status;
    int exitCode;
    std::string out;
    std::string err;
};

Result run(const std::string& code, const std::vector<std::string>& programArgs = {}) {
    std::ostringstream out;
    std::ostringstream err;
    std::istringstream in;
    RunResult result;
    {
        Interpreter interpreter(out, in);
        std::vector<Value> args;
        for (const auto& arg : programArgs) args.push_back(Value::fromString(arg));
        interpreter.defineGlobal("args", Value::fromList(interpreter.newList(std::move(args))));
        RunOptions options;
        options.filename = "test.asc";
        result = runSource(interpreter, code, options, err);
    }
    return {result.status, result.exitCode, out.str(), err.str()};
}

std::string output(const std::string& code) {
    Result result = run(code);
    if (result.status != RunStatus::Ok) return "UNEXPECTED ERROR:\n" + result.err;
    return result.out;
}

std::string error(const std::string& code) {
    Result result = run(code);
    return result.status == RunStatus::Ok ? "" : result.err;
}

bool contains(const std::string& text, const std::string& fragment) { return text.find(fragment) != std::string::npos; }

// Files in the working directory that are deleted when the test ends.
struct TempFiles {
    void add(const std::string& path, const std::string& contents) {
        fs::path file(path);
        if (file.has_parent_path()) {
            fs::create_directories(file.parent_path());
            folders.push_back(file.parent_path().string());
        }
        std::ofstream(file, std::ios::binary) << contents;
        files.push_back(path);
    }
    ~TempFiles() {
        std::error_code ignored;
        for (const auto& file : files) fs::remove(file, ignored);
        for (const auto& folder : folders) fs::remove_all(folder, ignored);
    }
    std::vector<std::string> files;
    std::vector<std::string> folders;
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
// C-style statements

TEST(lang_c_style_for_loops) {
    CHECK_OUTPUT("for (i32 i = 0; i < 3; i++) { print(i) }", "0\n1\n2\n");
    CHECK_OUTPUT("for i := 3; i > 0; i-- { print(i) }", "3\n2\n1\n");
    CHECK_OUTPUT("for (i32 a = 0, b = 6; a < b; a += 2, b -= 2) print(a, b)", "0 6\n2 4\n");
    CHECK_OUTPUT("n := 0\nfor (;;) { n++\n if (n == 4) break }\nprint(n)", "4\n");
    CHECK_OUTPUT("n := 0\nfor { n += 2\n if (n > 5) break }\nprint(n)", "6\n");
    // continue still runs the step.
    CHECK_OUTPUT("for (i32 i = 0; i < 5; i++) {\n if (i % 2 == 0) continue\n print(i)\n}", "1\n3\n");
    // The loop variable belongs to the loop...
    CHECK_ERROR("for (i32 i = 0; i < 1; i++) {}\nprint(i)", "undefined variable 'i'");
    // ...unless it was declared before it.
    CHECK_OUTPUT("i := 0\nfor (i = 0; i < 3; i++) {}\nprint(i)", "3\n");
    // Each iteration's functions see that iteration's value, like `let` in JavaScript.
    CHECK_OUTPUT("fns := []\nfor (i32 k = 0; k < 3; k++) fns.push(func() { return k })\nprint(map(fns, func(f) { return f() }))",
                 "[0, 1, 2]\n");
    CHECK_ERROR("for (u8 i = 250; i < 300; i++) {}", "integer overflow: 255 + 1 doesn't fit in u8");
    CHECK_OUTPUT("for (string s : [\"a\", 'b']) print(s, type(s))", "a string\nb string\n");
    CHECK_ERROR("for (i32 n in [1, \"x\"]) {}", "'n' is declared as i32, so it can't hold a string");
}

TEST(lang_increment_and_decrement) {
    CHECK_OUTPUT("i32 x = 5\nprint(x++, x, ++x, x--, --x, x)", "5 6 7 7 5 5\n");
    CHECK_OUTPUT("items := [1, 2]\nitems[0]++\n--items[1]\nprint(items)", "[2, 1]\n");
    CHECK_OUTPUT("m := {count: 1}\nm.count++\nm[\"count\"] += 1\nprint(m.count)", "3\n");
    CHECK_OUTPUT("i := 0\nitems := [10, 20, 30]\nprint(items[i++], items[i++], i)", "10 20 2\n");
    CHECK_OUTPUT("char c = 'a'\nc++\nprint(c)\nf64 f = 1.5\nf--\nprint(f)", "b\n0.5\n");
    CHECK_ERROR("u8 b = 255\nb++", "integer overflow: 255 + 1 doesn't fit in u8");
    CHECK_ERROR("const N = 1\nN++", "can't change 'N': it's a constant");
    CHECK_ERROR("s := \"a\"\ns++", "can't use '++' on a string value");
    CHECK_ERROR("missing++", "undefined variable 'missing'");
}

TEST(lang_bodies_without_braces) {
    CHECK_OUTPUT("x := 3\nif (x > 2) print(\"big\")\nelse print(\"small\")", "big\n");
    CHECK_OUTPUT("x := 1\nif (x > 2) print(\"big\") else print(\"small\")", "small\n");
    CHECK_OUTPUT("x := 0\nif (x > 0)\n    print(\"positive\")\nelse if (x < 0)\n    print(\"negative\")\nelse\n    print(\"zero\")",
                 "zero\n");
    CHECK_OUTPUT("i := 0\nwhile (i < 3) i++\nprint(i)", "3\n");
    CHECK_OUTPUT("total := 0\nfor (x in [1, 2, 3]) total += x\nprint(total)", "6\n");
    CHECK_OUTPUT("i := 0\nwhile (i++ < 5);\nprint(i)", "6\n");
    CHECK_OUTPUT("done := true\ncount := 0\nif (done) ++count\nprint(count)", "1\n");
    CHECK_OUTPUT("if (1 < 2)\n{\n    print(\"allman\")\n}\nelse\n{\n    print(\"no\")\n}", "allman\n");
}

TEST(lang_do_while) {
    CHECK_OUTPUT("i := 10\ndo {\n    print(i)\n    i++\n} while (i < 3)", "10\n");
    CHECK_OUTPUT("i := 0\ndo { i++ } while (i < 3)\nprint(i)", "3\n");
    CHECK_OUTPUT("i := 0\ndo i += 5; while (i < 12)\nprint(i)", "15\n");
    CHECK_OUTPUT("i := 0\ndo {\n i++\n if (i == 2) continue\n if (i == 4) break\n print(i)\n} while (true)", "1\n3\n");
}

TEST(lang_switch) {
    const char* program =
        "func describe(n) {\n"
        "    switch (n) {\n"
        "    case 1:\n"
        "        return \"one\"\n"
        "    case 2, 3:\n"
        "        return \"two or three\"\n"
        "    default:\n"
        "        return \"many\"\n"
        "    }\n"
        "}\n"
        "print(describe(1), describe(3), describe(9))";
    CHECK_OUTPUT(program, "one two or three many\n");
    // Like C, cases fall through until a break.
    CHECK_OUTPUT("switch (2) {\ncase 1:\n print(1)\ncase 2:\n print(2)\ncase 3:\n print(3)\n break\ncase 4:\n print(4)\n}", "2\n3\n");
    CHECK_OUTPUT("switch (9) {\ncase 1:\n print(1)\n}\nprint(\"none\")", "none\n");
    CHECK_OUTPUT("switch \"b\" { case \"a\": print(\"A\"); break; case \"b\": print(\"B\"); break }", "B\n");
    CHECK_OUTPUT("switch 5 {\ndefault:\n print(\"default\")\ncase 1:\n print(\"one\")\n}", "default\none\n");
    // break leaves the switch, continue goes to the loop around it.
    CHECK_OUTPUT("for i in range(4) {\n switch (i) {\n case 1: continue\n case 3: break\n }\n print(i)\n}", "0\n2\n3\n");
    CHECK_OUTPUT("switch 1 {\ncase 1:\n x := 5\n print(x)\n}", "5\n");
    CHECK_ERROR("switch 1 { case 1: continue }", "'continue' can only be used inside a loop");
    CHECK_ERROR("break", "'break' can only be used inside a loop or switch");
}

TEST(lang_casts_and_type_aliases) {
    CHECK_OUTPUT("print((f64) 7 / 2, (i32) 3.9, (string) 5 + \"!\", (char) 65, (bool) 0)", "3.5 3 5! A false\n");
    CHECK_OUTPUT("double d = 2\nlong l = 5\nshort s = 3\nprint(type(d), type(l), type(s), d)", "f64 i64 i16 2.0\n");
    CHECK_OUTPUT("print(double(\"1.5\"), long(2.0), short(-7))", "1.5 2 -7\n");
    CHECK_ERROR("short s = 40000", "can't hold 40000 (i16 is -32768 to 32767)");
}

TEST(lang_auto) {
    CHECK_OUTPUT("auto n = 5\nauto f = 2.5f32\nauto s = \"x\"\nprint(type(n), type(f), type(s))", "int f32 string\n");
    CHECK_ERROR("auto n = 5\nn = \"five\"", "'n' is declared as int, so it can't hold a string");
    CHECK_ERROR("auto n = 5\nn = 2.5", "'n' is declared as int, so it can't hold 2.5");
    CHECK_OUTPUT("const auto LIMIT = 10u8\nprint(type(LIMIT))", "u8\n");
    CHECK_OUTPUT("auto anything = null\nanything = 5\nprint(anything)", "5\n");
}

TEST(lang_main_function) {
    Result result = run("int main() {\n    print(\"in main\")\n    return 3\n}\nprint(\"top level first\")");
    CHECK(result.status == RunStatus::Exited);
    CHECK_EQ(result.exitCode, 3);
    CHECK_EQ(result.out, "top level first\nin main\n");

    Result withArgs = run("void main(list args) { print(args) }", {"a", "b"});
    CHECK_EQ(withArgs.out, "[\"a\", \"b\"]\n");
    CHECK(withArgs.status == RunStatus::Ok);

    Result cStyle = run("int main(int argc, list argv) {\n    printf(\"%d %s\\n\", argc, argv[0])\n    return 0\n}", {"x"});
    CHECK_EQ(cStyle.out, "1 x\n");
    CHECK(cStyle.status == RunStatus::Ok);

    // A program that calls main() itself isn't run twice.
    CHECK_OUTPUT("func main() { print(\"once\") }\nmain()", "once\n");
    CHECK_ERROR("int main() { return 1 / 0 }", "division by zero");
}

// ---------------------------------------------------------------------------
// Import statements

TEST(lang_import_statements) {
    TempFiles files;
    files.add("ascend_test_geo.asc", "f64 square(f64 s) { return s * s }\nVERSION := \"1.0\"\n_hidden := 1\n");
    files.add("ascend_test_lib/shapes.asc", "struct Box { f64 w; f64 h = 1 }\nf64 area(Box b) { return b.w * b.h }\n");

    CHECK_OUTPUT("import ascend_test_geo\nprint(ascend_test_geo.square(3))", "9.0\n");
    CHECK_OUTPUT("import ascend_test_geo as geo, \"ascend_test_geo.asc\" as again\nprint(geo.VERSION, geo == again)",
                 "1.0 true\n");
    CHECK_OUTPUT("import ascend_test_lib.shapes\nprint(shapes.area(shapes.Box(2, 3)))", "6.0\n");
    CHECK_OUTPUT("from ascend_test_lib.shapes import Box, area as box_area\nBox b = Box(4)\nprint(box_area(b), b)",
                 "4.0 Box{w: 4.0, h: 1.0}\n");
    CHECK_OUTPUT("from ascend_test_geo import *\nprint(square(2), VERSION)", "4.0 1.0\n");
    CHECK_ERROR("from ascend_test_geo import _hidden", "\"ascend_test_geo\" has no '_hidden' (it has: VERSION, square)");
    CHECK_ERROR("import ascend_test_nothing", "no installed package named 'ascend_test_nothing'");
    CHECK_OUTPUT("func load() {\n    import ascend_test_geo as g\n    return g.square(5)\n}\nprint(load())", "25.0\n");
    CHECK_ERROR("const geo = 1\nimport ascend_test_geo as geo", "'geo' is a constant and can't be declared again");
}

// ---------------------------------------------------------------------------
// Structs and enums

TEST(lang_structs) {
    const char* point = "struct Point {\n    f64 x\n    f64 y = 0\n    string label = \"p\"\n}\n";
    CHECK_OUTPUT(std::string(point) + "p := Point(1, 2)\nprint(p, p.x, type(p))", "Point{x: 1.0, y: 2.0, label: \"p\"} 1.0 Point\n");
    CHECK_OUTPUT(std::string(point) + "print(Point(), Point(5).y)", "Point{x: 0.0, y: 0.0, label: \"p\"} 0.0\n");
    CHECK_OUTPUT(std::string(point) + "Point p\np.x = 3\np.y += 1\np.x++\nprint(p)", "Point{x: 4.0, y: 1.0, label: \"p\"}\n");
    CHECK_ERROR(std::string(point) + "p := Point()\np.x = \"far\"", "field 'x' of Point is f64, so it can't hold a string");
    CHECK_ERROR(std::string(point) + "p := Point()\np.z = 1", "Point has no field 'z' (its fields are: x, y, label)");
    CHECK_ERROR(std::string(point) + "p := Point()\np[\"z\"] = 1", "Point has no field 'z'");
    CHECK_ERROR(std::string(point) + "print(Point().z)", "Point has no field 'z'");
    CHECK_ERROR(std::string(point) + "Point(1, 2, \"a\", 4)", "Point() takes at most 3 arguments (x, y, label), but got 4");
    CHECK_ERROR(std::string(point) + "Point(\"left\")", "Point(): field 'x' is f64, so it can't hold a string");
    CHECK_ERROR(std::string(point) + "remove(Point(), \"x\")", "remove() can't remove fields from a Point");

    // Struct types in declarations, parameters and return values.
    const char* typed = "struct Point { f64 x; f64 y }\n"
                        "struct Size { f64 w; f64 h }\n"
                        "f64 distance(Point a, Point b) { return hypot(b.x - a.x, b.y - a.y) }\n"
                        "Point? find(list points, f64 x) {\n"
                        "    for (Point p : points) if (p.x == x) return p\n"
                        "    return null\n"
                        "}\n";
    CHECK_OUTPUT(std::string(typed) + "print(distance(Point(0, 0), Point(3, 4)))", "5.0\n");
    CHECK_OUTPUT(std::string(typed) + "print(find([Point(1, 1), Point(2, 5)], 2), find([], 1))", "Point{x: 2.0, y: 5.0} null\n");
    CHECK_ERROR(std::string(typed) + "distance(Size(1, 1), Point(0, 0))",
                "distance() expects 'a' to be Point, but got a Size");
    CHECK_ERROR(std::string(typed) + "distance({x: 1, y: 1}, Point(0, 0))", "but got a map (use Point(...) to make one)");
    CHECK_ERROR(std::string(typed) + "Point p = Size(1, 2)", "'p' is declared as Point, so it can't hold a Size");

    // Method-style calls find functions that take the struct first.
    CHECK_OUTPUT(std::string(typed) + "print(Point(0, 0).distance(Point(0, 2)))", "2.0\n");
    // Nested structs start with their own defaults; copies and equality work.
    CHECK_OUTPUT("struct A { i32 n = 7 }\nstruct B { A inner; A? other }\nB b\nprint(b, copy(b) == b, type(copy(b)))",
                 "B{inner: A{n: 7}, other: null} true B\n");
    CHECK_OUTPUT("struct A { i32 n }\nstruct B { i32 n }\nprint(A(1) == A(1), A(1) == B(1), A(1) == {n: 1})", "true false false\n");
    // Structs can be used before they're declared, like functions.
    CHECK_OUTPUT("print(Later(2))\nstruct Later { i32 n }", "Later{n: 2}\n");
    // Each value gets its own fresh default.
    CHECK_OUTPUT("struct Bag { list items = [] }\na := Bag()\nb := Bag()\na.items.push(1)\nprint(b.items)", "[]\n");

    CHECK_ERROR("struct P { f64 x; f64 x }", "struct P has two fields named 'x'");
    CHECK_ERROR("Pointt p", "unknown type 'Pointt'");
    CHECK_ERROR("struct T { u8 small = 300 }", "field 'small' is declared as u8, so its default can't be 300");
}

TEST(lang_enums) {
    CHECK_OUTPUT("enum Color { Red, Green = 5, Blue }\nprint(Color.Red, Color.Green, Color.Blue, Color)",
                 "0 5 6 {\"Red\": 0, \"Green\": 5, \"Blue\": 6}\n");
    CHECK_OUTPUT("enum Level {\n    Low\n    High\n}\nswitch (Level.High) {\ncase Level.Low: print(\"low\"); break\ncase Level.High: print(\"high\")\n}",
                 "high\n");
    CHECK_ERROR("enum Color { Red }\nColor.Red = 2", "the members of enum Color can't be changed");
    CHECK_ERROR("enum Color { Red }\nprint(Color.Pink)", "enum Color has no member 'Pink'");
    CHECK_ERROR("enum Color { Red }\nColor = 5", "can't change 'Color': it's a constant");
    CHECK_ERROR("enum E { A, A }", "enum E lists 'A' more than once");
}

// ---------------------------------------------------------------------------
// Builtins

TEST(lang_printf) {
    CHECK_OUTPUT("printf(\"%d|%5d|%-5d|%05d|%+d|% d\\n\", 1, 2, 3, 4, 5, 6)", "1|    2|3    |00004|+5| 6\n");
    CHECK_OUTPUT("printf(\"%x %X %#x %o %#o %b %c %c\\n\", 255, 255, 255, 8, 8, 5, 65, 'z')", "ff FF 0xff 10 010 101 A z\n");
    CHECK_OUTPUT("printf(\"%f|%.2f|%8.3f|%-8.1f|%e|%g\\n\", 3.14159, 3.14159, 2.5, 2.5, 12345.678, 0.0001)",
                 "3.141590|3.14|   2.500|2.5     |1.234568e+04|0.0001\n");
    CHECK_OUTPUT("printf(\"%s|%10s|%-6s|%.3s|%s\\n\", \"hi\", \"right\", \"left\", \"abcdef\", [1, 2])",
                 "hi|     right|left  |abc|[1, 2]\n");
    CHECK_OUTPUT("print(sprintf(\"%*d|%-*d|%.*f|%%|%ld|%lld|%zu\", 4, 7, 3, 8, 1, 2.25, 1, 2, 3))", "   7|8  |2.2|%|1|2|3\n");
    CHECK_OUTPUT("printf(\"%05.1f|%-+5d|%d\\n\", -2.5, 3, 'a')", "-02.5|+3   |97\n");
    CHECK_ERROR("printf(\"%d\", 2.5)", "printf(): %d needs an integer, but got 2.5 (use %f for floats");
    CHECK_ERROR("printf(\"%d %d\", 1)", "no value for %d");
    CHECK_ERROR("printf(\"%d\", 1, 2)", "2 values were given, but the format only uses 1");
    CHECK_ERROR("printf(\"%q\", 1)", "unknown conversion '%q'");
    CHECK_ERROR("printf(\"50%\")", "the format ends in the middle of '%'");
}

TEST(lang_regex) {
    CHECK_OUTPUT("print(regex_match(\"2024-01-05\", \"\\\\d{4}-\\\\d{2}-\\\\d{2}\"), regex_match(\"x2024\", \"\\\\d+\"))",
                 "true false\n");
    CHECK_OUTPUT("print(regex_search(\"id=42; id=7\", \"id=(\\\\d+)\"), regex_search(\"x\", \"y\"))", "[\"id=42\", \"42\"] null\n");
    CHECK_OUTPUT("print(regex_search(\"ab\", \"a(x)?b\"))", "[\"ab\", null]\n");
    CHECK_OUTPUT("print(regex_find_all(\"a1b22c333\", \"\\\\d+\"))", "[\"1\", \"22\", \"333\"]\n");
    CHECK_OUTPUT("print(regex_replace(\"John Smith\", \"(\\\\w+) (\\\\w+)\", \"$2, $1\"))", "Smith, John\n");
    CHECK_OUTPUT("print(regex_split(\"a, b,c\", \",\\\\s*\"))", "[\"a\", \"b\", \"c\"]\n");
    CHECK_ERROR("regex_match(\"x\", \"(\")", "regex_match(): invalid regular expression \"(\"");
}

TEST(lang_folders) {
    TempFiles files;
    files.add("ascend_test_folder/b.txt", "b");
    files.add("ascend_test_folder/a.txt", "a");
    CHECK_OUTPUT("print(is_dir(\"ascend_test_folder\"), is_dir(\"ascend_test_folder/a.txt\"), list_dir(\"ascend_test_folder\"))",
                 "true false [\"a.txt\", \"b.txt\"]\n");
    CHECK_OUTPUT("print(make_dir(\"ascend_test_folder/x/y\"), make_dir(\"ascend_test_folder/x/y\"), is_dir(\"ascend_test_folder/x/y\"))",
                 "true false true\n");
    CHECK_ERROR("list_dir(\"ascend_test_no_such_folder\")", "list_dir(): can't read the folder 'ascend_test_no_such_folder'");
}

TEST(lang_float_printing) {
    CHECK_OUTPUT("print(20.0, 100.0, 1e15, 1e16, 0.0001, 0.00001, 123456.789, -20.0)",
                 "20.0 100.0 1000000000000000.0 1e+16 0.0001 1e-05 123456.789 -20.0\n");
}

// ---------------------------------------------------------------------------
// Methods, lambdas, comprehensions and more

TEST(lang_struct_methods) {
    const char* vec =
        "struct Vec {\n"
        "    f64 x\n"
        "    f64 y = 0\n"
        "    f64 length() { return hypot(this.x, this.y) }\n"
        "    void scale(f64 k) {\n"
        "        this.x *= k\n"
        "        this.y *= k\n"
        "    }\n"
        "    func plus(other) { return Vec(this.x + other.x, this.y + other.y) }\n"
        "    label() { return f\"({this.x}, {this.y})\" }\n"
        "}\n";
    CHECK_OUTPUT(std::string(vec) + "v := Vec(3, 4)\nprint(v.length(), v.plus(Vec(1)).label())", "5.0 (4.0, 4.0)\n");
    CHECK_OUTPUT(std::string(vec) + "v := Vec(1, 2)\nv.scale(3)\nprint(v)", "Vec{x: 3.0, y: 6.0}\n");
    CHECK_ERROR(std::string(vec) + "print(Vec(1).length)", "'length' is a method of Vec; call it: .length()");
    CHECK_ERROR(std::string(vec) + "Vec(1).size()", "Vec has no method or field 'size'");
    CHECK_ERROR(std::string(vec) + "Vec(1).scale(\"big\")", "Vec.scale() expects 'k' to be f64, but got a string");
    // Methods win over functions with the same name, and fields hold their own functions.
    CHECK_OUTPUT("func size(x) { return \"global\" }\nstruct S { size() { return \"method\" } }\nprint(S().size())", "method\n");
    // init() runs when a value is made.
    const char* account =
        "struct Account {\n"
        "    string owner\n"
        "    f64 balance = 0\n"
        "    void init() {\n"
        "        assert(this.balance >= 0, \"balance can't be negative\")\n"
        "        this.owner = upper(this.owner)\n"
        "    }\n"
        "}\n";
    CHECK_OUTPUT(std::string(account) + "print(Account(\"ada\", 5))", "Account{owner: \"ADA\", balance: 5.0}\n");
    CHECK_ERROR(std::string(account) + "Account(\"bob\", -1)", "balance can't be negative");
    CHECK_ERROR("struct P { i32 a; f64 a() { return 1 } }", "struct P already has a field or method named 'a'");
    CHECK_ERROR("struct P { void init(i32 x) { } }", "init() is called with no arguments when a P is made");
}

TEST(lang_lambdas) {
    CHECK_OUTPUT("double := x => x * 2\nadd := (a, b) => a + b\nprint(double(4), add(2, 3))", "8 5\n");
    CHECK_OUTPUT("print(map([1, 2, 3], x => x * x), filter(range(10), n => n % 4 == 0))", "[1, 4, 9] [0, 4, 8]\n");
    CHECK_OUTPUT("print(sort([\"ccc\", \"a\", \"bb\"], s => len(s)))", "[\"a\", \"bb\", \"ccc\"]\n");
    CHECK_OUTPUT("square := (i32 n) => { return n * n }\nprint(square(7))", "49\n");
    CHECK_ERROR("square := (i32 n) => n * n\nsquare(\"x\")", "expects 'n' to be i32");
    CHECK_OUTPUT("make := n => () => n + 1\nprint(make(5)())", "6\n");
    CHECK_OUTPUT("x := 3\nmatch x {\n    3 => print(\"three\")\n    else => print(\"other\")\n}", "three\n");
}

TEST(lang_comprehensions) {
    CHECK_OUTPUT("print([x * x for x in 1..=5])", "[1, 4, 9, 16, 25]\n");
    CHECK_OUTPUT("print([x for x in range(20) if x % 7 == 0])", "[0, 7, 14]\n");
    CHECK_OUTPUT("print([[a, b] for a in 1..3 for b in \"xy\"])", "[[1, 'x'], [1, 'y'], [2, 'x'], [2, 'y']]\n");
    CHECK_OUTPUT("print({k: v * 10 for k, v in {a: 1, b: 2}})", "{\"a\": 10, \"b\": 20}\n");
    CHECK_OUTPUT("print({s: len(s) for s in [\"hi\", \"hey\"] if len(s) > 2})", "{\"hey\": 3}\n");
    CHECK_OUTPUT("print([i for i, c in \"abc\" if c != 'b'], [k for k in {x: 1, y: 2}])", "[0, 2] [\"x\", \"y\"]\n");
    // The loop variables don't leak out.
    CHECK_ERROR("[x for x in [1]]\nprint(x)", "undefined variable 'x'");
    CHECK_OUTPUT("fns := [() => i for i in 0..3]\nprint([f() for f in fns])", "[0, 1, 2]\n");
}

TEST(lang_destructuring) {
    CHECK_OUTPUT("a, b := 1, 2\na, b = b, a\nprint(a, b)", "2 1\n");
    CHECK_OUTPUT("func minmax(list xs) { return min(xs), max(xs) }\nlo, hi := minmax([5, 3, 9])\nprint(lo, hi)", "3 9\n");
    CHECK_OUTPUT("struct P { i32 x; i32 y }\nx, y := P(7, 8)\nprint(x, y)", "7 8\n");
    CHECK_OUTPUT("items := [0, 0]\nm := {}\nitems[0], m.key = \"a\", \"b\"\nprint(items, m)", "[\"a\", 0] {\"key\": \"b\"}\n");
    CHECK_ERROR("a, b := [1]", "can't unpack 1 value into 2 variables");
    CHECK_ERROR("a, b := 5", "can't unpack an int value into 2 variables (it needs a list or a struct)");
    CHECK_ERROR("i32 a = 0\nb := 0\na, b = \"x\", 1", "'a' is declared as i32, so it can't hold a string");
}

TEST(lang_null_operators) {
    CHECK_OUTPUT("string? name = null\nprint(name ?? \"anonymous\", \"set\" ?? \"other\", 0 ?? 5, false ?? true)",
                 "anonymous set 0 false\n");
    CHECK_OUTPUT("user := {address: null}\nprint(user.address?.city, user?.address, null?.length())", "null null null\n");
    CHECK_OUTPUT("user := {address: {city: \"Oslo\"}}\nprint(user.address?.city ?? \"?\")", "Oslo\n");
    CHECK_OUTPUT("calls := 0\nfunc f() { calls++\n return 1 }\nx := 5 ?? f()\nprint(x, calls)", "5 0\n");
}

TEST(lang_ranges) {
    CHECK_OUTPUT("print(0..4, 1..=3, 'a'..='d', 5..2)", "[0, 1, 2, 3] [1, 2, 3] ['a', 'b', 'c', 'd'] []\n");
    CHECK_OUTPUT("total := 0\nfor i in 1..=100 { total += i }\nprint(total)", "5050\n");
    CHECK_OUTPUT("for i in 0..1000000000 { if (i == 3) break }\nprint(\"no list was built\")", "no list was built\n");
    CHECK_OUTPUT("print(5 in 1..10, 10 in 1..10, 10 in 1..=10, 'q' in 'a'..='z', 2.5 in 0..3, \"x\" in 0..3)",
                 "true false true true true false\n");
    CHECK_OUTPUT("func grade(i32 score) {\n    switch (score) {\n    case 90..=100: return \"A\"\n    case 80..90: return \"B\"\n"
                 "    default: return \"C\"\n    }\n}\nprint(grade(95), grade(85), grade(10))",
                 "A B C\n");
    CHECK_OUTPUT("match 15 {\n    0..10 => print(\"small\")\n    10..100 => print(\"medium\")\n}", "medium\n");
    CHECK_ERROR("print(\"a\"..\"b\")", "a range needs two integers or two chars, not a string and a string");
    CHECK_ERROR("x := 1..=100000000", "the range is too large to make a list from");
}

TEST(lang_variadic_and_spread) {
    const char* sum = "i32 sum(i32 ...nums) {\n    i32 total = 0\n    for (n in nums) total += n\n    return total\n}\n";
    CHECK_OUTPUT(std::string(sum) + "print(sum(), sum(1, 2, 3), sum(...[4, 5], 6))", "0 6 15\n");
    CHECK_ERROR(std::string(sum) + "sum(1, \"x\")", "sum() expects each of 'nums' to be i32, but got a string");
    CHECK_OUTPUT("func log(string level, ...parts) { print(level + \":\", join(parts, \" \")) }\nlog(\"info\", \"a\", 1, true)\nlog(\"x\")",
                 "info: a 1 true\nx: \n");
    CHECK_ERROR("func f(a, ...rest) { }\nf()", "f() expects at least 1 argument but got 0");
    CHECK_OUTPUT("print([0, ...[1, 2], 3, ...(4..6)])", "[0, 1, 2, 3, 4, 5]\n");
    CHECK_OUTPUT("defaults := {color: \"red\", size: 1}\nprint({...defaults, size: 2})", "{\"color\": \"red\", \"size\": 2}\n");
    CHECK_OUTPUT("xs := [1, 2]\nprint([...xs, ...xs], max(...xs))", "[1, 2, 1, 2] 2\n");
    CHECK_ERROR("print(...5)", "'...' needs a list to spread into the arguments, not an int");
}

TEST(lang_named_arguments) {
    const char* greet = "func greet(string name, string greeting = \"Hello\", string mark = \"!\") {\n"
                        "    return greeting + \", \" + name + mark\n}\n";
    CHECK_OUTPUT(std::string(greet) + "print(greet(\"Ada\", mark: \"?\"), greet(greeting: \"Hi\", name: \"Bob\"))",
                 "Hello, Ada? Hi, Bob!\n");
    CHECK_ERROR(std::string(greet) + "greet(\"x\", nope: 1)",
                "greet() has no parameter named 'nope' (its parameters are: name, greeting, mark)");
    CHECK_ERROR(std::string(greet) + "greet(mark: \"!\")", "greet() needs a value for 'name'");
    CHECK_ERROR(std::string(greet) + "greet(\"x\", name: \"y\")", "greet() got two values for 'name'");
    CHECK_ERROR("print(1, sep: \"\")", "print() doesn't take arguments by name");
    CHECK_OUTPUT("struct Box { f64 w = 1; f64 h = 1; string label = \"box\" }\nprint(Box(h: 3, label: \"tall\"))",
                 "Box{w: 1.0, h: 3.0, label: \"tall\"}\n");
    CHECK_ERROR("struct Box { f64 w }\nBox(depth: 2)", "Box() has no field named 'depth' (its fields are: w)");
}

TEST(lang_finally) {
    const char* risky = "func risky(n) {\n    try {\n        if (n == 0) throw \"zero!\"\n        return \"ok \" + str(n)\n"
                        "    } catch e {\n        return \"caught \" + e\n    } finally {\n        print(\"cleanup\", n)\n    }\n}\n";
    CHECK_OUTPUT(std::string(risky) + "print(risky(1))\nprint(risky(0))", "cleanup 1\nok 1\ncleanup 0\ncaught zero!\n");
    CHECK_OUTPUT("for i in range(3) {\n    try { if (i == 1) break } finally { print(\"finally\", i) }\n}", "finally 0\nfinally 1\n");
    CHECK_OUTPUT("try {\n    try { x := 1 / 0 } finally { print(\"inner\") }\n} catch e { print(\"outer:\", e) }",
                 "inner\nouter: division by zero\n");
    // The function's result survives calls made in finally.
    CHECK_OUTPUT("func helper() { return \"helper\" }\nfunc f() {\n    try { return \"result\" } finally { helper() }\n}\nprint(f())",
                 "result\n");
    CHECK_OUTPUT("func f() { try { throw \"x\" } finally { return \"replaced\" } }\nprint(f())", "replaced\n");
    // An error that escapes still points at where it happened.
    CHECK_ERROR("try {\n    x := 1 / 0\n} finally {\n    y := 2\n}", "test.asc:2:12: runtime error: division by zero");
}

TEST(lang_strings_raw_and_multiline) {
    CHECK_OUTPUT("print(r\"C:\\new\\d+\")", "C:\\new\\d+\n");
    CHECK_OUTPUT("print(regex_find_all(\"1-2 and 33-44\", r\"\\d+-\\d+\"))", "[\"1-2\", \"33-44\"]\n");
    CHECK_OUTPUT("text := \"\"\"\nLine one\n  Line \"two\"\n\"\"\"\nprint(repr(text))", "\"Line one\\n  Line \\\"two\\\"\\n\"\n");
}

TEST(lang_list_helpers) {
    CHECK_OUTPUT("print(group_by([\"apple\", \"bob\", \"avocado\"], w => str(w[0])))",
                 "{\"a\": [\"apple\", \"avocado\"], \"b\": [\"bob\"]}\n");
    CHECK_OUTPUT("print(count_by([1, 2, 3, 4, 5], n => n % 2 == 0))", "{false: 3, true: 2}\n");
    CHECK_OUTPUT("print(chunk(1..=7, 3), flat_map([1, 2], x => [x, x * 10]))", "[[1, 2, 3], [4, 5, 6], [7]] [1, 10, 2, 20]\n");
    CHECK_OUTPUT("print(mean([1, 2, 3, 4]), median([5, 1, 3]), median([4, 1, 3, 2]))", "2.5 3.0 2.5\n");
    CHECK_OUTPUT("words := [\"bob\", \"avocado\", \"cat\"]\nprint(min_by(words, len), max_by(words, len))", "bob avocado\n");
    CHECK_ERROR("mean([])", "mean() of an empty list");
    CHECK_ERROR("median([1, \"x\"])", "median() needs a list of numbers, but it contains a string");
    CHECK_ERROR("chunk([1], 0)", "chunk() size must be at least 1");
}

// ---------------------------------------------------------------------------
// Tracebacks, inheritance, operators, typed collections

TEST(lang_tracebacks) {
    const char* program =
        "f64 divide(f64 a, f64 b) {\n"
        "    if (b == 0) throw \"can't divide by zero\"\n"
        "    return a / b\n"
        "}\n"
        "func average(list xs) { return divide(sum(xs), len(xs)) }\n"
        "print(map([[1, 2], []], g => average(g)))\n";
    std::string err = error(program);
    CHECK(contains(err, "test.asc:2:17: runtime error: uncaught error: can't divide by zero"));
    CHECK(contains(err, "  in divide(), called from test.asc:5:32\n"));
    CHECK(contains(err, "  in average(), called from test.asc:6:30\n"));
    CHECK(contains(err, "  in an anonymous function, called by a builtin\n"));
    CHECK(contains(err, "  in map(), called from test.asc:6:7\n"));
    // Caught errors leave no traceback behind.
    std::string caught = error("func f() { return [][0] }\ntry { f() } catch e { }\nx := 1 / 0");
    CHECK(contains(caught, "division by zero"));
    CHECK(!contains(caught, "  in f()"));
    // Deep recursion is summarized.
    std::string deep = error("func f(n) { return n == 0 ? 1 / 0 : f(n - 1) }\nf(50)");
    CHECK(contains(deep, "  in f(), called from test.asc:1:37\n    (and 49 more times)\n  in f(), called from test.asc:2:1\n"));
}

TEST(lang_inheritance) {
    const char* animals =
        "struct Animal {\n"
        "    string name\n"
        "    i32 legs = 4\n"
        "    string sound() { return \"...\" }\n"
        "    string describe() { return f\"{this.name} says {this.sound()}\" }\n"
        "}\n"
        "struct Dog : Animal {\n"
        "    string breed = \"mutt\"\n"
        "    string sound() { return \"Woof\" }\n"
        "    string describe() { return super.describe() + \" (\" + this.breed + \")\" }\n"
        "}\n"
        "struct Bird : Animal {\n"
        "    void init() { this.legs = 2 }\n"
        "    string sound() { return \"Tweet\" }\n"
        "}\n";
    std::string base = animals;
    CHECK_OUTPUT(base + "d := Dog(\"Rex\", breed: \"beagle\")\nprint(d.describe(), d.legs)", "Rex says Woof (beagle) 4\n");
    CHECK_OUTPUT(base + "b := Bird(\"Tweety\")\nprint(b.describe(), b.legs, b)",
                 "Tweety says Tweet 2 Bird{name: \"Tweety\", legs: 2}\n");
    CHECK_OUTPUT(base + "Animal a = Dog(\"Rex\")\nfunc greet(Animal x) { return x.name }\nprint(type(a), greet(Bird(\"B\")))",
                 "Dog B\n");
    CHECK_OUTPUT(base + "d := Dog(\"Rex\")\nprint(is_a(d, Animal), is_a(d, Dog), is_a(d, Bird), is_a(d, \"Animal\"), is_a(5, \"int\"))",
                 "true true false true true\n");
    CHECK_ERROR(base + "Dog d = Bird(\"B\")", "'d' is declared as Dog, so it can't hold a Bird");
    CHECK_ERROR("struct A { i32 x }\nstruct B : A { i32 x }", "struct B can't declare 'x' again: it already has it from A");
    CHECK_ERROR("struct B : Missing { }", "unknown type 'Missing'");
    CHECK_ERROR("struct A { f() { return super.f() } }\nA().f()", "'super' can only be used in the methods of a struct that extends another");
}

TEST(lang_operator_overloading) {
    const char* vec =
        "struct Vec {\n"
        "    f64 x\n"
        "    f64 y\n"
        "    Vec operator+(Vec o) { return Vec(this.x + o.x, this.y + o.y) }\n"
        "    Vec operator*(f64 k) { return Vec(this.x * k, this.y * k) }\n"
        "    bool operator==(Vec o) { return this.x == o.x and this.y == o.y }\n"
        "    bool operator<(Vec o) { return this.length() < o.length() }\n"
        "    f64 operator[](i32 i) { return i == 0 ? this.x : this.y }\n"
        "    f64 length() { return hypot(this.x, this.y) }\n"
        "    string to_string() { return f\"({this.x}, {this.y})\" }\n"
        "}\n";
    std::string base = vec;
    CHECK_OUTPUT(base + "v := Vec(1, 2) + Vec(3, 4) * 2\nprint(v, v[0], v[1])", "(7.0, 10.0) 7.0 10.0\n");
    CHECK_OUTPUT(base + "v := Vec(3, 4)\nprint(v == Vec(3, 4), v != Vec(3, 4), Vec(1, 1) < v, v > Vec(1, 1), v <= v, v >= Vec(9, 9))",
                 "true false true true true false\n");
    CHECK_OUTPUT(base + "v := Vec(1, 1)\nv += Vec(1, 2)\nprint(v, [v], f\"{v}\", \"v=\" + v, str(v))",
                 "(2.0, 3.0) [(2.0, 3.0)] (2.0, 3.0) v=(2.0, 3.0) (2.0, 3.0)\n");
    CHECK_OUTPUT(base + "print(sort([Vec(3, 3), Vec(1, 0), Vec(2, 2)]), max([Vec(3, 3), Vec(1, 0)]), Vec(1, 0) in [Vec(1, 0)])",
                 "[(1.0, 0.0), (2.0, 2.0), (3.0, 3.0)] (3.0, 3.0) true\n");
    CHECK_ERROR("struct P { i32 x }\nsort([P(1), P(2)])", "can't compare a P with a P (give the struct an operator< method to sort it)");
    CHECK_ERROR("struct P { to_string() { return 5 } }\nprint(P())", "P.to_string() must return a string, not an int");
    CHECK_ERROR("struct P { P operator+() { return this } }", "operator+ takes one parameter");
}

TEST(lang_custom_iteration) {
    const char* countdown =
        "struct Countdown {\n"
        "    i32 from\n"
        "    list iter() { return [this.from - i for i in 0..this.from] }\n"
        "    i32 len() { return this.from }\n"
        "    bool contains(i32 n) { return n >= 1 and n <= this.from }\n"
        "}\n";
    std::string base = countdown;
    CHECK_OUTPUT(base + "c := Countdown(3)\nfor n in c print(n)\nprint([n * 10 for n in c], len(c), 2 in c, 5 in c)",
                 "3\n2\n1\n[30, 20, 10] 3 true false\n");
}

TEST(lang_typed_collections) {
    CHECK_OUTPUT("list<i32> xs = [1, 2]\nxs.push(3)\nxs[0] = 10\nxs.insert(0, 0)\nxs.extend([4])\nprint(xs)", "[0, 10, 2, 3, 4]\n");
    CHECK_ERROR("list<i32> xs = [1]\nxs.push(\"two\")", "this is a list<i32>, so it can't hold a string");
    CHECK_ERROR("list<i32> xs = [1]\nxs[0] = 2.5", "this is a list<i32>, so it can't hold 2.5");
    CHECK_OUTPUT("list<f64> fs = [1, 2]\nprint(fs)", "[1.0, 2.0]\n");
    CHECK_OUTPUT("map<string, i32> ages = {ada: 36}\nages[\"bob\"] = 40\nages.cy = 22\nprint(ages)", "{\"ada\": 36, \"bob\": 40, \"cy\": 22}\n");
    CHECK_ERROR("map<string, i32> ages = {}\nages[\"x\"] = \"old\"", "this is a map<string, i32>, so its values can't be a string");
    CHECK_ERROR("map<string, i32> ages = {}\nages[5] = 1", "this is a map<string, i32>, so its keys can't be 5");
    CHECK_OUTPUT("list<list<i32>> grid = [[1, 2], [3]]\ngrid[1].push(4)\nprint(grid)", "[[1, 2], [3, 4]]\n");
    CHECK_ERROR("list<list<i32>> grid = [[1]]\ngrid[0].push(\"x\")", "this is a list<i32>, so it can't hold a string");
    // A list used as a list<T> stays one, whatever variable it's reached through.
    CHECK_ERROR("plain := [1]\nlist<i32> typed = plain\nplain.push(\"x\")", "this is a list<i32>");
    CHECK_ERROR("plain := [1]\nlist<i32> a = plain\nlist<string> b = plain", "can't hold a list<i32> (a list or map keeps the element types");
    CHECK_OUTPUT("list<string> names\nnames.push(\"a\")\nprint(names, len(names))", "[\"a\"] 1\n");
    CHECK_OUTPUT("func total(list<f64> values) { return sum(values) }\nprint(total([1, 2.5]))", "3.5\n");
    CHECK_ERROR("func total(list<f64> values) { return sum(values) }\ntotal([\"x\"])",
                "total() expects 'values' to be list<f64>, but got a list containing a string");
    CHECK_OUTPUT("struct Team { list<string> members = [] }\nt := Team()\nt.members.push(\"ann\")\nprint(t)", "Team{members: [\"ann\"]}\n");
    CHECK_ERROR("struct Team { list<string> members = [] }\nTeam().members.push(5)", "this is a list<string>, so it can't hold 5");
    CHECK_ERROR("list<u8> small = [1, 300]", "'small' is declared as list<u8>, so it can't hold a list containing 300");
    CHECK_ERROR("map<string i32> m", "expected ',' between the key and value types");
    CHECK_OUTPUT("list<i32>? maybe = null\nprint(maybe)\nx := 3\nprint(x < 5, x > 2)", "null\ntrue true\n");
}

TEST(lang_deep_recursion_is_an_error) {
    CHECK_ERROR("func forever(n) { return forever(n + 1) }\nforever(0)", "test.asc:1:26: runtime error:");
}

// ---------------------------------------------------------------------------
// The scripting library

TEST(lang_run_programs) {
#if defined(_WIN32)
    CHECK_OUTPUT("r := run(\"echo hello\")\nprint(r.code, trim(r.stdout), r.ok)", "0 hello true\n");
    CHECK_OUTPUT("r := run(\"exit 3\")\nprint(r.code, r.ok)", "3 false\n");
#else
    CHECK_OUTPUT("r := run([\"echo\", \"hello\", \"world\"])\nprint(r.code, r.stdout, r.ok)", "0 hello world\n true\n");
    CHECK_OUTPUT("r := run(\"echo out; echo err >&2; exit 3\")\nprint(r.code, repr(r.stdout), repr(r.stderr), r.ok)",
                 "3 \"out\\n\" \"err\\n\" false\n");
    CHECK_OUTPUT("print(run([\"cat\"], \"fed in\").stdout)", "fed in\n");
#endif
    CHECK_ERROR("run([\"ascend-no-such-program\"])", "run(): couldn't start 'ascend-no-such-program'");
    CHECK_ERROR("run([])", "run(): the command list is empty");
}

TEST(lang_paths_and_folders) {
    TempFiles files;
    files.add("ascend_test_tree/inner/a.txt", "abc");
    files.add("ascend_test_tree/b.txt", "hello");
    CHECK_OUTPUT("print(path_join(\"a\", \"b\", \"c.txt\"), basename(\"/x/y/z.tar.gz\"), dirname(\"/x/y/z.txt\"), extension(\"p.JPG\"))",
                 "a/b/c.txt z.tar.gz /x/y .JPG\n");
    CHECK_OUTPUT("print(walk_dir(\"ascend_test_tree\"), file_size(\"ascend_test_tree/b.txt\"))", "[\"b.txt\", \"inner/a.txt\"] 5\n");
    CHECK_OUTPUT("copy_file(\"ascend_test_tree/b.txt\", \"ascend_test_tree/c.txt\")\n"
                 "move_file(\"ascend_test_tree/c.txt\", \"ascend_test_tree/d.txt\")\n"
                 "print(list_dir(\"ascend_test_tree\"), read_file(\"ascend_test_tree/d.txt\"))",
                 "[\"b.txt\", \"d.txt\", \"inner\"] hello\n");
    CHECK_OUTPUT("print(remove_dir(\"ascend_test_tree/inner\"), is_dir(\"ascend_test_tree/inner\"), remove_dir(\"nope\"))",
                 "true false false\n");
    CHECK_OUTPUT("print(len(cwd()) > 0, is_dir(temp_dir()), platform() in [\"linux\", \"macos\", \"windows\", \"unix\"])",
                 "true true true\n");
    CHECK_OUTPUT("set_env(\"ASCEND_TEST_SETTING\", 42)\nprint(env(\"ASCEND_TEST_SETTING\"))", "42\n");
    CHECK_ERROR("file_size(\"ascend_no_such_file\")", "file_size(): 'ascend_no_such_file':");
}

TEST(lang_text_math_and_sets) {
    CHECK_OUTPUT("print(capitalize(\"hello world\"), title(\"hello big-world\"), \"[\" + trim_start(\"  x \") + \"]\", \"[\" + trim_end(\"  x \") + \"]\")",
                 "Hello world Hello Big-World [x ] [  x]\n");
    CHECK_OUTPUT("print(last_index_of(\"banana\", \"an\"), last_index_of([1, 2, 1], 1), last_index_of(\"héllo\", 'l'), last_index_of(\"x\", \"y\"))",
                 "3 2 3 -1\n");
    CHECK_OUTPUT("print(lcm(4, 6), factorial(10), degrees(PI), radians(180), trunc(-3.7), trunc(5))",
                 "12 3628800 180.0 3.141592653589793 -3 5\n");
    CHECK_ERROR("factorial(21)", "factorial(21) is too large for an int");
    CHECK_OUTPUT("print(union([1, 2, 2], [2, 3]), intersect([1, 2, 3, 2], [2, 3, 4]), difference([1, 2, 3], [2]))",
                 "[1, 2, 3] [2, 3] [1, 3]\n");
}

TEST(lang_dates_csv_and_encodings) {
    CHECK_OUTPUT("n := now()\nprint(n.year > 2000, n.month in 1..=12, n.weekday in 0..7, type(n.timestamp))", "true true true float\n");
    CHECK_OUTPUT("print(format_time(86400 * 365, \"%Y\") in [\"1970\", \"1971\"])", "true\n");
    CHECK_OUTPUT("rows := csv_parse(\"name,age\\n\\\"Smith, John\\\",42\\n\\\"say \\\"\\\"hi\\\"\\\"\\\",7\\n\")\nprint(rows)",
                 "[[\"name\", \"age\"], [\"Smith, John\", \"42\"], [\"say \\\"hi\\\"\", \"7\"]]\n");
    CHECK_OUTPUT("print(repr(csv_stringify([[\"a\", \"b, c\"], [1, \"say \\\"hi\\\"\"]])))", "\"a,\\\"b, c\\\"\\n1,\\\"say \\\"\\\"hi\\\"\\\"\\\"\\n\"\n");
    CHECK_OUTPUT("print(csv_parse(\"a;b\\r\\nc;d\", ';'))", "[[\"a\", \"b\"], [\"c\", \"d\"]]\n");
    CHECK_ERROR("csv_parse(\"\\\"open\")", "csv_parse(): a quoted field is never closed");
    CHECK_OUTPUT("print(base64_encode(\"Hello, Ascend!\"), base64_decode(\"SGVsbG8sIEFzY2VuZCE=\"), base64_encode(\"a\"), base64_encode(\"ab\"))",
                 "SGVsbG8sIEFzY2VuZCE= Hello, Ascend! YQ== YWI=\n");
    CHECK_ERROR("base64_decode(\"*\")", "base64_decode(): '*' isn't base64");
    CHECK_OUTPUT("print(sha256(\"abc\"))", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
    CHECK_OUTPUT("id := uuid()\nprint(len(id), id[14], id[8], uuid() != id)", "36 4 - true\n");
}

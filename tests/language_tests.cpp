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

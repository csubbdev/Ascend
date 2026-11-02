# Ascend

[![Build](https://github.com/csubbdev/Ascend/actions/workflows/build.yml/badge.svg)](https://github.com/csubbdev/Ascend/actions/workflows/build.yml)

Ascend is a friendly, general-purpose scripting language with optional C-style
types. Write quick scripts without types, or add them where you want the
checks: typed variables, functions, structs and collections are enforced, and
many mistakes are reported before the program even runs. It comes with a
package manager, a test runner, a REPL and a large standard library, and runs
on Linux, macOS and Windows.

```
struct Animal {
    string name
    string sound() { return "..." }
    string to_string() { return f"{this.name} says {this.sound()}" }
}

struct Dog : Animal {
    string sound() { return "Woof" }
}

list<Animal> pets = [Dog("Rex"), Animal("Generic")]
for (Animal pet : pets) print(pet)

scores := {ada: 95, linus: 88, grace: 91}
best := max_by(keys(scores), name => scores[name])
printf("best: %s with %d\n", best, scores[best])

squares := [n * n for n in 1..=10 if n % 2 == 0]
response := http_get("https://api.github.com/repos/csubbdev/Ascend")
if (response.ok) print(json_parse(response.body).full_name)
```

## Features

**The language**
- **Optional C-style types**: `u8 age = 30`, `f64 area(f64 w, f64 h) { }`, with
  `i8`–`i64`, `u8`–`u64`, `f32`, `f64`, `bool`, `char`, `string`, nullable
  types like `string?`, `auto`, and typed collections like `list<i32>` and
  `map<string, f64>`. Integer overflow is an error instead of a silent wrap.
- **Structs** with typed fields, methods, inheritance (`struct Dog : Animal`),
  operator overloading (`operator+`, `operator==`, `operator<`, ...) and
  special methods (`to_string`, `iter`, `len`); **enums**.
- **Functions**: default and named arguments, `...rest` parameters, closures,
  lambdas (`x => x * 2`), multiple return values (`return lo, hi`).
- **Expressive code**: comprehensions (`[x * x for x in xs if x > 0]`), ranges
  (`0..n`), destructuring (`a, b = b, a`), `??` and `?.`, `match`, f-strings
  with format specs, raw and multi-line strings.
- **C-style code works too**: `for (i32 i = 0; i < n; i++)`, `i++`,
  `switch`/`case` with fall-through, `do`/`while`, bodies without braces,
  casts, `printf`, and a `main()` that runs automatically.
- **Errors**: `try`/`catch`/`finally`/`throw`, clear messages that point at
  the problem, and tracebacks through function calls:

  ```
  script.asc:3:10: error: 'age' is declared as u8, so it can't hold 300 (u8 is 0 to 255)
   3 | u8 age = 300
     |          ^
  ```

**The tools**
- **`apkg`**, a package manager like pip: `apkg install colors`, then
  `import colors`. Packages come from the registry, GitHub, any git
  repository or a folder.
- **`ascend test`** runs your `*_test.asc` files.
- An interactive REPL, `ascend --check` to find errors without running, and
  syntax highlighting for VS Code.

**The standard library** (170+ builtin functions)
- Strings, lists, maps and sets, math, regular expressions, JSON and CSV
- Files, folders and paths; running programs and capturing their output
- HTTP requests, dates and times, base64, SHA-256, UUIDs

## Quick start

```sh
cmake -S . -B build
cmake --build build
./build/ascend examples/shapes.asc     # run a program
./build/ascend                          # interactive REPL
./build/ascend test examples            # run the test files in a folder
```

See [examples/](examples/) for more: [functional.asc](examples/functional.asc),
[methods.asc](examples/methods.asc), [scripting.asc](examples/scripting.asc),
[c_style.asc](examples/c_style.asc) and others.

Install a package and use it:

```sh
./build/apkg install colors
./build/ascend -e 'import colors
print(colors.green("Hello!"))'
```

## Documentation

- [Language guide](docs/syntax.md)
- [Standard library](docs/standard_library.md)
- [Packages and apkg](docs/packages.md)
- [Building, installing, testing and editor support](docs/installation.md)
- [Example programs](examples/)

## How it works

| Stage | Source |
|-------|--------|
| Lexer: source text to tokens | `src/lexer.cpp` |
| Parser: tokens to a syntax tree | `src/parser.cpp`, `src/include/ast.hpp` |
| Checks before running (types of literals, `break` outside loops, ...) | `src/semantic_analyzer.cpp`, `src/typecheck.cpp` |
| Tree-walking interpreter | `src/interpreter.cpp` |
| Values, numeric types and checked arithmetic | `src/value.cpp`, `src/numeric.cpp`, `src/types.cpp` |
| Builtin functions, formatting and JSON | `src/stdlib.cpp`, `src/stdlib_extra.cpp`, `src/format.cpp`, `src/json.cpp` |
| Running other programs | `src/process.cpp` |
| Command line, REPL, `ascend test` | `src/main.cpp`, `src/repl.cpp`, `src/runner.cpp`, `src/big_stack.cpp` |
| Package manager (`apkg`) and finding packages for `import` | `src/apkg_main.cpp`, `src/package_manager.cpp`, `src/packages.cpp` |

## License

Apache License 2.0; see [LICENSE](LICENSE).

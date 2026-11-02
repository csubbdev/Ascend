# Ascend

Ascend is a small, friendly scripting language with optional types, written in
C++17.

```
func fizzbuzz(n: i32) -> string {
    if n % 15 == 0 { return "FizzBuzz" }
    if n % 3 == 0 { return "Fizz" }
    if n % 5 == 0 { return "Buzz" }
    return str(n)
}

for i in range(1, 16) {
    print(fizzbuzz(i))
}

level: u8 = 2
match level {
    1 => print("easy")
    2, 3 => print("normal")
    else => print("hard")
}

scores := {ada: 95, linus: 88}
for name, score in scores {
    print(f"{name:<6} {score:>3}")
}
```

## Features

- **Optional types**: `i8`–`i64`, `u8`–`u64`, `byte`, `f32`, `f64`, `bool`,
  `char`, `string`, `list`, `map`, `func` and nullable `T?`. Typed variables,
  parameters and return values are checked, and integer overflow is an error
  instead of a silent wrap.
- Constants, `match`, `if` / `else if`, `while`, `for ... in` over ranges,
  lists, strings and maps, `break` / `continue`
- Functions with default parameters, recursion, closures and anonymous functions
- f-strings with format specs: `f"{price:>8.2f}"`
- Hex, binary and octal literals, type suffixes (`255u8`) and bitwise operators
- Unicode strings and a `char` type
- Modules with `import("file")`
- `try` / `catch` / `throw` error handling
- 100+ builtins for strings, lists, maps, math, files, JSON and the system
- Clear error messages that point at the problem, many caught before running:

  ```
  script.asc:3:9: error: 'age' is declared as u8, so it can't hold 300 (u8 is 0 to 255)
   3 | age: u8 = 300
     |         ^
  ```

- An interactive REPL, a `--check` mode, and syntax highlighting for VS Code

## Quick start

```sh
cmake -S . -B build
cmake --build build
./build/ascend examples/types.asc
./build/ascend          # interactive REPL
```

## Documentation

- [Language guide](docs/syntax.md)
- [Standard library](docs/standard_library.md)
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
| Builtin functions, formatting and JSON | `src/stdlib.cpp`, `src/format.cpp`, `src/json.cpp` |
| Command line and REPL | `src/main.cpp`, `src/repl.cpp`, `src/runner.cpp` |

## License

Apache License 2.0; see [LICENSE](LICENSE).

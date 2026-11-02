# Ascend

Ascend is a small, friendly, dynamically typed scripting language with an
interpreter written in C++17.

```
func fizzbuzz(n) {
    if n % 15 == 0 { return "FizzBuzz" }
    if n % 3 == 0 { return "Fizz" }
    if n % 5 == 0 { return "Buzz" }
    return str(n)
}

for i in range(1, 16) {
    print(fizzbuzz(i))
}

scores := {ada: 95, linus: 88}
scores.grace = 91
for name, score in scores {
    print(name, score >= 90 ? "top" : "ok")
}
```

## Features

- Ints, floats, strings, bools, `null`, lists and insertion-ordered maps
- `if` / `else if` / `else`, `while`, `for ... in` over ranges, lists, strings and maps, `break` / `continue`
- Functions with default parameters, recursion, closures and anonymous functions
- Method-call syntax for builtins: `items.push(4)`, `"a,b".split(",")`
- `try` / `catch` / `throw` error handling
- 50+ builtins for strings, lists, maps, math and I/O
- Clear error messages that point at the problem:

  ```
  script.asc:2:7: runtime error: undefined variable 'cuont' (did you mean 'count'?)
   2 | print(cuont)
     |       ^
  ```

- An interactive REPL with multi-line input

## Quick start

```sh
cmake -S . -B build
cmake --build build
./build/ascend examples/fizzbuzz.asc
./build/ascend          # interactive REPL
```

## Documentation

- [Language guide](docs/syntax.md)
- [Standard library](docs/standard_library.md)
- [Building, installing and testing](docs/installation.md)
- [Example programs](examples/)

## How it works

| Stage | Source |
|-------|--------|
| Lexer: source text to tokens | `src/lexer.cpp` |
| Parser: tokens to a syntax tree | `src/parser.cpp`, `src/include/ast.hpp` |
| Semantic checks (e.g. `break` outside a loop) | `src/semantic_analyzer.cpp` |
| Tree-walking interpreter | `src/interpreter.cpp` |
| Values, and a cycle collector for closures | `src/value.cpp`, `src/include/environment.hpp` |
| Builtin functions | `src/stdlib.cpp` |
| Command line and REPL | `src/main.cpp`, `src/repl.cpp`, `src/runner.cpp` |

## License

Apache License 2.0; see [LICENSE](LICENSE).

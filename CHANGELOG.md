# Changelog

## 0.3.0

### New

- **Optional C-style types**: `type name = value` declarations, like
  `u8 age = 30`, with `i8` `i16` `i32` `i64` `u8` `u16` `u32` `u64` `byte`
  `int` `f32` `f64` `float` `bool` `char` `string` `list` `map` `func` `any`,
  and nullable types like `string?`. Typed variables reject values of the
  wrong type, and numbers that don't fit. `type name` without a value starts
  at the type's zero value.
- Every number carries its type. Arithmetic stays in that type and reports
  overflow as an error (`250u8 + 10` fails instead of wrapping).
- C-style typed functions: `f64 area(f64 w, f64 h) { }` and
  `void log(string message) { }`. Untyped functions still use `func`, and
  anonymous functions can declare a return type with `func(i32 x) -> i32 { }`.
- `const` declarations.
- `match` statements.
- f-strings with format specs: `f"{name:<10} {price:>8.2f}"`, and `format()`.
- `char` type and literals: `'a'`, `'\n'`, `'\u{1F600}'`; char arithmetic.
- Hex, binary and octal literals (`0xFF`, `0b1010`, `0o17`) and type
  suffixes (`255u8`, `1.5f32`).
- Bitwise operators `& | ^ ~ << >>` and their compound assignments.
- Unicode strings: `len`, indexing, slicing and iteration count characters.
- Modules: `import("file")` returns a file's top-level names as a map.
- Type mismatches in literals (`u8 x = 300`) and in `return` values are
  reported before the program runs.
- New builtins: type conversions (`i8()` ... `u64()`, `f32()`, `f64()`,
  `char()`, `string()`, `byte()`), `format`, `hex`, `bin`, `chars`, `bytes`,
  `from_bytes`, `lines`, `pad_left`, `pad_right`, `count`, `is_digit` and
  friends, `zip`, `unique`, `flatten`, `extend`, `clear`, `find`, `any`, `all`,
  `items`, `merge`, `clamp`, `sign`, `gcd`, trigonometry and logarithms,
  `choice`, `shuffle`, `INF`, `NAN`, file functions (`read_file`,
  `write_file`, `append_file`, `read_lines`, `file_exists`, `delete_file`),
  `json_parse`, `json_stringify`, `clock`, `sleep`, `date`, `env`, `system`.
- `ascend --check file.asc` reports errors without running the program.
- Syntax highlighting for VS Code in `editors/vscode`.
- GitHub Actions builds and tests every change on Linux, macOS and Windows,
  and uploads the built interpreters.

### Changed (may affect existing programs)

- Single quotes now make a `char`, not a string: write `"it's"` instead of
  `'it\'s'`.
- `&` and `|` are now bitwise operators (they used to be errors). Use `and` /
  `or` (or `&&` / `||`) for logic.
- Indexing a string gives a `char` instead of a one-character string. Chars
  compare equal to the matching one-character string, so `s[0] == "a"` still
  works.
- String lengths, indexes and slices count characters instead of bytes.
- `\xHH` in a string is now the character with that code (as in Python),
  encoded as UTF-8.
- `type()` returns `"func"` for functions (was `"function"`).
- Builtins now live in a scope above the program's globals, so a program can
  reuse their names for its own variables.

## 0.2.0

- Rewrote the interpreter as a lexer, parser, semantic checker and
  tree-walking interpreter, fixing the bugs in `!=`, `while`, `for` and
  function calls.
- Added floats, strings, bools, lists, maps, closures, `try` / `catch`,
  `else if`, `break` / `continue`, method-call syntax, slices, a REPL, error
  messages with source locations, a test suite and documentation.

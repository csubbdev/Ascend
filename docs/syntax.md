# Ascend language guide

Ascend is a small scripting language with optional C-style types. This page
covers the whole language; the builtin functions are listed in
[standard_library.md](standard_library.md).

```
string greet(string name, string greeting = "Hello") {
    return f"{greeting}, {name}!"
}

for name in ["Ada", "Grace"] {
    print(greet(name))
}
```

## Comments and statements

```
// line comment
/* block
   comment */
```

One statement per line. Semicolons are optional, but let you put several
statements on one line: `a := 1; b := 2`.

A statement can continue on the next line after an operator (`x := 1 +`) or
inside `( )` and `[ ]`, so long calls and lists can span several lines.

## Variables and types

Types are optional. Write `name := value` to let the value decide, or put a
type in front of the name, like in C, to fix the variable's type:

```
count := 0            // no declared type
u8 age = 30           // always a u8
f32 ratio = 0.5
char initial = 'A'
string title = "Ascend"
string? nickname      // the ? allows null; without a value this starts as null
i64 total             // without a value it starts at zero
```

`=` changes an existing variable (`count = 5`, `count += 1`). Assigning to a
name that was never declared is an error, which catches typos.

A typed variable only ever holds values of its type. Numbers convert
automatically when nothing is lost; anything else is an error:

```
u8 age = 30
age = 31          // fine
age = 300         // error: 'age' is declared as u8, so it can't hold 300 (u8 is 0 to 255)
age = "old"       // error: ... can't hold a string (use u8(...) to convert a string)
i32 x = 2.0       // fine: 2.0 is a whole number
x = 2.5           // error: ... (use i32(...) to drop the fraction)
```

Mistakes in literals like `u8 age = 300` are reported before the program
starts running.

### The types

| Type | Values |
|------|--------|
| `i8` `i16` `i32` `i64` | signed integers of 8, 16, 32 and 64 bits |
| `u8` `u16` `u32` `u64` | unsigned integers; `byte` is another name for `u8` |
| `int` | the type of integer literals like `42`: a 64-bit signed integer |
| `f32` `f64` | 32- and 64-bit floating point numbers |
| `float` | the type of float literals like `1.5`: a 64-bit float |
| `bool` | `true`, `false` |
| `char` | one Unicode character: `'a'`, `'\n'`, `'é'` |
| `string` | text: `"hello"` |
| `list` | `[1, "two", 3.0]` |
| `map` | `{name: "Ada", "a key": 1}` |
| `func` | a function |
| `any` | anything (the same as writing no type) |
| `void` | only as a return type: the function returns nothing |
| `T?` | `T` or `null`, e.g. `string?` |

`type(value)` returns the type's name: `type(5)` is `"int"`, `type(u8(5))` is
`"u8"`, `type('a')` is `"char"`.

Without a value, a typed variable starts at its type's zero value: `0`,
`0.0`, `false`, `""`, `'\0'`, `[]` or `{}` (and `null` for `T?` types).

### Constants

```
const MAX_PLAYERS = 4
const f64 RATE = 0.07
MAX_PLAYERS = 5      // error: can't change 'MAX_PLAYERS': it's a constant
```

### Converting between types

Every type name is also a conversion function: `i32("42")`, `u8(3.9)` (drops
the fraction), `f32(1)`, `char(65)`, `string(12)`, `byte('A')`, `int("0xFF")`.
A conversion fails if the value doesn't fit: `u8(300)` and `i8(-129)` are
errors.

### Truthiness

`false`, `null`, `0`, `0.0`, `'\0'`, `""`, `[]` and `{}` are falsy. Everything
else is truthy.

## Numbers

```
42      -7      1_000_000          // int
3.14    2.5e-3  1.0                // float
0xFF    0b1010  0o17               // hex, binary and octal
255u8   -128i8  7i64  1.5f32       // a suffix sets the type
```

How arithmetic picks its result type:

- A plain literal adapts to the other side: with `u8 x`, `x + 1` is a `u8`.
- Two different integer types give the smallest type that holds both:
  `u8 + i32` is an `i32`, `u8 + i8` is an `i16`. (`u64` can't be mixed with
  signed types; convert one side.)
- An integer and a float give the float's type: `i32 + f32` is an `f32`.
- `f32` and `f64` give `f64`.

**Overflow is an error**, never a silent wrap: with `u8 x = 250`, `x + 10`
reports `integer overflow: 250 + 10 doesn't fit in u8 (0 to 255)`.

- `int / int` truncates toward zero (`7 / 2 == 3`). If either side is a float
  the result is a float (`7 / 2.0 == 3.5`).
- `%` has the sign of the left side (`-7 % 3 == -1`), like C and JavaScript.
- Dividing by zero is an error.
- `f32` values are kept at 32-bit precision: `f32(1) / 3` is `0.33333334`.

## Operators

From lowest to highest precedence:

| Operators                       | Meaning |
|---------------------------------|---------|
| `a ? b : c`                     | conditional expression |
| `or`, `\|\|`                    | logical or (short-circuit) |
| `and`, `&&`                     | logical and (short-circuit) |
| `not`, `!`                      | logical not |
| `==`, `!=`                      | equality (deep for lists and maps; `1 == 1.0`) |
| `<`, `<=`, `>`, `>=`, `in`      | comparison; `x in list`, `key in map`, `"sub" in string` |
| `\|`                            | bitwise or |
| `^`                             | bitwise xor |
| `&`                             | bitwise and |
| `<<`, `>>`                      | shifts |
| `+`, `-`                        | add, subtract, concatenate |
| `*`, `/`, `%`                   | multiply, divide, remainder |
| `-x`, `~x`                      | negation, bitwise not |
| `**`                            | power (right associative, `-2 ** 2 == -4`) |
| `f(x)`, `a[i]`, `a[i:j]`, `m.k` | call, index, slice, field |

Notes:

- Bitwise operators bind tighter than comparisons (unlike C), so
  `flags & 1 == 0` means `(flags & 1) == 0`.
- Shifts keep the left side's type and drop bits shifted out of it:
  `0xF0u8 << 1` is `224`. `>>` is arithmetic for signed types.
- `~x` flips the bits within the type: `~0u8` is `255`, `~0` is `-1`.
- Compound assignment works for every operator: `+= -= *= /= %= &= |= ^= <<= >>=`.
- `and`, `or` and `not` always produce `true` or `false`. `not` applies to the
  whole comparison after it: `not x in items` means `not (x in items)`.
- `+` with a string on either side converts the other side to a string:
  `"total: " + 5`.
- `"ab" * 3` repeats a string; `[0] * 3` repeats a list; `[1] + [2]` joins lists.

## Strings and chars

Strings use double quotes; single quotes are for a single `char`.

```
s := "héllo"
len(s)       // 5: lengths, indexes and slices count characters (Unicode code points)
s[1]         // 'é' (a char)
s[-1]        // 'o' (negative indexes count from the end)
s[1:3]       // "él"
"ll" in s    // true
```

Escapes: `\n \t \r \0 \\ \" \' \{ \}`, `\xHH` (a character code in hex) and
`\u{1F600}` (any Unicode character).

Chars support a little arithmetic: `'a' + 1 == 'b'`, `'z' - 'a' == 25`,
`'-' * 3 == "---"`. A char equals the one-character string holding it
(`'a' == "a"`), and `char + string` concatenates.

Strings are immutable. Use `bytes(s)` for the raw UTF-8 bytes.

### f-strings

An `f` before a string makes `{...}` insert the value of an expression:

```
name := "Ada"
print(f"Hello, {name}! Next year you'll be {age + 1}.")
print(f"{price:.2f}  {count:>5}  {name:<10}|  {255:x}  {1234567:,}  {0.5:%}")
```

The part after `:` is a format spec: `[[fill]align][sign][0][width][,][.precision][type]`
with align `<` `>` `^`, and type `d` `x` `X` `o` `b` `f` `e` `g` `%` `s` `c`.
`{{` and `}}` are literal braces. Put a conditional expression in parentheses:
`{(ok ? "yes" : "no")}`. The same specs work with `format("{:>5}", x)`.

## Lists and maps

```
items := [10, 20, 30]
items[0] = 11
items.push(40)          // see "Method calls" below
items[1:]               // [20, 30, 40]

person := {name: "Ada", age: 36}
person.name             // "Ada"   (same as person["name"])
person["age"] += 1
person.city = "London"  // adds a key
"city" in person        // true
```

In a map literal, a bare name before `:` is a string key (`{name: 1}` is
`{"name": 1}`). Put an expression in parentheses to use its value as the key:
`{(key): 1}`. Keys may be strings, numbers, chars, bools or `null`.

Reading a missing map key or an out-of-range index is an error. Use
`get(map, key, default)` when a key may be missing.

Lists, maps and functions are **references**: `b := a` makes `b` refer to the
same list as `a`. Use `copy(a)` for a shallow copy.

## Control flow

Parentheses around conditions are optional.

```
if score >= 90 {
    print("A")
} else if score >= 80 {
    print("B")
} else {
    print("C")
}

while n > 0 {
    n -= 1
}

for i in range(5) { }            // 0, 1, 2, 3, 4
for i in range(2, 10, 2) { }     // 2, 4, 6, 8
for item in items { }
for i, item in items { }         // index and item
for c in "text" { }              // chars
for key in person { }            // map keys
for key, value in person { }
```

`break` leaves the innermost loop and `continue` skips to its next iteration.
Each loop iteration gets fresh loop variables, so closures created in a loop
capture the value from their own iteration.

### match

`match` runs the first arm whose value equals the subject:

```
match command {
    "start", "go" => start()
    "stop" => {
        save()
        stop()
    }
    else => print("unknown command")     // `_ =>` works too
}
```

Each arm is a single statement or a `{ }` block. If nothing matches and there
is no `else` arm, nothing happens. `break`, `continue` and `return` inside an
arm affect the surrounding loop or function.

## Functions

A typed function is written like in C: the return type first, then the name,
then the parameters, each with its type before its name:

```
f64 area(f64 width, f64 height = width) {
    return width * height
}

void log(string message) {
    print("log:", message)
}

area(3)       // 9.0
area(3, 4)    // 12.0
```

Functions without types start with `func`:

```
func add(a, b) {
    return a + b
}
```

- Types are optional, and can be mixed: `func f(i32 count, label) { }`.
  Arguments are converted to the parameter's type (an `int` passed to an `f64`
  parameter becomes a float) or rejected: `area("big")` is an error.
- `void` means the function returns nothing; `string?` and other nullable
  return types allow returning `null`. A function with a return type that
  ends without `return` is an error.
- A function without `return` (or with a bare `return`) returns `null`.
- Parameters with default values must come after the ones without.
- Functions can be called anywhere in the block that declares them, even
  before the declaration, so they can call each other.
- Anonymous functions are expressions: `double := func(x) { return x * 2 }`.
  They can have types too, with the return type after an arrow:
  `func(i32 x) -> i32 { return x * 2 }`.
- To store a function in a typed variable, use the type `func`:
  `func handler = print`.
- Functions are closures: they keep access to the variables around them.

```
func makeCounter() {
    count := 0
    return func() {
        count += 1
        return count
    }
}
```

### Method calls

`value.name(args)` calls `name(value, args)`, so builtins read naturally:

```
items.push(4)           // push(items, 4)
"a,b".split(",")        // split("a,b", ",")
words.join(" ")         // join(words, " ")
```

If `value` is a map with a key `name`, the function stored there is called
instead, which lets maps act as simple objects:

```
dog := {name: "Rex", speak: func() { return "Woof" }}
dog.speak()
```

## Modules

`import(path)` runs another Ascend file once and returns its top-level names
as a map. Paths are relative to the importing file, and `.asc` can be left out.
Names starting with `_` stay private.

```
// geometry.asc
f64 circle_area(f64 r) { return PI * r ** 2 }

// main.asc
geometry := import("geometry")
print(geometry.circle_area(2))
```

## Errors

Errors stop the program with a message that points at the problem:

```
script.asc:2:9: runtime error: division by zero
 2 | print(x / 0)
   |         ^
```

They can be caught with `try`/`catch`. The error variable holds the error
message (or the value given to `throw`):

```
try {
    risky()
} catch e {            // also: catch (e) { }   or just: catch { }
    print("failed:", e)
}

throw "something went wrong"
throw {code: 404, message: "not found"}   // any value can be thrown
```

`assert(condition, message)` throws if the condition is falsy, and
`exit(code)` ends the program immediately (it is not caught by `try`).

Recursion that goes too deep (for example, a missing base case) is reported
as a "stack overflow" error instead of crashing.

## Command line

```
ascend                       interactive REPL
ascend file.asc [args...]    run a program; extra arguments are in the list `args`
ascend -e 'print(1 + 2)'     run code from the command line
ascend --check file.asc      report errors without running the program
```

Exit codes: `0` success, `65` syntax or type error found before running, `70`
runtime error, or the value passed to `exit()`. A first line starting with
`#!` is ignored, so scripts can start with `#!/usr/bin/env ascend`.

In the REPL, the value of an expression is printed automatically, and
unfinished input (like an open `{`) continues on a `...` prompt.

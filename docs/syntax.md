# Ascend language guide

Ascend is a small, dynamically typed scripting language. This page covers the
whole language; the builtin functions are listed in
[standard_library.md](standard_library.md).

```
func greet(name, greeting = "Hello") {
    return greeting + ", " + name + "!"
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

## Values

| Type       | Examples                       | Notes |
|------------|--------------------------------|-------|
| `null`     | `null`                         | the absence of a value |
| `bool`     | `true`, `false`                | |
| `int`      | `42`, `-7`, `1_000_000`        | 64-bit; overflow is an error, not a silent wrap |
| `float`    | `3.14`, `2.5e-3`, `1.0`        | 64-bit IEEE double |
| `string`   | `"hi\n"`, `'it\'s'`            | escapes: `\n \t \r \0 \\ \" \' \xHH` |
| `list`     | `[1, "two", 3.0]`              | |
| `map`      | `{name: "Ada", "a key": 1}`    | insertion ordered |
| `function` | `func(x) { return x * 2 }`     | |

`type(value)` returns the type name as a string.

Lists, maps and functions are **references**: `b := a` makes `b` refer to the
same list as `a`. Use `copy(a)` for a shallow copy.

### Truthiness

`false`, `null`, `0`, `0.0`, `""`, `[]` and `{}` are falsy. Everything else is
truthy.

## Variables

```
count := 0      // declare a new variable
count = 5       // change an existing variable
count += 1      // also -=, *=, /=, %=
```

`:=` creates a variable in the current scope; `=` changes one that already
exists (assigning to an undeclared name is an error, which catches typos).

Every `{ }` block is a scope. A `:=` inside a block creates a new variable
that disappears at the end of the block, even if a variable with the same
name exists outside:

```
x := 1
if true {
    x := 2      // a new x, only inside this block
    x = 3       // changes the inner x
}
print(x)        // 1
```

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
| `+`, `-`                        | add, subtract, concatenate |
| `*`, `/`, `%`                   | multiply, divide, remainder |
| `-x`                            | negation |
| `**`                            | power (right associative, `-2 ** 2 == -4`) |
| `f(x)`, `a[i]`, `a[i:j]`, `m.k` | call, index, slice, field |

`and`, `or` and `not` always produce `true` or `false`.

`not` applies to the whole comparison after it, so `not x in items` means
`not (x in items)`.

Arithmetic details:

- `int / int` truncates toward zero (`7 / 2 == 3`). If either side is a float
  the result is a float (`7 / 2.0 == 3.5`).
- `%` has the sign of the left side (`-7 % 3 == -1`), like C and JavaScript.
- Dividing by zero is an error.
- `+` with a string on either side converts the other side to a string:
  `"total: " + 5`.
- `"ab" * 3` repeats a string; `[0] * 3` repeats a list; `[1] + [2]` joins lists.

## Strings, lists and maps

```
s := "hello"
s[0]        // "h"
s[-1]       // "o"   (negative indexes count from the end)
s[1:3]      // "el"  (slices; either bound may be left out: s[2:], s[:2])
len(s)      // 5

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
`{(key): 1}`. Keys may be strings, numbers, bools or `null`.

Reading a missing map key or an out-of-range index is an error. Use
`get(map, key, default)` when a key may be missing.

Strings are immutable sequences of bytes.

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
for ch in "text" { }
for key in person { }            // map keys
for key, value in person { }
```

`break` leaves the innermost loop and `continue` skips to its next iteration.
Each loop iteration gets fresh loop variables, so closures created in a loop
capture the value from their own iteration.

## Functions

```
func area(width, height = width) {
    return width * height
}

area(3)       // 9
area(3, 4)    // 12
```

- A function without `return` (or with a bare `return`) returns `null`.
- Parameters with default values must come after the ones without.
- Functions declared with `func name(...)` can be called anywhere in the block
  that declares them, even before the declaration, so they can call each other.
- Anonymous functions are expressions: `double := func(x) { return x * 2 }`.
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

## Errors

Runtime errors stop the program with a message that points at the problem:

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
```

Exit codes: `0` success, `65` syntax error, `70` runtime error, or the value
passed to `exit()`. A first line starting with `#!` is ignored, so scripts can
start with `#!/usr/bin/env ascend`.

In the REPL, the value of an expression is printed automatically, and
unfinished input (like an open `{`) continues on a `...` prompt.

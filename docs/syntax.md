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

`=` changes an existing variable (`count = 5`, `count += 1`, `count++`).
Assigning to a name that was never declared is an error, which catches typos.

Several variables of one type can be declared together, like in C:
`i32 low = 0, high = 10`.

`auto` declares a variable whose type is taken from its first value, like
C++: after `auto total = 0`, `total` only holds ints.

### Several at once

```
a, b := 1, 2            // declare several variables
a, b = b, a             // swap: all the values are worked out first
low, high := minmax(xs) // unpack a list (a function can `return a, b`)
x, y := point           // unpack a struct's fields, in order
```

The number of names must match the number of values.

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
| `short` `long` | other names for `i16` and `i64`, as in C |
| `f32` `f64` | 32- and 64-bit floating point numbers; `double` is another name for `f64` |
| `float` | the type of float literals like `1.5`: a 64-bit float |
| `bool` | `true`, `false` |
| `char` | one Unicode character: `'a'`, `'\n'`, `'é'` |
| `string` | text: `"hello"` |
| `list` | `[1, "two", 3.0]` |
| `list<T>` | a list whose elements are all `T`: `list<string>`, `list<list<i32>>` |
| `map` | `{name: "Ada", "a key": 1}` |
| `map<K, V>` | a map with keys of type `K` and values of type `V`: `map<string, f64>` |
| `func` | a function |
| `any` | anything (the same as writing no type) |
| `void` | only as a return type: the function returns nothing |
| `T?` | `T` or `null`, e.g. `string?` |
| a struct's name | a value made by that struct, e.g. `Point` (see [Structs](#structs)) |

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

C-style casts do the same: `(f64) total / count` converts `total` before
dividing, `(i32) 3.9` is `3`, `(char) 65` is `'A'`. A cast binds tighter than
the arithmetic operators.

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
| `a ?? b`                        | `a`, or `b` if `a` is null |
| `or`, `\|\|`                    | logical or (short-circuit) |
| `and`, `&&`                     | logical and (short-circuit) |
| `not`, `!`                      | logical not |
| `==`, `!=`                      | equality (deep for lists and maps; `1 == 1.0`) |
| `<`, `<=`, `>`, `>=`, `in`      | comparison; `x in list`, `key in map`, `"sub" in string`, `x in 1..10` |
| `a..b`, `a..=b`                 | range: the numbers (or chars) from `a` up to `b`, without or with `b` |
| `\|`                            | bitwise or |
| `^`                             | bitwise xor |
| `&`                             | bitwise and |
| `<<`, `>>`                      | shifts |
| `+`, `-`                        | add, subtract, concatenate |
| `*`, `/`, `%`                   | multiply, divide, remainder |
| `-x`, `~x`, `++x`, `--x`, `(type) x` | negation, bitwise not, increment, decrement, cast |
| `**`                            | power (right associative, `-2 ** 2 == -4`) |
| `f(x)`, `a[i]`, `a[i:j]`, `m.k`, `m?.k`, `x++`, `x--` | call, index, slice, field, optional field, increment, decrement |

Notes:

- Bitwise operators bind tighter than comparisons (unlike C), so
  `flags & 1 == 0` means `(flags & 1) == 0`.
- Shifts keep the left side's type and drop bits shifted out of it:
  `0xF0u8 << 1` is `224`. `>>` is arithmetic for signed types.
- `~x` flips the bits within the type: `~0u8` is `255`, `~0` is `-1`.
- Compound assignment works for every operator: `+= -= *= /= %= &= |= ^= <<= >>=`.
- `x++` and `x--` add or subtract 1 and give the old value; `++x` and `--x`
  give the new one. They work on variables, elements and fields
  (`items[i]++`, `p.count--`) holding numbers or chars, and overflow is an
  error like any other arithmetic.
- `and`, `or` and `not` always produce `true` or `false`. `not` applies to the
  whole comparison after it: `not x in items` means `not (x in items)`.
- `+` with a string on either side converts the other side to a string:
  `"total: " + 5`.
- `"ab" * 3` repeats a string; `[0] * 3` repeats a list; `[1] + [2]` joins lists.
- `a?.b` is `null` when `a` is null, instead of an error; `a?.f()` doesn't
  call `f` then. Combine it with `??` for a default: `user?.name ?? "guest"`.
- `0..5` is the list `[0, 1, 2, 3, 4]` and `1..=3` is `[1, 2, 3]`;
  `'a'..='e'` gives chars. In a `for` loop, `in` test, `switch` case or
  `match` pattern no list is made, so `for i in 0..1_000_000_000` is fine.

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

Raw strings keep backslashes as they are, which is handy for regular
expressions and Windows paths: `r"\d+-\d+"`, `r"C:\temp"`.

Triple quotes make a string that can span several lines (a newline right
after the opening quotes is left out). Add `r` in front to make it raw too.

```
usage := """
Usage: tool [options]
  -h   show "help"
"""
```

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

`...` spreads one list into another, or one map into another (later keys
win): `[0, ...items, 99]`, `{...defaults, size: 2}`.

### Comprehensions

A comprehension builds a list or map with a loop in one expression:

```
squares := [x * x for x in 1..=10]
evens := [x for x in numbers if x % 2 == 0]
pairs := [[a, b] for a in 1..3 for b in "xy"]     // nested loops
lengths := {word: len(word) for word in words}     // a map
doubled := {k: v * 2 for k, v in prices if v > 0}
```

The loop variables only exist inside the comprehension.

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
for i in 0..5 { }                // the same
for i in 1..=5 { }               // 1, 2, 3, 4, 5
for i in range(2, 10, 2) { }     // 2, 4, 6, 8
for item in items { }
for i, item in items { }         // index and item
for c in "text" { }              // chars
for key in person { }            // map keys
for key, value in person { }
```

Loop variables can have a type, and `:` can be used instead of `in`, like in
C++ and Java: `for (string name : names) { }`.

`break` leaves the innermost loop and `continue` skips to its next iteration.
Each loop iteration gets fresh loop variables, so closures created in a loop
capture the value from their own iteration.

### C-style loops

```
for (i32 i = 0; i < 10; i++) { }           // init; condition; step
for (i32 lo = 0, hi = n - 1; lo < hi; lo++, hi--) { }
for i := 0; i < 10; i++ { }                // the parentheses are optional
for (;;) { }                               // forever, until break
for { }                                    // the same

string? line
do {
    line = input("> ")
} while (line != null and line != "quit")  // the body runs at least once
```

Variables declared in the first part belong to the loop. `continue` still
runs the step.

### Bodies without braces

Like C, the body of an `if`, `else`, `while`, `for` or `do` can be a single
statement instead of a `{ }` block, on the same line or the next one. A `;`
alone is an empty body.

```
if (x < 0) x = -x
else if (x == 0) print("zero")
else
    print("positive")

while (i < n) i++
while (busy());
```

When the condition is in parentheses, its `)` ends it, so the body can
follow directly. (Without braces or parentheses, the condition would run on
into the body.)

### switch

`switch` works like C: execution starts at the first `case` equal to the
value (or at `default`) and continues into the cases after it until a
`break`.

```
switch (command) {
case "start":
case "go":            // either word runs the next lines
    start()
    break
case "stop", "halt":  // several values in one case
    stop()
    break
default:
    print("unknown command")
}
```

`break` inside a switch leaves the switch; `continue` goes to the loop around
it. For a version without fall-through, see `match` below.

A case can also be a range: `case 90..=100:` matches any number from 90 to
100 (and so can a `match` pattern).

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
- Like C, if a program declares a top-level function `main` and never calls
  it, `main()` runs after the rest of the file. It can take no parameters,
  the list of command line arguments (`int main(list args)`), or a count and
  the list (`int main(int argc, list argv)`). If it returns an integer, that's
  the program's exit code.
- Functions are closures: they keep access to the variables around them.
- `return a, b` returns the list `[a, b]`, which the caller can unpack:
  `low, high := minmax(xs)`.

### Lambdas

Short anonymous functions, handy with `map`, `filter` and `sort`:

```
double := x => x * 2
add := (a, b) => a + b
square := (i32 n) => n * n            // parameters can have types
greet := () => { print("hi") }        // a { block } body for statements

map(prices, p => p * 1.2)
sort(people, p => p.age)
```

### Named arguments

Arguments can be given by name after the positional ones, in any order. This
is most useful for parameters with defaults:

```
string label(string text, i32 width = 10, char fill = ' ') { ... }

label("total", fill: '.')
label(width: 4, text: "id")
Box(height: 2)                        // struct fields too
```

### Any number of arguments

A last parameter written `...name` collects the remaining arguments in a
list; with a type, each one must have that type. In a call, `...list`
passes a list's items as separate arguments.

```
i32 sum(i32 ...numbers) {
    i32 total = 0
    for (n in numbers) total += n
    return total
}

sum()               // 0
sum(1, 2, 3)        // 6
sum(...[4, 5], 6)   // 15
```

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

## Structs

A struct is a value with named, typed fields:

```
struct Point {
    f64 x
    f64 y = 0                 // a default value
}

struct Rect {
    Point origin              // fields can be structs
    f64 width, height         // several fields of one type
    string label = "rect"
}

p := Point(1, 2)              // the fields in order
q := Point(5)                 // missing fields get their default (or zero)
Point origin                  // all defaults: Point(0, 0)
print(p)                      // Point{x: 1.0, y: 2.0}
p.x = 3                       // fields keep their types: p.x = "far" is an error
type(p)                       // "Point"
```

- A struct's name is a type: `Point p = ...`, `f64 length(Point a, Point b)`,
  `Point? find(...)`. A struct value only fits its own struct type.
- Fields can't be added or removed, and reading a missing field is an error.
- Two struct values are equal when they're the same struct with equal fields.
- Structs are references, like maps; `copy(p)` makes a (shallow) copy.
- Structs can be used before their declaration, like functions.
- Arguments can be given by name: `Rect(width: 3, height: 2)`.

### Methods

Functions written inside a struct are its methods. They get the value they're
called on as `this`:

```
struct Counter {
    i32 count = 0
    string name = "counter"

    void add(i32 n = 1) { this.count += n }
    bool done() { return this.count >= 10 }
    func label() { return f"{this.name}: {this.count}" }

    // init() runs whenever a Counter is made, e.g. to check its fields.
    void init() { assert(this.count >= 0, "count can't be negative") }
}

c := Counter()
c.add(3)
print(c.label())    // counter: 3
```

Methods can have a return type (`bool done()`), `func`, or nothing in front
of their name. A function outside the struct that takes it as its first
parameter can be called the same way: `f64 area(Rect r)` can be called as
`r.area()`.

### Inheritance

A struct can extend another one, C++ style. It gets the other struct's
fields (first) and methods, and can replace methods; `super.name()` calls the
version it replaced.

```
struct Animal {
    string name
    string sound() { return "..." }
    string speak() { return f"{this.name} says {this.sound()}" }
}

struct Dog : Animal {
    string breed = "mutt"
    string sound() { return "Woof" }
    string speak() { return super.speak() + "!" }
}

d := Dog("Rex", breed: "beagle")
d.speak()                  // "Rex says Woof!"
Animal a = d               // a Dog is also an Animal
is_a(d, Animal)            // true
type(d)                    // "Dog"
```

The struct being extended must be declared first (or imported).

### Operators and special methods

Methods with these names make structs work with the language's operators and
builtins:

| Method | Used by |
|--------|---------|
| `operator+(other)`, `operator-`, `operator*`, `operator/`, `operator%`, `operator**` | `a + b` and the others (also `+=`, ...) |
| `operator==(other)` | `==`, `!=`, `in`, `index_of`, `unique`, ... |
| `operator<(other)` | `<`, `>`, `<=`, `>=`, `sort`, `min`, `max` |
| `operator[](index)` | `value[index]` |
| `to_string()` | `print`, `str`, f-strings, `+` with a string, and printing lists that contain it |
| `iter()` | `for x in value` and comprehensions: return a list (or anything else you can loop over) |
| `len()` | `len(value)` |
| `contains(item)` | `item in value` |

```
struct Vec {
    f64 x
    f64 y
    Vec operator+(Vec o) { return Vec(this.x + o.x, this.y + o.y) }
    Vec operator*(f64 k) { return Vec(this.x * k, this.y * k) }
    bool operator==(Vec o) { return this.x == o.x and this.y == o.y }
    string to_string() { return f"({this.x}, {this.y})" }
}

print(Vec(1, 2) + Vec(3, 4) * 2)    // (7.0, 10.0)
```

## Typed lists and maps

`list<T>` and `map<K, V>` are lists and maps whose elements have a type:

```
list<i32> scores = [90, 85]
scores.push(77)               // fine
scores.push("A+")             // error: this is a list<i32>, so it can't hold a string
map<string, f64> prices = {tea: 2.5}
prices["cake"] = 4            // stored as 4.0
list<list<i32>> grid = [[1, 2], [3, 4]]
f64 total(list<f64> values) { return sum(values) }
```

When a list or map is stored in a typed variable (or passed to a typed
parameter), its elements are checked and converted, and the list remembers
its element type: from then on `push`, `insert`, `extend` and element
assignments are checked too, through any variable that refers to it. A list
can't later be used as a different type (and `copy()` keeps the type); build
a new list instead: `list<f64> as_floats = [f64(s) for s in scores]`.

## Enums

```
enum Color { Red, Green, Blue = 10, Purple }

Color.Green     // 1
Color.Purple    // 11
```

An enum is a constant map from names to ints. Each member is one more than
the one before it, starting at 0, unless it's given a value. Its members
can't be changed. Members can be separated by commas or newlines.

## Modules

`import` runs another Ascend file once and stores its top-level names in a
map. Paths are relative to the importing file. Names starting with `_` stay
private.

```
// geometry.asc
f64 circle_area(f64 r) { return PI * r ** 2 }

// main.asc
import geometry
print(geometry.circle_area(2))
```

```
import geometry as geo             // choose the variable name
import lib.shapes                  // the file lib/shapes.asc, stored as `shapes`
import "../tools/strings.asc" as s // any path, in quotes
import geometry, lib.shapes        // several at once
from geometry import circle_area   // just some of the names
from geometry import area as a, PI_2
from geometry import *             // every exported name
```

If there's no such file, `import` looks for an installed package with that
name, so after `apkg install colors` you can write `import colors`. See
[packages.md](packages.md).

`import(path)` also works as a function that returns the module's map:
`geometry := import("geometry")`.

## Errors

Errors stop the program with a message that points at the problem:

```
script.asc:2:9: runtime error: division by zero
 2 | print(x / 0)
   |         ^
```

When an error happens inside function calls, the message is followed by the
calls it passed through, most recent first:

```
script.asc:2:17: runtime error: can't divide by zero
 2 |     if (b == 0) throw "can't divide by zero"
   |                 ^
  in divide(), called from script.asc:5:12
  in average(), called from script.asc:9:7
```

Errors can be caught with `try`/`catch`. The error variable holds the error
message (or the value given to `throw`). A `finally` block runs however the
`try` ends: normally, with an error, or with `return`/`break`.

```
try {
    risky()
} catch e {            // also: catch (e) { }   or just: catch { }
    print("failed:", e)
}

try {
    file := open_things()
    use(file)
} finally {
    close_things()       // runs even if use() fails; the error still goes on
}

throw "something went wrong"
throw {code: 404, message: "not found"}   // any value can be thrown
```

`assert(condition, message)` throws if the condition is falsy, and
`exit(code)` ends the program immediately (it is not caught by `try`).

Recursion can go 100,000 calls deep. Deeper than that (for example, with a
missing base case) is reported as an error instead of crashing.

## Command line

```
ascend                       interactive REPL
ascend file.asc [args...]    run a program; extra arguments are in the list `args`
ascend -e 'print(1 + 2)'     run code from the command line
ascend --check file.asc      report errors without running the program
ascend test [folders...]     run every test file (named *_test.asc or test_*.asc)
```

`ascend test` runs each test file in a fresh interpreter. A file passes if it
finishes without an error and with exit code 0, so `assert(...)` (or the
`testing` package) is all a test needs:

```
// math_test.asc
import geometry
assert(geometry.circle_area(1) == PI, "area of a unit circle")
```

Exit codes: `0` success, `65` syntax or type error found before running, `70`
runtime error, or the value passed to `exit()`. A first line starting with
`#!` is ignored, so scripts can start with `#!/usr/bin/env ascend`.

In the REPL, the value of an expression is printed automatically, and
unfinished input (like an open `{`) continues on a `...` prompt.

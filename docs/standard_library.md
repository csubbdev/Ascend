# Ascend standard library

All builtins are global functions. Any of them can also be called with method
syntax, where the value before the dot becomes the first argument:
`items.push(4)` is `push(items, 4)` and `"hi".upper()` is `upper("hi")`.

Functions that return a list or string always return a **new** one unless
noted otherwise; only `push`, `pop`, `insert`, `remove`, `extend` and `clear`
change their argument.

## Input and output

| Function | Description |
|----------|-------------|
| `print(values...)` | Prints the values separated by spaces, then a newline. `print()` prints an empty line. |
| `input(prompt?)` | Prints `prompt` (if given) and reads a line from standard input. Returns `null` at end of input. |
| `format(template, values...)` | Fills each `{}` in the template with the next value; `{0}` picks a value by position and `{:spec}` formats it like an f-string: `format("{:>6.2f}", x)`. |

## Types and conversion

Each type name converts a value to that type, and fails if it doesn't fit.

| Function | Description |
|----------|-------------|
| `type(x)` | The type's name: `"int"`, `"u8"`, `"f32"`, `"char"`, `"string"`, `"list"`, `"map"`, `"func"`, `"bool"`, `"null"`, ... |
| `int(x)`, `i8(x)`, `i16(x)`, `i32(x)`, `i64(x)` | Convert to a signed integer type. Floats drop their fraction; chars give their code; strings are parsed (decimal, or `0x`/`0b`/`0o`). |
| `u8(x)`, `u16(x)`, `u32(x)`, `u64(x)`, `byte(x)` | The same for unsigned types. `u8(-1)` and `u8(256)` are errors. |
| `float(x)`, `f32(x)`, `f64(x)` | Convert a number, bool or numeric string to a float type. |
| `char(x)` | A char from a character code (`char(65)` is `'A'`) or a one-character string. |
| `str(x)`, `string(x)` | `x` as a string, the way `print` shows it. |
| `repr(x)` | `x` as source-like text (strings and chars are quoted and escaped). |
| `bool(x)` | `true` if `x` is truthy. |
| `hex(n)`, `bin(n)` | An integer in hex or binary: `hex(255)` is `"0xff"`. |

## Lists

| Function | Description |
|----------|-------------|
| `len(list)` | Number of elements. |
| `range(stop)`, `range(start, stop, step?)` | A list of ints from `start` (default 0) up to but not including `stop`. In a `for` loop no list is built, so huge ranges are fine there. |
| `push(list, values...)` | Appends values to the end of the list (in place). |
| `pop(list, index?)` | Removes and returns the element at `index` (default: the last one). |
| `insert(list, index, value)` | Inserts `value` before position `index` (in place). |
| `remove(list, index)` | Removes and returns the element at `index`. |
| `extend(list, other)` | Appends all elements of `other` (in place). |
| `clear(list)` | Removes all elements (in place). |
| `contains(list, value)` | `true` if the list has an element equal to `value` (same as `value in list`). |
| `index_of(list, value)` | Position of the first element equal to `value`, or `-1`. |
| `count(list, value)` | How many elements equal `value`. |
| `slice(list, start, end?)` | Same as `list[start:end]`. |
| `reverse(list)` | The elements in reverse order. |
| `sort(list, key?)` | The elements in ascending order. With a `key` function, sorts by `key(element)`: `sort(words, len)`. Numbers, strings, chars and lists can be sorted. |
| `unique(list)` | The elements without duplicates, in their original order. |
| `flatten(list)` | Lists inside the list are spliced in: `flatten([[1, 2], 3])` is `[1, 2, 3]`. |
| `zip(a, b, ...)` | Pairs up elements: `zip([1, 2], ["a", "b"])` is `[[1, "a"], [2, "b"]]`. |
| `join(list, separator?)` | The elements converted to strings and joined: `join([1, 2], ", ")` is `"1, 2"`. |
| `map(list, f)` | A list of `f(element)` for each element. |
| `filter(list, f)` | The elements for which `f(element)` is truthy. |
| `reduce(list, f, initial?)` | Combines the elements: `reduce([1, 2, 3], func(a, b) { return a + b })` is `6`. |
| `find(list, f)` | The first element for which `f(element)` is truthy, or `null`. |
| `any(list, f?)`, `all(list, f?)` | Whether any / all elements (or `f(element)`) are truthy. |
| `copy(x)` | A shallow copy of a list or map. |

## Maps

| Function | Description |
|----------|-------------|
| `len(map)` | Number of keys. |
| `keys(map)`, `values(map)` | Lists of keys or values, in insertion order. |
| `items(map)` | A list of `[key, value]` pairs. |
| `get(map, key, default?)` | The value for `key`, or `default` (`null` if not given) when it's missing. Also works with a list or string and an index. |
| `contains(map, key)` | `true` if the key exists (same as `key in map`). |
| `remove(map, key)` | Removes the key and returns its value. |
| `merge(a, b, ...)` | A new map with the keys of all maps; later maps win. |
| `clear(map)` | Removes all keys (in place). |

## Strings and chars

Lengths and positions count characters, not bytes.

| Function | Description |
|----------|-------------|
| `len(s)` | Number of characters. |
| `split(s, separator?)` | Splits on `separator`; without one, splits on whitespace. `split(s, "")` gives the characters as strings. |
| `lines(s)` | Splits into lines (handles `\n` and `\r\n`). |
| `chars(s)` | A list of the string's chars. |
| `bytes(s)` | A list of the UTF-8 bytes as `u8` values. |
| `from_bytes(list)` | A string from a list of byte values (0-255). |
| `join(list, separator?)` | See Lists. |
| `upper(s)`, `lower(s)` | Upper/lower case (ASCII letters). Also work on a char. |
| `trim(s)` | Removes leading and trailing whitespace. |
| `replace(s, old, new)` | Replaces every occurrence of `old`. |
| `starts_with(s, prefix)`, `ends_with(s, suffix)` | Prefix/suffix tests. |
| `contains(s, part)` | `true` if `part` occurs in `s` (same as `part in s`). |
| `index_of(s, part)` | Position of the first occurrence of `part`, or `-1`. |
| `count(s, part)` | How many times `part` occurs. |
| `slice(s, start, end?)` | Same as `s[start:end]`. |
| `reverse(s)` | The string reversed. |
| `pad_left(x, width, fill?)`, `pad_right(x, width, fill?)` | `x` as a string, padded to `width` characters with spaces (or `fill`): `pad_left(7, 3, '0')` is `"007"`. |
| `is_digit(s)`, `is_alpha(s)`, `is_alnum(s)`, `is_space(s)`, `is_upper(s)`, `is_lower(s)` | `true` if every character of the string (or the char) is in that ASCII class. |
| `ord(c)` | The character code of a char or one-character string. |
| `chr(n)` | The char with character code `n` (same as `char(n)`). |

## Math

| Name | Description |
|------|-------------|
| `PI`, `E`, `INF`, `NAN` | The constants π, e, infinity and not-a-number. |
| `abs(x)` | Absolute value (keeps the type). |
| `min(a, b, ...)`, `min(list)` | Smallest value. |
| `max(a, b, ...)`, `max(list)` | Largest value. |
| `clamp(x, low, high)` | `x` limited to the range `low`..`high`. |
| `sum(list)` | Sum of a list of numbers. Small integer types are widened, so the sum of `u8` values is an `int`. |
| `sign(x)` | `-1`, `0` or `1`. |
| `gcd(a, b)` | Greatest common divisor. |
| `sqrt(x)`, `cbrt(x)` | Square and cube root. |
| `pow(a, b)` | Same as `a ** b`. |
| `exp(x)`, `log(x, base?)`, `log2(x)`, `log10(x)` | Exponential and logarithms (`log` is natural without a base). |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2(y, x)` | Trigonometry, in radians. |
| `hypot(x, y)` | `sqrt(x*x + y*y)`. |
| `floor(x)`, `ceil(x)` | Round down / up to an int. |
| `round(x)` | Round to the nearest int (halves round away from zero). |
| `round(x, digits)` | Round to `digits` decimal places (keeps the float type). |
| `is_nan(x)`, `is_inf(x)` | Tests for not-a-number and infinity. |
| `random()` | A random float from 0 (inclusive) to 1 (exclusive). |
| `randint(low, high)` | A random int from `low` to `high`, both inclusive. |
| `choice(list)` | A random element. |
| `shuffle(list)` | The elements in random order. |
| `seed(n)` | Seeds the random number generator for repeatable results. |

## Files and JSON

| Function | Description |
|----------|-------------|
| `read_file(path)` | The whole file as a string. |
| `read_lines(path)` | The file as a list of lines. |
| `write_file(path, text)` | Writes `text` (any value, converted with `str`) to the file, replacing it. |
| `append_file(path, text)` | Adds `text` to the end of the file. |
| `file_exists(path)` | `true` if the file can be opened. |
| `delete_file(path)` | Deletes the file; returns `true` if it was deleted. |
| `json_parse(text)` | Converts JSON to Ascend values (objects become maps, arrays become lists). |
| `json_stringify(value, indent?)` | Converts a value to JSON; with `indent`, pretty-prints with that many spaces. |

## Modules and program control

| Function | Description |
|----------|-------------|
| `import(path)` | Runs another Ascend file once and returns its top-level names as a map. Relative paths start from the importing file; `.asc` is optional. |
| `assert(condition, message?)` | Throws an error if `condition` is falsy. |
| `exit(code?)` | Ends the program with exit code `code` (default 0). |
| `args` | List of the extra command line arguments given after the script name. |

## Time and system

| Function | Description |
|----------|-------------|
| `time()` | Seconds since 1970-01-01 as a float. |
| `clock()` | Seconds from a steady clock; subtract two readings to time code. |
| `sleep(seconds)` | Pauses the program. |
| `date(format?)` | The local date and time, formatted with `strftime` codes (default `"%Y-%m-%d %H:%M:%S"`). |
| `env(name)` | The value of an environment variable, or `null`. |
| `system(command)` | Runs a shell command and returns its exit code. |

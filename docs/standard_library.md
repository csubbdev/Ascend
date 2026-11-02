# Ascend standard library

All builtins are global functions. Any of them can also be called with method
syntax, where the value before the dot becomes the first argument:
`items.push(4)` is `push(items, 4)` and `"hi".upper()` is `upper("hi")`.

Functions that return a list or string always return a **new** one unless
noted otherwise; only `push`, `pop`, `insert` and `remove` change their
argument.

## Input and output

| Function | Description |
|----------|-------------|
| `print(values...)` | Prints the values separated by spaces, then a newline. `print()` prints an empty line. |
| `input(prompt?)` | Prints `prompt` (if given) and reads a line from standard input. Returns `null` at end of input. |

## Types and conversion

| Function | Description |
|----------|-------------|
| `type(x)` | `"null"`, `"bool"`, `"int"`, `"float"`, `"string"`, `"list"`, `"map"` or `"function"`. |
| `str(x)` | `x` as a string, the way `print` shows it. |
| `repr(x)` | `x` as source-like text (strings are quoted and escaped). |
| `int(x)` | Converts a float (truncating), bool or numeric string to an int. |
| `float(x)` | Converts an int, bool or numeric string to a float. |
| `bool(x)` | `true` if `x` is truthy. |
| `len(x)` | Length of a string, list or map. |

## Lists

| Function | Description |
|----------|-------------|
| `range(stop)`, `range(start, stop, step?)` | A list of ints from `start` (default 0) up to but not including `stop`. In a `for` loop no list is built, so huge ranges are fine there. |
| `push(list, values...)` | Appends values to the end of the list (in place). |
| `pop(list, index?)` | Removes and returns the element at `index` (default: the last one). |
| `insert(list, index, value)` | Inserts `value` before position `index` (in place). |
| `remove(list, index)` | Removes and returns the element at `index`. |
| `contains(list, value)` | `true` if the list has an element equal to `value` (same as `value in list`). |
| `index_of(list, value)` | Position of the first element equal to `value`, or `-1`. |
| `slice(list, start, end?)` | Same as `list[start:end]`. |
| `reverse(list)` | The elements in reverse order. |
| `sort(list, key?)` | The elements in ascending order. With a `key` function, sorts by `key(element)`: `sort(words, len)`. Numbers, strings and lists can be sorted. |
| `join(list, separator?)` | The elements converted to strings and joined: `join([1, 2], ", ")` is `"1, 2"`. |
| `map(list, f)` | A list of `f(element)` for each element. |
| `filter(list, f)` | The elements for which `f(element)` is truthy. |
| `reduce(list, f, initial?)` | Combines the elements: `reduce([1, 2, 3], func(a, b) { return a + b })` is `6`. |
| `copy(x)` | A shallow copy of a list or map. |

## Maps

| Function | Description |
|----------|-------------|
| `keys(map)` | List of keys, in insertion order. |
| `values(map)` | List of values, in insertion order. |
| `get(map, key, default?)` | The value for `key`, or `default` (`null` if not given) when it's missing. Also works with a list or string and an index. |
| `contains(map, key)` | `true` if the key exists (same as `key in map`). |
| `remove(map, key)` | Removes the key and returns its value. |

## Strings

| Function | Description |
|----------|-------------|
| `split(s, separator?)` | Splits on `separator`; without one, splits on whitespace. `split(s, "")` gives the characters. |
| `join(list, separator?)` | See Lists. |
| `upper(s)`, `lower(s)` | Upper/lower case (ASCII). |
| `trim(s)` | Removes leading and trailing whitespace. |
| `replace(s, old, new)` | Replaces every occurrence of `old`. |
| `starts_with(s, prefix)`, `ends_with(s, suffix)` | Prefix/suffix tests. |
| `contains(s, part)` | `true` if `part` occurs in `s` (same as `part in s`). |
| `index_of(s, part)` | Position of the first occurrence of `part`, or `-1`. |
| `slice(s, start, end?)` | Same as `s[start:end]`. |
| `reverse(s)` | The string reversed. |
| `ord(c)` | The byte value of a one-character string. |
| `chr(n)` | The one-character string for byte value `n` (0-255). |

## Math

| Name | Description |
|------|-------------|
| `PI`, `E` | The constants π and e. |
| `abs(x)` | Absolute value. |
| `min(a, b, ...)`, `min(list)` | Smallest value. |
| `max(a, b, ...)`, `max(list)` | Largest value. |
| `sum(list)` | Sum of a list of numbers. |
| `sqrt(x)` | Square root (a float). |
| `pow(a, b)` | Same as `a ** b`. |
| `floor(x)`, `ceil(x)` | Round down / up to an int. |
| `round(x)` | Round to the nearest int (halves round away from zero). |
| `round(x, digits)` | Round to `digits` decimal places (a float). |
| `random()` | A random float from 0 (inclusive) to 1 (exclusive). |
| `randint(low, high)` | A random int from `low` to `high`, both inclusive. |
| `seed(n)` | Seeds the random number generator for repeatable results. |

## Program control

| Function | Description |
|----------|-------------|
| `assert(condition, message?)` | Throws an error if `condition` is falsy. |
| `exit(code?)` | Ends the program with exit code `code` (default 0). |
| `time()` | Seconds since 1970-01-01 as a float; useful for timing code. |
| `args` | List of the extra command line arguments given after the script name. |

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
| `printf(format, values...)` | Prints values with a C format string, without adding a newline: `printf("%-10s %5.2f\n", name, price)`. See below. |
| `sprintf(format, values...)` | The same, but returns the text instead of printing it. |

`printf` and `sprintf` understand C's conversions: `%d` `%i` `%u` (integers;
a char gives its code), `%f` `%e` `%g` (and `%F` `%E` `%G`), `%x` `%X` `%o`
and `%b` (hex, octal, binary), `%c` (a char, or a character code), `%s` (any
value, as `print` shows it) and `%%`, with the flags `-` (left-align), `+`,
space, `0` (pad with zeros) and `#` (`0x` prefix), a width and a
`.precision`, either of which can be `*` to take it from the values. Length
modifiers like `l`, `ll` and `z` are accepted and ignored. Giving more or
fewer values than the format uses is an error.

## Types and conversion

Each type name converts a value to that type, and fails if it doesn't fit.

| Function | Description |
|----------|-------------|
| `type(x)` | The type's name: `"int"`, `"u8"`, `"f32"`, `"char"`, `"string"`, `"list"`, `"map"`, `"func"`, `"bool"`, `"null"`, ..., or a struct's name like `"Point"`. |
| `int(x)`, `i8(x)`, `i16(x)`, `i32(x)`, `i64(x)`, `short(x)`, `long(x)` | Convert to a signed integer type. Floats drop their fraction; chars give their code; strings are parsed (decimal, or `0x`/`0b`/`0o`). |
| `u8(x)`, `u16(x)`, `u32(x)`, `u64(x)`, `byte(x)` | The same for unsigned types. `u8(-1)` and `u8(256)` are errors. |
| `float(x)`, `f32(x)`, `f64(x)`, `double(x)` | Convert a number, bool or numeric string to a float type. |
| `char(x)` | A char from a character code (`char(65)` is `'A'`) or a one-character string. |
| `str(x)`, `string(x)` | `x` as a string, the way `print` shows it. |
| `repr(x)` | `x` as source-like text (strings and chars are quoted and escaped). |
| `bool(x)` | `true` if `x` is truthy. |
| `hex(n)`, `bin(n)` | An integer in hex or binary: `hex(255)` is `"0xff"`. |
| `is_a(x, type)` | `true` if `x` is a value of the struct `type` (or of a struct that extends it), or if `type` is a type name string matching `type(x)`: `is_a(d, Animal)`, `is_a(5, "int")`. |

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
| `flat_map(list, f)` | Calls `f` on each element and joins the lists it returns: `flat_map([1, 2], x => [x, x])` is `[1, 1, 2, 2]`. |
| `group_by(list, f)` | A map from each `f(element)` to the list of elements that gave it: `group_by(words, w => len(w))`. |
| `count_by(list, f)` | A map from each `f(element)` to how many elements gave it. |
| `chunk(list, size)` | The list cut into lists of `size` elements (the last may be shorter). |
| `min_by(list, f)`, `max_by(list, f)` | The element with the smallest / largest `f(element)`: `max_by(people, p => p.age)`. |
| `copy(x)` | A shallow copy of a list, map or struct. |

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
| `trim_start(s)`, `trim_end(s)` | Removes whitespace from one end only. |
| `capitalize(s)` | Upper-cases the first letter: `"hello world"` → `"Hello world"`. |
| `title(s)` | Upper-cases the first letter of every word: `"hello world"` → `"Hello World"`. |
| `last_index_of(s, part)` | Position of the last occurrence of `part` in a string (or of a value in a list), or `-1`. |
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
| `mean(list)`, `median(list)` | The average / middle value of a list of numbers, as a float. |
| `sign(x)` | `-1`, `0` or `1`. |
| `gcd(a, b)`, `lcm(a, b)` | Greatest common divisor and least common multiple. |
| `factorial(n)` | `n!` for `n` from 0 to 20. |
| `trunc(x)` | Drops the fraction, giving an int (toward zero: `trunc(-3.7)` is `-3`). |
| `degrees(x)`, `radians(x)` | Converts radians to degrees and back. |
| `union(a, b)`, `intersect(a, b)`, `difference(a, b)` | Set operations on lists: the results have no duplicates and keep the order of `a`. |
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

## Files, folders and JSON

| Function | Description |
|----------|-------------|
| `read_file(path)` | The whole file as a string. |
| `read_lines(path)` | The file as a list of lines. |
| `write_file(path, text)` | Writes `text` (any value, converted with `str`) to the file, replacing it. |
| `append_file(path, text)` | Adds `text` to the end of the file. |
| `file_exists(path)` | `true` if the file can be opened. |
| `delete_file(path)` | Deletes the file; returns `true` if it was deleted. |
| `is_dir(path)` | `true` if `path` is a folder. |
| `make_dir(path)` | Creates a folder (and the folders above it if needed). Returns `true` if it was created, `false` if it already existed. |
| `list_dir(path?)` | The names of the files and folders in a folder (default: the current one), sorted. |
| `walk_dir(path?)` | Every file below a folder (default: the current one), as sorted paths relative to it. |
| `remove_dir(path)` | Deletes a folder and everything in it. Returns `false` if there was no such folder. |
| `copy_file(from, to)`, `move_file(from, to)` | Copies or moves (renames) a file, replacing `to` if it exists. |
| `file_size(path)` | The size of a file in bytes. |
| `path_join(parts...)` | Joins path parts with `/`: `path_join("data", "2024", "log.txt")`. |
| `basename(path)`, `dirname(path)`, `extension(path)` | The last part of a path, everything before it, and its extension (`".txt"`). |
| `abs_path(path)` | The full path. |
| `cwd()`, `chdir(path)` | The current folder, and changing it. |
| `temp_dir()` | The folder for temporary files. |
| `csv_parse(text, separator?)` | Parses CSV into a list of rows, each a list of strings. Handles quoted fields with commas, quotes (`""`) and newlines. |
| `csv_stringify(rows, separator?)` | The opposite: rows (lists of values) as CSV text, quoting fields where needed. |
| `json_parse(text)` | Converts JSON to Ascend values (objects become maps, arrays become lists). |
| `json_stringify(value, indent?)` | Converts a value to JSON; with `indent`, pretty-prints with that many spaces. |

## Regular expressions

Patterns use JavaScript (ECMAScript) syntax. Raw strings keep backslashes as
written, so they're the easiest way to write patterns: `r"\d+"` (the same as
`"\\d+"`).

| Function | Description |
|----------|-------------|
| `regex_match(text, pattern)` | `true` if the whole text matches the pattern. |
| `regex_search(text, pattern)` | The first match as a list: the matched text, then each group (`null` for groups that didn't match). `null` if there's no match. |
| `regex_find_all(text, pattern)` | A list of every matching piece of text. |
| `regex_replace(text, pattern, replacement)` | Replaces every match. In the replacement, `$1` is the first group, `$&` the whole match. |
| `regex_split(text, pattern)` | Splits the text wherever the pattern matches. |

## Modules and program control

| Function | Description |
|----------|-------------|
| `import(path)` | Runs another Ascend file once and returns its top-level names as a map. Relative paths start from the importing file; `.asc` is optional. If there's no such file, imports the installed package with that name (see [packages.md](packages.md)). The `import name` statement does the same and stores the map in a variable (see [syntax.md](syntax.md#modules)). |
| `assert(condition, message?)` | Throws an error if `condition` is falsy. |
| `exit(code?)` | Ends the program with exit code `code` (default 0). |
| `args` | List of the extra command line arguments given after the script name. |

## Running programs and HTTP

| Function | Description |
|----------|-------------|
| `run(command, input?)` | Runs a program and waits for it. `command` is a list (`["git", "status"]`, run directly) or a string (run by the system shell, so pipes work: `"ls | wc -l"`). `input` is sent to its standard input. Returns `{code, stdout, stderr, ok}`, where `ok` is `code == 0`. |
| `system(command)` | Runs a shell command with its output going straight to the terminal; returns the exit code. |
| `http_get(url, headers?)` | Downloads a URL. Returns `{status, ok, body}` (`ok` for statuses 200-299). `headers` is a map: `{"Authorization": "Bearer ..."}`. |
| `http_post(url, body, headers?)` | Sends `body`: a string as is, or a map or list as JSON (with a JSON content type). Returns the same as `http_get`. |
| `http_request(method, url, body?, headers?)` | Any other method: `http_request("DELETE", url)`. |

The HTTP functions use the `curl` program (installed on macOS, Windows 10
and later, and most Linux systems). A connection problem is an error; an
HTTP error status like 404 isn't, so check `ok` or `status`.

```
response := http_get("https://api.github.com/repos/csubbdev/Ascend")
if (response.ok) print(json_parse(response.body).description)
```

## Encodings and ids

| Function | Description |
|----------|-------------|
| `base64_encode(text)`, `base64_decode(text)` | Base64. |
| `sha256(text)` | The SHA-256 hash, as 64 hex digits. |
| `uuid()` | A random UUID, like `"3f2a8c1e-5b7d-4e9a-8c1f-2d6b9e0a4c3b"`. |

## Time and system

| Function | Description |
|----------|-------------|
| `now()` | The local date and time as a map: `year`, `month`, `day`, `hour`, `minute`, `second`, `weekday` (0 is Sunday), `yearday` and `timestamp`. |
| `format_time(seconds, format?)` | A timestamp (like `time()`) as local time, formatted with `strftime` codes (default `"%Y-%m-%d %H:%M:%S"`). |
| `time()` | Seconds since 1970-01-01 as a float. |
| `clock()` | Seconds from a steady clock; subtract two readings to time code. |
| `sleep(seconds)` | Pauses the program. |
| `date(format?)` | The local date and time, formatted with `strftime` codes (default `"%Y-%m-%d %H:%M:%S"`). |
| `env(name)` | The value of an environment variable, or `null`. |
| `set_env(name, value)` | Sets an environment variable (for this program and the programs it runs). |
| `platform()` | `"linux"`, `"macos"` or `"windows"`. |

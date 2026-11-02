# strings

Text tools that go beyond Ascend's builtins.

```
apkg install strings
```

```
strings := import("strings")

print(strings.snake_case("parseHTTPResponse"))   // parse_http_response
print(strings.title("the quick brown fox"))      // The Quick Brown Fox
print(strings.fill(long_text, 60))               // wrapped to 60 columns
print(strings.closest("prnt", ["print", "input"]))  // print
```

| Function | What it does |
|----------|--------------|
| `capitalize(s)`, `title(s)` | Upper-cases the first letter / the first letter of every word. |
| `words(s)` | The words in a name or sentence, in lower case (`"userID"` → `["user", "id"]`). |
| `snake_case`, `kebab_case`, `camel_case`, `pascal_case`, `constant_case` | Converts between naming styles. |
| `slugify(s)` | `"Hello, World!"` → `"hello-world"`. |
| `center(s, width, fill?)` | Centers `s` in `width` characters. |
| `truncate(s, width, ending?)` | Shortens `s` to `width` characters, ending with `"..."` if it was cut. |
| `wrap(text, width)`, `fill(text, width)` | Word-wraps text into a list of lines / one string. |
| `indent(text, prefix?)`, `dedent(text)` | Adds / removes indentation on every line. |
| `remove_prefix(s, prefix)`, `remove_suffix(s, suffix)` | Removes text from the start / end if it's there. |
| `is_blank(s)`, `count_words(s)`, `is_palindrome(s)` | Small checks. |
| `distance(a, b)` | Levenshtein edit distance. |
| `closest(word, candidates, max_distance?)` | The most similar candidate, or `null` — for "did you mean ...?" messages. |

# testing

Write and run unit tests for Ascend code.

```
apkg install testing
```

```
t := import("testing")
geometry := import("geometry")

t.test("area of a square", func() {
    t.equal(geometry.area(3, 3), 9)
})

t.test("negative sizes are rejected", func() {
    t.throws(func() { return geometry.area(-1, 2) }, "negative")
})

t.run()
```

```
$ ascend geometry_test.asc
  pass area of a square
  pass negative sizes are rejected

2 passed (0.00s)
```

`run()` ends the program with exit code 1 if any test failed, so test files
work in CI scripts.

| Function | What it does |
|----------|--------------|
| `test(name, body)` | Adds a test; `body` is a function with no parameters. |
| `skip(name, body)` | Adds a test that's listed but not run. |
| `run()` | Runs the tests and prints a report. |
| `equal(actual, expected, message?)` | Fails unless the values are equal (`==`). |
| `not_equal(actual, unexpected, message?)` | Fails if the values are equal. |
| `ok(condition, message?)` | Fails unless the condition is truthy. |
| `close(actual, expected, tolerance?)` | Fails unless two numbers are within `tolerance` (default `1e-9`). |
| `throws(body, contains?)` | Fails unless calling `body` throws an error (whose message contains `contains`, if given). Returns the error. |
| `fail(message?)` | Fails the current test. |

Depends on [`colors`](../colors) for its output.

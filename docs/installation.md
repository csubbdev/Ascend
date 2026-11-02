# Building and installing Ascend

## Requirements

- A C++17 compiler: GCC 8+, Clang 7+ or Visual Studio 2019+
- CMake 3.10 or newer

Ascend has no other dependencies.

## Build

```sh
cmake -S . -B build
cmake --build build
```

This produces the interpreter, `build/ascend` (`build\Release\ascend.exe`
with Visual Studio). Builds default to the optimized `Release` configuration.

## Run

```sh
./build/ascend examples/fizzbuzz.asc   # run a program
./build/ascend                          # start the interactive REPL
./build/ascend -e 'print(2 ** 10)'      # run a one-liner
```

To run `ascend` from anywhere, copy `build/ascend` into a directory on your
`PATH`, or run `cmake --install build` (add `--prefix ~/.local` to install
without administrator rights).

## Test

```sh
cmake --build build
cd build && ctest --output-on-failure
```

This runs the unit tests in `tests/` and checks that every program in
`examples/` runs without errors. You can also run the unit tests directly and
filter them by name:

```sh
./build/ascend_tests            # all tests
./build/ascend_tests parser     # only tests whose name contains "parser"
```

To skip building the tests, configure with `-DASCEND_BUILD_TESTS=OFF`.

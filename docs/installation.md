# Building and installing Ascend

## Requirements

- A C++17 compiler: GCC 8+, Clang 7+ or Visual Studio 2019+
- CMake 3.10 or newer

Ascend has no other dependencies. The package manager, `apkg`, uses
[git](https://git-scm.com) to download packages, so install git too if you
want to use packages from the internet.

## Build

```sh
cmake -S . -B build
cmake --build build
```

This produces the interpreter, `build/ascend`, and the package manager,
`build/apkg` (`build\Release\ascend.exe` and `build\Release\apkg.exe` with
Visual Studio). Builds default to the optimized `Release` configuration.

## Run

```sh
./build/ascend examples/fizzbuzz.asc   # run a program
./build/ascend                          # start the interactive REPL
./build/ascend -e 'print(2 ** 10)'      # run a one-liner
./build/ascend --check my_program.asc   # find errors without running
```

To run `ascend` and `apkg` from anywhere, copy them from `build/` into a
directory on your `PATH`, or run `cmake --install build` (add
`--prefix ~/.local` to install without administrator rights).

```sh
apkg install colors                     # see docs/packages.md
ascend -e 'import colors
print(colors.bold("ready"))'
```

## Test

```sh
cmake --build build
cd build && ctest --output-on-failure
```

This runs the unit tests in `tests/` (including tests that install the
packages in `packages/` with `apkg`, and from temporary git repositories when
git is available) and checks that every program in `examples/` runs without
errors. You can also run the unit tests directly and filter them by name:

```sh
./build/ascend_tests            # all tests
./build/ascend_tests parser     # only tests whose name contains "parser"
```

To skip building the tests, configure with `-DASCEND_BUILD_TESTS=OFF`.

## Continuous integration

Every push to `main` and every pull request is built and tested by GitHub
Actions (`.github/workflows/build.yml`) on Linux (GCC and Clang), macOS and
Windows (MSVC), plus a run with AddressSanitizer and UndefinedBehaviorSanitizer.
Each run also uploads ready-to-use builds of `ascend` and `apkg` for Linux,
macOS and Windows: open the run on the repository's **Actions** tab and
download them under **Artifacts**.

## Editor support

`editors/vscode` is a Visual Studio Code extension with syntax highlighting,
bracket matching and comment toggling for `.asc` files. See its
[README](../editors/vscode/README.md) to install it.

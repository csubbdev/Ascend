# Packages and apkg

`apkg` is Ascend's package manager. It's built and installed next to the
`ascend` interpreter, and works much like `pip` or `npm`:

```sh
apkg install colors        # install a package
apkg list                  # see what's installed
apkg uninstall colors      # remove it again
```

Programs use installed packages with `import`:

```
import colors
print(colors.green("it works!"))
```

`apkg` downloads packages with [git](https://git-scm.com), so git must be
installed and on your `PATH` (folders on your computer can be installed
without it).

## Commands

| Command | What it does |
|---------|--------------|
| `apkg install <package>...` | Installs packages and everything they depend on. Aliases: `add`, `i`. |
| `apkg install` | In a project, installs everything its `ascend.json` lists. |
| `apkg uninstall <package>...` | Removes packages. Aliases: `remove`, `rm`. |
| `apkg list` | Lists the installed packages. Alias: `ls`. |
| `apkg show <package>` | Shows an installed package's details: version, source, what it needs and what needs it. Alias: `info`. |
| `apkg search [text]` | Searches the registry by name and description. Without text, lists every package. |
| `apkg init [name]` | Makes the current folder a project by writing an `ascend.json` (and a `main.asc`, if there isn't one). |

Options (they can go anywhere on the command line):

| Option | |
|--------|--|
| `-g`, `--global` | Use your own packages folder even inside a project. |
| `-U`, `--upgrade` | Get the newest version of packages that are already installed. Without it, installing a package that's already there does nothing. |
| `-h`, `--help` | Show help. |
| `-v`, `--version` | Show the version. |

## Where packages come from

```sh
apkg install colors                                     # the registry, newest version
apkg install colors@1.0.0                               # the registry, a particular version
apkg install github:someone/their-package               # a GitHub repository
apkg install github:someone/their-package@v2.1          # ...at a tag or branch
apkg install github:someone/monorepo/packages/thing     # ...a folder inside it
apkg install git+https://gitlab.com/someone/pkg.git     # any git repository
apkg install git+https://example.com/repo.git@v1#sub    # ...at a tag, in a folder
apkg install git@github.com:someone/private-pkg.git     # git over SSH
apkg install ./my-library                               # a folder on this computer
```

The **registry** is a list of known packages: [`registry/index.json`](../registry/index.json)
in the Ascend repository. To use a different one, set `ASCEND_REGISTRY` to
another git repository or to a folder with the same layout.

## Projects and your own packages

**Inside a project** (a folder with an `ascend.json`, or any folder below
one), `apkg install` puts packages into the project's `ascend_packages` folder
and records them in `ascend.json`:

```sh
mkdir weather && cd weather
apkg init                  # writes ascend.json and main.asc
apkg install colors        # installs into ./ascend_packages, adds "colors" to ascend.json
```

```json
{
  "name": "weather",
  "version": "0.1.0",
  "description": "",
  "main": "main.asc",
  "dependencies": {
    "colors": "*"
  }
}
```

Commit `ascend.json` but not `ascend_packages` (`apkg init` adds it to
`.gitignore`). Anyone who checks out the project runs `apkg install` with no
package names to install everything it lists.

**Anywhere else**, packages are installed just for you, in
`~/.ascend/packages` (`%USERPROFILE%\.ascend\packages` on Windows). Use `-g`
to install there from inside a project. Set `ASCEND_HOME` to keep them
somewhere else.

## How import finds packages

`import name` first looks for a file `name.asc` next to the importing file,
as always. If there isn't one, it looks for an installed package called
`name`:

1. in `ascend_packages/name` next to the importing file, then in the
   `ascend_packages` folder of each folder above it;
2. in your own packages (`~/.ascend/packages/name`).

It runs the package's main file (`main` in its `ascend.json`). To import a
different file from a package, add its path: `import strings.strings` (or
`import("strings/strings")`).

A package that depends on another one imports it the same way, so packages
can be used together without knowing where they're installed.

## Making a package

A package is a folder with an `ascend.json`:

```json
{
  "name": "shapes",
  "version": "1.0.0",
  "description": "Areas and perimeters of common shapes",
  "main": "shapes.asc",
  "dependencies": {
    "strings": "*",
    "units": "1.2.0",
    "geometry": "github:someone/geometry@v3.0"
  }
}
```

| Field | |
|-------|--|
| `name` | Required. Lowercase letters, digits, `-` and `_`. This is the name used to `import` it. |
| `version` | Required. Use `major.minor.patch` numbers. |
| `description` | A one-line summary, shown by `apkg show` and `apkg search`. |
| `main` | The file `import name` runs. Defaults to `main.asc`. |
| `dependencies` | Other packages it needs: `"*"` for any version from the registry, a version number like `"1.2.0"`, or any source `apkg install` accepts, like `"github:user/repo@v1.0"` or `"file:../other-folder"`. |

Everything at the top level of the main file that doesn't start with `_`
becomes part of the package's map when it's imported.

To share a package, put the folder in a git repository and others can install
it with `apkg install github:you/your-package`. Tag each release `v` + its
version (`v1.0.0`), so `name@1.0.0` can find it once it's in the registry.

### Adding a package to the registry

Open a pull request that adds an entry to [`registry/index.json`](../registry/index.json):

```json
"shapes": {
  "description": "Areas and perimeters of common shapes",
  "source": "git+https://github.com/you/shapes.git"
}
```

`source` can be any git source (as above, but without a version: `apkg`
adds `@v<version>` when someone asks for a particular one), or
`registry:packages/<name>` for packages kept in the Ascend repository's
[`packages`](../packages) folder.

## Official packages

| Package | |
|---------|--|
| [`colors`](../packages/colors) | Color and style text in the terminal. |
| [`strings`](../packages/strings) | Case conversion, word wrapping, padding, edit distance and other text tools. |
| [`testing`](../packages/testing) | Write and run unit tests for Ascend code. |

## Environment variables

| Variable | |
|----------|--|
| `ASCEND_HOME` | Where your own packages (`packages/`) and downloaded data (`cache/`) are kept. Default: `~/.ascend`. |
| `ASCEND_REGISTRY` | The registry: a git repository address or a folder containing `registry/index.json`. Default: `https://github.com/csubbdev/Ascend.git`. |

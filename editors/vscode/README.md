# Ascend for Visual Studio Code

Syntax highlighting, bracket matching, comment toggling and auto-indent for
Ascend (`.asc`) files.

## Install

Copy this folder into your VS Code extensions folder and restart VS Code:

- Linux / macOS: `cp -r editors/vscode ~/.vscode/extensions/ascend-language`
- Windows (PowerShell): `Copy-Item -Recurse editors\vscode "$env:USERPROFILE\.vscode\extensions\ascend-language"`

To build a `.vsix` package you can share instead, run `npx @vscode/vsce package`
in this folder and install the result with `code --install-extension`.

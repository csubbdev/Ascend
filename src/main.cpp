#include "big_stack.hpp"
#include "interpreter.hpp"
#include "repl.hpp"
#include "runner.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printUsage(std::ostream& out) {
    out << "Usage:\n"
           "  ascend                       start the interactive REPL\n"
           "  ascend <file.asc> [args...]  run a program\n"
           "  ascend -e <code> [args...]   run code given on the command line\n"
           "  ascend --check <file.asc>    check a program for errors without running it\n"
           "  ascend test [paths...]       run the tests: files named *_test.asc or test_*.asc\n"
           "  ascend -h | --help           show this help\n"
           "  ascend -v | --version        show the version\n"
           "\n"
           "Extra arguments are available to the program as the list `args`.\n";
}

bool readFile(const std::string& path, std::string& contents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    ascend::prepareSource(contents);
    return true;
}

bool isTestFile(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    if (path.extension() != ".asc") return false;
    return name.compare(0, 5, "test_") == 0 || (name.size() > 9 && name.compare(name.size() - 9, 9, "_test.asc") == 0);
}

// ascend test [paths...]: runs every test file in the given files and
// folders (default: the current folder), each in a fresh interpreter. A test
// file fails if it ends with an error or a nonzero exit code.
int runTests(const std::vector<std::string>& paths) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    for (const auto& path : paths) {
        std::error_code error;
        if (fs::is_regular_file(path, error)) {
            files.emplace_back(path);
            continue;
        }
        if (!fs::is_directory(path, error)) {
            std::cerr << "error: '" << path << "' is not a file or folder\n";
            return 1;
        }
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, error), end;
        for (; !error && it != end; it.increment(error)) {
            std::string name = it->path().filename().string();
            bool skipped = !name.empty() && (name[0] == '.' || name == "ascend_packages" || name == "build" ||
                                             name == "node_modules");
            if (it->is_directory() && skipped) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && isTestFile(it->path())) files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cout << "No test files found (test files are named like math_test.asc or test_math.asc)\n";
        return 0;
    }

    int failed = 0;
    auto started = std::chrono::steady_clock::now();
    for (const auto& file : files) {
        std::string name = file.generic_string();
        std::string source;
        std::cout << "=== " << name << "\n";
        std::cout.flush();
        int code = 1;
        if (!readFile(file.string(), source)) {
            std::cerr << "error: can't open file '" << name << "'\n";
        } else {
            ascend::Interpreter interpreter;
            interpreter.defineGlobal("args", ascend::Value::fromList(interpreter.newList()));
            ascend::RunOptions options;
            options.filename = name;
            code = ascend::runSource(interpreter, source, options, std::cerr).exitCode;
        }
        std::cout.flush();
        std::cerr.flush();
        if (code == 0) {
            std::cout << "--- PASS " << name << "\n";
        } else {
            std::cout << "--- FAIL " << name << " (exit code " << code << ")\n";
            ++failed;
        }
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    char time[32];
    std::snprintf(time, sizeof time, "%.2f", seconds);
    std::cout << "\n" << (files.size() - static_cast<size_t>(failed)) << " of " << files.size() << " test files passed"
              << (failed > 0 ? " (" + std::to_string(failed) + " failed)" : "") << " in " << time << "s\n";
    return failed > 0 ? 1 : 0;
}

}  // namespace

static int runCommandLine(const std::vector<std::string>& args) {

    if (args.empty()) {
        ascend::Interpreter interpreter;
        interpreter.defineGlobal("args", ascend::Value::fromList(interpreter.newList()));
        return ascend::runRepl(interpreter, std::cin, std::cout, std::cerr);
    }

    const std::string& first = args[0];
    if (first == "-h" || first == "--help") {
        printUsage(std::cout);
        return 0;
    }
    if (first == "-v" || first == "--version") {
        std::cout << "Ascend " << ascend::kVersion << '\n';
        return 0;
    }

    std::string source;
    ascend::RunOptions options;
    size_t scriptArgsStart;
    if (first == "test") {
        std::vector<std::string> paths(args.begin() + 1, args.end());
        if (paths.empty()) paths.push_back(".");
        return runTests(paths);
    }
    if (first == "--check") {
        if (args.size() != 2) {
            std::cerr << "error: --check needs exactly one file\n\n";
            printUsage(std::cerr);
            return 1;
        }
        if (!readFile(args[1], source)) {
            std::cerr << "error: can't open file '" << args[1] << "'\n";
            return 1;
        }
        options.filename = args[1];
        options.checkOnly = true;
        ascend::Interpreter interpreter;
        ascend::RunResult result = ascend::runSource(interpreter, source, options, std::cerr);
        if (result.status == ascend::RunStatus::Ok) std::cout << args[1] << ": no errors found\n";
        return result.exitCode;
    }
    if (first == "-e" || first == "--eval") {
        if (args.size() < 2) {
            std::cerr << "error: " << first << " needs some code to run\n\n";
            printUsage(std::cerr);
            return 1;
        }
        source = args[1];
        options.filename = "<eval>";
        scriptArgsStart = 2;
    } else if (first.size() > 1 && first[0] == '-') {
        std::cerr << "error: unknown option '" << first << "'\n\n";
        printUsage(std::cerr);
        return 1;
    } else {
        if (!readFile(first, source)) {
            std::cerr << "error: can't open file '" << first << "'\n";
            return 1;
        }
        options.filename = first;
        scriptArgsStart = 1;
    }

    ascend::Interpreter interpreter;
    std::vector<ascend::Value> scriptArgs;
    for (size_t i = scriptArgsStart; i < args.size(); ++i) scriptArgs.push_back(ascend::Value::fromString(args[i]));
    interpreter.defineGlobal("args", ascend::Value::fromList(interpreter.newList(std::move(scriptArgs))));

    return ascend::runSource(interpreter, source, options, std::cerr).exitCode;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    // Programs run on a thread with a large stack, so they can recurse deeply.
    return ascend::runWithLargeStack([&] { return runCommandLine(args); });
}

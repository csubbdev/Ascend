#include "interpreter.hpp"
#include "repl.hpp"
#include "runner.hpp"

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
           "  ascend -h | --help           show this help\n"
           "  ascend -v | --version        show the version\n"
           "\n"
           "Extra arguments are available to the program as the list `args`.\n";
}

bool readFile(const std::string& path, std::string& contents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (contents.compare(0, 3, "\xEF\xBB\xBF") == 0) contents.erase(0, 3);  // UTF-8 byte order mark
    if (contents.compare(0, 2, "#!") == 0) contents.replace(0, 2, "//");     // allow #!/usr/bin/env ascend
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

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

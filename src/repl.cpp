#include "repl.hpp"

#include "runner.hpp"

#include <string>

namespace ascend {

namespace {

std::string trim(const std::string& text) {
    const char* space = " \t\r\n";
    size_t first = text.find_first_not_of(space);
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(space);
    return text.substr(first, last - first + 1);
}

}  // namespace

int runRepl(Interpreter& interpreter, std::istream& in, std::ostream& out, std::ostream& err) {
    interpreter.setReplMode(true);
    out << "Ascend " << kVersion << " - type 'exit' or press Ctrl-D to quit\n";

    RunOptions options;
    options.filename = "<repl>";
    options.repl = true;

    std::string buffer;
    std::string line;
    while (true) {
        out << (buffer.empty() ? ">>> " : "... ") << std::flush;
        if (!std::getline(in, line)) {
            out << '\n';
            return 0;
        }

        if (buffer.empty()) {
            std::string command = trim(line);
            if (command.empty()) continue;
            if (command == "exit" || command == "quit") return 0;
        }

        buffer += line;
        buffer += '\n';
        RunResult result = runSource(interpreter, buffer, options, err);
        if (result.status == RunStatus::Incomplete) continue;
        buffer.clear();
        if (result.status == RunStatus::Exited) return result.exitCode;
    }
}

}  // namespace ascend

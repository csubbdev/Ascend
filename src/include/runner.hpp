#ifndef ASCEND_RUNNER_HPP
#define ASCEND_RUNNER_HPP

#include "interpreter.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace ascend {

constexpr const char* kVersion = "1.0.0";

// Process exit codes (following the BSD sysexits convention).
constexpr int kExitSyntaxError = 65;
constexpr int kExitRuntimeError = 70;

enum class RunStatus {
    Ok,
    Incomplete,    // REPL only: the input ends in the middle of a statement
    SyntaxError,   // the program was rejected before running
    RuntimeError,  // the program failed while running
    Exited         // the program called exit()
};

struct RunOptions {
    std::string filename = "<input>";  // shown in error messages
    bool repl = false;
    bool checkOnly = false;  // only check for errors; don't run the program
};

struct RunResult {
    RunStatus status = RunStatus::Ok;
    int exitCode = 0;
};

// Lexes, parses, checks and runs `source`, printing any errors to `err`.
RunResult runSource(Interpreter& interpreter, const std::string& source, const RunOptions& options,
                    std::ostream& err);

// Prepares a file's text for running: drops a UTF-8 byte order mark and
// turns a first line starting with #! into a comment.
void prepareSource(std::string& text);

// The "in f(), called from file:line:column" lines printed after an uncaught
// runtime error.
std::string formatTraceback(const std::vector<TraceFrame>& frames, const std::string& mainFile);

}  // namespace ascend

#endif  // ASCEND_RUNNER_HPP

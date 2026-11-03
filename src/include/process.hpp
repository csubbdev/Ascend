#ifndef ASCEND_PROCESS_HPP
#define ASCEND_PROCESS_HPP

#include <string>
#include <vector>

namespace ascend {

// Runs a program (found on the PATH) with the given arguments and waits for
// it. No shell is involved, so arguments are passed exactly as given.
// Returns the program's exit code, or -1 if it couldn't be started.
// With `quiet`, the program's output is discarded.
int runProcess(const std::vector<std::string>& args, bool quiet = false);

// What a program printed, and how it ended.
struct CapturedOutput {
    int code = -1;
    std::string out;
    std::string err;
};

// Runs a program (no shell) with `input` as its standard input, and captures
// what it prints. Returns false if it couldn't be started.
bool captureProcess(const std::vector<std::string>& args, const std::string& input, CapturedOutput& result);

// The same for a command run by the system shell (sh, or cmd on Windows).
bool captureShell(const std::string& command, const std::string& input, CapturedOutput& result);

// Reads an environment variable ("" if it isn't set).
std::string environmentVariable(const std::string& name);

void setEnvironmentVariable(const std::string& name, const std::string& value);

}  // namespace ascend

#endif  // ASCEND_PROCESS_HPP

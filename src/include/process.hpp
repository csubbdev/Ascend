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

// Reads an environment variable ("" if it isn't set).
std::string environmentVariable(const std::string& name);

void setEnvironmentVariable(const std::string& name, const std::string& value);

}  // namespace ascend

#endif  // ASCEND_PROCESS_HPP

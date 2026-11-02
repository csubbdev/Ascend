#include "process.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace ascend {

namespace {

#if defined(_WIN32)
// Quotes one argument so the C runtime of the started program splits the
// command line back into the same arguments.
std::string quoteArgument(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
        } else {
            out.append(backslashes, '\\');
        }
        out += arg[i];
    }
    out += '"';
    return out;
}
#endif

}  // namespace

int runProcess(const std::vector<std::string>& args, bool quiet) {
    if (args.empty()) return -1;
    std::cout.flush();
    std::cerr.flush();

#if defined(_WIN32)
    std::vector<std::string> quoted;
    for (const auto& arg : args) quoted.push_back(quoteArgument(arg));
    std::vector<const char*> argv;
    for (const auto& arg : quoted) argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    // The child inherits our standard handles, so point them at NUL while it runs.
    int savedOut = -1;
    int savedErr = -1;
    int nul = -1;
    if (quiet) {
        std::fflush(stdout);
        std::fflush(stderr);
        nul = _open("NUL", _O_WRONLY);
        savedOut = _dup(1);
        savedErr = _dup(2);
        if (nul != -1) {
            _dup2(nul, 1);
            _dup2(nul, 2);
        }
    }
    intptr_t result = _spawnvp(_P_WAIT, args[0].c_str(), argv.data());
    if (quiet) {
        if (savedOut != -1) {
            _dup2(savedOut, 1);
            _close(savedOut);
        }
        if (savedErr != -1) {
            _dup2(savedErr, 2);
            _close(savedErr);
        }
        if (nul != -1) _close(nul);
    }
    return result == -1 ? -1 : static_cast<int>(result);
#else
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (quiet) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }
    pid_t pid;
    int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return -1;

    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::string environmentVariable(const std::string& name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name.c_str()) != 0 || value == nullptr) return "";
    std::string result = value;
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name.c_str());
    return value == nullptr ? "" : value;
#endif
}

void setEnvironmentVariable(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

}  // namespace ascend

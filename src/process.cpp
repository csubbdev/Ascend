#include "process.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
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

namespace {

// Three temporary files for a child's standard input, output and error.
struct TempFiles {
    std::filesystem::path in;
    std::filesystem::path out;
    std::filesystem::path err;

    explicit TempFiles(const std::string& input) {
        std::random_device device;
        std::filesystem::path dir = std::filesystem::temp_directory_path();
        std::string base = "ascend-" + std::to_string(device()) + "-" + std::to_string(device());
        in = dir / (base + ".in");
        out = dir / (base + ".out");
        err = dir / (base + ".err");
        std::ofstream(in, std::ios::binary) << input;
    }
    ~TempFiles() {
        std::error_code ignored;
        std::filesystem::remove(in, ignored);
        std::filesystem::remove(out, ignored);
        std::filesystem::remove(err, ignored);
    }

    void read(CapturedOutput& result) const {
        result.out = contents(out);
        result.err = contents(err);
    }

    static std::string contents(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
};

}  // namespace

bool captureProcess(const std::vector<std::string>& args, const std::string& input, CapturedOutput& result) {
    if (args.empty()) return false;
    std::cout.flush();
    std::cerr.flush();
    TempFiles files(input);
#if defined(_WIN32)
    std::vector<std::string> quoted;
    for (const auto& arg : args) quoted.push_back(quoteArgument(arg));
    std::vector<const char*> argv;
    for (const auto& arg : quoted) argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    std::fflush(stdout);
    std::fflush(stderr);
    int inFile = _open(files.in.string().c_str(), _O_RDONLY | _O_BINARY);
    int outFile = _open(files.out.string().c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    int errFile = _open(files.err.string().c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (inFile == -1 || outFile == -1 || errFile == -1) {
        if (inFile != -1) _close(inFile);
        if (outFile != -1) _close(outFile);
        if (errFile != -1) _close(errFile);
        return false;
    }
    int saved[3] = {_dup(0), _dup(1), _dup(2)};
    _dup2(inFile, 0);
    _dup2(outFile, 1);
    _dup2(errFile, 2);
    intptr_t code = _spawnvp(_P_WAIT, args[0].c_str(), argv.data());
    for (int fd = 0; fd < 3; ++fd) {
        if (saved[fd] != -1) {
            _dup2(saved[fd], fd);
            _close(saved[fd]);
        }
    }
    _close(inFile);
    _close(outFile);
    _close(errFile);
    if (code == -1) return false;
    result.code = static_cast<int>(code);
#else
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    std::string inName = files.in.string();
    std::string outName = files.out.string();
    std::string errName = files.err.string();
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, inName.c_str(), O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, outName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_addopen(&actions, 2, errName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t pid;
    int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return false;
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) return false;
    }
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    files.read(result);
    return true;
}

bool captureShell(const std::string& command, const std::string& input, CapturedOutput& result) {
    std::cout.flush();
    std::cerr.flush();
    TempFiles files(input);
#if defined(_WIN32)
    std::string line = "(" + command + ") <\"" + files.in.string() + "\" >\"" + files.out.string() + "\" 2>\"" +
                       files.err.string() + "\"";
    int status = std::system(line.c_str());
    if (status == -1) return false;
    result.code = status;
#else
    std::string line = "(" + command + ") <'" + files.in.string() + "' >'" + files.out.string() + "' 2>'" +
                       files.err.string() + "'";
    int status = std::system(line.c_str());
    if (status == -1) return false;
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    files.read(result);
    return true;
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

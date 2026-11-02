#ifndef ASCEND_ERROR_HPP
#define ASCEND_ERROR_HPP

#include <stdexcept>
#include <string>

namespace ascend {

// A program's source code, kept for error messages.
struct SourceFile {
    std::string name;  // shown in error messages, e.g. "script.asc" or "<repl>"
    std::string text;
};

// A problem found before the program runs (while lexing, parsing or analyzing).
struct Diagnostic {
    int line = 0;
    int column = 0;
    std::string message;
};

// An error raised while a program runs. Errors created deep inside the runtime
// (for example inside a builtin function) don't know where in the source they
// happened, so they start with line == 0 and the interpreter fills in the
// location of the nearest enclosing expression or statement.
class RuntimeError : public std::runtime_error {
public:
    explicit RuntimeError(const std::string& message, int line = 0, int column = 0)
        : std::runtime_error(message), line(line), column(column) {}

    bool hasLocation() const { return line > 0; }

    int line;
    int column;
    const SourceFile* source = nullptr;  // the file the location refers to
};

// Formats an error the way the command line tools print it:
//
//   file.asc:3:7: runtime error: undefined variable 'y'
//    3 | print(y)
//      |       ^
std::string formatDiagnostic(const std::string& source, const std::string& filename, int line,
                             int column, const std::string& kind, const std::string& message);

}  // namespace ascend

#endif  // ASCEND_ERROR_HPP

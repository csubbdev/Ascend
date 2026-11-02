#include "runner.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "semantic_analyzer.hpp"

#include <new>
#include <utility>
#include <vector>

namespace ascend {

std::string formatTraceback(const std::vector<TraceFrame>& frames, const std::string& mainFile) {
    if (frames.empty()) return "";
    auto describe = [&](const TraceFrame& frame) {
        std::string text = "  in " + frame.function;
        if (frame.line > 0) {
            text += ", called from " + (frame.source != nullptr ? frame.source->name : mainFile) + ":" +
                    std::to_string(frame.line) + ":" + std::to_string(frame.column);
        } else if (frame.line == 0) {
            text += ", called by a builtin";
        }
        return text + "\n";
    };
    // Repeated calls (deep recursion) are shown once with a count, and very
    // long tracebacks keep their start and end.
    std::vector<std::pair<std::string, size_t>> lines;
    for (const auto& frame : frames) {
        std::string line = describe(frame);
        if (!lines.empty() && lines.back().first == line) {
            ++lines.back().second;
        } else {
            lines.emplace_back(line, 1);
        }
    }
    std::string out;
    const size_t head = 15;
    const size_t tail = 5;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines.size() > head + tail && i == head) {
            out += "  ... " + std::to_string(lines.size() - head - tail) + " more ...\n";
            i = lines.size() - tail - 1;
            continue;
        }
        out += lines[i].first;
        if (lines[i].second > 1) out += "    (and " + std::to_string(lines[i].second - 1) + " more times)\n";
    }
    return out;
}

void prepareSource(std::string& text) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // UTF-8 byte order mark
    if (text.compare(0, 2, "#!") == 0) text.replace(0, 2, "//");     // allow #!/usr/bin/env ascend
}

RunResult runSource(Interpreter& interpreter, const std::string& source, const RunOptions& options,
                    std::ostream& err) {
    auto report = [&](const std::vector<Diagnostic>& diagnostics, const char* kind) {
        interpreter.out().flush();  // keep program output and errors in order
        for (const auto& d : diagnostics) {
            err << formatDiagnostic(source, options.filename, d.line, d.column, kind, d.message);
        }
        err.flush();
    };
    // Runtime errors may come from an imported file. After the message come
    // the calls the error passed through, innermost first.
    auto reportAt = [&](const SourceFile* file, int line, int column, const std::string& message) {
        interpreter.out().flush();
        if (file != nullptr) {
            err << formatDiagnostic(file->text, file->name, line, column, "runtime error", message);
        } else {
            err << formatDiagnostic(source, options.filename, line, column, "runtime error", message);
        }
        err << formatTraceback(interpreter.traceback(), options.filename);
        err.flush();
    };

    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    if (!lexer.errors().empty()) {
        if (options.repl && lexer.endedInsideComment() && lexer.errors().size() == 1) {
            return {RunStatus::Incomplete, 0};
        }
        report(lexer.errors(), "syntax error");
        return {RunStatus::SyntaxError, kExitSyntaxError};
    }

    Parser parser(std::move(tokens));
    std::unique_ptr<Program> program = parser.parseProgram();
    if (!parser.errors().empty()) {
        if (options.repl && parser.incomplete()) return {RunStatus::Incomplete, 0};
        report(parser.errors(), "syntax error");
        return {RunStatus::SyntaxError, kExitSyntaxError};
    }

    SemanticAnalyzer analyzer;
    analyzer.checkTypeNames = !options.repl;
    std::vector<Diagnostic> problems = analyzer.analyze(*program);
    if (!problems.empty()) {
        report(problems, "error");
        return {RunStatus::SyntaxError, kExitSyntaxError};
    }
    if (options.checkOnly) return {RunStatus::Ok, 0};

    // Like C, a program with a main() function runs it after the top-level
    // code, unless the program calls it itself.
    const bool callMain = !options.repl && analyzer.shouldCallMain();
    auto file = std::make_unique<SourceFile>();
    file->name = options.filename;
    file->text = source;
    try {
        interpreter.execute(std::move(program), std::move(file));
        if (callMain) {
            int code = interpreter.runMain();
            if (code != 0) {
                interpreter.out().flush();
                return {RunStatus::Exited, code};
            }
        }
    } catch (const RuntimeError& error) {
        reportAt(error.source, error.line, error.column, error.what());
        return {RunStatus::RuntimeError, kExitRuntimeError};
    } catch (const ThrownValue& thrown) {
        reportAt(thrown.source, thrown.line, thrown.column, "uncaught error: " + toString(thrown.value));
        return {RunStatus::RuntimeError, kExitRuntimeError};
    } catch (const ExitRequest& exit) {
        interpreter.out().flush();
        return {RunStatus::Exited, exit.code};
    } catch (const std::bad_alloc&) {
        reportAt(nullptr, 0, 0, "out of memory");
        return {RunStatus::RuntimeError, kExitRuntimeError};
    }

    interpreter.out().flush();
    return {RunStatus::Ok, 0};
}

}  // namespace ascend

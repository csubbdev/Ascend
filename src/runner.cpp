#include "runner.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "semantic_analyzer.hpp"

#include <new>
#include <utility>
#include <vector>

namespace ascend {

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
    // Runtime errors may come from an imported file.
    auto reportAt = [&](const SourceFile* file, int line, int column, const std::string& message) {
        interpreter.out().flush();
        if (file != nullptr) {
            err << formatDiagnostic(file->text, file->name, line, column, "runtime error", message);
        } else {
            err << formatDiagnostic(source, options.filename, line, column, "runtime error", message);
        }
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
    std::vector<Diagnostic> problems = analyzer.analyze(*program);
    if (!problems.empty()) {
        report(problems, "error");
        return {RunStatus::SyntaxError, kExitSyntaxError};
    }
    if (options.checkOnly) return {RunStatus::Ok, 0};

    auto file = std::make_unique<SourceFile>();
    file->name = options.filename;
    file->text = source;
    try {
        interpreter.execute(std::move(program), std::move(file));
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

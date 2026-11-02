#include "runner.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "semantic_analyzer.hpp"

#include <new>
#include <utility>
#include <vector>

namespace ascend {

RunResult runSource(Interpreter& interpreter, const std::string& source, const RunOptions& options,
                    std::ostream& err) {
    auto report = [&](const std::vector<Diagnostic>& diagnostics, const char* kind) {
        interpreter.out().flush();  // keep program output and errors in order
        for (const auto& d : diagnostics) {
            err << formatDiagnostic(source, options.filename, d.line, d.column, kind, d.message);
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

    try {
        interpreter.execute(std::move(program));
    } catch (const RuntimeError& error) {
        report({{error.line, error.column, error.what()}}, "runtime error");
        return {RunStatus::RuntimeError, kExitRuntimeError};
    } catch (const ThrownValue& thrown) {
        report({{thrown.line, thrown.column, "uncaught error: " + toString(thrown.value)}}, "runtime error");
        return {RunStatus::RuntimeError, kExitRuntimeError};
    } catch (const ExitRequest& exit) {
        interpreter.out().flush();
        return {RunStatus::Exited, exit.code};
    } catch (const std::bad_alloc&) {
        report({{0, 0, "out of memory"}}, "runtime error");
        return {RunStatus::RuntimeError, kExitRuntimeError};
    }

    interpreter.out().flush();
    return {RunStatus::Ok, 0};
}

}  // namespace ascend

#ifndef ASCEND_SEMANTIC_ANALYZER_HPP
#define ASCEND_SEMANTIC_ANALYZER_HPP

#include "ast.hpp"
#include "error.hpp"
#include "typecheck.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ascend {

// Checks a parsed program for mistakes that are grammatically valid but can
// never work, so they are reported before anything runs:
//   - `break` / `continue` outside a loop
//   - `return` outside a function, or a return that doesn't match the
//     function's declared return type
//   - a function with two parameters of the same name
//   - a literal that doesn't fit its declared type: `u8 x = 300`
class SemanticAnalyzer {
public:
    std::vector<Diagnostic> analyze(const Program& program);

private:
    void visitBlock(const Block& block);
    void visitStmt(const Stmt& stmt);
    void visitExpr(const Expr* expr);
    void visitFunction(const FunctionExpr& function);
    void report(int line, int column, std::string message);

    // Checks `expr` against `type` when its type is known without running it
    // (a literal, list, map, function or f-string). `describe` builds the
    // error message from the offending value's description.
    void checkType(const Expr* expr, const TypeSpec& type, const std::function<std::string(const CoerceFailure&)>& describe);

    int loopDepth_ = 0;
    std::vector<const FunctionExpr*> functions_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace ascend

#endif  // ASCEND_SEMANTIC_ANALYZER_HPP

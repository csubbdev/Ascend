#ifndef ASCEND_SEMANTIC_ANALYZER_HPP
#define ASCEND_SEMANTIC_ANALYZER_HPP

#include "ast.hpp"
#include "error.hpp"

#include <vector>

namespace ascend {

// Checks a parsed program for mistakes that are grammatically valid but can
// never work, so they are reported before anything runs:
//   - `break` / `continue` outside a loop
//   - `return` outside a function
//   - a function with two parameters of the same name
class SemanticAnalyzer {
public:
    std::vector<Diagnostic> analyze(const Program& program);

private:
    void visitBlock(const Block& block);
    void visitStmt(const Stmt& stmt);
    void visitExpr(const Expr* expr);
    void visitFunction(const FunctionExpr& function);
    void report(int line, int column, std::string message);

    int loopDepth_ = 0;
    int functionDepth_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace ascend

#endif  // ASCEND_SEMANTIC_ANALYZER_HPP

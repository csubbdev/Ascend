#ifndef ASCEND_SEMANTIC_ANALYZER_HPP
#define ASCEND_SEMANTIC_ANALYZER_HPP

#include "ast.hpp"
#include "error.hpp"
#include "typecheck.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace ascend {

// Checks a parsed program for mistakes that are grammatically valid but can
// never work, so they are reported before anything runs:
//   - `break` outside a loop or switch, `continue` outside a loop
//   - `return` outside a function, or a return that doesn't match the
//     function's declared return type
//   - a function with two parameters of the same name
//   - a literal that doesn't fit its declared type: `u8 x = 300`
//   - a type name that is neither builtin nor a struct the program declares
//   - a struct with two fields of the same name, an enum with repeated members
class SemanticAnalyzer {
public:
    std::vector<Diagnostic> analyze(const Program& program);

    // True if the program declares a top-level `main` function and never
    // mentions it, so the runner should call it (like C's main).
    bool shouldCallMain() const { return hasMain_ && !mentionsMain_; }

    // The REPL checks one line at a time, so a struct declared on an earlier
    // line looks unknown; it turns this off.
    bool checkTypeNames = true;

private:
    struct TypeUse {
        std::string name;
        int line;
        int column;
    };

    void visitBlock(const Block& block);
    void visitStmt(const Stmt& stmt);
    void visitExpr(const Expr* expr);
    void visitFunction(const FunctionExpr& function);
    void report(int line, int column, std::string message);
    void noteType(const TypeSpec& type, int line, int column);

    // Checks `expr` against `type` when its type is known without running it
    // (a literal, list, map, function or f-string). `describe` builds the
    // error message from the offending value's description.
    void checkType(const Expr* expr, const TypeSpec& type, const std::function<std::string(const CoerceFailure&)>& describe);

    int loopDepth_ = 0;
    int switchDepth_ = 0;
    std::vector<const FunctionExpr*> functions_;
    std::vector<TypeUse> typeUses_;     // struct type names used, checked at the end
    std::set<std::string> declared_;    // names the program declares
    bool importsEverything_ = false;    // from x import *: any name may be declared
    bool hasMain_ = false;
    bool mentionsMain_ = false;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace ascend

#endif  // ASCEND_SEMANTIC_ANALYZER_HPP

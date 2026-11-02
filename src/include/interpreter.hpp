#ifndef ASCEND_INTERPRETER_HPP
#define ASCEND_INTERPRETER_HPP

#include "ast.hpp"
#include "environment.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace ascend {

// Raised by the `throw` statement. Carries any Ascend value.
struct ThrownValue {
    Value value;
    int line;
    int column;
};

// Raised by exit(). Unwinds the whole program; `try` does not catch it.
struct ExitRequest {
    int code;
};

// A tree-walking interpreter for Ascend programs.
class Interpreter {
public:
    explicit Interpreter(std::ostream& out = std::cout, std::istream& in = std::cin);
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    // Runs a parsed (and analyzed) program in the global scope. The
    // interpreter keeps the program alive, since functions point into it.
    // Throws RuntimeError, ThrownValue or ExitRequest.
    void execute(std::unique_ptr<Program> program);

    // Calls an Ascend or builtin function. `line`/`column` locate errors.
    Value call(const Value& callee, std::vector<Value> args, int line = 0, int column = 0);

    // Operations shared with the standard library.
    Value binaryOp(TokenType op, const Value& left, const Value& right);
    Value indexGet(const Value& object, const Value& index);
    Value slice(const Value& object, const Value& start, const Value& end);
    bool contains(const Value& container, const Value& item);

    // Allocation of objects the cycle collector should know about.
    std::shared_ptr<Environment> newEnvironment(std::shared_ptr<Environment> parent);
    std::shared_ptr<ListObj> newList(std::vector<Value> items = {});
    std::shared_ptr<MapObj> newMap();

    void defineNative(const std::string& name, int minArgs, int maxArgs, NativeFn fn);
    void defineGlobal(const std::string& name, Value value);
    const std::shared_ptr<Environment>& globals() const { return globals_; }

    // In REPL mode, the value of each top-level expression statement is printed.
    void setReplMode(bool enabled) { replMode_ = enabled; }

    // Recursion is limited by the C++ stack (deep recursion becomes a
    // "stack overflow" error instead of a crash) and by this call depth.
    void setMaxCallDepth(int depth) { maxCallDepth_ = depth; }

    std::ostream& out() { return out_; }
    std::istream& in() { return in_; }
    std::mt19937_64& random() { return random_; }

    // Frees unreachable reference cycles. Runs automatically as objects are
    // allocated; returns how many objects were found to be garbage.
    size_t collectGarbage();
    size_t trackedObjectCount() const { return heap_.size(); }

private:
    enum class Flow { Normal, Break, Continue, Return };
    using EnvPtr = std::shared_ptr<Environment>;

    Flow exec(const Stmt& stmt, const EnvPtr& env);
    Flow execStmt(const Stmt& stmt, const EnvPtr& env);
    Flow execBlock(const Block& block, const EnvPtr& env);
    Flow execScoped(const Block& block, const EnvPtr& env);
    Flow execAssign(const AssignStmt& stmt, const EnvPtr& env);
    Flow execFor(const ForStmt& stmt, const EnvPtr& env);
    Flow execTry(const TryStmt& stmt, const EnvPtr& env);
    void hoistFunctions(const Block& block, const EnvPtr& env);

    Value eval(const Expr& expr, const EnvPtr& env);
    Value evalExpr(const Expr& expr, const EnvPtr& env);
    Value evalCall(const CallExpr& expr, const EnvPtr& env);
    Value evalVariable(const VariableExpr& expr, const EnvPtr& env);
    Value evalUnary(const UnaryExpr& expr, const EnvPtr& env);
    Value evalMember(const MemberExpr& expr, const EnvPtr& env);
    Value makeFunction(const FunctionExpr& expr, const EnvPtr& env, const std::string& name);
    void indexSet(const Value& object, const Value& index, Value value);
    void checkStack() const;

    template <typename T>
    std::shared_ptr<T> track(std::shared_ptr<T> object);

    std::ostream& out_;
    std::istream& in_;
    std::mt19937_64 random_;
    EnvPtr globals_;
    std::vector<std::unique_ptr<Program>> programs_;
    Value returnValue_;
    const Callable* rangeFunction_ = nullptr;
    bool replMode_ = false;
    int callDepth_ = 0;
    int maxCallDepth_;
    uintptr_t stackBase_;
    size_t stackBudget_;

    std::vector<std::weak_ptr<GcObject>> heap_;
    size_t nextCollection_;
};

}  // namespace ascend

#endif  // ASCEND_INTERPRETER_HPP

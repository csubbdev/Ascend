#ifndef ASCEND_INTERPRETER_HPP
#define ASCEND_INTERPRETER_HPP

#include "ast.hpp"
#include "environment.hpp"
#include "error.hpp"
#include "stack_limit.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ascend {

// Raised by the `throw` statement. Carries any Ascend value.
struct ThrownValue {
    Value value;
    int line;
    int column;
    const SourceFile* source;
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
    // interpreter keeps the program and its source alive, since functions
    // point into them. Throws RuntimeError, ThrownValue or ExitRequest.
    void execute(std::unique_ptr<Program> program, std::unique_ptr<SourceFile> source = nullptr);

    // Calls an Ascend or builtin function. `line`/`column` locate errors.
    // `named` holds arguments given by name: f(1, size: 2).
    Value call(const Value& callee, std::vector<Value> args, int line = 0, int column = 0,
               const std::vector<std::pair<std::string, Value>>* named = nullptr);

    // Runs another Ascend file (once) and returns its top-level names as a
    // map. Relative paths are relative to the file that is running.
    Value importModule(const std::string& path);

    // Calls the program's main() function, C style: with no arguments, with
    // the list `args`, or with (count, args), depending on its parameters.
    // Returns its result as an exit code (0 unless it returns an integer).
    int runMain();

    // Stores `value` in a map element, checking the field types of structs
    // and refusing changes to enums.
    void setElement(MapObj& map, const Value& key, Value value);

    // Operations shared with the standard library.
    Value binaryOp(TokenType op, const Value& left, const Value& right);
    Value indexGet(const Value& object, const Value& index);
    Value slice(const Value& object, const Value& start, const Value& end);
    bool contains(const Value& container, const Value& item);

    // Allocation of objects the cycle collector should know about.
    std::shared_ptr<Environment> newEnvironment(std::shared_ptr<Environment> parent);
    std::shared_ptr<ListObj> newList(std::vector<Value> items = {});
    std::shared_ptr<MapObj> newMap();

    // Builtins live in a scope above the program's globals, so a program
    // (or module) can reuse their names for its own variables.
    void defineNative(const std::string& name, int minArgs, int maxArgs, NativeFn fn);
    void defineBuiltin(const std::string& name, Value value);
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
    class FrameGuard;
    class ErrorSite;

    void runTopLevel(const Program& program, const EnvPtr& env, bool echo);
    Flow exec(const Stmt& stmt, const EnvPtr& env);
    Flow execStmt(const Stmt& stmt, const EnvPtr& env);
    Flow execBlock(const Block& block, const EnvPtr& env);
    Flow execScoped(const Block& block, const EnvPtr& env);
    Flow execDeclare(const DeclareStmt& stmt, const EnvPtr& env);
    Flow execAssign(const AssignStmt& stmt, const EnvPtr& env);
    Flow execFor(const ForStmt& stmt, const EnvPtr& env);
    Flow execCFor(const CForStmt& stmt, const EnvPtr& env);
    Flow execWhile(const WhileStmt& stmt, const EnvPtr& env);
    Flow execSwitch(const SwitchStmt& stmt, const EnvPtr& env);
    Flow execImport(const ImportStmt& stmt, const EnvPtr& env);
    Flow execEnum(const EnumStmt& stmt, const EnvPtr& env);
    Flow execMultiAssign(const MultiAssignStmt& stmt, const EnvPtr& env);
    void assignTo(const Expr& target, Value value, const EnvPtr& env);
    Flow execTry(const TryStmt& stmt, const EnvPtr& env);
    Flow execTryCatch(const TryStmt& stmt, const EnvPtr& env);
    Flow execMatch(const MatchStmt& stmt, const EnvPtr& env);
    Flow execReturn(const ReturnStmt& stmt, const EnvPtr& env);
    void hoistFunctions(const Block& block, const EnvPtr& env);

    Value eval(const Expr& expr, const EnvPtr& env);
    Value evalExpr(const Expr& expr, const EnvPtr& env);
    Value evalCall(const CallExpr& expr, const EnvPtr& env);
    Value evalVariable(const VariableExpr& expr, const EnvPtr& env);
    Value evalUnary(const UnaryExpr& expr, const EnvPtr& env);
    Value evalMember(const MemberExpr& expr, const EnvPtr& env);
    Value evalFString(const FStringExpr& expr, const EnvPtr& env);
    Value evalIncDec(const IncDecExpr& expr, const EnvPtr& env);
    Value evalComprehension(const ComprehensionExpr& expr, const EnvPtr& env);
    Value evalRange(const RangeExpr& expr, const EnvPtr& env);
    bool inRange(const Value& value, const RangeExpr& range, const EnvPtr& env);
    bool matchesPattern(const Value& subject, const Expr& pattern, const EnvPtr& env);
    Value makeStruct(const StructStmt& stmt, const EnvPtr& env);
    Value constructStruct(const std::shared_ptr<StructType>& type, std::vector<Value> args, int line, int column,
                          const std::vector<std::pair<std::string, Value>>* named = nullptr);
    Value makeFunction(const FunctionExpr& expr, const EnvPtr& env, const std::string& name);
    void indexSet(const Value& object, const Value& index, Value value);

    Value zeroValue(const TypeSpec& type, const EnvPtr& env);
    void defineName(const EnvPtr& env, const std::string& name, Value value, const TypeSpec& type = TypeSpec(),
                    bool constant = false);
    Value coerceVariable(const std::string& name, const TypeSpec& type, Value value);
    void noteErrorSite(int line, int column);
    void applyErrorSite(RuntimeError& error);
    void clearErrorSite();
    void checkStack() const;
    std::string importDirectory() const;

    template <typename T>
    std::shared_ptr<T> track(std::shared_ptr<T> object);

    std::ostream& out_;
    std::istream& in_;
    std::mt19937_64 random_;
    EnvPtr builtins_;
    EnvPtr globals_;
    std::vector<std::unique_ptr<Program>> programs_;
    std::vector<std::unique_ptr<SourceFile>> sources_;
    std::unordered_map<std::string, Value> modules_;
    std::unordered_set<std::string> loadingModules_;
    Value returnValue_;
    const Callable* rangeFunction_ = nullptr;
    const SourceFile* currentSource_ = nullptr;
    const FunctionExpr* currentFunction_ = nullptr;
    bool hasErrorSite_ = false;  // where the error being raised happened (see ErrorSite)
    int errorLine_ = 0;
    int errorColumn_ = 0;
    const SourceFile* errorSource_ = nullptr;
    bool replMode_ = false;
    int callDepth_ = 0;
    int maxCallDepth_;
    StackLimit stack_;

    std::vector<std::weak_ptr<GcObject>> heap_;
    size_t nextCollection_;
};

}  // namespace ascend

#endif  // ASCEND_INTERPRETER_HPP

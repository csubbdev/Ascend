#include "interpreter.hpp"

#include "lexer.hpp"
#include "numeric.hpp"
#include "packages.hpp"
#include "parser.hpp"
#include "runner.hpp"
#include "semantic_analyzer.hpp"
#include "stack_limit.hpp"
#include "stdlib.hpp"
#include "typecheck.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>


namespace ascend {

namespace {

constexpr int kDefaultMaxCallDepth = 100000;
constexpr size_t kMinCollectionThreshold = 10000;
constexpr size_t kMaxRepeatSize = size_t(1) << 28;

const char* operatorSymbol(TokenType op) {
    switch (op) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::StarStar: return "**";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::Ampersand: return "&";
        case TokenType::Pipe: return "|";
        case TokenType::Caret: return "^";
        case TokenType::ShiftLeft: return "<<";
        case TokenType::ShiftRight: return ">>";
        case TokenType::EqualEqual: return "==";
        case TokenType::NotEqual: return "!=";
        case TokenType::Less: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::Greater: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::In: return "in";
        default: return "?";
    }
}

bool toNumOp(TokenType op, NumOp& out) {
    switch (op) {
        case TokenType::Plus: out = NumOp::Add; return true;
        case TokenType::Minus: out = NumOp::Sub; return true;
        case TokenType::Star: out = NumOp::Mul; return true;
        case TokenType::Slash: out = NumOp::Div; return true;
        case TokenType::Percent: out = NumOp::Mod; return true;
        case TokenType::StarStar: out = NumOp::Pow; return true;
        case TokenType::Ampersand: out = NumOp::BitAnd; return true;
        case TokenType::Pipe: out = NumOp::BitOr; return true;
        case TokenType::Caret: out = NumOp::BitXor; return true;
        case TokenType::ShiftLeft: out = NumOp::Shl; return true;
        case TokenType::ShiftRight: out = NumOp::Shr; return true;
        default: return false;
    }
}

TokenType compoundToBinary(TokenType op) {
    switch (op) {
        case TokenType::PlusEqual: return TokenType::Plus;
        case TokenType::MinusEqual: return TokenType::Minus;
        case TokenType::StarEqual: return TokenType::Star;
        case TokenType::SlashEqual: return TokenType::Slash;
        case TokenType::PercentEqual: return TokenType::Percent;
        case TokenType::AmpersandEqual: return TokenType::Ampersand;
        case TokenType::PipeEqual: return TokenType::Pipe;
        case TokenType::CaretEqual: return TokenType::Caret;
        case TokenType::ShiftLeftEqual: return TokenType::ShiftLeft;
        case TokenType::ShiftRightEqual: return TokenType::ShiftRight;
        default: return op;
    }
}

int64_t repeatCount(const Value& count, size_t unitSize) {
    int64_t n = 0;
    if (!toInt64(count, n)) throw RuntimeError("the repeated result would be too large");
    if (n <= 0) return 0;
    if (unitSize != 0 && static_cast<uint64_t>(n) > kMaxRepeatSize / unitSize) {
        throw RuntimeError("the repeated result would be too large");
    }
    return n;
}

std::string repeatString(const std::string& text, const Value& count) {
    int64_t n = repeatCount(count, text.size());
    std::string result;
    result.reserve(text.size() * static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) result += text;
    return result;
}

// 'a' + 1 == 'b'
Value shiftChar(const Value& c, const Value& amount, int sign) {
    int64_t n = 0;
    if (!toInt64(amount, n) || n > 0x10FFFF || n < -0x10FFFF) {
        throw RuntimeError(toRepr(c) + (sign > 0 ? " + " : " - ") + toString(amount) + " is not a valid character code");
    }
    int64_t result = static_cast<int64_t>(c.asChar()) + sign * n;
    if (result < 0 || !isValidCodePoint(static_cast<char32_t>(result))) {
        throw RuntimeError(std::to_string(result) + " is not a valid character code");
    }
    return Value::fromChar(static_cast<char32_t>(result));
}

std::string textOf(const Value& v) { return v.isChar() ? encodeUtf8(v.asChar()) : v.asString(); }

size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// " (did you mean 'count'?)" for a misspelled name, or "".
std::string suggestName(const std::string& name, const Environment& env) {
    if (name.size() < 3) return "";  // too short for a meaningful guess
    std::vector<std::string> names;
    env.collectNames(names);
    std::sort(names.begin(), names.end());
    size_t limit = name.size() <= 4 ? 1 : 2;
    std::string best;
    size_t bestDistance = limit + 1;
    for (const auto& candidate : names) {
        size_t distance = editDistance(name, candidate);
        if (distance > 0 && distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best.empty() ? "" : " (did you mean '" + best + "'?)";
}

std::string arityMessage(const std::string& name, size_t min, long long max, size_t got) {
    auto arguments = [](long long n) { return std::to_string(n) + (n == 1 ? " argument" : " arguments"); };
    std::string expected;
    if (max < 0) {
        expected = "at least " + arguments(static_cast<long long>(min));
    } else if (static_cast<long long>(min) == max) {
        expected = arguments(max);
    } else {
        expected = std::to_string(min) + " to " + arguments(max);
    }
    std::string who = name.empty() ? "this function" : name + "()";
    return who + " expects " + expected + " but got " + std::to_string(got);
}

std::string functionLabel(const std::string& name) { return name.empty() ? "this function" : name + "()"; }

bool isAbsolutePath(const std::string& path) {
    return (!path.empty() && (path[0] == '/' || path[0] == '\\')) || (path.size() > 1 && path[1] == ':');
}

bool hasExtension(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    return dot != std::string::npos && (slash == std::string::npos || dot > slash);
}

// import("colors") or import("colors/extra") may name an installed package;
// import("./helpers") and import("/abs/path") are always files.
bool mayBePackage(const std::string& path) {
    if (path.empty() || path[0] == '.' || isAbsolutePath(path)) return false;
    return isValidPackageName(path.substr(0, path.find('/')));
}

}  // namespace

// Switches the current function and source file for the duration of a call,
// and restores them afterwards, even when an error unwinds the call.
class Interpreter::FrameGuard {
public:
    FrameGuard(Interpreter& interpreter, const SourceFile* source, const FunctionExpr* function)
        : interpreter_(interpreter),
          savedSource_(interpreter.currentSource_),
          savedFunction_(interpreter.currentFunction_) {
        interpreter_.currentSource_ = source;
        interpreter_.currentFunction_ = function;
        ++interpreter_.callDepth_;
    }
    ~FrameGuard() {
        interpreter_.currentSource_ = savedSource_;
        interpreter_.currentFunction_ = savedFunction_;
        --interpreter_.callDepth_;
    }
    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;

    const SourceFile* callerSource() const { return savedSource_; }

private:
    Interpreter& interpreter_;
    const SourceFile* savedSource_;
    const FunctionExpr* savedFunction_;
};

// Notes where a runtime error happened, as the error unwinds through the
// expressions and statements being evaluated: the innermost one is the
// location reported. Doing this in a destructor instead of catching and
// rethrowing at every level matters for deep recursion: with MSVC, each
// rethrow happens on top of the frames being unwound, so thousands of them
// overflow the stack.
class Interpreter::ErrorSite {
public:
    ErrorSite(Interpreter& interpreter, int line, int column)
        : interpreter_(interpreter), line_(line), column_(column) {}
    ~ErrorSite() {
        if (runtimeErrorPending()) interpreter_.noteErrorSite(line_, column_);
    }
    ErrorSite(const ErrorSite&) = delete;
    ErrorSite& operator=(const ErrorSite&) = delete;

private:
    Interpreter& interpreter_;
    int line_;
    int column_;
};

// ---------------------------------------------------------------------------
// Setup and memory management

Interpreter::Interpreter(std::ostream& out, std::istream& in)
    : out_(out),
      in_(in),
      maxCallDepth_(kDefaultMaxCallDepth),
      nextCollection_(kMinCollectionThreshold) {
    try {
        random_.seed(std::random_device{}());
    } catch (...) {
        random_.seed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    builtins_ = newEnvironment(nullptr);
    globals_ = newEnvironment(builtins_);
    registerStandardLibrary(*this);
    if (Variable* range = builtins_->find("range")) rangeFunction_ = &range->value.asFunction();
}

Interpreter::~Interpreter() {
    // Break every remaining reference cycle so all memory is released.
    modules_.clear();
    std::vector<std::shared_ptr<GcObject>> live;
    for (auto& weak : heap_) {
        if (auto strong = weak.lock()) live.push_back(std::move(strong));
    }
    for (auto& object : live) object->clearReferences();
}

template <typename T>
std::shared_ptr<T> Interpreter::track(std::shared_ptr<T> object) {
    if (heap_.size() >= nextCollection_) {
        collectGarbage();
        nextCollection_ = std::max(kMinCollectionThreshold, heap_.size() * 2);
    }
    heap_.emplace_back(object);
    return object;
}

std::shared_ptr<Environment> Interpreter::newEnvironment(std::shared_ptr<Environment> parent) {
    return track(std::make_shared<Environment>(std::move(parent)));
}

std::shared_ptr<ListObj> Interpreter::newList(std::vector<Value> items) {
    auto list = track(std::make_shared<ListObj>());
    list->items = std::move(items);
    return list;
}

std::shared_ptr<MapObj> Interpreter::newMap() { return track(std::make_shared<MapObj>()); }

// Reference counting frees most objects as soon as they become unused, but
// not cycles (for example a function stored in the scope it closes over).
// This finds them with the "trial deletion" algorithm also used by CPython:
//   1. For every tracked object, start from its reference count.
//   2. Subtract each reference that comes from another tracked object.
//   3. Whatever still has references left is held from outside the heap
//      (a C++ local, the global scope, ...); it and everything reachable
//      from it is alive.
//   4. Everything else is only kept alive by cycles: clear it.
size_t Interpreter::collectGarbage() {
    std::vector<std::shared_ptr<GcObject>> live;
    live.reserve(heap_.size());
    for (auto& weak : heap_) {
        if (auto strong = weak.lock()) live.push_back(std::move(strong));
    }

    std::unordered_map<GcObject*, long> outsideReferences;
    outsideReferences.reserve(live.size());
    for (auto& object : live) {
        outsideReferences[object.get()] = object.use_count() - 1;  // minus the copy in `live`
    }
    for (auto& object : live) {
        object->forEachChild([&](GcObject* child) {
            auto it = outsideReferences.find(child);
            if (it != outsideReferences.end()) --it->second;
        });
    }

    std::unordered_set<GcObject*> reachable;
    std::vector<GcObject*> pending;
    for (const auto& entry : outsideReferences) {
        if (entry.second > 0) {
            reachable.insert(entry.first);
            pending.push_back(entry.first);
        }
    }
    while (!pending.empty()) {
        GcObject* object = pending.back();
        pending.pop_back();
        object->forEachChild([&](GcObject* child) {
            if (reachable.insert(child).second) pending.push_back(child);
        });
    }

    size_t garbage = 0;
    for (auto& object : live) {
        if (reachable.count(object.get()) == 0) {
            object->clearReferences();
            ++garbage;
        }
    }
    live.clear();  // drops the last references to the garbage

    heap_.erase(std::remove_if(heap_.begin(), heap_.end(),
                               [](const std::weak_ptr<GcObject>& weak) { return weak.expired(); }),
                heap_.end());
    return garbage;
}

void Interpreter::defineNative(const std::string& name, int minArgs, int maxArgs, NativeFn fn) {
    builtins_->define(name, Value::fromFunction(std::make_shared<NativeFunction>(name, minArgs, maxArgs, std::move(fn))));
}

void Interpreter::defineBuiltin(const std::string& name, Value value) { builtins_->define(name, std::move(value)); }

void Interpreter::defineGlobal(const std::string& name, Value value) { globals_->define(name, std::move(value)); }

void Interpreter::noteErrorSite(int line, int column) {
    runtimeErrorPending() = false;
    hasErrorSite_ = true;
    errorLine_ = line;
    errorColumn_ = column;
    errorSource_ = currentSource_;
}

// Fills in where an error happened, if it doesn't say already.
void Interpreter::applyErrorSite(RuntimeError& error) {
    if (hasErrorSite_) {
        if (!error.hasLocation()) {
            error.line = errorLine_;
            error.column = errorColumn_;
        }
        if (error.source == nullptr) error.source = errorSource_;
    }
    if (error.source == nullptr) error.source = currentSource_;
    clearErrorSite();
}

void Interpreter::clearErrorSite() {
    runtimeErrorPending() = false;
    hasErrorSite_ = false;
}

void Interpreter::checkStack() const {
    if (stack_.exceeded()) {
        throw RuntimeError("stack overflow: too much recursion (is there a recursive call without a base case?)");
    }
}

// ---------------------------------------------------------------------------
// Programs and modules

void Interpreter::execute(std::unique_ptr<Program> program, std::unique_ptr<SourceFile> source) {
    if (!source) {
        source = std::make_unique<SourceFile>();
        source->name = "<input>";
    }
    sources_.push_back(std::move(source));
    programs_.push_back(std::move(program));
    const SourceFile* saved = currentSource_;
    currentSource_ = sources_.back().get();
    clearErrorSite();
    try {
        runTopLevel(*programs_.back(), globals_, replMode_);
    } catch (RuntimeError& error) {
        applyErrorSite(error);
        currentSource_ = saved;
        throw;
    } catch (...) {
        currentSource_ = saved;
        throw;
    }
    currentSource_ = saved;
}

void Interpreter::runTopLevel(const Program& program, const EnvPtr& env, bool echo) {
    hoistFunctions(program.body, env);
    for (const auto& stmt : program.body.statements) {
        if (echo && stmt->kind == StmtKind::Expression &&
            stmt->as<ExpressionStmt>().expr->kind != ExprKind::IncDec) {
            Value result = eval(*stmt->as<ExpressionStmt>().expr, env);
            if (!result.isNull()) out_ << toRepr(result) << '\n';
            continue;
        }
        exec(*stmt, env);
    }
}

// Relative imports are resolved from the directory of the running file.
std::string Interpreter::importDirectory() const {
    if (currentSource_ == nullptr || currentSource_->name.empty() || currentSource_->name[0] == '<') return "";
    size_t slash = currentSource_->name.find_last_of("/\\");
    return slash == std::string::npos ? "" : currentSource_->name.substr(0, slash + 1);
}

Value Interpreter::importModule(const std::string& path) {
    std::string resolved = isAbsolutePath(path) ? path : importDirectory() + path;
    if (!hasExtension(resolved)) resolved += ".asc";

    // Not a file next to the importing one: look for an installed package.
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(resolved, ignored) && mayBePackage(path)) {
        std::filesystem::path found;
        std::string problem;
        std::string directory = importDirectory();
        std::filesystem::path from = directory.empty() ? std::filesystem::current_path() : std::filesystem::path(directory);
        if (findPackageModule(*this, path, from, found, problem)) {
            resolved = found.string();
        } else {
            std::string name = path.substr(0, path.find('/'));
            if (!problem.empty()) throw RuntimeError("can't import \"" + path + "\": " + problem);
            throw RuntimeError("can't import \"" + path + "\": there's no file '" + resolved +
                               "' and no installed package named '" + name + "' (to install it, run: apkg install " +
                               name + ")");
        }
    }

    auto cached = modules_.find(resolved);
    if (cached != modules_.end()) return cached->second;
    if (loadingModules_.count(resolved) != 0) {
        throw RuntimeError("can't import \"" + path + "\": it imports itself (directly or through other files)");
    }

    std::string text;
    if (!readTextFile(resolved, text)) throw RuntimeError("can't import \"" + path + "\": file '" + resolved + "' not found");
    prepareSource(text);

    // Check the whole module before running any of it.
    std::string problems;
    auto collect = [&](const std::vector<Diagnostic>& diagnostics, const char* kind) {
        for (const auto& d : diagnostics) problems += formatDiagnostic(text, resolved, d.line, d.column, kind, d.message);
    };
    Lexer lexer(text);
    std::vector<Token> tokens = lexer.tokenize();
    std::unique_ptr<Program> program;
    if (!lexer.errors().empty()) {
        collect(lexer.errors(), "syntax error");
    } else {
        Parser parser(std::move(tokens));
        program = parser.parseProgram();
        if (!parser.errors().empty()) {
            collect(parser.errors(), "syntax error");
        } else {
            SemanticAnalyzer analyzer;
            collect(analyzer.analyze(*program), "error");
        }
    }
    if (!problems.empty()) {
        if (problems.back() == '\n') problems.pop_back();
        throw RuntimeError("can't import \"" + path + "\" because it has errors:\n" + problems);
    }

    auto file = std::make_unique<SourceFile>();
    file->name = resolved;
    file->text = std::move(text);
    sources_.push_back(std::move(file));
    programs_.push_back(std::move(program));
    const SourceFile* source = sources_.back().get();
    const Program& module = *programs_.back();

    auto env = newEnvironment(builtins_);
    loadingModules_.insert(resolved);
    try {
        FrameGuard frame(*this, source, nullptr);
        runTopLevel(module, env, false);
    } catch (...) {
        loadingModules_.erase(resolved);
        throw;
    }
    loadingModules_.erase(resolved);

    // Export every top-level name that doesn't start with an underscore.
    std::vector<std::string> names;
    for (const auto& entry : env->locals()) {
        if (entry.first.empty() || entry.first[0] != '_') names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());
    auto exports = newMap();
    for (const auto& name : names) exports->set(Value::fromString(name), env->findLocal(name)->value);
    Value result = Value::fromMap(exports);
    modules_[resolved] = result;
    return result;
}

// ---------------------------------------------------------------------------
// Statements

Interpreter::Flow Interpreter::exec(const Stmt& stmt, const EnvPtr& env) {
    ErrorSite site(*this, stmt.line, stmt.column);
    return execStmt(stmt, env);
}

Interpreter::Flow Interpreter::execStmt(const Stmt& stmt, const EnvPtr& env) {
    switch (stmt.kind) {
        case StmtKind::Expression:
            eval(*stmt.as<ExpressionStmt>().expr, env);
            return Flow::Normal;

        case StmtKind::Declare:
            return execDeclare(stmt.as<DeclareStmt>(), env);

        case StmtKind::Assign:
            return execAssign(stmt.as<AssignStmt>(), env);

        case StmtKind::Block:
            return execScoped(stmt.as<BlockStmt>().body, env);

        case StmtKind::If: {
            const auto& branch = stmt.as<IfStmt>();
            if (eval(*branch.condition, env).truthy()) return execScoped(branch.thenBody, env);
            if (branch.elseBranch) return exec(*branch.elseBranch, env);
            return Flow::Normal;
        }

        case StmtKind::While:
            return execWhile(stmt.as<WhileStmt>(), env);

        case StmtKind::For:
            return execFor(stmt.as<ForStmt>(), env);

        case StmtKind::CFor:
            return execCFor(stmt.as<CForStmt>(), env);

        case StmtKind::Switch:
            return execSwitch(stmt.as<SwitchStmt>(), env);

        case StmtKind::Import:
            return execImport(stmt.as<ImportStmt>(), env);

        case StmtKind::Struct:
            return Flow::Normal;  // defined when its block started, like functions

        case StmtKind::Enum:
            return execEnum(stmt.as<EnumStmt>(), env);

        case StmtKind::Group:
            for (const auto& inner : stmt.as<GroupStmt>().statements) {
                Flow flow = exec(*inner, env);
                if (flow != Flow::Normal) return flow;
            }
            return Flow::Normal;

        case StmtKind::Break:
            return Flow::Break;

        case StmtKind::Continue:
            return Flow::Continue;

        case StmtKind::Return:
            return execReturn(stmt.as<ReturnStmt>(), env);

        case StmtKind::Function:
            return Flow::Normal;  // already defined when its block started (hoisting)

        case StmtKind::Try:
            return execTry(stmt.as<TryStmt>(), env);

        case StmtKind::Throw: {
            Value value = eval(*stmt.as<ThrowStmt>().value, env);
            throw ThrownValue{std::move(value), stmt.line, stmt.column, currentSource_};
        }

        case StmtKind::Match:
            return execMatch(stmt.as<MatchStmt>(), env);
    }
    return Flow::Normal;
}

Interpreter::Flow Interpreter::execBlock(const Block& block, const EnvPtr& env) {
    hoistFunctions(block, env);
    for (const auto& stmt : block.statements) {
        Flow flow = exec(*stmt, env);
        if (flow != Flow::Normal) return flow;
    }
    return Flow::Normal;
}

// Runs a block in its own scope, unless it declares nothing (then the extra
// scope would be invisible, so it's skipped to save an allocation).
Interpreter::Flow Interpreter::execScoped(const Block& block, const EnvPtr& env) {
    if (block.declaresNames) return execBlock(block, newEnvironment(env));
    return execBlock(block, env);
}

// Functions declared with `func` can be called anywhere in their block, even
// before the declaration, so they're defined before the block runs.
void Interpreter::hoistFunctions(const Block& block, const EnvPtr& env) {
    if (!block.hasFunctions) return;
    for (const auto& stmt : block.statements) {
        if (stmt->kind == StmtKind::Function) {
            const FunctionExpr& function = *stmt->as<FunctionStmt>().function;
            env->define(function.name, makeFunction(function, env, function.name));
        } else if (stmt->kind == StmtKind::Struct) {
            const auto& declaration = stmt->as<StructStmt>();
            ErrorSite site(*this, stmt->line, stmt->column);
            defineName(env, declaration.name, makeStruct(declaration, env));
        }
    }
}

// The starting value of `type name` without an initializer. A struct starts
// with each field at its default: `Point p` is `Point()`.
Value Interpreter::zeroValue(const TypeSpec& type, const EnvPtr& env) {
    if (type.nullable) return Value();
    switch (type.kind) {
        case TypeKind::Bool: return Value::fromBool(false);
        case TypeKind::Number:
            return isFloatKind(type.num) ? Value::fromFloat(0.0, type.num) : Value::fromInt(0, type.num);
        case TypeKind::Char: return Value::fromChar(0);
        case TypeKind::String: return Value::fromString("");
        case TypeKind::List: return Value::fromList(newList());
        case TypeKind::Map: return Value::fromMap(newMap());
        case TypeKind::Struct: {
            Variable* variable = env->find(type.structName);
            if (variable == nullptr || !variable->value.isFunction() ||
                variable->value.asFunction().kind != Callable::Kind::Struct) {
                throw RuntimeError("'" + type.structName + "' isn't a struct type");
            }
            Value constructor = variable->value;
            return constructStruct(static_cast<StructType&>(constructor.asFunction()), {}, 0, 0);
        }
        default: return Value();
    }
}

// Defines a name, unless it's a constant already defined in this scope.
void Interpreter::defineName(const EnvPtr& env, const std::string& name, Value value, const TypeSpec& type,
                             bool constant) {
    Variable* existing = env->findLocal(name);
    if (existing != nullptr && existing->constant) {
        throw RuntimeError("'" + name + "' is a constant and can't be declared again");
    }
    env->define(name, std::move(value), type, constant);
}

namespace {

// The type `auto x = value` gives x.
TypeSpec typeOfValue(const Value& value) {
    TypeSpec type;
    switch (value.type()) {
        case Value::Type::Null: break;
        case Value::Type::Bool: type.kind = TypeKind::Bool; break;
        case Value::Type::Int:
        case Value::Type::Float:
            type.kind = TypeKind::Number;
            type.num = value.numKind();
            break;
        case Value::Type::Char: type.kind = TypeKind::Char; break;
        case Value::Type::String: type.kind = TypeKind::String; break;
        case Value::Type::List: type.kind = TypeKind::List; break;
        case Value::Type::Map:
            if (value.asMap().structInfo) return structType(value.asMap().structInfo->name);
            type.kind = TypeKind::Map;
            break;
        case Value::Type::Function: type.kind = TypeKind::Function; break;
    }
    return type;
}

}  // namespace

Value Interpreter::coerceVariable(const std::string& name, const TypeSpec& type, Value value) {
    if (type.isAny()) return value;
    Value converted;
    CoerceFailure failure;
    if (!coerceToType(value, type, converted, failure)) {
        throw RuntimeError(
            withHint("'" + name + "' is declared as " + type.name() + ", so it can't hold " + failure.what, failure));
    }
    return converted;
}

Interpreter::Flow Interpreter::execDeclare(const DeclareStmt& decl, const EnvPtr& env) {
    Variable* existing = env->findLocal(decl.name);
    if (existing != nullptr && existing->constant) {
        throw RuntimeError("'" + decl.name + "' is a constant and can't be declared again");
    }

    Value value;
    if (!decl.value) {
        value = zeroValue(decl.type, env);
    } else if (decl.value->kind == ExprKind::Function) {
        // `name := func(...) {...}` gives the function a name for messages.
        const auto& function = decl.value->as<FunctionExpr>();
        value = makeFunction(function, env, function.name.empty() ? decl.name : function.name);
    } else {
        value = eval(*decl.value, env);
    }
    if (decl.hasType) value = coerceVariable(decl.name, decl.type, std::move(value));
    env->define(decl.name, std::move(value), decl.inferType ? typeOfValue(value) : decl.type, decl.constant);
    return Flow::Normal;
}

Interpreter::Flow Interpreter::execAssign(const AssignStmt& stmt, const EnvPtr& env) {
    const bool compound = stmt.op != TokenType::Equal;
    const TokenType op = compoundToBinary(stmt.op);

    switch (stmt.target->kind) {
        case ExprKind::Variable: {
            const std::string& name = stmt.target->as<VariableExpr>().name;
            Variable* variable = env->find(name);
            if (variable == nullptr) {
                std::string hint = suggestName(name, *env);
                if (hint.empty()) hint = " (to create it, write '" + name + " := ...')";
                throw RuntimeError("undefined variable '" + name + "'" + hint, stmt.target->line, stmt.target->column);
            }
            if (variable->constant) {
                throw RuntimeError("can't change '" + name + "': it's a constant", stmt.target->line,
                                   stmt.target->column);
            }
            Value result;
            if (compound) {
                Value current = variable->value;
                Value operand = eval(*stmt.value, env);
                result = binaryOp(op, current, operand);
            } else {
                result = eval(*stmt.value, env);
            }
            // Evaluating the right side may have defined new variables, which
            // can move existing ones, so look the variable up again.
            variable = env->find(name);
            variable->value = coerceVariable(name, variable->type, std::move(result));
            return Flow::Normal;
        }

        case ExprKind::Index: {
            const auto& target = stmt.target->as<IndexExpr>();
            Value object = eval(*target.object, env);
            Value index = eval(*target.index, env);
            Value result;
            if (compound) {
                Value current = indexGet(object, index);
                Value operand = eval(*stmt.value, env);
                result = binaryOp(op, current, operand);
            } else {
                result = eval(*stmt.value, env);
            }
            indexSet(object, index, std::move(result));
            return Flow::Normal;
        }

        case ExprKind::Member: {
            const auto& target = stmt.target->as<MemberExpr>();
            Value object = eval(*target.object, env);
            if (!object.isMap()) {
                throw RuntimeError("can't set field '" + target.name + "' on " + aTypeName(object) +
                                       " value (only maps have fields)",
                                   target.line, target.column);
            }
            Value key = Value::fromString(target.name);
            Value result;
            if (compound) {
                Value current = indexGet(object, key);
                Value operand = eval(*stmt.value, env);
                result = binaryOp(op, current, operand);
            } else {
                result = eval(*stmt.value, env);
            }
            setElement(object.asMap(), key, std::move(result));
            return Flow::Normal;
        }

        default:
            throw RuntimeError("can't assign to this expression");
    }
}

Interpreter::Flow Interpreter::execReturn(const ReturnStmt& stmt, const EnvPtr& env) {
    Value value = stmt.value ? eval(*stmt.value, env) : Value();
    if (currentFunction_ != nullptr && !currentFunction_->returnType.isAny()) {
        const TypeSpec& type = currentFunction_->returnType;
        if (type.kind == TypeKind::Void) {
            if (!value.isNull()) {
                throw RuntimeError(functionLabel(currentFunction_->name) + " returns void, so it can't return a value");
            }
        } else {
            Value converted;
            CoerceFailure failure;
            if (!coerceToType(value, type, converted, failure)) {
                throw RuntimeError(withHint(functionLabel(currentFunction_->name) + " must return " + type.name() +
                                                ", but returned " + failure.what,
                                            failure));
            }
            value = std::move(converted);
        }
    }
    returnValue_ = std::move(value);
    return Flow::Return;
}

Interpreter::Flow Interpreter::execFor(const ForStmt& loop, const EnvPtr& env) {
    // Runs one iteration. With one loop variable it gets `item`; with two,
    // the first gets `key` (an index for lists and strings) and the second `item`.
    auto iteration = [&](Value key, Value item) -> Flow {
        auto scope = newEnvironment(env);
        if (loop.second.empty()) {
            scope->define(loop.first, coerceVariable(loop.first, loop.firstType, std::move(item)), loop.firstType);
        } else {
            scope->define(loop.first, coerceVariable(loop.first, loop.firstType, std::move(key)), loop.firstType);
            scope->define(loop.second, coerceVariable(loop.second, loop.secondType, std::move(item)), loop.secondType);
        }
        return execBlock(loop.body, scope);
    };

    // Fast path: `for i in range(...)` counts without building a list.
    if (loop.iterable->kind == ExprKind::Call && rangeFunction_ != nullptr) {
        const auto& call = loop.iterable->as<CallExpr>();
        if (call.callee->kind == ExprKind::Variable) {
            Variable* callee = env->find(call.callee->as<VariableExpr>().name);
            if (callee != nullptr && callee->value.isFunction() && &callee->value.asFunction() == rangeFunction_) {
                std::vector<Value> args;
                for (const auto& arg : call.args) args.push_back(eval(*arg, env));
                RangeSpec range;
                {
                    ErrorSite site(*this, call.line, call.column);  // a bad range() argument
                    range = makeRange(args);
                }
                for (uint64_t i = 0; i < range.count; ++i) {
                    Flow flow = iteration(Value::fromInt(static_cast<int64_t>(i)), Value::fromInt(range.at(i)));
                    if (flow == Flow::Break) break;
                    if (flow == Flow::Return) return flow;
                }
                return Flow::Normal;
            }
        }
    }

    Value iterable = eval(*loop.iterable, env);
    switch (iterable.type()) {
        case Value::Type::List: {
            // Index-based so the body may safely add or remove elements.
            const auto& items = iterable.asList().items;
            for (size_t i = 0; i < items.size(); ++i) {
                Flow flow = iteration(Value::fromInt(static_cast<int64_t>(i)), items[i]);
                if (flow == Flow::Break) break;
                if (flow == Flow::Return) return flow;
            }
            return Flow::Normal;
        }
        case Value::Type::String: {
            const std::string text = iterable.asString();
            int64_t index = 0;
            for (size_t pos = 0; pos < text.size(); ++index) {
                char32_t c = decodeUtf8(text, pos);
                Flow flow = iteration(Value::fromInt(index), Value::fromChar(c));
                if (flow == Flow::Break) break;
                if (flow == Flow::Return) return flow;
            }
            return Flow::Normal;
        }
        case Value::Type::Map: {
            // Iterate over a snapshot so the body may modify the map.
            const auto entries = iterable.asMap().entries;
            for (const auto& entry : entries) {
                Flow flow = iteration(entry.first, loop.second.empty() ? entry.first : entry.second);
                if (flow == Flow::Break) break;
                if (flow == Flow::Return) return flow;
            }
            return Flow::Normal;
        }
        default:
            throw RuntimeError("can't loop over " + aTypeName(iterable) +
                                   " value (expected a list, map, string or range)",
                               loop.iterable->line, loop.iterable->column);
    }
}

Interpreter::Flow Interpreter::execWhile(const WhileStmt& loop, const EnvPtr& env) {
    if (loop.doWhile) {
        while (true) {
            Flow flow = execScoped(loop.body, env);
            if (flow == Flow::Break) break;
            if (flow == Flow::Return) return flow;
            if (!eval(*loop.condition, env).truthy()) break;
        }
        return Flow::Normal;
    }
    while (eval(*loop.condition, env).truthy()) {
        Flow flow = execScoped(loop.body, env);
        if (flow == Flow::Break) break;
        if (flow == Flow::Return) return flow;
    }
    return Flow::Normal;
}

// for (init; condition; step) body
Interpreter::Flow Interpreter::execCFor(const CForStmt& loop, const EnvPtr& env) {
    EnvPtr scope = loop.init.empty() ? env : newEnvironment(env);
    for (const auto& init : loop.init) exec(*init, scope);
    while (true) {
        if (loop.condition && !eval(*loop.condition, scope).truthy()) break;
        Flow flow = execScoped(loop.body, scope);
        if (flow == Flow::Break) break;
        if (flow == Flow::Return) return flow;
        if (loop.copyPerIteration && scope != env) {
            // Functions made in this iteration keep the variables as they are now.
            auto next = newEnvironment(env);
            for (const auto& entry : scope->locals()) {
                next->define(entry.first, entry.second.value, entry.second.type, entry.second.constant);
            }
            scope = next;
        }
        for (const auto& step : loop.step) exec(*step, scope);
    }
    return Flow::Normal;
}

// Runs from the first case equal to the value (or `default`) to the end of
// the switch, or until `break`.
Interpreter::Flow Interpreter::execSwitch(const SwitchStmt& stmt, const EnvPtr& env) {
    Value subject = eval(*stmt.subject, env);
    const SwitchCase* chosen = nullptr;
    const SwitchCase* fallback = nullptr;
    for (const auto& entry : stmt.cases) {
        if (entry.values.empty()) {
            fallback = &entry;
            continue;
        }
        for (const auto& value : entry.values) {
            if (valuesEqual(subject, eval(*value, env))) {
                chosen = &entry;
                break;
            }
        }
        if (chosen != nullptr) break;
    }
    if (chosen == nullptr) chosen = fallback;
    if (chosen == nullptr) return Flow::Normal;

    EnvPtr scope = stmt.body.declaresNames ? newEnvironment(env) : env;
    hoistFunctions(stmt.body, scope);
    for (size_t i = chosen->start; i < stmt.body.statements.size(); ++i) {
        Flow flow = exec(*stmt.body.statements[i], scope);
        if (flow == Flow::Break) return Flow::Normal;
        if (flow != Flow::Normal) return flow;
    }
    return Flow::Normal;
}

Interpreter::Flow Interpreter::execImport(const ImportStmt& stmt, const EnvPtr& env) {
    if (!stmt.from) {
        for (const auto& item : stmt.names) defineName(env, item.binding, importModule(item.name));
        return Flow::Normal;
    }
    Value module = importModule(stmt.fromPath);
    const MapObj& exports = module.asMap();
    if (stmt.all) {
        for (const auto& entry : exports.entries) defineName(env, entry.first.asString(), entry.second);
        return Flow::Normal;
    }
    for (const auto& item : stmt.names) {
        const Value* value = exports.find(Value::fromString(item.name));
        if (value == nullptr) {
            std::string names;
            for (const auto& entry : exports.entries) names += (names.empty() ? "" : ", ") + entry.first.asString();
            throw RuntimeError("\"" + stmt.fromPath + "\" has no '" + item.name + "'" +
                               (names.empty() ? " (it exports nothing)" : " (it has: " + names + ")"));
        }
        defineName(env, item.binding, *value);
    }
    return Flow::Normal;
}

// enum Color { Red, Green = 5, Blue } defines the constant map
// Color = {Red: 0, Green: 5, Blue: 6}.
Interpreter::Flow Interpreter::execEnum(const EnumStmt& stmt, const EnvPtr& env) {
    auto members = newMap();
    int64_t next = 0;
    for (const auto& member : stmt.members) {
        Value value;
        if (member.value) {
            value = eval(*member.value, env);
            int64_t number = 0;
            if (value.isInt() && toInt64(value, number) && number < std::numeric_limits<int64_t>::max()) next = number + 1;
        } else {
            if (next == std::numeric_limits<int64_t>::max()) {
                throw RuntimeError("enum " + stmt.name + ": the value of '" + member.name + "' is too large",
                                   member.line, member.column);
            }
            value = Value::fromInt(next++);
        }
        members->set(Value::fromString(member.name), std::move(value));
    }
    members->frozenAs = "enum " + stmt.name;
    defineName(env, stmt.name, Value::fromMap(members), TypeSpec(), true);
    return Flow::Normal;
}

Interpreter::Flow Interpreter::execTry(const TryStmt& stmt, const EnvPtr& env) {
    Value caught;
    try {
        return execScoped(stmt.body, env);
    } catch (const RuntimeError& error) {
        clearErrorSite();  // handled, so where it happened no longer matters
        caught = Value::fromString(error.what());
    } catch (const ThrownValue& thrown) {
        caught = thrown.value;
    }
    auto scope = newEnvironment(env);
    if (!stmt.errorName.empty()) scope->define(stmt.errorName, std::move(caught));
    return execBlock(stmt.handler, scope);
}

// Runs the first arm with a pattern equal to the subject, or the `else` arm.
Interpreter::Flow Interpreter::execMatch(const MatchStmt& stmt, const EnvPtr& env) {
    Value subject = eval(*stmt.subject, env);
    const MatchArm* fallback = nullptr;
    for (const auto& arm : stmt.arms) {
        if (arm.patterns.empty()) {
            fallback = &arm;
            continue;
        }
        for (const auto& pattern : arm.patterns) {
            if (valuesEqual(subject, eval(*pattern, env))) return execScoped(arm.body, env);
        }
    }
    return fallback != nullptr ? execScoped(fallback->body, env) : Flow::Normal;
}

// ---------------------------------------------------------------------------
// Expressions

Value Interpreter::eval(const Expr& expr, const EnvPtr& env) {
    ErrorSite site(*this, expr.line, expr.column);
    checkStack();
    return evalExpr(expr, env);
}

Value Interpreter::evalExpr(const Expr& expr, const EnvPtr& env) {
    switch (expr.kind) {
        case ExprKind::Literal:
            return expr.as<LiteralExpr>().value;

        case ExprKind::Variable:
            return evalVariable(expr.as<VariableExpr>(), env);

        case ExprKind::Unary:
            return evalUnary(expr.as<UnaryExpr>(), env);

        case ExprKind::Binary: {
            const auto& binary = expr.as<BinaryExpr>();
            Value left = eval(*binary.left, env);
            Value right = eval(*binary.right, env);
            return binaryOp(binary.op, left, right);
        }

        case ExprKind::Logical: {
            const auto& logical = expr.as<LogicalExpr>();
            bool left = eval(*logical.left, env).truthy();
            if (logical.op == TokenType::And && !left) return Value::fromBool(false);
            if (logical.op == TokenType::Or && left) return Value::fromBool(true);
            return Value::fromBool(eval(*logical.right, env).truthy());
        }

        case ExprKind::Ternary: {
            const auto& ternary = expr.as<TernaryExpr>();
            return eval(*ternary.condition, env).truthy() ? eval(*ternary.thenExpr, env)
                                                          : eval(*ternary.elseExpr, env);
        }

        case ExprKind::Call:
            return evalCall(expr.as<CallExpr>(), env);

        case ExprKind::Index: {
            const auto& index = expr.as<IndexExpr>();
            Value object = eval(*index.object, env);
            Value key = eval(*index.index, env);
            return indexGet(object, key);
        }

        case ExprKind::Slice: {
            const auto& range = expr.as<SliceExpr>();
            Value object = eval(*range.object, env);
            Value start = range.start ? eval(*range.start, env) : Value();
            Value end = range.end ? eval(*range.end, env) : Value();
            return slice(object, start, end);
        }

        case ExprKind::Member:
            return evalMember(expr.as<MemberExpr>(), env);

        case ExprKind::List: {
            const auto& literal = expr.as<ListExpr>();
            auto list = newList();
            list->items.reserve(literal.elements.size());
            for (const auto& element : literal.elements) list->items.push_back(eval(*element, env));
            return Value::fromList(list);
        }

        case ExprKind::Map: {
            const auto& literal = expr.as<MapExpr>();
            auto map = newMap();
            for (const auto& entry : literal.entries) {
                Value key = eval(*entry.first, env);
                Value value = eval(*entry.second, env);
                ErrorSite site(*this, entry.first->line, entry.first->column);  // e.g. a list used as a key
                map->set(key, std::move(value));
            }
            return Value::fromMap(map);
        }

        case ExprKind::Function: {
            const auto& function = expr.as<FunctionExpr>();
            return makeFunction(function, env, function.name);
        }

        case ExprKind::FString:
            return evalFString(expr.as<FStringExpr>(), env);

        case ExprKind::IncDec:
            return evalIncDec(expr.as<IncDecExpr>(), env);
    }
    return Value();
}

// x++ and friends: like x += 1, but an expression whose value is the old
// value (x++) or the new one (++x).
Value Interpreter::evalIncDec(const IncDecExpr& expr, const EnvPtr& env) {
    const char* symbol = expr.increment ? "++" : "--";
    auto step = [&](const Value& current) {
        if (!current.isNumber() && !current.isChar()) {
            throw RuntimeError(std::string("can't use '") + symbol + "' on " + aTypeName(current) +
                               " value (it needs a number or a char)");
        }
        return binaryOp(expr.increment ? TokenType::Plus : TokenType::Minus, current, Value::fromInt(1));
    };

    const Expr& target = *expr.target;
    switch (target.kind) {
        case ExprKind::Variable: {
            const std::string& name = target.as<VariableExpr>().name;
            Variable* variable = env->find(name);
            if (variable == nullptr) {
                throw RuntimeError("undefined variable '" + name + "'" + suggestName(name, *env), target.line,
                                   target.column);
            }
            if (variable->constant) throw RuntimeError("can't change '" + name + "': it's a constant");
            Value old = variable->value;
            variable->value = coerceVariable(name, variable->type, step(old));
            return expr.prefix ? variable->value : old;
        }
        case ExprKind::Index: {
            const auto& index = target.as<IndexExpr>();
            Value object = eval(*index.object, env);
            Value key = eval(*index.index, env);
            Value old = indexGet(object, key);
            indexSet(object, key, step(old));
            return expr.prefix ? indexGet(object, key) : old;
        }
        case ExprKind::Member: {
            const auto& member = target.as<MemberExpr>();
            Value object = eval(*member.object, env);
            if (!object.isMap()) {
                throw RuntimeError("can't change field '" + member.name + "' of " + aTypeName(object) + " value");
            }
            Value key = Value::fromString(member.name);
            Value old = indexGet(object, key);
            setElement(object.asMap(), key, step(old));
            return expr.prefix ? indexGet(object, key) : old;
        }
        default:
            throw RuntimeError(std::string("'") + symbol + "' needs a variable, element or field");
    }
}

Value Interpreter::evalVariable(const VariableExpr& expr, const EnvPtr& env) {
    Variable* variable = env->find(expr.name);
    if (variable == nullptr) {
        throw RuntimeError("undefined variable '" + expr.name + "'" + suggestName(expr.name, *env), expr.line,
                           expr.column);
    }
    return variable->value;
}

Value Interpreter::evalUnary(const UnaryExpr& expr, const EnvPtr& env) {
    Value operand = eval(*expr.operand, env);
    switch (expr.op) {
        case TokenType::Not:
            return Value::fromBool(!operand.truthy());
        case TokenType::Tilde:
            if (!operand.isInt()) throw RuntimeError("'~' needs an integer, not " + aTypeName(operand));
            return bitwiseNot(operand);
        default:
            if (!operand.isNumber()) throw RuntimeError("can't negate " + aTypeName(operand) + " value");
            return negateNumber(operand);
    }
}

Value Interpreter::evalMember(const MemberExpr& expr, const EnvPtr& env) {
    Value object = eval(*expr.object, env);
    if (!object.isMap()) {
        std::string hint;
        Variable* function = env->find(expr.name);
        if (function != nullptr && function->value.isFunction()) {
            hint = " (to call " + expr.name + "(), write ." + expr.name + "())";
        }
        throw RuntimeError(std::string("can't read field '") + expr.name + "' of " + aTypeName(object) + " value" + hint,
                           expr.line, expr.column);
    }
    const Value* field = object.asMap().find(Value::fromString(expr.name));
    if (field == nullptr) {
        const MapObj& map = object.asMap();
        if (map.structInfo) {
            throw RuntimeError(map.structInfo->name + " has no field '" + expr.name + "'", expr.line, expr.column);
        }
        if (!map.frozenAs.empty()) {
            throw RuntimeError(map.frozenAs + " has no member '" + expr.name + "'", expr.line, expr.column);
        }
        throw RuntimeError("map has no key \"" + expr.name + "\"", expr.line, expr.column);
    }
    return *field;
}

Value Interpreter::evalFString(const FStringExpr& expr, const EnvPtr& env) {
    std::string result;
    for (const auto& part : expr.parts) {
        if (!part.expr) {
            result += part.text;
            continue;
        }
        Value value = eval(*part.expr, env);
        ErrorSite site(*this, part.expr->line, part.expr->column);  // e.g. a bad format spec
        result += part.spec.empty() ? toString(value) : formatValue(value, part.spec);
    }
    return Value::fromString(result);
}

// Calls `f(args)`. A method-style call `x.f(args)` calls the function stored
// in map field `f` if there is one, and otherwise calls the function `f` with
// `x` as the first argument, so `items.push(4)` means `push(items, 4)`.
Value Interpreter::evalCall(const CallExpr& expr, const EnvPtr& env) {
    Value callee;
    std::vector<Value> args;
    args.reserve(expr.args.size() + 1);

    if (expr.callee->kind == ExprKind::Member) {
        const auto& member = expr.callee->as<MemberExpr>();
        Value object = eval(*member.object, env);
        const Value* field = object.isMap() ? object.asMap().find(Value::fromString(member.name)) : nullptr;
        if (field != nullptr) {
            callee = *field;
        } else {
            Variable* function = env->find(member.name);
            if (function == nullptr || !function->value.isFunction()) {
                std::string message =
                    object.isMap()
                        ? "map has no key \"" + member.name + "\" and there is no function named '" + member.name + "'"
                        : aTypeName(object) + " value has no method '" + member.name + "'";
                throw RuntimeError(message + suggestName(member.name, *env), member.line, member.column);
            }
            callee = function->value;
            args.push_back(std::move(object));
        }
    } else {
        callee = eval(*expr.callee, env);
    }

    for (const auto& arg : expr.args) args.push_back(eval(*arg, env));
    return call(callee, std::move(args), expr.line, expr.column);
}

Value Interpreter::makeFunction(const FunctionExpr& expr, const EnvPtr& env, const std::string& name) {
    return Value::fromFunction(track(std::make_shared<FunctionObj>(name, &expr, env, currentSource_)));
}

Value Interpreter::makeStruct(const StructStmt& stmt, const EnvPtr& env) {
    auto info = std::make_shared<StructInfo>();
    info->name = stmt.name;
    for (const auto& field : stmt.fields) info->fields.emplace_back(field.name, field.type);
    return Value::fromFunction(track(std::make_shared<StructType>(info, &stmt, env, currentSource_)));
}

// Point(1, 2): the arguments fill the fields in order; fields left out get
// their default value, or their type's zero value.
Value Interpreter::constructStruct(StructType& type, std::vector<Value> args, int line, int column) {
    const StructStmt& declaration = *type.declaration;
    const std::string& name = declaration.name;
    if (args.size() > declaration.fields.size()) {
        std::string fields;
        for (const auto& field : declaration.fields) fields += (fields.empty() ? "" : ", ") + field.name;
        throw RuntimeError(name + "() takes at most " + std::to_string(declaration.fields.size()) + " argument" +
                               (declaration.fields.size() == 1 ? "" : "s") + (fields.empty() ? "" : " (" + fields + ")") +
                               ", but got " + std::to_string(args.size()),
                           line, column);
    }
    if (callDepth_ >= maxCallDepth_) {
        throw RuntimeError("maximum recursion depth (" + std::to_string(maxCallDepth_) + ") exceeded while making a " +
                               name,
                           line, column);
    }
    checkStack();

    auto object = newMap();
    object->structInfo = type.info;
    EnvPtr scope;  // where default values are evaluated, made when first needed
    FrameGuard frame(*this, type.source, nullptr);
    for (size_t i = 0; i < declaration.fields.size(); ++i) {
        const StructField& field = declaration.fields[i];
        Value value;
        if (i < args.size()) {
            value = std::move(args[i]);
        } else if (field.defaultValue) {
            if (!scope) scope = newEnvironment(type.closure);
            value = eval(*field.defaultValue, scope);
        } else {
            ErrorSite site(*this, field.line, field.column);
            value = zeroValue(field.type, type.closure);
        }
        Value converted;
        CoerceFailure failure;
        if (!coerceToType(value, field.type, converted, failure)) {
            std::string message = withHint(name + "(): field '" + field.name + "' is " + field.type.name() +
                                               ", so it can't hold " + failure.what,
                                           failure);
            if (i < args.size()) {
                RuntimeError error(message, line, column);  // a bad argument: report it at the call
                error.source = frame.callerSource();
                throw error;
            }
            throw RuntimeError(message, field.line, field.column);
        }
        object->set(Value::fromString(field.name), std::move(converted));
    }
    return Value::fromMap(object);
}

void Interpreter::setElement(MapObj& map, const Value& key, Value value) {
    if (!map.frozenAs.empty()) throw RuntimeError("the members of " + map.frozenAs + " can't be changed");
    if (map.structInfo) {
        const StructInfo& info = *map.structInfo;
        const TypeSpec* type = key.isString() ? info.fieldType(key.asString()) : nullptr;
        if (type == nullptr) {
            std::string fields;
            for (const auto& field : info.fields) fields += (fields.empty() ? "" : ", ") + field.first;
            throw RuntimeError(info.name + " has no field " + (key.isString() ? "'" + key.asString() + "'" : toRepr(key)) +
                               " (its fields are: " + (fields.empty() ? "none" : fields) + ")");
        }
        if (!type->isAny()) {
            Value converted;
            CoerceFailure failure;
            if (!coerceToType(value, *type, converted, failure)) {
                throw RuntimeError(withHint("field '" + key.asString() + "' of " + info.name + " is " + type->name() +
                                                ", so it can't hold " + failure.what,
                                            failure));
            }
            value = std::move(converted);
        }
    }
    map.set(key, std::move(value));
}

int Interpreter::runMain() {
    Variable* main = globals_->findLocal("main");
    if (main == nullptr || !main->value.isFunction() || main->value.asFunction().kind != Callable::Kind::User) return 0;
    Value function = main->value;
    const auto& declaration = *static_cast<FunctionObj&>(function.asFunction()).declaration;

    std::vector<Value> args;
    Variable* programArgs = globals_->find("args");
    Value list = programArgs != nullptr && programArgs->value.isList() ? programArgs->value : Value::fromList(newList());
    if (declaration.params.size() == 1) {
        args.push_back(list);
    } else if (declaration.params.size() >= 2) {
        args.push_back(Value::fromInt(static_cast<int64_t>(list.asList().items.size())));
        args.push_back(list);
    }

    const SourceFile* saved = currentSource_;
    clearErrorSite();
    Value result;
    try {
        ErrorSite site(*this, declaration.line, declaration.column);
        result = call(function, std::move(args), declaration.line, declaration.column);
    } catch (RuntimeError& error) {
        applyErrorSite(error);
        currentSource_ = saved;
        throw;
    }
    currentSource_ = saved;
    int64_t code = 0;
    if (result.isInt() && toInt64(result, code)) return static_cast<int>(code);
    return 0;
}

Value Interpreter::call(const Value& callee, std::vector<Value> args, int line, int column) {
    if (!callee.isFunction()) {
        throw RuntimeError(aTypeName(callee) + " value can't be called like a function", line, column);
    }
    Callable& callable = callee.asFunction();

    if (callable.kind == Callable::Kind::Native) {
        auto& native = static_cast<NativeFunction&>(callable);
        size_t count = args.size();
        if (count < static_cast<size_t>(native.minArgs) ||
            (native.maxArgs >= 0 && count > static_cast<size_t>(native.maxArgs))) {
            throw RuntimeError(arityMessage(native.name, static_cast<size_t>(native.minArgs), native.maxArgs, count),
                               line, column);
        }
        // Errors from the builtin are reported at the call (the enclosing ErrorSite).
        return native.fn(*this, args);
    }

    if (callable.kind == Callable::Kind::Struct) {
        return constructStruct(static_cast<StructType&>(callable), std::move(args), line, column);
    }

    auto& function = static_cast<FunctionObj&>(callable);
    const FunctionExpr& decl = *function.declaration;
    if (args.size() < decl.requiredParams || args.size() > decl.params.size()) {
        throw RuntimeError(arityMessage(function.name, decl.requiredParams, static_cast<long long>(decl.params.size()),
                                        args.size()),
                           line, column);
    }
    if (callDepth_ >= maxCallDepth_) {
        throw RuntimeError("maximum recursion depth (" + std::to_string(maxCallDepth_) +
                               ") exceeded; is there a recursive call without a base case?",
                           line, column);
    }
    checkStack();

    auto scope = newEnvironment(function.closure);
    {
        FrameGuard frame(*this, function.source, &decl);
        for (size_t i = 0; i < decl.params.size(); ++i) {
            const Param& param = decl.params[i];
            Value arg = i < args.size() ? std::move(args[i]) : eval(*param.defaultValue, scope);
            if (!param.type.isAny()) {
                Value converted;
                CoerceFailure failure;
                if (!coerceToType(arg, param.type, converted, failure)) {
                    // Reported at the call site, so it belongs to the caller's file.
                    RuntimeError error(withHint(functionLabel(function.name) + " expects '" + param.name + "' to be " + param.type.name() +
                                                    ", but got " + failure.what,
                                                failure),
                                       line, column);
                    error.source = frame.callerSource();
                    throw error;
                }
                arg = std::move(converted);
            }
            scope->define(param.name, std::move(arg), param.type);
        }

        if (execBlock(decl.body, scope) == Flow::Return) {
            Value result = std::move(returnValue_);
            returnValue_ = Value();
            return result;
        }
    }

    // The function ended without `return`.
    const TypeSpec& type = decl.returnType;
    if (!type.isAny() && type.kind != TypeKind::Void && !type.nullable) {
        throw RuntimeError(functionLabel(function.name) + " must return " + type.name() +
                               ", but it ended without returning a value",
                           line, column);
    }
    return Value();
}

// ---------------------------------------------------------------------------
// Operators

Value Interpreter::binaryOp(TokenType op, const Value& a, const Value& b) {
    NumOp numOp;
    if (toNumOp(op, numOp) && a.isNumber() && b.isNumber()) return numericOp(numOp, a, b);

    switch (op) {
        case TokenType::Plus:
            if (a.isChar() && b.isInt()) return shiftChar(a, b, 1);
            if (a.isInt() && b.isChar()) return shiftChar(b, a, 1);
            if (a.isString() || b.isString() || (a.isChar() && b.isChar())) {
                return Value::fromString(toString(a) + toString(b));
            }
            if (a.isList() && b.isList()) {
                std::vector<Value> items = a.asList().items;
                const auto& more = b.asList().items;
                items.insert(items.end(), more.begin(), more.end());
                return Value::fromList(newList(std::move(items)));
            }
            break;

        case TokenType::Minus:
            if (a.isChar() && b.isInt()) return shiftChar(a, b, -1);
            if (a.isChar() && b.isChar()) {
                return Value::fromInt(static_cast<int64_t>(a.asChar()) - static_cast<int64_t>(b.asChar()));
            }
            break;

        case TokenType::Star:
            if ((a.isString() || a.isChar()) && b.isInt()) return Value::fromString(repeatString(textOf(a), b));
            if (a.isInt() && (b.isString() || b.isChar())) return Value::fromString(repeatString(textOf(b), a));
            if ((a.isList() && b.isInt()) || (a.isInt() && b.isList())) {
                const auto& items = (a.isList() ? a : b).asList().items;
                int64_t count = repeatCount(a.isInt() ? a : b, items.size());
                std::vector<Value> result;
                result.reserve(items.size() * static_cast<size_t>(count));
                for (int64_t i = 0; i < count; ++i) result.insert(result.end(), items.begin(), items.end());
                return Value::fromList(newList(std::move(result)));
            }
            break;

        case TokenType::EqualEqual:
            return Value::fromBool(valuesEqual(a, b));

        case TokenType::NotEqual:
            return Value::fromBool(!valuesEqual(a, b));

        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual: {
            int order;
            if (a.isNumber() && b.isNumber()) {
                if ((a.isFloat() && std::isnan(a.asFloat())) || (b.isFloat() && std::isnan(b.asFloat()))) {
                    return Value::fromBool(false);
                }
                order = compareNumbers(a, b);
            } else if (((a.isString() || a.isChar()) && (b.isString() || b.isChar())) || (a.isList() && b.isList())) {
                order = compareValues(a, b);
            } else {
                throw RuntimeError(std::string("can't compare ") + a.typeName() + " and " + b.typeName() + " with '" +
                                   operatorSymbol(op) + "'");
            }
            switch (op) {
                case TokenType::Less: return Value::fromBool(order < 0);
                case TokenType::LessEqual: return Value::fromBool(order <= 0);
                case TokenType::Greater: return Value::fromBool(order > 0);
                default: return Value::fromBool(order >= 0);
            }
        }

        case TokenType::In:
            return Value::fromBool(contains(b, a));

        case TokenType::Ampersand:
        case TokenType::Pipe:
            if (a.isBool() && b.isBool()) {
                throw RuntimeError(std::string("'") + operatorSymbol(op) +
                                   "' works on the bits of integers; for true/false use '" +
                                   (op == TokenType::Ampersand ? "and" : "or") + "'");
            }
            break;

        default:
            break;
    }
    throw RuntimeError(std::string("unsupported operand types for '") + operatorSymbol(op) + "': " + a.typeName() +
                       " and " + b.typeName());
}

bool Interpreter::contains(const Value& container, const Value& item) {
    switch (container.type()) {
        case Value::Type::String:
            if (!item.isString() && !item.isChar()) {
                throw RuntimeError(std::string("'in <string>' needs a string or char on the left, not ") +
                                   item.typeName());
            }
            return container.asString().find(textOf(item)) != std::string::npos;
        case Value::Type::List:
            for (const Value& element : container.asList().items) {
                if (valuesEqual(element, item)) return true;
            }
            return false;
        case Value::Type::Map:
            return container.asMap().find(item) != nullptr;
        default:
            throw RuntimeError(std::string("'in' needs a list, map or string on the right, not ") + container.typeName());
    }
}

Value Interpreter::indexGet(const Value& object, const Value& index) {
    switch (object.type()) {
        case Value::Type::List: {
            const auto& items = object.asList().items;
            return items[resolveIndex(index, items.size(), "list")];
        }
        case Value::Type::String: {
            const std::string& text = object.asString();
            size_t position = utf8Offset(text, resolveIndex(index, utf8Length(text), "string"));
            return Value::fromChar(decodeUtf8(text, position));
        }
        case Value::Type::Map: {
            const Value* value = object.asMap().find(index);
            if (value == nullptr) throw RuntimeError("key " + toRepr(index) + " not found in map");
            return *value;
        }
        default:
            throw RuntimeError("can't index into " + aTypeName(object) + " value");
    }
}

void Interpreter::indexSet(const Value& object, const Value& index, Value value) {
    switch (object.type()) {
        case Value::Type::List: {
            auto& items = object.asList().items;
            items[resolveIndex(index, items.size(), "list")] = std::move(value);
            return;
        }
        case Value::Type::Map:
            setElement(object.asMap(), index, std::move(value));
            return;
        case Value::Type::String:
            throw RuntimeError("strings can't be changed in place; build a new string instead");
        default:
            throw RuntimeError("can't set an element of " + aTypeName(object) + " value");
    }
}

Value Interpreter::slice(const Value& object, const Value& start, const Value& end) {
    size_t length;
    if (object.isList()) {
        length = object.asList().items.size();
    } else if (object.isString()) {
        length = utf8Length(object.asString());
    } else {
        throw RuntimeError("can't slice " + aTypeName(object) + " value");
    }

    const int64_t size = static_cast<int64_t>(length);
    auto bound = [size](const Value& value, int64_t fallback) -> int64_t {
        if (value.isNull()) return fallback;
        if (!value.isInt()) throw RuntimeError("slice bounds must be integers, not " + aTypeName(value));
        int64_t i = 0;
        if (!toInt64(value, i)) return size;  // a huge u64
        if (i < 0) i += size;
        return std::max<int64_t>(0, std::min(i, size));
    };
    int64_t from = bound(start, 0);
    int64_t to = std::max(from, bound(end, size));

    if (object.isString()) {
        const std::string& text = object.asString();
        size_t first = utf8Offset(text, static_cast<size_t>(from));
        size_t last = utf8Offset(text, static_cast<size_t>(to));
        return Value::fromString(text.substr(first, last - first));
    }
    const auto& items = object.asList().items;
    return Value::fromList(newList(std::vector<Value>(items.begin() + from, items.begin() + to)));
}

}  // namespace ascend

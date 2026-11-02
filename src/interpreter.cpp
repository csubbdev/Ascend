#include "interpreter.hpp"

#include "stdlib.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ascend {

namespace {

constexpr int kDefaultMaxCallDepth = 100000;
constexpr size_t kMiB = 1024 * 1024;
constexpr size_t kMinCollectionThreshold = 10000;
constexpr size_t kMaxRepeatSize = size_t(1) << 28;

constexpr int64_t kIntMax = std::numeric_limits<int64_t>::max();
constexpr int64_t kIntMin = std::numeric_limits<int64_t>::min();

const char* operatorSymbol(TokenType op) {
    switch (op) {
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::StarStar: return "**";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
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

TokenType compoundToBinary(TokenType op) {
    switch (op) {
        case TokenType::PlusEqual: return TokenType::Plus;
        case TokenType::MinusEqual: return TokenType::Minus;
        case TokenType::StarEqual: return TokenType::Star;
        case TokenType::SlashEqual: return TokenType::Slash;
        case TokenType::PercentEqual: return TokenType::Percent;
        default: return op;
    }
}

[[noreturn]] void overflow() { throw RuntimeError("integer overflow (the result doesn't fit in a 64-bit int)"); }

int64_t checkedAdd(int64_t a, int64_t b) {
    if ((b > 0 && a > kIntMax - b) || (b < 0 && a < kIntMin - b)) overflow();
    return a + b;
}

int64_t checkedSub(int64_t a, int64_t b) {
    if ((b < 0 && a > kIntMax + b) || (b > 0 && a < kIntMin + b)) overflow();
    return a - b;
}

int64_t checkedMul(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return 0;
    bool overflows;
    if (a > 0) {
        overflows = b > 0 ? a > kIntMax / b : b < kIntMin / a;
    } else {
        overflows = b > 0 ? a < kIntMin / b : a < kIntMax / b;
    }
    if (overflows) overflow();
    return a * b;
}

Value power(const Value& base, const Value& exponent) {
    if (base.isInt() && exponent.isInt() && exponent.asInt() >= 0) {
        int64_t result = 1;
        int64_t factor = base.asInt();
        int64_t remaining = exponent.asInt();
        while (remaining > 0) {
            if (remaining & 1) result = checkedMul(result, factor);
            remaining >>= 1;
            if (remaining > 0) factor = checkedMul(factor, factor);
        }
        return Value::fromInt(result);
    }
    return Value::fromFloat(std::pow(base.toDouble(), exponent.toDouble()));
}

int64_t repeatCount(int64_t count, size_t unitSize) {
    if (count <= 0) return 0;
    if (unitSize != 0 && static_cast<uint64_t>(count) > kMaxRepeatSize / unitSize) {
        throw RuntimeError("the repeated result would be too large");
    }
    return count;
}

std::string repeatString(const std::string& text, int64_t count) {
    count = repeatCount(count, text.size());
    std::string result;
    result.reserve(text.size() * static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) result += text;
    return result;
}

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

// Address of the current stack frame, used to measure how much stack is in use.
uintptr_t stackAddress() {
#if defined(__GNUC__) || defined(__clang__)
    return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
#elif defined(_MSC_VER)
    return reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
#else
    volatile char marker = 0;
    return reinterpret_cast<uintptr_t>(&marker);
#endif
}

// How much stack the interpreter may use before reporting a stack overflow.
// Leaves a safety margin below the real limit so the error can be reported
// cleanly instead of crashing.
size_t stackBudget() {
    size_t total = 8 * kMiB;  // the default on Linux and macOS; CMake links Windows builds with 8 MB too
#if defined(__unix__) || defined(__APPLE__)
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY) {
        total = static_cast<size_t>(limit.rlim_cur);
    }
#endif
    return total > 2 * kMiB ? total - std::max(kMiB, total / 8) : total / 2;
}

void locate(RuntimeError& error, int line, int column) {
    if (!error.hasLocation()) {
        error.line = line;
        error.column = column;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Setup and memory management

Interpreter::Interpreter(std::ostream& out, std::istream& in)
    : out_(out),
      in_(in),
      maxCallDepth_(kDefaultMaxCallDepth),
      stackBase_(stackAddress()),
      stackBudget_(stackBudget()),
      nextCollection_(kMinCollectionThreshold) {
    try {
        random_.seed(std::random_device{}());
    } catch (...) {
        random_.seed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    globals_ = newEnvironment(nullptr);
    registerStandardLibrary(*this);
    if (Value* range = globals_->find("range")) rangeFunction_ = &range->asFunction();
}

Interpreter::~Interpreter() {
    // Break every remaining reference cycle so all memory is released.
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
    globals_->define(name, Value::fromFunction(std::make_shared<NativeFunction>(name, minArgs, maxArgs, std::move(fn))));
}

void Interpreter::defineGlobal(const std::string& name, Value value) { globals_->define(name, std::move(value)); }

// ---------------------------------------------------------------------------
// Statements

void Interpreter::execute(std::unique_ptr<Program> program) {
    programs_.push_back(std::move(program));
    const Block& body = programs_.back()->body;
    hoistFunctions(body, globals_);
    for (const auto& stmt : body.statements) {
        if (replMode_ && stmt->kind == StmtKind::Expression) {
            Value result = eval(*stmt->as<ExpressionStmt>().expr, globals_);
            if (!result.isNull()) out_ << toRepr(result) << '\n';
            continue;
        }
        exec(*stmt, globals_);
    }
}

Interpreter::Flow Interpreter::exec(const Stmt& stmt, const EnvPtr& env) {
    try {
        return execStmt(stmt, env);
    } catch (RuntimeError& error) {
        locate(error, stmt.line, stmt.column);
        throw;
    }
}

Interpreter::Flow Interpreter::execStmt(const Stmt& stmt, const EnvPtr& env) {
    switch (stmt.kind) {
        case StmtKind::Expression:
            eval(*stmt.as<ExpressionStmt>().expr, env);
            return Flow::Normal;

        case StmtKind::Declare: {
            const auto& decl = stmt.as<DeclareStmt>();
            Value value;
            if (decl.value->kind == ExprKind::Function) {
                // `name := func(...) {...}` gives the function a name for messages.
                const auto& function = decl.value->as<FunctionExpr>();
                value = makeFunction(function, env, function.name.empty() ? decl.name : function.name);
            } else {
                value = eval(*decl.value, env);
            }
            env->define(decl.name, std::move(value));
            return Flow::Normal;
        }

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

        case StmtKind::While: {
            const auto& loop = stmt.as<WhileStmt>();
            while (eval(*loop.condition, env).truthy()) {
                Flow flow = execScoped(loop.body, env);
                if (flow == Flow::Break) break;
                if (flow == Flow::Return) return flow;
            }
            return Flow::Normal;
        }

        case StmtKind::For:
            return execFor(stmt.as<ForStmt>(), env);

        case StmtKind::Break:
            return Flow::Break;

        case StmtKind::Continue:
            return Flow::Continue;

        case StmtKind::Return: {
            const auto& ret = stmt.as<ReturnStmt>();
            returnValue_ = ret.value ? eval(*ret.value, env) : Value();
            return Flow::Return;
        }

        case StmtKind::Function:
            return Flow::Normal;  // already defined when its block started (hoisting)

        case StmtKind::Try:
            return execTry(stmt.as<TryStmt>(), env);

        case StmtKind::Throw: {
            Value value = eval(*stmt.as<ThrowStmt>().value, env);
            throw ThrownValue{std::move(value), stmt.line, stmt.column};
        }
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
        if (stmt->kind != StmtKind::Function) continue;
        const FunctionExpr& function = *stmt->as<FunctionStmt>().function;
        env->define(function.name, makeFunction(function, env, function.name));
    }
}

Interpreter::Flow Interpreter::execAssign(const AssignStmt& stmt, const EnvPtr& env) {
    const bool compound = stmt.op != TokenType::Equal;
    const TokenType op = compoundToBinary(stmt.op);

    switch (stmt.target->kind) {
        case ExprKind::Variable: {
            const std::string& name = stmt.target->as<VariableExpr>().name;
            Value* slot = env->find(name);
            if (slot == nullptr) {
                std::string hint = suggestName(name, *env);
                if (hint.empty()) hint = " (to create it, write '" + name + " := ...')";
                throw RuntimeError("undefined variable '" + name + "'" + hint, stmt.target->line, stmt.target->column);
            }
            Value result;
            if (compound) {
                Value current = *slot;
                Value operand = eval(*stmt.value, env);
                result = binaryOp(op, current, operand);
            } else {
                result = eval(*stmt.value, env);
            }
            // Evaluating the right side may have defined new variables, which
            // can move existing ones, so look the variable up again.
            *env->find(name) = std::move(result);
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
                throw RuntimeError(std::string("can't set field '") + target.name + "' on a " + object.typeName() +
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
            object.asMap().set(key, std::move(result));
            return Flow::Normal;
        }

        default:
            throw RuntimeError("can't assign to this expression");
    }
}

Interpreter::Flow Interpreter::execFor(const ForStmt& loop, const EnvPtr& env) {
    // Runs one iteration. With one loop variable it gets `item`; with two,
    // the first gets `key` (an index for lists and strings) and the second `item`.
    auto iteration = [&](Value key, Value item) -> Flow {
        auto scope = newEnvironment(env);
        if (loop.second.empty()) {
            scope->define(loop.first, std::move(item));
        } else {
            scope->define(loop.first, std::move(key));
            scope->define(loop.second, std::move(item));
        }
        return execBlock(loop.body, scope);
    };

    // Fast path: `for i in range(...)` counts without building a list.
    if (loop.iterable->kind == ExprKind::Call && rangeFunction_ != nullptr) {
        const auto& call = loop.iterable->as<CallExpr>();
        if (call.callee->kind == ExprKind::Variable) {
            Value* callee = env->find(call.callee->as<VariableExpr>().name);
            if (callee != nullptr && callee->isFunction() && &callee->asFunction() == rangeFunction_) {
                std::vector<Value> args;
                for (const auto& arg : call.args) args.push_back(eval(*arg, env));
                RangeSpec range;
                try {
                    range = makeRange(args);
                } catch (RuntimeError& error) {
                    locate(error, call.line, call.column);
                    throw;
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
            for (size_t i = 0; i < text.size(); ++i) {
                Flow flow = iteration(Value::fromInt(static_cast<int64_t>(i)), Value::fromString(std::string(1, text[i])));
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
            throw RuntimeError(std::string("can't loop over a ") + iterable.typeName() +
                                   " value (expected a list, map, string or range)",
                               loop.iterable->line, loop.iterable->column);
    }
}

Interpreter::Flow Interpreter::execTry(const TryStmt& stmt, const EnvPtr& env) {
    Value caught;
    try {
        return execScoped(stmt.body, env);
    } catch (const RuntimeError& error) {
        caught = Value::fromString(error.what());
    } catch (const ThrownValue& thrown) {
        caught = thrown.value;
    }
    auto scope = newEnvironment(env);
    if (!stmt.errorName.empty()) scope->define(stmt.errorName, std::move(caught));
    return execBlock(stmt.handler, scope);
}

// ---------------------------------------------------------------------------
// Expressions

void Interpreter::checkStack() const {
    uintptr_t here = stackAddress();
    size_t used = here > stackBase_ ? here - stackBase_ : stackBase_ - here;
    if (used > stackBudget_) {
        throw RuntimeError("stack overflow: too much recursion (is there a recursive call without a base case?)");
    }
}

Value Interpreter::eval(const Expr& expr, const EnvPtr& env) {
    try {
        checkStack();
        return evalExpr(expr, env);
    } catch (RuntimeError& error) {
        locate(error, expr.line, expr.column);
        throw;
    }
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
                try {
                    map->set(key, std::move(value));
                } catch (RuntimeError& error) {
                    locate(error, entry.first->line, entry.first->column);
                    throw;
                }
            }
            return Value::fromMap(map);
        }

        case ExprKind::Function: {
            const auto& function = expr.as<FunctionExpr>();
            return makeFunction(function, env, function.name);
        }
    }
    return Value();
}

Value Interpreter::evalVariable(const VariableExpr& expr, const EnvPtr& env) {
    Value* value = env->find(expr.name);
    if (value == nullptr) {
        throw RuntimeError("undefined variable '" + expr.name + "'" + suggestName(expr.name, *env), expr.line,
                           expr.column);
    }
    return *value;
}

Value Interpreter::evalUnary(const UnaryExpr& expr, const EnvPtr& env) {
    Value operand = eval(*expr.operand, env);
    if (expr.op == TokenType::Not) return Value::fromBool(!operand.truthy());
    if (operand.isInt()) {
        if (operand.asInt() == kIntMin) overflow();
        return Value::fromInt(-operand.asInt());
    }
    if (operand.isFloat()) return Value::fromFloat(-operand.asFloat());
    throw RuntimeError(std::string("can't negate a ") + operand.typeName() + " value");
}

Value Interpreter::evalMember(const MemberExpr& expr, const EnvPtr& env) {
    Value object = eval(*expr.object, env);
    if (!object.isMap()) {
        std::string hint;
        Value* function = env->find(expr.name);
        if (function != nullptr && function->isFunction()) hint = " (to call " + expr.name + "(), write ." + expr.name + "())";
        throw RuntimeError(std::string("can't read field '") + expr.name + "' of a " + object.typeName() + " value" + hint,
                           expr.line, expr.column);
    }
    const Value* field = object.asMap().find(Value::fromString(expr.name));
    if (field == nullptr) throw RuntimeError("map has no key \"" + expr.name + "\"", expr.line, expr.column);
    return *field;
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
            Value* function = env->find(member.name);
            if (function == nullptr || !function->isFunction()) {
                std::string message = object.isMap()
                                          ? "map has no key \"" + member.name + "\" and there is no function named '" + member.name + "'"
                                          : std::string("a ") + object.typeName() + " value has no method '" + member.name + "'";
                throw RuntimeError(message + suggestName(member.name, *env), member.line, member.column);
            }
            callee = *function;
            args.push_back(std::move(object));
        }
    } else {
        callee = eval(*expr.callee, env);
    }

    for (const auto& arg : expr.args) args.push_back(eval(*arg, env));
    return call(callee, std::move(args), expr.line, expr.column);
}

Value Interpreter::makeFunction(const FunctionExpr& expr, const EnvPtr& env, const std::string& name) {
    return Value::fromFunction(track(std::make_shared<FunctionObj>(name, &expr, env)));
}

Value Interpreter::call(const Value& callee, std::vector<Value> args, int line, int column) {
    if (!callee.isFunction()) {
        throw RuntimeError(std::string("a ") + callee.typeName() + " value can't be called like a function", line, column);
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
        try {
            return native.fn(*this, args);
        } catch (RuntimeError& error) {
            locate(error, line, column);
            throw;
        }
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

    struct DepthGuard {
        explicit DepthGuard(int& counter) : depth(counter) { ++depth; }
        ~DepthGuard() { --depth; }
        int& depth;
    } guard(callDepth_);

    auto scope = newEnvironment(function.closure);
    for (size_t i = 0; i < decl.params.size(); ++i) {
        if (i < args.size()) {
            scope->define(decl.params[i].name, std::move(args[i]));
        } else {
            scope->define(decl.params[i].name, eval(*decl.params[i].defaultValue, scope));
        }
    }

    if (execBlock(decl.body, scope) == Flow::Return) {
        Value result = std::move(returnValue_);
        returnValue_ = Value();
        return result;
    }
    return Value();
}

// ---------------------------------------------------------------------------
// Operators

Value Interpreter::binaryOp(TokenType op, const Value& a, const Value& b) {
    switch (op) {
        case TokenType::Plus:
            if (a.isInt() && b.isInt()) return Value::fromInt(checkedAdd(a.asInt(), b.asInt()));
            if (a.isNumber() && b.isNumber()) return Value::fromFloat(a.toDouble() + b.toDouble());
            if (a.isString() || b.isString()) return Value::fromString(toString(a) + toString(b));
            if (a.isList() && b.isList()) {
                std::vector<Value> items = a.asList().items;
                const auto& more = b.asList().items;
                items.insert(items.end(), more.begin(), more.end());
                return Value::fromList(newList(std::move(items)));
            }
            break;

        case TokenType::Minus:
            if (a.isInt() && b.isInt()) return Value::fromInt(checkedSub(a.asInt(), b.asInt()));
            if (a.isNumber() && b.isNumber()) return Value::fromFloat(a.toDouble() - b.toDouble());
            break;

        case TokenType::Star:
            if (a.isInt() && b.isInt()) return Value::fromInt(checkedMul(a.asInt(), b.asInt()));
            if (a.isNumber() && b.isNumber()) return Value::fromFloat(a.toDouble() * b.toDouble());
            if (a.isString() && b.isInt()) return Value::fromString(repeatString(a.asString(), b.asInt()));
            if (a.isInt() && b.isString()) return Value::fromString(repeatString(b.asString(), a.asInt()));
            if ((a.isList() && b.isInt()) || (a.isInt() && b.isList())) {
                const auto& items = (a.isList() ? a : b).asList().items;
                int64_t count = repeatCount((a.isInt() ? a : b).asInt(), items.size());
                std::vector<Value> result;
                result.reserve(items.size() * static_cast<size_t>(count));
                for (int64_t i = 0; i < count; ++i) result.insert(result.end(), items.begin(), items.end());
                return Value::fromList(newList(std::move(result)));
            }
            break;

        case TokenType::Slash:
            if (a.isNumber() && b.isNumber()) {
                if (b.toDouble() == 0.0) throw RuntimeError("division by zero");
                if (a.isInt() && b.isInt()) {
                    if (a.asInt() == kIntMin && b.asInt() == -1) overflow();
                    return Value::fromInt(a.asInt() / b.asInt());  // truncates toward zero
                }
                return Value::fromFloat(a.toDouble() / b.toDouble());
            }
            break;

        case TokenType::Percent:
            if (a.isNumber() && b.isNumber()) {
                if (b.toDouble() == 0.0) throw RuntimeError("modulo by zero");
                if (a.isInt() && b.isInt()) {
                    if (b.asInt() == -1) return Value::fromInt(0);
                    return Value::fromInt(a.asInt() % b.asInt());
                }
                return Value::fromFloat(std::fmod(a.toDouble(), b.toDouble()));
            }
            break;

        case TokenType::StarStar:
            if (a.isNumber() && b.isNumber()) return power(a, b);
            break;

        case TokenType::EqualEqual:
            return Value::fromBool(valuesEqual(a, b));

        case TokenType::NotEqual:
            return Value::fromBool(!valuesEqual(a, b));

        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual: {
            bool less;
            bool equal;
            if (a.isInt() && b.isInt()) {
                less = a.asInt() < b.asInt();
                equal = a.asInt() == b.asInt();
            } else if (a.isNumber() && b.isNumber()) {
                double x = a.toDouble();
                double y = b.toDouble();
                if (std::isnan(x) || std::isnan(y)) return Value::fromBool(false);
                less = x < y;
                equal = x == y;
            } else if ((a.isString() && b.isString()) || (a.isList() && b.isList())) {
                int order = compareValues(a, b);
                less = order < 0;
                equal = order == 0;
            } else {
                throw RuntimeError(std::string("can't compare ") + a.typeName() + " and " + b.typeName() + " with '" +
                                   operatorSymbol(op) + "'");
            }
            switch (op) {
                case TokenType::Less: return Value::fromBool(less);
                case TokenType::LessEqual: return Value::fromBool(less || equal);
                case TokenType::Greater: return Value::fromBool(!less && !equal);
                default: return Value::fromBool(!less);
            }
        }

        case TokenType::In:
            return Value::fromBool(contains(b, a));

        default:
            break;
    }
    throw RuntimeError(std::string("unsupported operand types for '") + operatorSymbol(op) + "': " + a.typeName() +
                       " and " + b.typeName());
}

bool Interpreter::contains(const Value& container, const Value& item) {
    switch (container.type()) {
        case Value::Type::String:
            if (!item.isString()) {
                throw RuntimeError(std::string("'in <string>' needs a string on the left, not ") + item.typeName());
            }
            return container.asString().find(item.asString()) != std::string::npos;
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
            return Value::fromString(std::string(1, text[resolveIndex(index, text.size(), "string")]));
        }
        case Value::Type::Map: {
            const Value* value = object.asMap().find(index);
            if (value == nullptr) throw RuntimeError("key " + toRepr(index) + " not found in map");
            return *value;
        }
        default:
            throw RuntimeError(std::string("can't index into a ") + object.typeName() + " value");
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
            object.asMap().set(index, std::move(value));
            return;
        case Value::Type::String:
            throw RuntimeError("strings can't be changed in place; build a new string instead");
        default:
            throw RuntimeError(std::string("can't set an element of a ") + object.typeName() + " value");
    }
}

Value Interpreter::slice(const Value& object, const Value& start, const Value& end) {
    size_t length;
    if (object.isList()) {
        length = object.asList().items.size();
    } else if (object.isString()) {
        length = object.asString().size();
    } else {
        throw RuntimeError(std::string("can't slice a ") + object.typeName() + " value");
    }

    const int64_t size = static_cast<int64_t>(length);
    auto bound = [size](const Value& value, int64_t fallback) -> int64_t {
        if (value.isNull()) return fallback;
        if (!value.isInt()) throw RuntimeError(std::string("slice bounds must be ints, not ") + value.typeName());
        int64_t i = value.asInt();
        if (i < 0) i += size;
        return std::max<int64_t>(0, std::min(i, size));
    };
    int64_t from = bound(start, 0);
    int64_t to = std::max(from, bound(end, size));

    if (object.isString()) {
        return Value::fromString(object.asString().substr(static_cast<size_t>(from), static_cast<size_t>(to - from)));
    }
    const auto& items = object.asList().items;
    return Value::fromList(newList(std::vector<Value>(items.begin() + from, items.begin() + to)));
}

}  // namespace ascend

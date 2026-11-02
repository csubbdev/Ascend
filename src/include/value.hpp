#ifndef ASCEND_VALUE_HPP
#define ASCEND_VALUE_HPP

#include "error.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ascend {

class Environment;
class Interpreter;
struct FunctionExpr;
struct ListObj;
struct MapObj;
struct Callable;

// Heap objects that can be part of a reference cycle (lists, maps, functions
// and environments). Reference counting frees everything else; the
// interpreter's cycle collector uses these hooks to find and break cycles,
// such as a function stored in the scope it closes over.
class GcObject {
public:
    virtual ~GcObject() = default;
    virtual void forEachChild(const std::function<void(GcObject*)>& visit) const = 0;
    virtual void clearReferences() = 0;
};

// A dynamically typed Ascend value. Lists, maps and functions are reference
// types: copying a Value copies the reference, not the contents.
class Value {
public:
    // The order must match the alternatives of Data below.
    enum class Type { Null, Bool, Int, Float, String, List, Map, Function };

    Value() = default;  // null

    static Value fromBool(bool value);
    static Value fromInt(int64_t value);
    static Value fromFloat(double value);
    static Value fromString(std::string value);
    static Value fromList(std::shared_ptr<ListObj> value);
    static Value fromMap(std::shared_ptr<MapObj> value);
    static Value fromFunction(std::shared_ptr<Callable> value);

    Type type() const { return static_cast<Type>(data_.index()); }
    bool isNull() const { return type() == Type::Null; }
    bool isBool() const { return type() == Type::Bool; }
    bool isInt() const { return type() == Type::Int; }
    bool isFloat() const { return type() == Type::Float; }
    bool isNumber() const { return isInt() || isFloat(); }
    bool isString() const { return type() == Type::String; }
    bool isList() const { return type() == Type::List; }
    bool isMap() const { return type() == Type::Map; }
    bool isFunction() const { return type() == Type::Function; }

    bool asBool() const { return std::get<1>(data_); }
    int64_t asInt() const { return std::get<2>(data_); }
    double asFloat() const { return std::get<3>(data_); }
    double toDouble() const { return isInt() ? static_cast<double>(asInt()) : asFloat(); }
    const std::string& asString() const { return std::get<4>(data_); }
    ListObj& asList() const { return *std::get<5>(data_); }
    MapObj& asMap() const { return *std::get<6>(data_); }
    Callable& asFunction() const { return *std::get<7>(data_); }
    const std::shared_ptr<ListObj>& listPtr() const { return std::get<5>(data_); }
    const std::shared_ptr<MapObj>& mapPtr() const { return std::get<6>(data_); }

    bool truthy() const;
    const char* typeName() const;

    // The collectable object this value refers to, if any.
    GcObject* gcObject() const;

private:
    using Data = std::variant<std::monostate, bool, int64_t, double, std::string,
                              std::shared_ptr<ListObj>, std::shared_ptr<MapObj>,
                              std::shared_ptr<Callable>>;
    Data data_;
};

struct ListObj final : GcObject {
    std::vector<Value> items;

    void forEachChild(const std::function<void(GcObject*)>& visit) const override;
    void clearReferences() override { items.clear(); }
};

// An insertion-ordered hash map. Keys may be null, bools, numbers or strings.
// Integral floats and ints are the same key, so m[1] and m[1.0] match.
struct MapObj final : GcObject {
    std::vector<std::pair<Value, Value>> entries;

    const Value* find(const Value& key) const;  // nullptr if missing (or unhashable)
    Value* find(const Value& key);
    void set(const Value& key, Value value);  // throws RuntimeError for unhashable keys
    bool remove(const Value& key, Value* removed = nullptr);
    size_t size() const { return entries.size(); }

    void forEachChild(const std::function<void(GcObject*)>& visit) const override;
    void clearReferences() override;

private:
    static bool hashKey(const Value& key, std::string& out);
    std::unordered_map<std::string, size_t> index_;
};

struct Callable {
    enum class Kind { Native, User };

    explicit Callable(Kind kind) : kind(kind) {}
    virtual ~Callable() = default;
    virtual GcObject* gcObject() { return nullptr; }

    const Kind kind;
};

using NativeFn = std::function<Value(Interpreter&, std::vector<Value>&)>;

// A builtin implemented in C++. maxArgs == -1 means "any number".
struct NativeFunction final : Callable {
    NativeFunction(std::string name, int minArgs, int maxArgs, NativeFn fn)
        : Callable(Kind::Native), name(std::move(name)), minArgs(minArgs), maxArgs(maxArgs), fn(std::move(fn)) {}

    std::string name;
    int minArgs;
    int maxArgs;
    NativeFn fn;
};

// A function written in Ascend, together with the scope it was created in.
struct FunctionObj final : Callable, GcObject {
    FunctionObj(std::string name, const FunctionExpr* declaration, std::shared_ptr<Environment> closure)
        : Callable(Kind::User), name(std::move(name)), declaration(declaration), closure(std::move(closure)) {}

    GcObject* gcObject() override { return this; }
    void forEachChild(const std::function<void(GcObject*)>& visit) const override;
    void clearReferences() override { closure.reset(); }

    std::string name;
    const FunctionExpr* declaration;  // owned by the interpreter's list of programs
    std::shared_ptr<Environment> closure;
};

// str(): how print() shows a value (strings without quotes).
std::string toString(const Value& value);
// repr(): how the REPL and containers show a value (strings quoted).
std::string toRepr(const Value& value);
std::string formatFloat(double value);
std::string quoteString(const std::string& text);
std::string functionName(const Callable& callable);

// Converts an index into a position in a sequence of `length` elements.
// Negative indexes count from the end (-1 is the last element). Throws
// RuntimeError if the index is not an int or is out of range.
size_t resolveIndex(const Value& index, size_t length, const char* what);

bool valuesEqual(const Value& a, const Value& b);
// Total ordering used by sort(), min() and max(). Throws RuntimeError for
// values that can't be ordered (e.g. a string and a number).
int compareValues(const Value& a, const Value& b);

}  // namespace ascend

#endif  // ASCEND_VALUE_HPP

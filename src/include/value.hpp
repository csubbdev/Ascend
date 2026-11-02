#ifndef ASCEND_VALUE_HPP
#define ASCEND_VALUE_HPP

#include "error.hpp"
#include "types.hpp"

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
struct SourceFile;

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

struct IntData {
    int64_t bits;  // the value; for u64 the bit pattern (read it with asUInt)
    NumKind kind;
};

struct FloatData {
    double value;  // f32 values are kept rounded to f32 precision
    NumKind kind;
};

// A dynamically typed Ascend value. Lists, maps and functions are reference
// types: copying a Value copies the reference, not the contents.
class Value {
public:
    // The order must match the alternatives of Data below.
    enum class Type { Null, Bool, Int, Float, Char, String, List, Map, Function };

    Value() = default;  // null

    static Value fromBool(bool value);
    static Value fromInt(int64_t value, NumKind kind = NumKind::Int);
    static Value fromUInt(uint64_t value, NumKind kind = NumKind::U64);
    static Value fromFloat(double value, NumKind kind = NumKind::Float);
    static Value fromChar(char32_t value);
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
    bool isChar() const { return type() == Type::Char; }
    bool isString() const { return type() == Type::String; }
    bool isList() const { return type() == Type::List; }
    bool isMap() const { return type() == Type::Map; }
    bool isFunction() const { return type() == Type::Function; }

    bool asBool() const { return std::get<1>(data_); }
    NumKind numKind() const { return isInt() ? std::get<2>(data_).kind : std::get<3>(data_).kind; }
    int64_t asInt() const { return std::get<2>(data_).bits; }
    uint64_t asUInt() const { return static_cast<uint64_t>(std::get<2>(data_).bits); }
    bool isNegativeInt() const { return numKind() != NumKind::U64 && asInt() < 0; }
    bool fitsInt64() const { return numKind() != NumKind::U64 || asInt() >= 0; }
    double asFloat() const { return std::get<3>(data_).value; }
    double toDouble() const;
    char32_t asChar() const { return std::get<4>(data_); }
    const std::string& asString() const { return std::get<5>(data_); }
    ListObj& asList() const { return *std::get<6>(data_); }
    MapObj& asMap() const { return *std::get<7>(data_); }
    Callable& asFunction() const { return *std::get<8>(data_); }
    const std::shared_ptr<ListObj>& listPtr() const { return std::get<6>(data_); }
    const std::shared_ptr<MapObj>& mapPtr() const { return std::get<7>(data_); }

    bool truthy() const;
    const char* typeName() const;  // "int", "u8", "f32", "char", "string", ...

    // The collectable object this value refers to, if any.
    GcObject* gcObject() const;

private:
    using Data = std::variant<std::monostate, bool, IntData, FloatData, char32_t, std::string,
                              std::shared_ptr<ListObj>, std::shared_ptr<MapObj>,
                              std::shared_ptr<Callable>>;
    Data data_;
};

struct ListObj final : GcObject {
    std::vector<Value> items;

    void forEachChild(const std::function<void(GcObject*)>& visit) const override;
    void clearReferences() override { items.clear(); }
};

// An insertion-ordered hash map. Keys may be null, bools, numbers, chars or
// strings. Equal numbers are the same key (m[1] and m[1.0]), and so are a char
// and the one-character string holding it.
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
    FunctionObj(std::string name, const FunctionExpr* declaration, std::shared_ptr<Environment> closure,
                const SourceFile* source)
        : Callable(Kind::User),
          name(std::move(name)),
          declaration(declaration),
          closure(std::move(closure)),
          source(source) {}

    GcObject* gcObject() override { return this; }
    void forEachChild(const std::function<void(GcObject*)>& visit) const override;
    void clearReferences() override { closure.reset(); }

    std::string name;
    const FunctionExpr* declaration;  // owned by the interpreter's list of programs
    std::shared_ptr<Environment> closure;
    const SourceFile* source;  // the file the function was written in
};

// str(): how print() shows a value (strings and chars without quotes).
std::string toString(const Value& value);
// repr(): how the REPL and containers show a value (strings and chars quoted).
std::string toRepr(const Value& value);
std::string formatFloat(double value, bool singlePrecision = false);
std::string quoteString(const std::string& text);
std::string quoteChar(char32_t c);
std::string functionName(const Callable& callable);
// The value's type name with "a" or "an": "an int", "a u8", "a string".
std::string aTypeName(const Value& value);

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

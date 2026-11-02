#include "stdlib.hpp"

#include "interpreter.hpp"
#include "numeric.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

namespace ascend {

namespace {

using Args = std::vector<Value>;

constexpr uint64_t kMaxRangeList = 10000000;

// ---------------------------------------------------------------------------
// Argument helpers

std::string article(const Value& value) { return aTypeName(value); }

[[noreturn]] void typeError(const char* fn, size_t index, const char* expected, const Value& got) {
    throw RuntimeError(std::string(fn) + "() expects " + expected + " as argument " + std::to_string(index + 1) +
                       ", not " + article(got));
}

int64_t intArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isInt()) typeError(fn, i, "an integer", args[i]);
    int64_t value = 0;
    if (!toInt64(args[i], value)) {
        throw RuntimeError(std::string(fn) + "(): argument " + std::to_string(i + 1) + " is too large");
    }
    return value;
}

double numberArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isNumber()) typeError(fn, i, "a number", args[i]);
    return args[i].toDouble();
}

const std::string& stringArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isString()) typeError(fn, i, "a string", args[i]);
    return args[i].asString();
}

// A string or a char, as text.
std::string textArg(const Args& args, size_t i, const char* fn) {
    if (args[i].isChar()) return encodeUtf8(args[i].asChar());
    if (!args[i].isString()) typeError(fn, i, "a string", args[i]);
    return args[i].asString();
}

ListObj& listArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isList()) typeError(fn, i, "a list", args[i]);
    return args[i].asList();
}

MapObj& mapArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isMap()) typeError(fn, i, "a map", args[i]);
    return args[i].asMap();
}

const Value& functionArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isFunction()) typeError(fn, i, "a function", args[i]);
    return args[i];
}

std::string trim(const std::string& text) {
    const char* space = " \t\r\n\f\v";
    size_t first = text.find_first_not_of(space);
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(space);
    return text.substr(first, last - first + 1);
}

std::string toUpper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string toLower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Splits text into lines, without the line endings (\n or \r\n).
std::vector<Value> splitLines(const std::string& text) {
    std::vector<Value> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(Value::fromString(line));
        start = end + 1;
    }
    return lines;
}

std::vector<char32_t> decodeAll(const std::string& text) {
    std::vector<char32_t> chars;
    for (size_t pos = 0; pos < text.size();) chars.push_back(decodeUtf8(text, pos));
    return chars;
}

// Parses an integer written in decimal, or with a 0x / 0b / 0o prefix.
bool parseInteger(const std::string& input, Value& out) {
    std::string text = trim(input);
    bool negative = false;
    size_t i = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    int base = 10;
    if (text.size() > i + 1 && text[i] == '0') {
        char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 1])));
        if (prefix == 'x' || prefix == 'b' || prefix == 'o') {
            base = prefix == 'x' ? 16 : (prefix == 'b' ? 2 : 8);
            i += 2;
        }
    }
    std::string digits;
    for (; i < text.size(); ++i) {
        if (text[i] != '_') digits += text[i];
    }
    if (digits.empty() || digits[0] == '+' || digits[0] == '-') return false;
    errno = 0;
    char* end = nullptr;
    unsigned long long magnitude = std::strtoull(digits.c_str(), &end, base);
    if (errno == ERANGE || end != digits.c_str() + digits.size()) return false;
    if (negative) {
        if (magnitude > 9223372036854775808ULL) return false;
        out = Value::fromInt(static_cast<int64_t>(0 - static_cast<uint64_t>(magnitude)));
    } else if (magnitude > static_cast<unsigned long long>(std::numeric_limits<int64_t>::max())) {
        out = Value::fromUInt(magnitude);
    } else {
        out = Value::fromInt(static_cast<int64_t>(magnitude));
    }
    return true;
}

bool parseFloat(const std::string& input, double& out) {
    std::string text = trim(input);
    if (text.empty()) return false;
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) return false;
    out = value;
    return true;
}

// The conversion functions int(), i8(), ..., u64(), byte().
Value toIntegerKind(const Value& value, NumKind kind, const char* fn) {
    switch (value.type()) {
        case Value::Type::Int:
        case Value::Type::Float: return convertNumberOrThrow(value, kind);
        case Value::Type::Bool: return Value::fromInt(value.asBool() ? 1 : 0, kind);
        case Value::Type::Char: return convertNumberOrThrow(Value::fromInt(static_cast<int64_t>(value.asChar())), kind);
        case Value::Type::String: {
            Value parsed;
            if (parseInteger(value.asString(), parsed)) return convertNumberOrThrow(parsed, kind);
            double number;
            if (parseFloat(value.asString(), number)) {
                throw RuntimeError(std::string(fn) + "() can't convert " + quoteString(value.asString()) +
                                   " to an integer (it has a fraction; use float() first)");
            }
            throw RuntimeError(std::string(fn) + "() can't convert " + quoteString(value.asString()) + " to an integer");
        }
        default:
            throw RuntimeError(std::string(fn) + "() can't convert " + article(value) + " to an integer");
    }
}

// The conversion functions float(), f32(), f64().
Value toFloatKind(const Value& value, NumKind kind, const char* fn) {
    switch (value.type()) {
        case Value::Type::Int:
        case Value::Type::Float: return Value::fromFloat(value.toDouble(), kind);
        case Value::Type::Bool: return Value::fromFloat(value.asBool() ? 1.0 : 0.0, kind);
        case Value::Type::String: {
            double number;
            if (parseFloat(value.asString(), number)) return Value::fromFloat(number, kind);
            throw RuntimeError(std::string(fn) + "() can't convert " + quoteString(value.asString()) + " to a float");
        }
        default:
            throw RuntimeError(std::string(fn) + "() can't convert " + article(value) + " to a float");
    }
}

Value extreme(const Args& args, const char* fn, int wanted) {
    const std::vector<Value>* pool = &args;
    if (args.size() == 1) {
        if (!args[0].isList()) typeError(fn, 0, "a list (or two or more values)", args[0]);
        pool = &args[0].asList().items;
    }
    if (pool->empty()) throw RuntimeError(std::string(fn) + "() of an empty list");
    Value best = (*pool)[0];
    for (size_t i = 1; i < pool->size(); ++i) {
        if (compareValues((*pool)[i], best) == wanted) best = (*pool)[i];
    }
    return best;
}

// Checks every character of a string or char with an ASCII classifier.
Value classify(const Args& args, const char* fn, int (*test)(int)) {
    std::string text = textArg(args, 0, fn);
    if (text.empty()) return Value::fromBool(false);
    for (char c : text) {
        unsigned char byte = static_cast<unsigned char>(c);
        if (byte >= 0x80 || !test(byte)) return Value::fromBool(false);
    }
    return Value::fromBool(true);
}

std::string padText(const Args& args, const char* fn, bool left) {
    std::string text = toString(args[0]);
    int64_t width = intArg(args, 1, fn);
    std::string fill = " ";
    if (args.size() > 2) {
        fill = textArg(args, 2, fn);
        if (utf8Length(fill) != 1) throw RuntimeError(std::string(fn) + "() fill must be a single character");
    }
    size_t length = utf8Length(text);
    if (width <= static_cast<int64_t>(length)) return text;
    if (width > (int64_t(1) << 24)) throw RuntimeError(std::string(fn) + "() width is too large");
    std::string padding;
    for (int64_t i = static_cast<int64_t>(length); i < width; ++i) padding += fill;
    return left ? padding + text : text + padding;
}

// ---------------------------------------------------------------------------
// Builtins

void registerCore(Interpreter& interp) {
    interp.defineNative("print", 0, -1, [](Interpreter& in, Args& args) {
        std::ostream& out = in.out();
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) out << ' ';
            out << toString(args[i]);
        }
        out << '\n';
        return Value();
    });

    interp.defineNative("input", 0, 1, [](Interpreter& in, Args& args) {
        if (!args.empty()) in.out() << toString(args[0]);
        in.out().flush();
        std::string line;
        if (!std::getline(in.in(), line)) return Value();  // end of input
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return Value::fromString(line);
    });

    interp.defineNative("type", 1, 1, [](Interpreter&, Args& args) { return Value::fromString(args[0].typeName()); });
    interp.defineNative("str", 1, 1, [](Interpreter&, Args& args) { return Value::fromString(toString(args[0])); });
    interp.defineNative("string", 1, 1, [](Interpreter&, Args& args) { return Value::fromString(toString(args[0])); });
    interp.defineNative("repr", 1, 1, [](Interpreter&, Args& args) { return Value::fromString(toRepr(args[0])); });
    interp.defineNative("bool", 1, 1, [](Interpreter&, Args& args) { return Value::fromBool(args[0].truthy()); });

    interp.defineNative("len", 1, 1, [](Interpreter&, Args& args) {
        const Value& value = args[0];
        switch (value.type()) {
            case Value::Type::String: return Value::fromInt(static_cast<int64_t>(utf8Length(value.asString())));
            case Value::Type::List: return Value::fromInt(static_cast<int64_t>(value.asList().items.size()));
            case Value::Type::Map: return Value::fromInt(static_cast<int64_t>(value.asMap().size()));
            default: typeError("len", 0, "a string, list or map", value);
        }
    });

    interp.defineNative("range", 1, 3, [](Interpreter& in, Args& args) {
        RangeSpec range = makeRange(args);
        if (range.count > kMaxRangeList) {
            throw RuntimeError("range() is too large to turn into a list (use it directly in a for loop instead)");
        }
        std::vector<Value> items;
        items.reserve(static_cast<size_t>(range.count));
        for (uint64_t i = 0; i < range.count; ++i) items.push_back(Value::fromInt(range.at(i)));
        return Value::fromList(in.newList(std::move(items)));
    });

    interp.defineNative("assert", 1, 2, [](Interpreter&, Args& args) {
        if (!args[0].truthy()) {
            throw RuntimeError(args.size() > 1 ? "assertion failed: " + toString(args[1]) : "assertion failed");
        }
        return Value();
    });

    interp.defineNative("exit", 0, 1, [](Interpreter&, Args& args) -> Value {
        int code = 0;
        if (!args.empty()) code = static_cast<int>(intArg(args, 0, "exit"));
        throw ExitRequest{code};
    });

    interp.defineNative("import", 1, 1, [](Interpreter& in, Args& args) {
        return in.importModule(stringArg(args, 0, "import"));
    });

    interp.defineNative("format", 1, -1, [](Interpreter&, Args& args) {
        return Value::fromString(formatTemplate(stringArg(args, 0, "format"), args, 1));
    });
}

void registerConversions(Interpreter& interp) {
    struct IntConversion {
        const char* name;
        NumKind kind;
    };
    static const IntConversion intConversions[] = {
        {"int", NumKind::Int}, {"i8", NumKind::I8},   {"i16", NumKind::I16}, {"i32", NumKind::I32},
        {"i64", NumKind::I64}, {"u8", NumKind::U8},   {"u16", NumKind::U16}, {"u32", NumKind::U32},
        {"u64", NumKind::U64}, {"byte", NumKind::U8},
    };
    for (const auto& conversion : intConversions) {
        const char* name = conversion.name;
        NumKind kind = conversion.kind;
        interp.defineNative(name, 1, 1, [name, kind](Interpreter&, Args& args) { return toIntegerKind(args[0], kind, name); });
    }

    struct FloatConversion {
        const char* name;
        NumKind kind;
    };
    static const FloatConversion floatConversions[] = {
        {"float", NumKind::Float}, {"f32", NumKind::F32}, {"f64", NumKind::F64}};
    for (const auto& conversion : floatConversions) {
        const char* name = conversion.name;
        NumKind kind = conversion.kind;
        interp.defineNative(name, 1, 1, [name, kind](Interpreter&, Args& args) { return toFloatKind(args[0], kind, name); });
    }

    interp.defineNative("char", 1, 1, [](Interpreter&, Args& args) {
        const Value& value = args[0];
        if (value.isChar()) return value;
        if (value.isString()) {
            if (utf8Length(value.asString()) != 1) {
                throw RuntimeError("char() needs a string with exactly one character, got " + quoteString(value.asString()));
            }
            size_t pos = 0;
            return Value::fromChar(decodeUtf8(value.asString(), pos));
        }
        int64_t code = intArg(args, 0, "char");
        if (code < 0 || code > 0x10FFFF || !isValidCodePoint(static_cast<char32_t>(code))) {
            throw RuntimeError("char(): " + std::to_string(code) + " is not a valid character code");
        }
        return Value::fromChar(static_cast<char32_t>(code));
    });

    interp.defineNative("hex", 1, 1, [](Interpreter&, Args& args) {
        if (!args[0].isInt()) typeError("hex", 0, "an integer", args[0]);
        bool negative = args[0].isNegativeInt();
        return Value::fromString((negative ? "-0x" : "0x") +
                                 formatValue(negative ? negateNumber(Value::fromInt(args[0].asInt())) : args[0], "x"));
    });

    interp.defineNative("bin", 1, 1, [](Interpreter&, Args& args) {
        if (!args[0].isInt()) typeError("bin", 0, "an integer", args[0]);
        bool negative = args[0].isNegativeInt();
        return Value::fromString((negative ? "-0b" : "0b") +
                                 formatValue(negative ? negateNumber(Value::fromInt(args[0].asInt())) : args[0], "b"));
    });
}

void registerCollections(Interpreter& interp) {
    interp.defineNative("push", 2, -1, [](Interpreter&, Args& args) {
        auto& items = listArg(args, 0, "push").items;
        for (size_t i = 1; i < args.size(); ++i) items.push_back(args[i]);
        return Value();
    });

    interp.defineNative("pop", 1, 2, [](Interpreter&, Args& args) {
        auto& items = listArg(args, 0, "pop").items;
        if (items.empty()) throw RuntimeError("pop() from an empty list");
        size_t index = args.size() > 1 ? resolveIndex(args[1], items.size(), "pop()") : items.size() - 1;
        Value removed = std::move(items[index]);
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
        return removed;
    });

    interp.defineNative("insert", 3, 3, [](Interpreter&, Args& args) {
        auto& items = listArg(args, 0, "insert").items;
        int64_t size = static_cast<int64_t>(items.size());
        int64_t index = intArg(args, 1, "insert");
        if (index < 0) index += size;
        if (index < 0 || index > size) {
            throw RuntimeError("insert() index " + toString(args[1]) + " is out of range (length " + std::to_string(size) + ")");
        }
        items.insert(items.begin() + index, args[2]);
        return Value();
    });

    interp.defineNative("remove", 2, 2, [](Interpreter&, Args& args) {
        if (args[0].isMap()) {
            Value removed;
            if (!args[0].asMap().remove(args[1], &removed)) {
                throw RuntimeError("remove(): key " + toRepr(args[1]) + " not found in map");
            }
            return removed;
        }
        auto& items = listArg(args, 0, "remove").items;
        size_t index = resolveIndex(args[1], items.size(), "remove()");
        Value removed = std::move(items[index]);
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
        return removed;
    });

    interp.defineNative("extend", 2, 2, [](Interpreter&, Args& args) {
        auto& items = listArg(args, 0, "extend").items;
        std::vector<Value> more = listArg(args, 1, "extend").items;  // copy: extending a list with itself is fine
        items.insert(items.end(), more.begin(), more.end());
        return Value();
    });

    interp.defineNative("clear", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isMap()) {
            args[0].asMap().clearReferences();
        } else {
            listArg(args, 0, "clear").items.clear();
        }
        return Value();
    });

    interp.defineNative("contains", 2, 2, [](Interpreter& in, Args& args) {
        return Value::fromBool(in.contains(args[0], args[1]));
    });

    interp.defineNative("index_of", 2, 2, [](Interpreter&, Args& args) {
        if (args[0].isString()) {
            const std::string& text = args[0].asString();
            size_t position = text.find(textArg(args, 1, "index_of"));
            if (position == std::string::npos) return Value::fromInt(-1);
            return Value::fromInt(static_cast<int64_t>(utf8Length(text.substr(0, position))));
        }
        const auto& items = listArg(args, 0, "index_of").items;
        for (size_t i = 0; i < items.size(); ++i) {
            if (valuesEqual(items[i], args[1])) return Value::fromInt(static_cast<int64_t>(i));
        }
        return Value::fromInt(-1);
    });

    interp.defineNative("count", 2, 2, [](Interpreter&, Args& args) {
        int64_t total = 0;
        if (args[0].isString()) {
            const std::string& text = args[0].asString();
            std::string part = textArg(args, 1, "count");
            if (part.empty()) throw RuntimeError("count() can't count empty strings");
            for (size_t pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + part.size())) ++total;
            return Value::fromInt(total);
        }
        for (const Value& item : listArg(args, 0, "count").items) {
            if (valuesEqual(item, args[1])) ++total;
        }
        return Value::fromInt(total);
    });

    interp.defineNative("slice", 2, 3, [](Interpreter& in, Args& args) {
        return in.slice(args[0], args[1], args.size() > 2 ? args[2] : Value());
    });

    interp.defineNative("reverse", 1, 1, [](Interpreter& in, Args& args) {
        if (args[0].isString()) {
            std::vector<char32_t> chars = decodeAll(args[0].asString());
            std::string text;
            for (auto it = chars.rbegin(); it != chars.rend(); ++it) appendUtf8(text, *it);
            return Value::fromString(text);
        }
        std::vector<Value> items = listArg(args, 0, "reverse").items;
        std::reverse(items.begin(), items.end());
        return Value::fromList(in.newList(std::move(items)));
    });

    interp.defineNative("sort", 1, 2, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "sort").items;
        if (args.size() > 1) {
            const Value& key = functionArg(args, 1, "sort");
            std::vector<std::pair<Value, Value>> keyed;
            keyed.reserve(items.size());
            for (Value& item : items) keyed.emplace_back(in.call(key, {item}), item);
            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const auto& a, const auto& b) { return compareValues(a.first, b.first) < 0; });
            for (size_t i = 0; i < keyed.size(); ++i) items[i] = std::move(keyed[i].second);
        } else {
            std::stable_sort(items.begin(), items.end(),
                             [](const Value& a, const Value& b) { return compareValues(a, b) < 0; });
        }
        return Value::fromList(in.newList(std::move(items)));
    });

    interp.defineNative("unique", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> result;
        for (const Value& item : listArg(args, 0, "unique").items) {
            bool seen = false;
            for (const Value& kept : result) {
                if (valuesEqual(kept, item)) {
                    seen = true;
                    break;
                }
            }
            if (!seen) result.push_back(item);
        }
        return Value::fromList(in.newList(std::move(result)));
    });

    interp.defineNative("flatten", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> result;
        for (const Value& item : listArg(args, 0, "flatten").items) {
            if (item.isList()) {
                const auto& inner = item.asList().items;
                result.insert(result.end(), inner.begin(), inner.end());
            } else {
                result.push_back(item);
            }
        }
        return Value::fromList(in.newList(std::move(result)));
    });

    interp.defineNative("zip", 2, -1, [](Interpreter& in, Args& args) {
        size_t length = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < args.size(); ++i) length = std::min(length, listArg(args, i, "zip").items.size());
        std::vector<Value> result;
        for (size_t row = 0; row < length; ++row) {
            std::vector<Value> tuple;
            for (const Value& list : args) tuple.push_back(list.asList().items[row]);
            result.push_back(Value::fromList(in.newList(std::move(tuple))));
        }
        return Value::fromList(in.newList(std::move(result)));
    });

    interp.defineNative("keys", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> keys;
        for (const auto& entry : mapArg(args, 0, "keys").entries) keys.push_back(entry.first);
        return Value::fromList(in.newList(std::move(keys)));
    });

    interp.defineNative("values", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> values;
        for (const auto& entry : mapArg(args, 0, "values").entries) values.push_back(entry.second);
        return Value::fromList(in.newList(std::move(values)));
    });

    interp.defineNative("items", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> pairs;
        for (const auto& entry : mapArg(args, 0, "items").entries) {
            pairs.push_back(Value::fromList(in.newList({entry.first, entry.second})));
        }
        return Value::fromList(in.newList(std::move(pairs)));
    });

    interp.defineNative("merge", 1, -1, [](Interpreter& in, Args& args) {
        auto result = in.newMap();
        for (size_t i = 0; i < args.size(); ++i) {
            for (const auto& entry : mapArg(args, i, "merge").entries) result->set(entry.first, entry.second);
        }
        return Value::fromMap(result);
    });

    interp.defineNative("get", 2, 3, [](Interpreter&, Args& args) {
        Value fallback = args.size() > 2 ? args[2] : Value();
        if (args[0].isMap()) {
            const Value* value = args[0].asMap().find(args[1]);
            return value != nullptr ? *value : fallback;
        }
        if (!args[0].isList() && !args[0].isString()) typeError("get", 0, "a map, list or string", args[0]);
        if (!args[1].isInt()) typeError("get", 1, "an integer index", args[1]);
        size_t length = args[0].isList() ? args[0].asList().items.size() : utf8Length(args[0].asString());
        int64_t index = 0;
        if (!toInt64(args[1], index)) return fallback;
        if (index < 0) index += static_cast<int64_t>(length);
        if (index < 0 || index >= static_cast<int64_t>(length)) return fallback;
        if (args[0].isList()) return args[0].asList().items[static_cast<size_t>(index)];
        size_t pos = utf8Offset(args[0].asString(), static_cast<size_t>(index));
        return Value::fromChar(decodeUtf8(args[0].asString(), pos));
    });

    interp.defineNative("copy", 1, 1, [](Interpreter& in, Args& args) {
        if (args[0].isList()) return Value::fromList(in.newList(args[0].asList().items));
        if (args[0].isMap()) {
            auto copy = in.newMap();
            for (const auto& entry : args[0].asMap().entries) copy->set(entry.first, entry.second);
            return Value::fromMap(copy);
        }
        return args[0];
    });

    interp.defineNative("map", 2, 2, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "map").items;
        const Value& fn = functionArg(args, 1, "map");
        std::vector<Value> result;
        result.reserve(items.size());
        for (const Value& item : items) result.push_back(in.call(fn, {item}));
        return Value::fromList(in.newList(std::move(result)));
    });

    interp.defineNative("filter", 2, 2, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "filter").items;
        const Value& fn = functionArg(args, 1, "filter");
        std::vector<Value> result;
        for (const Value& item : items) {
            if (in.call(fn, {item}).truthy()) result.push_back(item);
        }
        return Value::fromList(in.newList(std::move(result)));
    });

    interp.defineNative("reduce", 2, 3, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "reduce").items;
        const Value& fn = functionArg(args, 1, "reduce");
        size_t start = 0;
        Value accumulator;
        if (args.size() > 2) {
            accumulator = args[2];
        } else {
            if (items.empty()) throw RuntimeError("reduce() of an empty list with no initial value");
            accumulator = items[0];
            start = 1;
        }
        for (size_t i = start; i < items.size(); ++i) accumulator = in.call(fn, {accumulator, items[i]});
        return accumulator;
    });

    interp.defineNative("find", 2, 2, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "find").items;
        const Value& fn = functionArg(args, 1, "find");
        for (const Value& item : items) {
            if (in.call(fn, {item}).truthy()) return item;
        }
        return Value();
    });

    interp.defineNative("any", 1, 2, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "any").items;
        for (const Value& item : items) {
            if (args.size() > 1 ? in.call(functionArg(args, 1, "any"), {item}).truthy() : item.truthy()) {
                return Value::fromBool(true);
            }
        }
        return Value::fromBool(false);
    });

    interp.defineNative("all", 1, 2, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "all").items;
        for (const Value& item : items) {
            if (!(args.size() > 1 ? in.call(functionArg(args, 1, "all"), {item}).truthy() : item.truthy())) {
                return Value::fromBool(false);
            }
        }
        return Value::fromBool(true);
    });
}

void registerStrings(Interpreter& interp) {
    interp.defineNative("join", 1, 2, [](Interpreter&, Args& args) {
        const auto& items = listArg(args, 0, "join").items;
        std::string separator = args.size() > 1 ? textArg(args, 1, "join") : "";
        std::string result;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) result += separator;
            result += toString(items[i]);
        }
        return Value::fromString(result);
    });

    interp.defineNative("split", 1, 2, [](Interpreter& in, Args& args) {
        const std::string& text = stringArg(args, 0, "split");
        std::vector<Value> parts;
        if (args.size() < 2 || args[1].isNull()) {
            // Split on runs of whitespace, ignoring leading and trailing space.
            size_t i = 0;
            while (i < text.size()) {
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                size_t start = i;
                while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
                if (i > start) parts.push_back(Value::fromString(text.substr(start, i - start)));
            }
        } else {
            std::string separator = textArg(args, 1, "split");
            if (separator.empty()) {
                for (char32_t c : decodeAll(text)) parts.push_back(Value::fromString(encodeUtf8(c)));
            } else {
                size_t start = 0;
                while (true) {
                    size_t found = text.find(separator, start);
                    if (found == std::string::npos) break;
                    parts.push_back(Value::fromString(text.substr(start, found - start)));
                    start = found + separator.size();
                }
                parts.push_back(Value::fromString(text.substr(start)));
            }
        }
        return Value::fromList(in.newList(std::move(parts)));
    });

    interp.defineNative("lines", 1, 1, [](Interpreter& in, Args& args) {
        return Value::fromList(in.newList(splitLines(stringArg(args, 0, "lines"))));
    });

    interp.defineNative("chars", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> chars;
        for (char32_t c : decodeAll(stringArg(args, 0, "chars"))) chars.push_back(Value::fromChar(c));
        return Value::fromList(in.newList(std::move(chars)));
    });

    interp.defineNative("bytes", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> bytes;
        for (char c : textArg(args, 0, "bytes")) bytes.push_back(Value::fromInt(static_cast<unsigned char>(c), NumKind::U8));
        return Value::fromList(in.newList(std::move(bytes)));
    });

    interp.defineNative("from_bytes", 1, 1, [](Interpreter&, Args& args) {
        std::string text;
        for (const Value& item : listArg(args, 0, "from_bytes").items) {
            int64_t byte = -1;
            if (!toInt64(item, byte) || byte < 0 || byte > 255) {
                throw RuntimeError("from_bytes() needs a list of numbers from 0 to 255, found " + toRepr(item));
            }
            text += static_cast<char>(byte);
        }
        return Value::fromString(text);
    });

    interp.defineNative("upper", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isChar()) {
            char32_t c = args[0].asChar();
            return Value::fromChar(c < 0x80 ? static_cast<char32_t>(std::toupper(static_cast<int>(c))) : c);
        }
        return Value::fromString(toUpper(stringArg(args, 0, "upper")));
    });

    interp.defineNative("lower", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isChar()) {
            char32_t c = args[0].asChar();
            return Value::fromChar(c < 0x80 ? static_cast<char32_t>(std::tolower(static_cast<int>(c))) : c);
        }
        return Value::fromString(toLower(stringArg(args, 0, "lower")));
    });

    interp.defineNative("trim", 1, 1, [](Interpreter&, Args& args) { return Value::fromString(trim(stringArg(args, 0, "trim"))); });

    interp.defineNative("replace", 3, 3, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "replace");
        std::string from = textArg(args, 1, "replace");
        std::string to = textArg(args, 2, "replace");
        if (from.empty()) throw RuntimeError("replace() can't replace an empty string");
        std::string result;
        size_t start = 0;
        while (true) {
            size_t found = text.find(from, start);
            if (found == std::string::npos) break;
            result.append(text, start, found - start);
            result += to;
            start = found + from.size();
        }
        result.append(text, start, std::string::npos);
        return Value::fromString(result);
    });

    interp.defineNative("starts_with", 2, 2, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "starts_with");
        std::string prefix = textArg(args, 1, "starts_with");
        return Value::fromBool(text.compare(0, prefix.size(), prefix) == 0);
    });

    interp.defineNative("ends_with", 2, 2, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "ends_with");
        std::string suffix = textArg(args, 1, "ends_with");
        return Value::fromBool(text.size() >= suffix.size() &&
                               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
    });

    interp.defineNative("pad_left", 2, 3, [](Interpreter&, Args& args) { return Value::fromString(padText(args, "pad_left", true)); });
    interp.defineNative("pad_right", 2, 3, [](Interpreter&, Args& args) { return Value::fromString(padText(args, "pad_right", false)); });

    interp.defineNative("is_digit", 1, 1, [](Interpreter&, Args& args) { return classify(args, "is_digit", std::isdigit); });
    interp.defineNative("is_alpha", 1, 1, [](Interpreter&, Args& args) { return classify(args, "is_alpha", std::isalpha); });
    interp.defineNative("is_alnum", 1, 1, [](Interpreter&, Args& args) { return classify(args, "is_alnum", std::isalnum); });
    interp.defineNative("is_space", 1, 1, [](Interpreter&, Args& args) { return classify(args, "is_space", std::isspace); });
    interp.defineNative("is_upper", 1, 1, [](Interpreter&, Args& args) { return classify(args, "is_upper", std::isupper); });
    interp.defineNative("is_lower", 1, 1, [](Interpreter&, Args& args) { return classify(args, "is_lower", std::islower); });

    interp.defineNative("ord", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isChar()) return Value::fromInt(static_cast<int64_t>(args[0].asChar()));
        const std::string& text = stringArg(args, 0, "ord");
        if (utf8Length(text) != 1) {
            throw RuntimeError("ord() expects a single character, got a string of length " + std::to_string(utf8Length(text)));
        }
        size_t pos = 0;
        return Value::fromInt(static_cast<int64_t>(decodeUtf8(text, pos)));
    });

    interp.defineNative("chr", 1, 1, [](Interpreter&, Args& args) {
        int64_t code = intArg(args, 0, "chr");
        if (code < 0 || code > 0x10FFFF || !isValidCodePoint(static_cast<char32_t>(code))) {
            throw RuntimeError("chr(): " + std::to_string(code) + " is not a valid character code");
        }
        return Value::fromChar(static_cast<char32_t>(code));
    });
}

void registerMath(Interpreter& interp) {
    interp.defineBuiltin("PI", Value::fromFloat(3.14159265358979323846));
    interp.defineBuiltin("E", Value::fromFloat(2.71828182845904523536));
    interp.defineBuiltin("INF", Value::fromFloat(std::numeric_limits<double>::infinity()));
    interp.defineBuiltin("NAN", Value::fromFloat(std::numeric_limits<double>::quiet_NaN()));

    interp.defineNative("abs", 1, 1, [](Interpreter&, Args& args) {
        if (!args[0].isNumber()) typeError("abs", 0, "a number", args[0]);
        if (args[0].isInt() ? args[0].isNegativeInt() : std::signbit(args[0].asFloat())) return negateNumber(args[0]);
        return args[0];
    });

    interp.defineNative("min", 1, -1, [](Interpreter&, Args& args) { return extreme(args, "min", -1); });
    interp.defineNative("max", 1, -1, [](Interpreter&, Args& args) { return extreme(args, "max", 1); });

    interp.defineNative("clamp", 3, 3, [](Interpreter&, Args& args) {
        for (size_t i = 0; i < 3; ++i) numberArg(args, i, "clamp");
        if (compareNumbers(args[1], args[2]) > 0) throw RuntimeError("clamp() needs low <= high");
        if (compareNumbers(args[0], args[1]) < 0) return args[1];
        if (compareNumbers(args[0], args[2]) > 0) return args[2];
        return args[0];
    });

    interp.defineNative("sum", 1, 1, [](Interpreter& in, Args& args) {
        // Small integer types are widened so totals don't overflow: the sum of
        // a list of u8 values is an int.
        Value total = Value::fromInt(0);
        for (const Value& item : listArg(args, 0, "sum").items) {
            if (!item.isNumber()) {
                throw RuntimeError(std::string("sum() can only add numbers, but the list contains ") + article(item));
            }
            int64_t small = 0;
            Value widened = (item.isInt() && toInt64(item, small)) ? Value::fromInt(small) : item;
            total = in.binaryOp(TokenType::Plus, total, widened);
        }
        return total;
    });

    interp.defineNative("sign", 1, 1, [](Interpreter&, Args& args) {
        numberArg(args, 0, "sign");
        if (args[0].isFloat() && std::isnan(args[0].asFloat())) return args[0];
        int order = compareNumbers(args[0], Value::fromInt(0));
        return Value::fromInt(order);
    });

    interp.defineNative("gcd", 2, 2, [](Interpreter&, Args& args) {
        int64_t a = intArg(args, 0, "gcd");
        int64_t b = intArg(args, 1, "gcd");
        uint64_t x = a < 0 ? uint64_t(0) - static_cast<uint64_t>(a) : static_cast<uint64_t>(a);
        uint64_t y = b < 0 ? uint64_t(0) - static_cast<uint64_t>(b) : static_cast<uint64_t>(b);
        while (y != 0) {
            uint64_t r = x % y;
            x = y;
            y = r;
        }
        if (x > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return Value::fromUInt(x);
        return Value::fromInt(static_cast<int64_t>(x));
    });

    interp.defineNative("sqrt", 1, 1, [](Interpreter&, Args& args) {
        double value = numberArg(args, 0, "sqrt");
        if (value < 0) throw RuntimeError("sqrt() of a negative number");
        return Value::fromFloat(std::sqrt(value));
    });

    interp.defineNative("pow", 2, 2, [](Interpreter& in, Args& args) {
        numberArg(args, 0, "pow");
        numberArg(args, 1, "pow");
        return in.binaryOp(TokenType::StarStar, args[0], args[1]);
    });

    interp.defineNative("log", 1, 2, [](Interpreter&, Args& args) {
        double value = numberArg(args, 0, "log");
        if (value <= 0) throw RuntimeError("log() of a number that isn't positive");
        if (args.size() == 1) return Value::fromFloat(std::log(value));
        double base = numberArg(args, 1, "log");
        if (base <= 0 || base == 1) throw RuntimeError("log() base must be positive and not 1");
        return Value::fromFloat(std::log(value) / std::log(base));
    });

    struct MathFunction {
        const char* name;
        double (*fn)(double);
    };
    static const MathFunction functions[] = {
        {"sin", [](double x) { return std::sin(x); }},     {"cos", [](double x) { return std::cos(x); }},
        {"tan", [](double x) { return std::tan(x); }},     {"asin", [](double x) { return std::asin(x); }},
        {"acos", [](double x) { return std::acos(x); }},   {"atan", [](double x) { return std::atan(x); }},
        {"exp", [](double x) { return std::exp(x); }},     {"log2", [](double x) { return std::log2(x); }},
        {"log10", [](double x) { return std::log10(x); }}, {"cbrt", [](double x) { return std::cbrt(x); }},
    };
    for (const auto& function : functions) {
        const char* name = function.name;
        double (*fn)(double) = function.fn;
        interp.defineNative(name, 1, 1, [name, fn](Interpreter&, Args& args) {
            return Value::fromFloat(fn(numberArg(args, 0, name)));
        });
    }

    interp.defineNative("atan2", 2, 2, [](Interpreter&, Args& args) {
        return Value::fromFloat(std::atan2(numberArg(args, 0, "atan2"), numberArg(args, 1, "atan2")));
    });

    interp.defineNative("hypot", 2, 2, [](Interpreter&, Args& args) {
        return Value::fromFloat(std::hypot(numberArg(args, 0, "hypot"), numberArg(args, 1, "hypot")));
    });

    interp.defineNative("is_nan", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromBool(std::isnan(numberArg(args, 0, "is_nan")));
    });

    interp.defineNative("is_inf", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromBool(std::isinf(numberArg(args, 0, "is_inf")));
    });

    interp.defineNative("floor", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isInt()) return args[0];
        return convertNumberOrThrow(Value::fromFloat(std::floor(numberArg(args, 0, "floor"))), NumKind::Int);
    });

    interp.defineNative("ceil", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isInt()) return args[0];
        return convertNumberOrThrow(Value::fromFloat(std::ceil(numberArg(args, 0, "ceil"))), NumKind::Int);
    });

    interp.defineNative("round", 1, 2, [](Interpreter&, Args& args) {
        double value = numberArg(args, 0, "round");
        if (args.size() == 1) {
            if (args[0].isInt()) return args[0];
            return convertNumberOrThrow(Value::fromFloat(std::round(value)), NumKind::Int);
        }
        double factor = std::pow(10.0, static_cast<double>(intArg(args, 1, "round")));
        NumKind kind = args[0].isFloat() ? args[0].numKind() : NumKind::Float;
        return Value::fromFloat(std::round(value * factor) / factor, kind);
    });

    interp.defineNative("random", 0, 0, [](Interpreter& in, Args&) {
        return Value::fromFloat(std::uniform_real_distribution<double>(0.0, 1.0)(in.random()));
    });

    interp.defineNative("randint", 2, 2, [](Interpreter& in, Args& args) {
        int64_t low = intArg(args, 0, "randint");
        int64_t high = intArg(args, 1, "randint");
        if (low > high) throw RuntimeError("randint() needs low <= high");
        return Value::fromInt(std::uniform_int_distribution<int64_t>(low, high)(in.random()));
    });

    interp.defineNative("choice", 1, 1, [](Interpreter& in, Args& args) {
        const auto& items = listArg(args, 0, "choice").items;
        if (items.empty()) throw RuntimeError("choice() from an empty list");
        return items[std::uniform_int_distribution<size_t>(0, items.size() - 1)(in.random())];
    });

    interp.defineNative("shuffle", 1, 1, [](Interpreter& in, Args& args) {
        std::vector<Value> items = listArg(args, 0, "shuffle").items;
        std::shuffle(items.begin(), items.end(), in.random());
        return Value::fromList(in.newList(std::move(items)));
    });

    interp.defineNative("seed", 1, 1, [](Interpreter& in, Args& args) {
        in.random().seed(static_cast<uint64_t>(intArg(args, 0, "seed")));
        return Value();
    });
}

void registerSystem(Interpreter& interp) {
    interp.defineNative("time", 0, 0, [](Interpreter&, Args&) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return Value::fromFloat(std::chrono::duration<double>(now).count());
    });

    interp.defineNative("clock", 0, 0, [](Interpreter&, Args&) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return Value::fromFloat(std::chrono::duration<double>(now).count());
    });

    interp.defineNative("sleep", 1, 1, [](Interpreter&, Args& args) {
        double seconds = numberArg(args, 0, "sleep");
        if (seconds > 0) std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        return Value();
    });

    interp.defineNative("date", 0, 1, [](Interpreter&, Args& args) {
        std::string format = args.empty() ? "%Y-%m-%d %H:%M:%S" : stringArg(args, 0, "date");
        std::time_t now = std::time(nullptr);
        std::tm local = *std::localtime(&now);
        char buffer[256];
        size_t length = std::strftime(buffer, sizeof buffer, format.c_str(), &local);
        if (length == 0 && !format.empty()) throw RuntimeError("date(): the formatted date is too long or the format is invalid");
        return Value::fromString(std::string(buffer, length));
    });

    interp.defineNative("env", 1, 1, [](Interpreter&, Args& args) {
        const char* value = std::getenv(stringArg(args, 0, "env").c_str());
        return value == nullptr ? Value() : Value::fromString(value);
    });

    interp.defineNative("system", 1, 1, [](Interpreter& in, Args& args) {
        in.out().flush();
        int status = std::system(stringArg(args, 0, "system").c_str());
#if defined(__unix__) || defined(__APPLE__)
        if (status != -1 && WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
        return Value::fromInt(status);
    });
}

void registerFiles(Interpreter& interp) {
    interp.defineNative("read_file", 1, 1, [](Interpreter&, Args& args) {
        const std::string& path = stringArg(args, 0, "read_file");
        std::string text;
        if (!readTextFile(path, text)) throw RuntimeError("read_file(): can't open '" + path + "'");
        return Value::fromString(text);
    });

    interp.defineNative("read_lines", 1, 1, [](Interpreter& in, Args& args) {
        const std::string& path = stringArg(args, 0, "read_lines");
        std::string text;
        if (!readTextFile(path, text)) throw RuntimeError("read_lines(): can't open '" + path + "'");
        return Value::fromList(in.newList(splitLines(text)));
    });

    auto writer = [](const char* name, std::ios::openmode mode) {
        return [name, mode](Interpreter&, Args& args) {
            const std::string& path = stringArg(args, 0, name);
            std::ofstream file(path, mode);
            if (!file) throw RuntimeError(std::string(name) + "(): can't open '" + path + "' for writing");
            file << toString(args[1]);
            if (!file) throw RuntimeError(std::string(name) + "(): writing to '" + path + "' failed");
            return Value();
        };
    };
    interp.defineNative("write_file", 2, 2, writer("write_file", std::ios::binary | std::ios::trunc));
    interp.defineNative("append_file", 2, 2, writer("append_file", std::ios::binary | std::ios::app));

    interp.defineNative("file_exists", 1, 1, [](Interpreter&, Args& args) {
        std::ifstream file(stringArg(args, 0, "file_exists"));
        return Value::fromBool(static_cast<bool>(file));
    });

    interp.defineNative("delete_file", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromBool(std::remove(stringArg(args, 0, "delete_file").c_str()) == 0);
    });

    interp.defineNative("json_parse", 1, 1, [](Interpreter& in, Args& args) {
        return jsonParse(in, stringArg(args, 0, "json_parse"));
    });

    interp.defineNative("json_stringify", 1, 2, [](Interpreter&, Args& args) {
        int64_t indent = args.size() > 1 ? intArg(args, 1, "json_stringify") : 0;
        if (indent < 0 || indent > 16) throw RuntimeError("json_stringify() indent must be from 0 to 16");
        return Value::fromString(jsonStringify(args[0], static_cast<int>(indent)));
    });
}

}  // namespace

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

RangeSpec makeRange(const std::vector<Value>& args) {
    if (args.empty() || args.size() > 3) {
        throw RuntimeError("range() expects 1 to 3 arguments but got " + std::to_string(args.size()));
    }
    RangeSpec range;
    int64_t stop;
    if (args.size() == 1) {
        stop = intArg(args, 0, "range");
    } else {
        range.start = intArg(args, 0, "range");
        stop = intArg(args, 1, "range");
        if (args.size() == 3) range.step = intArg(args, 2, "range");
    }
    if (range.step == 0) throw RuntimeError("range() step can't be zero");

    const uint64_t start = static_cast<uint64_t>(range.start);
    const uint64_t end = static_cast<uint64_t>(stop);
    if (range.step > 0 && range.start < stop) {
        range.count = (end - start - 1) / static_cast<uint64_t>(range.step) + 1;
    } else if (range.step < 0 && range.start > stop) {
        range.count = (start - end - 1) / (uint64_t(0) - static_cast<uint64_t>(range.step)) + 1;
    }
    return range;
}

void registerStandardLibrary(Interpreter& interpreter) {
    registerCore(interpreter);
    registerConversions(interpreter);
    registerCollections(interpreter);
    registerStrings(interpreter);
    registerMath(interpreter);
    registerSystem(interpreter);
    registerFiles(interpreter);
}

}  // namespace ascend

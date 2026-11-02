#include "stdlib.hpp"

#include "interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>

namespace ascend {

namespace {

using Args = std::vector<Value>;

constexpr uint64_t kMaxRangeList = 10000000;

[[noreturn]] void typeError(const char* fn, size_t index, const char* expected, const Value& got) {
    throw RuntimeError(std::string(fn) + "() expects " + expected + " as argument " + std::to_string(index + 1) +
                       ", not " + (got.isNull() ? "null" : std::string("a ") + got.typeName()));
}

int64_t intArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isInt()) typeError(fn, i, "an int", args[i]);
    return args[i].asInt();
}

double numberArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isNumber()) typeError(fn, i, "a number", args[i]);
    return args[i].toDouble();
}

const std::string& stringArg(const Args& args, size_t i, const char* fn) {
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

int64_t floatToInt(double value, const char* fn) {
    if (!std::isfinite(value)) {
        throw RuntimeError(std::string(fn) + "() can't convert " + formatFloat(value) + " to an int");
    }
    if (value >= 9223372036854775808.0 || value < -9223372036854775808.0) {
        throw RuntimeError(std::string(fn) + "(): " + formatFloat(value) + " is too large for an int");
    }
    return static_cast<int64_t>(value);
}

std::string trim(const std::string& text) {
    const char* space = " \t\r\n\f\v";
    size_t first = text.find_first_not_of(space);
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(space);
    return text.substr(first, last - first + 1);
}

bool parseInt(const std::string& text, int64_t& out) {
    std::string s = trim(text);
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long long value = std::strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE || end != s.c_str() + s.size()) return false;
    out = value;
    return true;
}

bool parseFloat(const std::string& text, double& out) {
    std::string s = trim(text);
    if (s.empty()) return false;
    char* end = nullptr;
    double value = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    out = value;
    return true;
}

std::string toUpper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string toLower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
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

    interp.defineNative("repr", 1, 1, [](Interpreter&, Args& args) { return Value::fromString(toRepr(args[0])); });

    interp.defineNative("int", 1, 1, [](Interpreter&, Args& args) {
        const Value& value = args[0];
        switch (value.type()) {
            case Value::Type::Int: return value;
            case Value::Type::Float: return Value::fromInt(floatToInt(value.asFloat(), "int"));
            case Value::Type::Bool: return Value::fromInt(value.asBool() ? 1 : 0);
            case Value::Type::String: {
                int64_t result;
                if (parseInt(value.asString(), result)) return Value::fromInt(result);
                throw RuntimeError("int() can't convert " + quoteString(value.asString()) + " to an int");
            }
            default:
                throw RuntimeError(std::string("int() can't convert a ") + value.typeName() + " to an int");
        }
    });

    interp.defineNative("float", 1, 1, [](Interpreter&, Args& args) {
        const Value& value = args[0];
        switch (value.type()) {
            case Value::Type::Int:
            case Value::Type::Float: return Value::fromFloat(value.toDouble());
            case Value::Type::Bool: return Value::fromFloat(value.asBool() ? 1.0 : 0.0);
            case Value::Type::String: {
                double result;
                if (parseFloat(value.asString(), result)) return Value::fromFloat(result);
                throw RuntimeError("float() can't convert " + quoteString(value.asString()) + " to a float");
            }
            default:
                throw RuntimeError(std::string("float() can't convert a ") + value.typeName() + " to a float");
        }
    });

    interp.defineNative("bool", 1, 1, [](Interpreter&, Args& args) { return Value::fromBool(args[0].truthy()); });

    interp.defineNative("len", 1, 1, [](Interpreter&, Args& args) {
        const Value& value = args[0];
        switch (value.type()) {
            case Value::Type::String: return Value::fromInt(static_cast<int64_t>(value.asString().size()));
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

    interp.defineNative("time", 0, 0, [](Interpreter&, Args&) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return Value::fromFloat(std::chrono::duration<double>(now).count());
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
            throw RuntimeError("insert() index " + std::to_string(args[1].asInt()) + " is out of range (length " +
                               std::to_string(size) + ")");
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

    interp.defineNative("contains", 2, 2, [](Interpreter& in, Args& args) {
        return Value::fromBool(in.contains(args[0], args[1]));
    });

    interp.defineNative("index_of", 2, 2, [](Interpreter&, Args& args) {
        if (args[0].isString()) {
            size_t position = args[0].asString().find(stringArg(args, 1, "index_of"));
            return Value::fromInt(position == std::string::npos ? -1 : static_cast<int64_t>(position));
        }
        const auto& items = listArg(args, 0, "index_of").items;
        for (size_t i = 0; i < items.size(); ++i) {
            if (valuesEqual(items[i], args[1])) return Value::fromInt(static_cast<int64_t>(i));
        }
        return Value::fromInt(-1);
    });

    interp.defineNative("slice", 2, 3, [](Interpreter& in, Args& args) {
        return in.slice(args[0], args[1], args.size() > 2 ? args[2] : Value());
    });

    interp.defineNative("reverse", 1, 1, [](Interpreter& in, Args& args) {
        if (args[0].isString()) {
            std::string text = args[0].asString();
            std::reverse(text.begin(), text.end());
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
            std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
                return compareValues(a.first, b.first) < 0;
            });
            for (size_t i = 0; i < keyed.size(); ++i) items[i] = std::move(keyed[i].second);
        } else {
            std::stable_sort(items.begin(), items.end(),
                             [](const Value& a, const Value& b) { return compareValues(a, b) < 0; });
        }
        return Value::fromList(in.newList(std::move(items)));
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

    interp.defineNative("get", 2, 3, [](Interpreter&, Args& args) {
        Value fallback = args.size() > 2 ? args[2] : Value();
        if (args[0].isMap()) {
            const Value* value = args[0].asMap().find(args[1]);
            return value != nullptr ? *value : fallback;
        }
        if (!args[0].isList() && !args[0].isString()) typeError("get", 0, "a map, list or string", args[0]);
        if (!args[1].isInt()) typeError("get", 1, "an int index", args[1]);
        size_t length = args[0].isList() ? args[0].asList().items.size() : args[0].asString().size();
        int64_t index = args[1].asInt();
        if (index < 0) index += static_cast<int64_t>(length);
        if (index < 0 || index >= static_cast<int64_t>(length)) return fallback;
        if (args[0].isList()) return args[0].asList().items[static_cast<size_t>(index)];
        return Value::fromString(std::string(1, args[0].asString()[static_cast<size_t>(index)]));
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
}

void registerStrings(Interpreter& interp) {
    interp.defineNative("join", 1, 2, [](Interpreter&, Args& args) {
        const auto& items = listArg(args, 0, "join").items;
        std::string separator = args.size() > 1 ? stringArg(args, 1, "join") : "";
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
            const std::string& separator = stringArg(args, 1, "split");
            if (separator.empty()) {
                for (char c : text) parts.push_back(Value::fromString(std::string(1, c)));
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

    interp.defineNative("upper", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(toUpper(stringArg(args, 0, "upper")));
    });

    interp.defineNative("lower", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(toLower(stringArg(args, 0, "lower")));
    });

    interp.defineNative("trim", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(trim(stringArg(args, 0, "trim")));
    });

    interp.defineNative("replace", 3, 3, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "replace");
        const std::string& from = stringArg(args, 1, "replace");
        const std::string& to = stringArg(args, 2, "replace");
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
        const std::string& prefix = stringArg(args, 1, "starts_with");
        return Value::fromBool(text.compare(0, prefix.size(), prefix) == 0);
    });

    interp.defineNative("ends_with", 2, 2, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "ends_with");
        const std::string& suffix = stringArg(args, 1, "ends_with");
        return Value::fromBool(text.size() >= suffix.size() &&
                               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
    });

    interp.defineNative("ord", 1, 1, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "ord");
        if (text.size() != 1) throw RuntimeError("ord() expects a single character, got a string of length " + std::to_string(text.size()));
        return Value::fromInt(static_cast<unsigned char>(text[0]));
    });

    interp.defineNative("chr", 1, 1, [](Interpreter&, Args& args) {
        int64_t code = intArg(args, 0, "chr");
        if (code < 0 || code > 255) throw RuntimeError("chr() expects a number from 0 to 255, got " + std::to_string(code));
        return Value::fromString(std::string(1, static_cast<char>(code)));
    });
}

void registerMath(Interpreter& interp) {
    interp.defineGlobal("PI", Value::fromFloat(3.14159265358979323846));
    interp.defineGlobal("E", Value::fromFloat(2.71828182845904523536));

    interp.defineNative("abs", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isInt()) {
            int64_t value = args[0].asInt();
            if (value == std::numeric_limits<int64_t>::min()) throw RuntimeError("integer overflow in abs()");
            return Value::fromInt(value < 0 ? -value : value);
        }
        return Value::fromFloat(std::fabs(numberArg(args, 0, "abs")));
    });

    interp.defineNative("min", 1, -1, [](Interpreter&, Args& args) { return extreme(args, "min", -1); });
    interp.defineNative("max", 1, -1, [](Interpreter&, Args& args) { return extreme(args, "max", 1); });

    interp.defineNative("sum", 1, 1, [](Interpreter& in, Args& args) {
        Value total = Value::fromInt(0);
        for (const Value& item : listArg(args, 0, "sum").items) {
            if (!item.isNumber()) {
                throw RuntimeError(std::string("sum() can only add numbers, but the list contains a ") + item.typeName());
            }
            total = in.binaryOp(TokenType::Plus, total, item);
        }
        return total;
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

    interp.defineNative("floor", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isInt()) return args[0];
        return Value::fromInt(floatToInt(std::floor(numberArg(args, 0, "floor")), "floor"));
    });

    interp.defineNative("ceil", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isInt()) return args[0];
        return Value::fromInt(floatToInt(std::ceil(numberArg(args, 0, "ceil")), "ceil"));
    });

    interp.defineNative("round", 1, 2, [](Interpreter&, Args& args) {
        double value = numberArg(args, 0, "round");
        if (args.size() == 1) {
            if (args[0].isInt()) return args[0];
            return Value::fromInt(floatToInt(std::round(value), "round"));
        }
        double factor = std::pow(10.0, static_cast<double>(intArg(args, 1, "round")));
        return Value::fromFloat(std::round(value * factor) / factor);
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

    interp.defineNative("seed", 1, 1, [](Interpreter& in, Args& args) {
        in.random().seed(static_cast<uint64_t>(intArg(args, 0, "seed")));
        return Value();
    });
}

}  // namespace

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
    registerCollections(interpreter);
    registerStrings(interpreter);
    registerMath(interpreter);
}

}  // namespace ascend

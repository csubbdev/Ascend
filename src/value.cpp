#include "value.hpp"

#include "environment.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ascend {

namespace {

constexpr int kMaxNesting = 1000;

void checkNesting(int depth) {
    if (depth > kMaxNesting) throw RuntimeError("value is nested too deeply (is it cyclic?)");
}

void write(std::string& out, const Value& value, bool repr, std::vector<const void*>& active) {
    switch (value.type()) {
        case Value::Type::Null: out += "null"; return;
        case Value::Type::Bool: out += value.asBool() ? "true" : "false"; return;
        case Value::Type::Int: out += std::to_string(value.asInt()); return;
        case Value::Type::Float: out += formatFloat(value.asFloat()); return;
        case Value::Type::String:
            out += repr ? quoteString(value.asString()) : value.asString();
            return;
        case Value::Type::List: {
            const ListObj* list = value.listPtr().get();
            if (std::find(active.begin(), active.end(), list) != active.end()) {
                out += "[...]";
                return;
            }
            checkNesting(static_cast<int>(active.size()));
            active.push_back(list);
            out += '[';
            for (size_t i = 0; i < list->items.size(); ++i) {
                if (i > 0) out += ", ";
                write(out, list->items[i], true, active);
            }
            out += ']';
            active.pop_back();
            return;
        }
        case Value::Type::Map: {
            const MapObj* map = value.mapPtr().get();
            if (std::find(active.begin(), active.end(), map) != active.end()) {
                out += "{...}";
                return;
            }
            checkNesting(static_cast<int>(active.size()));
            active.push_back(map);
            out += '{';
            bool first = true;
            for (const auto& entry : map->entries) {
                if (!first) out += ", ";
                first = false;
                write(out, entry.first, true, active);
                out += ": ";
                write(out, entry.second, true, active);
            }
            out += '}';
            active.pop_back();
            return;
        }
        case Value::Type::Function: {
            const Callable& fn = value.asFunction();
            std::string name = functionName(fn);
            if (fn.kind == Callable::Kind::Native) {
                out += "<builtin " + name + ">";
            } else {
                out += name.empty() ? "<func>" : "<func " + name + ">";
            }
            return;
        }
    }
}

bool equalAt(const Value& a, const Value& b, int depth) {
    if (a.isNumber() && b.isNumber()) {
        if (a.isInt() && b.isInt()) return a.asInt() == b.asInt();
        return a.toDouble() == b.toDouble();
    }
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Value::Type::Null: return true;
        case Value::Type::Bool: return a.asBool() == b.asBool();
        case Value::Type::String: return a.asString() == b.asString();
        case Value::Type::List: {
            if (a.listPtr() == b.listPtr()) return true;
            checkNesting(depth);
            const auto& x = a.asList().items;
            const auto& y = b.asList().items;
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); ++i) {
                if (!equalAt(x[i], y[i], depth + 1)) return false;
            }
            return true;
        }
        case Value::Type::Map: {
            if (a.mapPtr() == b.mapPtr()) return true;
            checkNesting(depth);
            const MapObj& x = a.asMap();
            const MapObj& y = b.asMap();
            if (x.size() != y.size()) return false;
            for (const auto& entry : x.entries) {
                const Value* other = y.find(entry.first);
                if (other == nullptr || !equalAt(entry.second, *other, depth + 1)) return false;
            }
            return true;
        }
        case Value::Type::Function: return &a.asFunction() == &b.asFunction();
        default: return false;
    }
}

int compareAt(const Value& a, const Value& b, int depth) {
    if (a.isInt() && b.isInt()) {
        return a.asInt() < b.asInt() ? -1 : (a.asInt() > b.asInt() ? 1 : 0);
    }
    if (a.isNumber() && b.isNumber()) {
        double x = a.toDouble();
        double y = b.toDouble();
        // NaN sorts before every other number so sorting stays well defined.
        bool xNan = std::isnan(x);
        bool yNan = std::isnan(y);
        if (xNan || yNan) return xNan == yNan ? 0 : (xNan ? -1 : 1);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (a.isString() && b.isString()) {
        int result = a.asString().compare(b.asString());
        return result < 0 ? -1 : (result > 0 ? 1 : 0);
    }
    if (a.isList() && b.isList()) {
        checkNesting(depth);
        const auto& x = a.asList().items;
        const auto& y = b.asList().items;
        for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
            int result = compareAt(x[i], y[i], depth + 1);
            if (result != 0) return result;
        }
        return x.size() < y.size() ? -1 : (x.size() > y.size() ? 1 : 0);
    }
    throw RuntimeError(std::string("can't compare ") + a.typeName() + " with " + b.typeName());
}

}  // namespace

// ---------------------------------------------------------------------------
// Value

Value Value::fromBool(bool value) {
    Value v;
    v.data_.emplace<1>(value);
    return v;
}

Value Value::fromInt(int64_t value) {
    Value v;
    v.data_.emplace<2>(value);
    return v;
}

Value Value::fromFloat(double value) {
    Value v;
    v.data_.emplace<3>(value);
    return v;
}

Value Value::fromString(std::string value) {
    Value v;
    v.data_.emplace<4>(std::move(value));
    return v;
}

Value Value::fromList(std::shared_ptr<ListObj> value) {
    Value v;
    v.data_.emplace<5>(std::move(value));
    return v;
}

Value Value::fromMap(std::shared_ptr<MapObj> value) {
    Value v;
    v.data_.emplace<6>(std::move(value));
    return v;
}

Value Value::fromFunction(std::shared_ptr<Callable> value) {
    Value v;
    v.data_.emplace<7>(std::move(value));
    return v;
}

bool Value::truthy() const {
    switch (type()) {
        case Type::Null: return false;
        case Type::Bool: return asBool();
        case Type::Int: return asInt() != 0;
        case Type::Float: return asFloat() != 0.0;
        case Type::String: return !asString().empty();
        case Type::List: return !asList().items.empty();
        case Type::Map: return asMap().size() != 0;
        case Type::Function: return true;
    }
    return false;
}

const char* Value::typeName() const {
    switch (type()) {
        case Type::Null: return "null";
        case Type::Bool: return "bool";
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::String: return "string";
        case Type::List: return "list";
        case Type::Map: return "map";
        case Type::Function: return "function";
    }
    return "unknown";
}

GcObject* Value::gcObject() const {
    switch (type()) {
        case Type::List: return listPtr().get();
        case Type::Map: return mapPtr().get();
        case Type::Function: return asFunction().gcObject();
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Containers

void ListObj::forEachChild(const std::function<void(GcObject*)>& visit) const {
    for (const Value& item : items) {
        if (GcObject* child = item.gcObject()) visit(child);
    }
}

bool MapObj::hashKey(const Value& key, std::string& out) {
    switch (key.type()) {
        case Value::Type::Null: out = "n"; return true;
        case Value::Type::Bool: out = key.asBool() ? "b1" : "b0"; return true;
        case Value::Type::Int: out = "i" + std::to_string(key.asInt()); return true;
        case Value::Type::Float: {
            double d = key.asFloat();
            if (std::isfinite(d) && d == std::floor(d) && d >= -9223372036854775808.0 &&
                d < 9223372036854775808.0) {
                out = "i" + std::to_string(static_cast<int64_t>(d));  // 1.0 is the same key as 1
                return true;
            }
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof bits);
            out = "f" + std::to_string(bits);
            return true;
        }
        case Value::Type::String:
            out.reserve(key.asString().size() + 1);
            out = "s";
            out += key.asString();
            return true;
        default: return false;
    }
}

const Value* MapObj::find(const Value& key) const {
    std::string hashed;
    if (!hashKey(key, hashed)) return nullptr;
    auto it = index_.find(hashed);
    return it == index_.end() ? nullptr : &entries[it->second].second;
}

Value* MapObj::find(const Value& key) {
    return const_cast<Value*>(static_cast<const MapObj*>(this)->find(key));
}

void MapObj::set(const Value& key, Value value) {
    std::string hashed;
    if (!hashKey(key, hashed)) {
        throw RuntimeError(std::string("a ") + key.typeName() +
                           " can't be used as a map key (use a string, number, bool or null)");
    }
    auto it = index_.find(hashed);
    if (it != index_.end()) {
        entries[it->second].second = std::move(value);
        return;
    }
    index_.emplace(std::move(hashed), entries.size());
    entries.emplace_back(key, std::move(value));
}

bool MapObj::remove(const Value& key, Value* removed) {
    std::string hashed;
    if (!hashKey(key, hashed)) return false;
    auto it = index_.find(hashed);
    if (it == index_.end()) return false;
    size_t position = it->second;
    index_.erase(it);
    if (removed != nullptr) *removed = std::move(entries[position].second);
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(position));
    for (auto& entry : index_) {
        if (entry.second > position) --entry.second;
    }
    return true;
}

void MapObj::forEachChild(const std::function<void(GcObject*)>& visit) const {
    for (const auto& entry : entries) {
        if (GcObject* child = entry.first.gcObject()) visit(child);
        if (GcObject* child = entry.second.gcObject()) visit(child);
    }
}

void MapObj::clearReferences() {
    entries.clear();
    index_.clear();
}

void FunctionObj::forEachChild(const std::function<void(GcObject*)>& visit) const {
    if (closure) visit(closure.get());
}

// ---------------------------------------------------------------------------
// Formatting and comparison

std::string formatFloat(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    // Use the shortest representation that reads back as the same number.
    char buffer[32];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    std::string text = buffer;
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";  // keep floats looking like floats
    return text;
}

std::string quoteString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\0': out += "\\0"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\x%02x", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

std::string functionName(const Callable& callable) {
    if (callable.kind == Callable::Kind::Native) return static_cast<const NativeFunction&>(callable).name;
    return static_cast<const FunctionObj&>(callable).name;
}

std::string toString(const Value& value) {
    if (value.isString()) return value.asString();
    std::string out;
    std::vector<const void*> active;
    write(out, value, false, active);
    return out;
}

std::string toRepr(const Value& value) {
    std::string out;
    std::vector<const void*> active;
    write(out, value, true, active);
    return out;
}

size_t resolveIndex(const Value& index, size_t length, const char* what) {
    if (!index.isInt()) throw RuntimeError(std::string(what) + " index must be an int, not " + index.typeName());
    const int64_t size = static_cast<int64_t>(length);
    int64_t i = index.asInt();
    if (i < 0) i += size;
    if (i < 0 || i >= size) {
        throw RuntimeError(std::string(what) + " index " + std::to_string(index.asInt()) + " is out of range (length " +
                           std::to_string(length) + ")");
    }
    return static_cast<size_t>(i);
}

bool valuesEqual(const Value& a, const Value& b) { return equalAt(a, b, 0); }

int compareValues(const Value& a, const Value& b) { return compareAt(a, b, 0); }

}  // namespace ascend

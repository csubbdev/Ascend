#include "typecheck.hpp"

#include "numeric.hpp"
#include "utf8.hpp"

namespace ascend {

std::string describeValue(const Value& value) {
    switch (value.type()) {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return value.asBool() ? "true" : "false";
        case Value::Type::Int:
        case Value::Type::Float: return toString(value);
        case Value::Type::Char: return "the char " + toRepr(value);
        default: return aTypeName(value);
    }
    return "a value";
}

std::string withHint(const std::string& message, const CoerceFailure& failure) {
    return failure.hint.empty() ? message : message + " (" + failure.hint + ")";
}

namespace {

// Guards against lists that contain themselves while checking element types.
int& checkDepth() {
    static thread_local int depth = 0;
    return depth;
}

struct DepthGuard {
    DepthGuard() { ++checkDepth(); }
    ~DepthGuard() { --checkDepth(); }
};

const char* kKeepsTypeHint = "a list or map keeps the element types it was first used with; use copy() for another type";

}  // namespace

Value checkListItem(const ListObj& list, Value value) {
    if (!list.elementType) return value;
    Value converted;
    CoerceFailure failure;
    if (!coerceToType(value, *list.elementType, converted, failure)) {
        throw RuntimeError(withHint("this is a list<" + list.elementType->name() + ">, so it can't hold " + failure.what,
                                    failure));
    }
    return converted;
}

void checkMapEntry(const MapObj& map, const Value& key, Value& value) {
    Value converted;
    CoerceFailure failure;
    std::string type = map.keyType ? "map<" + map.keyType->name() + ", " + map.valueType->name() + ">" : "";
    if (map.keyType && !coerceToType(key, *map.keyType, converted, failure)) {
        throw RuntimeError(withHint("this is a " + type + ", so its keys can't be " + failure.what, failure));
    }
    if (map.valueType) {
        if (!coerceToType(value, *map.valueType, converted, failure)) {
            throw RuntimeError(withHint("this is a " + type + ", so its values can't be " + failure.what, failure));
        }
        value = std::move(converted);
    }
}

bool coerceToType(const Value& value, const TypeSpec& type, Value& out, CoerceFailure& failure) {
    if (type.kind == TypeKind::Any) {
        out = value;
        return true;
    }
    if (value.isNull()) {
        if (type.nullable) {
            out = value;
            return true;
        }
        failure.what = "null";
        if (type.kind != TypeKind::Void) failure.hint = "declare it as " + type.name() + "? to allow null";
        return false;
    }

    failure.what = describeValue(value);
    switch (type.kind) {
        case TypeKind::Bool:
            if (!value.isBool()) break;
            out = value;
            return true;

        case TypeKind::Number: {
            if (!value.isNumber()) {
                if (value.isString()) failure.hint = "use " + type.name() + "(...) to convert a string";
                if (value.isBool()) failure.hint = "use " + type.name() + "(...) to convert a bool";
                if (value.isChar()) failure.hint = "use " + type.name() + "(...) to get the character code";
                break;
            }
            switch (convertNumber(value, type.num, false, out)) {
                case ConvertStatus::Ok: return true;
                case ConvertStatus::OutOfRange:
                    failure.hint = std::string(numKindName(type.num)) + " is " + rangeText(type.num);
                    return false;
                case ConvertStatus::FractionLost:
                    failure.hint = "use " + std::string(numKindName(type.num)) + "(...) to drop the fraction";
                    return false;
                default:
                    return false;
            }
        }

        case TypeKind::Char:
            if (value.isChar()) {
                out = value;
                return true;
            }
            if (value.isString()) failure.hint = "use single quotes for a char, like 'a'";
            break;

        case TypeKind::String:
            if (value.isString()) {
                out = value;
                return true;
            }
            if (value.isChar()) {
                out = Value::fromString(encodeUtf8(value.asChar()));
                return true;
            }
            failure.hint = "use str(...) to convert it to a string";
            break;

        case TypeKind::List: {
            if (!value.isList()) break;
            ListObj& list = value.asList();
            if (type.element) {
                if (list.elementType) {
                    if (!sameType(*list.elementType, *type.element)) {
                        failure.what = "a list<" + list.elementType->name() + ">";
                        failure.hint = kKeepsTypeHint;
                        return false;
                    }
                } else {
                    // Check (and convert) every item, then mark the list as a list<T>.
                    DepthGuard guard;
                    if (checkDepth() > 200) {
                        failure.what = "a list nested too deeply";
                        return false;
                    }
                    std::vector<Value> items;
                    items.reserve(list.items.size());
                    for (const Value& item : list.items) {
                        Value converted;
                        CoerceFailure itemFailure;
                        if (!coerceToType(item, *type.element, converted, itemFailure)) {
                            failure.what = "a list containing " + itemFailure.what;
                            failure.hint = itemFailure.hint;
                            return false;
                        }
                        items.push_back(std::move(converted));
                    }
                    list.items = std::move(items);
                    list.elementType = type.element;
                }
            }
            out = value;
            return true;
        }

        case TypeKind::Map: {
            if (!value.isMap()) break;
            MapObj& map = value.asMap();
            if (type.element && (map.structType || !map.frozenAs.empty())) {
                failure.what = map.structType ? aTypeName(value) : "an enum";  // these keep their own rules
                return false;
            }
            if (type.element) {
                if (map.valueType) {
                    if (!sameType(*map.valueType, *type.element) || !sameType(*map.keyType, *type.key)) {
                        failure.what = "a map<" + map.keyType->name() + ", " + map.valueType->name() + ">";
                        failure.hint = kKeepsTypeHint;
                        return false;
                    }
                } else {
                    DepthGuard guard;
                    if (checkDepth() > 200) {
                        failure.what = "a map nested too deeply";
                        return false;
                    }
                    std::vector<Value> values;
                    values.reserve(map.entries.size());
                    for (const auto& entry : map.entries) {
                        Value converted;
                        CoerceFailure itemFailure;
                        if (!coerceToType(entry.first, *type.key, converted, itemFailure)) {
                            failure.what = "a map with the key " + itemFailure.what;
                            failure.hint = itemFailure.hint;
                            return false;
                        }
                        if (!coerceToType(entry.second, *type.element, converted, itemFailure)) {
                            failure.what = "a map containing " + itemFailure.what;
                            failure.hint = itemFailure.hint;
                            return false;
                        }
                        values.push_back(std::move(converted));
                    }
                    for (size_t i = 0; i < values.size(); ++i) map.entries[i].second = std::move(values[i]);
                    map.keyType = type.key;
                    map.valueType = type.element;
                }
            }
            out = value;
            return true;
        }

        case TypeKind::Function:
            if (!value.isFunction()) break;
            out = value;
            return true;

        case TypeKind::Struct:
            if (value.isMap() && value.asMap().structInfo() && value.asMap().structInfo()->isA(type.structName)) {
                out = value;
                return true;
            }
            if (value.isMap() && !value.asMap().structInfo()) failure.hint = "use " + type.structName + "(...) to make one";
            break;

        default:
            break;
    }
    return false;
}

}  // namespace ascend

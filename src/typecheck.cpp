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

        case TypeKind::List:
            if (!value.isList()) break;
            out = value;
            return true;

        case TypeKind::Map:
            if (!value.isMap()) break;
            out = value;
            return true;

        case TypeKind::Function:
            if (!value.isFunction()) break;
            out = value;
            return true;

        case TypeKind::Struct:
            if (value.isMap() && value.asMap().structInfo && value.asMap().structInfo->name == type.structName) {
                out = value;
                return true;
            }
            if (value.isMap() && !value.asMap().structInfo) failure.hint = "use " + type.structName + "(...) to make one";
            break;

        default:
            break;
    }
    return false;
}

}  // namespace ascend

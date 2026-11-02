#include "types.hpp"

#include <limits>
#include <unordered_map>

namespace ascend {

int bitWidth(NumKind kind) {
    switch (kind) {
        case NumKind::I8:
        case NumKind::U8: return 8;
        case NumKind::I16:
        case NumKind::U16: return 16;
        case NumKind::I32:
        case NumKind::U32:
        case NumKind::F32: return 32;
        default: return 64;
    }
}

int64_t signedMin(NumKind kind) {
    switch (kind) {
        case NumKind::I8: return std::numeric_limits<int8_t>::min();
        case NumKind::I16: return std::numeric_limits<int16_t>::min();
        case NumKind::I32: return std::numeric_limits<int32_t>::min();
        default: return std::numeric_limits<int64_t>::min();
    }
}

int64_t signedMax(NumKind kind) {
    switch (kind) {
        case NumKind::I8: return std::numeric_limits<int8_t>::max();
        case NumKind::I16: return std::numeric_limits<int16_t>::max();
        case NumKind::I32: return std::numeric_limits<int32_t>::max();
        default: return std::numeric_limits<int64_t>::max();
    }
}

uint64_t unsignedMax(NumKind kind) {
    switch (kind) {
        case NumKind::U8: return std::numeric_limits<uint8_t>::max();
        case NumKind::U16: return std::numeric_limits<uint16_t>::max();
        case NumKind::U32: return std::numeric_limits<uint32_t>::max();
        default: return std::numeric_limits<uint64_t>::max();
    }
}

const char* numKindName(NumKind kind) {
    switch (kind) {
        case NumKind::Int: return "int";
        case NumKind::I8: return "i8";
        case NumKind::I16: return "i16";
        case NumKind::I32: return "i32";
        case NumKind::I64: return "i64";
        case NumKind::U8: return "u8";
        case NumKind::U16: return "u16";
        case NumKind::U32: return "u32";
        case NumKind::U64: return "u64";
        case NumKind::Float: return "float";
        case NumKind::F32: return "f32";
        case NumKind::F64: return "f64";
    }
    return "number";
}

std::string rangeText(NumKind kind) {
    if (isUnsignedKind(kind)) return "0 to " + std::to_string(unsignedMax(kind));
    return std::to_string(signedMin(kind)) + " to " + std::to_string(signedMax(kind));
}

std::string TypeSpec::name() const {
    std::string text;
    switch (kind) {
        case TypeKind::Any: text = "any"; break;
        case TypeKind::Void: text = "void"; break;
        case TypeKind::Bool: text = "bool"; break;
        case TypeKind::Number: text = numKindName(num); break;
        case TypeKind::Char: text = "char"; break;
        case TypeKind::String: text = "string"; break;
        case TypeKind::List: text = "list"; break;
        case TypeKind::Map: text = "map"; break;
        case TypeKind::Function: text = "func"; break;
        case TypeKind::Struct: text = structName; break;
    }
    if (nullable) text += '?';
    return text;
}

bool lookupType(const std::string& name, TypeSpec& out) {
    static const std::unordered_map<std::string, TypeSpec> types = [] {
        auto number = [](NumKind kind) {
            TypeSpec spec;
            spec.kind = TypeKind::Number;
            spec.num = kind;
            return spec;
        };
        auto simple = [](TypeKind kind) {
            TypeSpec spec;
            spec.kind = kind;
            return spec;
        };
        return std::unordered_map<std::string, TypeSpec>{
            {"int", number(NumKind::Int)},     {"i8", number(NumKind::I8)},
            {"i16", number(NumKind::I16)},     {"i32", number(NumKind::I32)},
            {"i64", number(NumKind::I64)},     {"u8", number(NumKind::U8)},
            {"byte", number(NumKind::U8)},     {"u16", number(NumKind::U16)},
            {"u32", number(NumKind::U32)},     {"u64", number(NumKind::U64)},
            {"float", number(NumKind::Float)}, {"f32", number(NumKind::F32)},
            {"f64", number(NumKind::F64)},     {"bool", simple(TypeKind::Bool)},
            {"char", simple(TypeKind::Char)},  {"string", simple(TypeKind::String)},
            {"list", simple(TypeKind::List)},  {"map", simple(TypeKind::Map)},
            {"func", simple(TypeKind::Function)}, {"any", simple(TypeKind::Any)},
            {"void", simple(TypeKind::Void)},
            // C names for some of the types above.
            {"double", number(NumKind::F64)},  {"long", number(NumKind::I64)},
            {"short", number(NumKind::I16)},
        };
    }();
    auto it = types.find(name);
    if (it == types.end()) return false;
    out = it->second;
    return true;
}

TypeSpec structType(const std::string& name) {
    TypeSpec spec;
    spec.kind = TypeKind::Struct;
    spec.structName = name;
    return spec;
}

}  // namespace ascend

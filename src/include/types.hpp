#ifndef ASCEND_TYPES_HPP
#define ASCEND_TYPES_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace ascend {

// The numeric types. Every number value carries one of these.
//
// `Int` and `Float` are the types of plain literals like `42` and `1.5`.
// They behave like i64 and f64, but adapt to the other operand in arithmetic,
// so with `u8 x = 200`, `x + 1` is a u8 (like untyped constants in Go).
enum class NumKind : uint8_t { Int, I8, I16, I32, I64, U8, U16, U32, U64, Float, F32, F64 };

inline bool isIntKind(NumKind kind) { return kind <= NumKind::U64; }
inline bool isFloatKind(NumKind kind) { return kind >= NumKind::Float; }
inline bool isUnsignedKind(NumKind kind) { return kind >= NumKind::U8 && kind <= NumKind::U64; }

int bitWidth(NumKind kind);           // 64 for Int and Float
int64_t signedMin(NumKind kind);      // for signed integer kinds
int64_t signedMax(NumKind kind);      // for signed integer kinds
uint64_t unsignedMax(NumKind kind);   // for unsigned integer kinds
const char* numKindName(NumKind kind);
std::string rangeText(NumKind kind);  // e.g. "0 to 255"

// Struct: a type declared with `struct Name { ... }`, named by structName.
enum class TypeKind : uint8_t { Any, Void, Bool, Number, Char, String, List, Map, Function, Struct };

// A type annotation, such as the `u8` in `u8 x = 1` or `string?` (nullable).
struct TypeSpec {
    TypeKind kind = TypeKind::Any;
    NumKind num = NumKind::Int;  // used when kind == Number
    bool nullable = false;
    std::string structName;      // used when kind == Struct
    // list<T>: element is T.  map<K, V>: key is K and element is V. Null for
    // plain `list` and `map`, which hold anything.
    std::shared_ptr<const TypeSpec> element;
    std::shared_ptr<const TypeSpec> key;

    bool isAny() const { return kind == TypeKind::Any; }
    std::string name() const;
};

// Whether two types are the same, including their element types.
bool sameType(const TypeSpec& a, const TypeSpec& b);

// Looks up a builtin type name such as "i32", "byte" or "string". Returns
// false for other names (which may be struct types).
bool lookupType(const std::string& name, TypeSpec& out);

// A struct type with the given name.
TypeSpec structType(const std::string& name);

}  // namespace ascend

#endif  // ASCEND_TYPES_HPP

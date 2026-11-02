#ifndef ASCEND_NUMERIC_HPP
#define ASCEND_NUMERIC_HPP

#include "types.hpp"
#include "value.hpp"

#include <cstdint>
#include <string>

namespace ascend {

// Arithmetic on numbers of any type.
//
// The result type follows these rules:
//   - int and float literals adapt to the other operand: u8 + 1 is a u8
//   - two different integer types give the smallest type that holds both:
//     u8 + i32 is an i32, u8 + i8 is an i16 (u64 can't mix with signed types)
//   - an integer and a float give the float's type: i32 + f32 is an f32
//   - f32 and f64 give f64
// Integer results that don't fit their type are an error, never a silent wrap.
enum class NumOp { Add, Sub, Mul, Div, Mod, Pow, BitAnd, BitOr, BitXor, Shl, Shr };

Value numericOp(NumOp op, const Value& left, const Value& right);  // both must be numbers
Value negateNumber(const Value& value);
Value bitwiseNot(const Value& value);  // value must be an int

// Compares two numbers by their mathematical value. NaN sorts first.
int compareNumbers(const Value& a, const Value& b);
// Numeric equality across types (1 == 1.0 == u8(1)); NaN is never equal.
bool numbersEqual(const Value& a, const Value& b);

enum class ConvertStatus { Ok, OutOfRange, NotFinite, NotANumber, FractionLost };

// Converts a number to another numeric type. With `truncate`, floats convert
// to integers by dropping the fraction; otherwise that's FractionLost.
ConvertStatus convertNumber(const Value& value, NumKind kind, bool truncate, Value& out);

// Like convertNumber with truncation, but throws a RuntimeError explaining
// any failure (used by the conversion functions i8(), u64(), f32(), ...).
Value convertNumberOrThrow(const Value& value, NumKind kind);

// Reads an int that fits in i64 (any integer type). False otherwise.
bool toInt64(const Value& value, int64_t& out);

std::string intToString(const Value& value);

}  // namespace ascend

#endif  // ASCEND_NUMERIC_HPP

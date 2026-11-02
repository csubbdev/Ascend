#include "numeric.hpp"

#include <cmath>
#include <limits>

namespace ascend {

namespace {

constexpr int64_t kI64Min = std::numeric_limits<int64_t>::min();
constexpr int64_t kI64Max = std::numeric_limits<int64_t>::max();
constexpr uint64_t kU64Max = std::numeric_limits<uint64_t>::max();

const char* symbol(NumOp op) {
    switch (op) {
        case NumOp::Add: return "+";
        case NumOp::Sub: return "-";
        case NumOp::Mul: return "*";
        case NumOp::Div: return "/";
        case NumOp::Mod: return "%";
        case NumOp::Pow: return "**";
        case NumOp::BitAnd: return "&";
        case NumOp::BitOr: return "|";
        case NumOp::BitXor: return "^";
        case NumOp::Shl: return "<<";
        case NumOp::Shr: return ">>";
    }
    return "?";
}

std::string describeKind(NumKind kind) { return std::string(numKindName(kind)) + " (" + rangeText(kind) + ")"; }

[[noreturn]] void overflow(NumOp op, const Value& a, const Value& b, NumKind kind) {
    throw RuntimeError("integer overflow: " + toString(a) + " " + symbol(op) + " " + toString(b) +
                       " doesn't fit in " + describeKind(kind));
}

uint64_t widthMask(int bits) { return bits >= 64 ? kU64Max : ((uint64_t(1) << bits) - 1); }

bool fitsKind(const Value& v, NumKind kind) {
    if (v.numKind() == NumKind::U64) {
        uint64_t u = v.asUInt();
        if (isUnsignedKind(kind)) return u <= unsignedMax(kind);
        return u <= static_cast<uint64_t>(signedMax(kind));
    }
    int64_t s = v.asInt();
    if (isUnsignedKind(kind)) return s >= 0 && static_cast<uint64_t>(s) <= unsignedMax(kind);
    return s >= signedMin(kind) && s <= signedMax(kind);
}

// Reinterprets an integer that fits `kind` as that kind. The bit patterns of
// all integer kinds agree for values they have in common.
Value retag(const Value& v, NumKind kind) { return Value::fromInt(v.asInt(), kind); }

// The type two integer operands are converted to.
NumKind peerIntKind(NumKind a, NumKind b) {
    if (a == b) return a;
    if (a == NumKind::Int) return b;
    if (b == NumKind::Int) return a;
    bool aUnsigned = isUnsignedKind(a);
    bool bUnsigned = isUnsignedKind(b);
    if (aUnsigned == bUnsigned) return bitWidth(a) >= bitWidth(b) ? a : b;
    NumKind u = aUnsigned ? a : b;
    NumKind s = aUnsigned ? b : a;
    if (bitWidth(s) > bitWidth(u)) return s;
    switch (bitWidth(u)) {
        case 8: return NumKind::I16;
        case 16: return NumKind::I32;
        case 32: return NumKind::I64;
        default:
            throw RuntimeError(std::string("can't mix ") + numKindName(a) + " and " + numKindName(b) +
                               ": no integer type can hold both; convert one side first, like i64(x) or u64(x)");
    }
}

NumKind peerFloatKind(const Value& a, const Value& b) {
    NumKind x = a.isFloat() ? a.numKind() : NumKind::Float;
    NumKind y = b.isFloat() ? b.numKind() : NumKind::Float;
    if (x == y) return x;
    if (x == NumKind::Float) return y;
    if (y == NumKind::Float) return x;
    return NumKind::F64;  // f32 with f64
}

// Converts an operand to the operation's type, failing if it doesn't fit
// (for example the literal 300 used with a u8).
Value adopt(const Value& v, NumKind kind) {
    if (v.numKind() == kind) return v;
    if (!fitsKind(v, kind)) {
        throw RuntimeError(toString(v) + " doesn't fit in " + describeKind(kind));
    }
    return retag(v, kind);
}

int64_t checkedSigned(NumOp op, int64_t x, int64_t y, bool& overflowed) {
    overflowed = false;
    switch (op) {
        case NumOp::Add:
            if ((y > 0 && x > kI64Max - y) || (y < 0 && x < kI64Min - y)) overflowed = true;
            return overflowed ? 0 : x + y;
        case NumOp::Sub:
            if ((y < 0 && x > kI64Max + y) || (y > 0 && x < kI64Min + y)) overflowed = true;
            return overflowed ? 0 : x - y;
        case NumOp::Mul: {
            if (x == 0 || y == 0) return 0;
            if (x > 0) {
                overflowed = y > 0 ? x > kI64Max / y : y < kI64Min / x;
            } else {
                overflowed = y > 0 ? x < kI64Min / y : x < kI64Max / y;
            }
            return overflowed ? 0 : x * y;
        }
        case NumOp::Div:
            if (x == kI64Min && y == -1) {
                overflowed = true;
                return 0;
            }
            return x / y;  // truncates toward zero
        case NumOp::Mod:
            return y == -1 ? 0 : x % y;
        default:
            return 0;
    }
}

uint64_t checkedUnsigned(NumOp op, uint64_t x, uint64_t y, bool& overflowed) {
    overflowed = false;
    switch (op) {
        case NumOp::Add:
            overflowed = x > kU64Max - y;
            return x + y;
        case NumOp::Sub:
            overflowed = x < y;
            return x - y;
        case NumOp::Mul:
            overflowed = x != 0 && y > kU64Max / x;
            return x * y;
        case NumOp::Div: return x / y;
        case NumOp::Mod: return x % y;
        default: return 0;
    }
}

Value intPower(const Value& base, const Value& exponent) {
    if (exponent.isNegativeInt()) {
        return Value::fromFloat(std::pow(base.toDouble(), exponent.toDouble()));
    }
    const NumKind kind = base.numKind();
    uint64_t remaining = exponent.asUInt();
    bool overflowed = false;
    if (kind == NumKind::U64) {
        uint64_t result = 1;
        uint64_t factor = base.asUInt();
        while (remaining > 0 && !overflowed) {
            if (remaining & 1) result = checkedUnsigned(NumOp::Mul, result, factor, overflowed);
            remaining >>= 1;
            if (remaining > 0 && !overflowed) factor = checkedUnsigned(NumOp::Mul, factor, factor, overflowed);
        }
        if (overflowed) overflow(NumOp::Pow, base, exponent, kind);
        return Value::fromUInt(result, kind);
    }
    int64_t result = 1;
    int64_t factor = base.asInt();
    while (remaining > 0 && !overflowed) {
        if (remaining & 1) result = checkedSigned(NumOp::Mul, result, factor, overflowed);
        remaining >>= 1;
        if (remaining > 0 && !overflowed) factor = checkedSigned(NumOp::Mul, factor, factor, overflowed);
    }
    if (overflowed || result < signedMin(kind) || result > signedMax(kind) ||
        (isUnsignedKind(kind) && static_cast<uint64_t>(result) > unsignedMax(kind))) {
        overflow(NumOp::Pow, base, exponent, kind);
    }
    return Value::fromInt(result, kind);
}

Value shift(NumOp op, const Value& value, const Value& amount) {
    const NumKind kind = value.numKind();
    const int width = bitWidth(kind);
    if (amount.isNegativeInt() || amount.asUInt() >= static_cast<uint64_t>(width)) {
        throw RuntimeError("shift amount " + toString(amount) + " is out of range for " + numKindName(kind) +
                           " (0 to " + std::to_string(width - 1) + ")");
    }
    const int n = static_cast<int>(amount.asUInt());
    const uint64_t mask = widthMask(width);
    uint64_t bits = value.asUInt();

    if (op == NumOp::Shl) {
        // Bits shifted out of the type's width are dropped.
        uint64_t result = (bits << n) & mask;
        if (!isUnsignedKind(kind) && width < 64 && ((result >> (width - 1)) & 1)) result |= ~mask;  // sign-extend
        return Value::fromInt(static_cast<int64_t>(result), kind);
    }
    if (isUnsignedKind(kind)) return Value::fromInt(static_cast<int64_t>((bits & mask) >> n), kind);
    int64_t signedValue = value.asInt();
    int64_t result = signedValue >= 0 ? (signedValue >> n) : ~(~signedValue >> n);  // arithmetic shift
    return Value::fromInt(result, kind);
}

Value intOp(NumOp op, const Value& a, const Value& b) {
    if (op == NumOp::Pow) return intPower(a, b);
    if (op == NumOp::Shl || op == NumOp::Shr) return shift(op, a, b);

    // Fast path for the common case: arithmetic on two 64-bit signed ints of the same type.
    if (a.numKind() == b.numKind() && (a.numKind() == NumKind::Int || a.numKind() == NumKind::I64) &&
        (op == NumOp::Add || op == NumOp::Sub || op == NumOp::Mul || op == NumOp::Div || op == NumOp::Mod)) {
        if (op == NumOp::Div && b.asInt() == 0) throw RuntimeError("division by zero");
        if (op == NumOp::Mod && b.asInt() == 0) throw RuntimeError("modulo by zero");
        bool overflowed = false;
        int64_t result = checkedSigned(op, a.asInt(), b.asInt(), overflowed);
        if (overflowed) overflow(op, a, b, a.numKind());
        return Value::fromInt(result, a.numKind());
    }

    const NumKind kind = peerIntKind(a.numKind(), b.numKind());
    Value x = adopt(a, kind);
    Value y = adopt(b, kind);

    if (op == NumOp::BitAnd || op == NumOp::BitOr || op == NumOp::BitXor) {
        // Both operands fit the type, so the result does too.
        uint64_t p = x.asUInt();
        uint64_t q = y.asUInt();
        uint64_t result = op == NumOp::BitAnd ? (p & q) : op == NumOp::BitOr ? (p | q) : (p ^ q);
        return Value::fromInt(static_cast<int64_t>(result), kind);
    }

    bool yIsZero = y.asInt() == 0;
    if (op == NumOp::Div && yIsZero) throw RuntimeError("division by zero");
    if (op == NumOp::Mod && yIsZero) throw RuntimeError("modulo by zero");

    bool overflowed = false;
    if (kind == NumKind::U64) {
        uint64_t result = checkedUnsigned(op, x.asUInt(), y.asUInt(), overflowed);
        if (overflowed) overflow(op, a, b, kind);
        return Value::fromUInt(result, kind);
    }
    int64_t result = checkedSigned(op, x.asInt(), y.asInt(), overflowed);
    bool outOfRange = isUnsignedKind(kind) ? (result < 0 || static_cast<uint64_t>(result) > unsignedMax(kind))
                                           : (result < signedMin(kind) || result > signedMax(kind));
    if (overflowed || outOfRange) overflow(op, a, b, kind);
    return Value::fromInt(result, kind);
}

Value floatOp(NumOp op, const Value& a, const Value& b) {
    const NumKind kind = peerFloatKind(a, b);
    double x = a.toDouble();
    double y = b.toDouble();
    switch (op) {
        case NumOp::Add: return Value::fromFloat(x + y, kind);
        case NumOp::Sub: return Value::fromFloat(x - y, kind);
        case NumOp::Mul: return Value::fromFloat(x * y, kind);
        case NumOp::Div:
            if (y == 0.0) throw RuntimeError("division by zero");
            return Value::fromFloat(x / y, kind);
        case NumOp::Mod:
            if (y == 0.0) throw RuntimeError("modulo by zero");
            return Value::fromFloat(std::fmod(x, y), kind);
        case NumOp::Pow: return Value::fromFloat(std::pow(x, y), kind);
        default:
            throw RuntimeError(std::string("'") + symbol(op) + "' needs integers, not " + a.typeName() + " and " +
                               b.typeName());
    }
}

}  // namespace

Value numericOp(NumOp op, const Value& left, const Value& right) {
    if (left.isInt() && right.isInt()) return intOp(op, left, right);
    return floatOp(op, left, right);
}

Value negateNumber(const Value& value) {
    if (value.isFloat()) return Value::fromFloat(-value.asFloat(), value.numKind());
    const NumKind kind = value.numKind();
    if (isUnsignedKind(kind)) {
        if (value.asInt() == 0) return value;
        throw RuntimeError("can't negate " + toString(value) + ": " + numKindName(kind) + " is unsigned");
    }
    if (value.asInt() == signedMin(kind)) {
        throw RuntimeError("integer overflow: -(" + toString(value) + ") doesn't fit in " + describeKind(kind));
    }
    return Value::fromInt(-value.asInt(), kind);
}

Value bitwiseNot(const Value& value) {
    const NumKind kind = value.numKind();
    if (isUnsignedKind(kind)) {
        return Value::fromInt(static_cast<int64_t>(~value.asUInt() & widthMask(bitWidth(kind))), kind);
    }
    return Value::fromInt(~value.asInt(), kind);
}

int compareNumbers(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) {
        bool aNegative = a.isNegativeInt();
        bool bNegative = b.isNegativeInt();
        if (aNegative != bNegative) return aNegative ? -1 : 1;
        if (aNegative) return a.asInt() < b.asInt() ? -1 : (a.asInt() > b.asInt() ? 1 : 0);
        return a.asUInt() < b.asUInt() ? -1 : (a.asUInt() > b.asUInt() ? 1 : 0);
    }
    double x = a.toDouble();
    double y = b.toDouble();
    bool xNan = std::isnan(x);
    bool yNan = std::isnan(y);
    if (xNan || yNan) return xNan == yNan ? 0 : (xNan ? -1 : 1);
    return x < y ? -1 : (x > y ? 1 : 0);
}

bool numbersEqual(const Value& a, const Value& b) {
    if (a.isInt() && b.isInt()) return compareNumbers(a, b) == 0;
    return a.toDouble() == b.toDouble();
}

ConvertStatus convertNumber(const Value& value, NumKind kind, bool truncate, Value& out) {
    if (!value.isNumber()) return ConvertStatus::NotANumber;

    if (isFloatKind(kind)) {
        out = Value::fromFloat(value.toDouble(), kind);
        return ConvertStatus::Ok;
    }

    if (value.isInt()) {
        if (!fitsKind(value, kind)) return ConvertStatus::OutOfRange;
        out = retag(value, kind);
        return ConvertStatus::Ok;
    }

    double d = value.asFloat();
    if (!std::isfinite(d)) return ConvertStatus::NotFinite;
    double whole = std::trunc(d);
    if (!truncate && whole != d) return ConvertStatus::FractionLost;
    if (isUnsignedKind(kind)) {
        if (whole < 0 || whole >= 18446744073709551616.0) return ConvertStatus::OutOfRange;
        uint64_t u = static_cast<uint64_t>(whole);
        if (u > unsignedMax(kind)) return ConvertStatus::OutOfRange;
        out = Value::fromUInt(u, kind);
        return ConvertStatus::Ok;
    }
    if (whole < -9223372036854775808.0 || whole >= 9223372036854775808.0) return ConvertStatus::OutOfRange;
    int64_t s = static_cast<int64_t>(whole);
    if (s < signedMin(kind) || s > signedMax(kind)) return ConvertStatus::OutOfRange;
    out = Value::fromInt(s, kind);
    return ConvertStatus::Ok;
}

Value convertNumberOrThrow(const Value& value, NumKind kind) {
    Value out;
    switch (convertNumber(value, kind, true, out)) {
        case ConvertStatus::Ok: return out;
        case ConvertStatus::OutOfRange:
            throw RuntimeError(toString(value) + " doesn't fit in " + describeKind(kind));
        case ConvertStatus::NotFinite:
            throw RuntimeError("can't convert " + toString(value) + " to " + numKindName(kind));
        default:
            throw RuntimeError(std::string("can't convert a ") + value.typeName() + " to " + numKindName(kind));
    }
}

bool toInt64(const Value& value, int64_t& out) {
    if (!value.isInt() || !value.fitsInt64()) return false;
    out = value.asInt();
    return true;
}

std::string intToString(const Value& value) {
    if (value.numKind() == NumKind::U64) return std::to_string(value.asUInt());
    return std::to_string(value.asInt());
}

}  // namespace ascend

// Format specs, used by f-strings (f"{x:>8.2f}") and format().
//
//   [[fill]align][sign][0][width][,][.precision][type]
//
//   align      <  left, >  right, ^  center (numbers default to right, text to left)
//   sign       +  always show a sign
//   0          pad numbers with zeros after the sign
//   ,          group thousands: 1,234,567
//   precision  digits after the point for numbers; maximum length for text
//   type       d  integer       x X o b  hex, octal, binary
//              f  fixed point   e  exponent   g  general   %  percent
//              s  text          c  character (from a char code)

#include "numeric.hpp"
#include "stdlib.hpp"
#include "utf8.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace ascend {

namespace {

struct Spec {
    char fill = ' ';
    char align = 0;
    char sign = '-';
    bool zeroPad = false;
    int width = 0;
    bool grouping = false;
    int precision = -1;
    char type = 0;
};

[[noreturn]] void badSpec(const std::string& spec, const std::string& why) {
    throw RuntimeError("invalid format spec \"" + spec + "\": " + why);
}

Spec parseSpec(const std::string& text) {
    Spec spec;
    size_t i = 0;
    const size_t n = text.size();
    auto isAlign = [](char c) { return c == '<' || c == '>' || c == '^'; };
    if (n >= 2 && isAlign(text[1])) {
        spec.fill = text[0];
        spec.align = text[1];
        i = 2;
    } else if (n >= 1 && isAlign(text[0])) {
        spec.align = text[0];
        i = 1;
    }
    if (i < n && (text[i] == '+' || text[i] == '-' || text[i] == ' ')) spec.sign = text[i++];
    if (i < n && text[i] == '0') {
        spec.zeroPad = true;
        ++i;
    }
    while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
        spec.width = spec.width * 10 + (text[i++] - '0');
        if (spec.width > 10000) badSpec(text, "width is too large");
    }
    if (i < n && text[i] == ',') {
        spec.grouping = true;
        ++i;
    }
    if (i < n && text[i] == '.') {
        ++i;
        if (i >= n || !std::isdigit(static_cast<unsigned char>(text[i]))) badSpec(text, "expected digits after '.'");
        spec.precision = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
            spec.precision = spec.precision * 10 + (text[i++] - '0');
            if (spec.precision > 100) badSpec(text, "precision is too large");
        }
    }
    if (i < n) {
        spec.type = text[i++];
        if (std::string("sdxXobfFeEgG%c").find(spec.type) == std::string::npos) {
            badSpec(text, std::string("unknown type '") + spec.type + "'");
        }
    }
    if (i != n) badSpec(text, "unexpected '" + text.substr(i) + "'");
    return spec;
}

std::string toBase(uint64_t value, int base, bool upper) {
    if (value == 0) return "0";
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string out;
    while (value > 0) {
        out += digits[value % static_cast<uint64_t>(base)];
        value /= static_cast<uint64_t>(base);
    }
    return std::string(out.rbegin(), out.rend());
}

// Inserts commas into the integer part of a digit string.
std::string group(const std::string& digits) {
    size_t end = digits.find_first_of(".eE");
    if (end == std::string::npos) end = digits.size();
    std::string integer = digits.substr(0, end);
    std::string grouped;
    for (size_t i = 0; i < integer.size(); ++i) {
        if (i > 0 && (integer.size() - i) % 3 == 0) grouped += ',';
        grouped += integer[i];
    }
    return grouped + digits.substr(end);
}

std::string pad(const std::string& text, const Spec& spec, char defaultAlign) {
    size_t length = utf8Length(text);
    if (spec.width <= 0 || length >= static_cast<size_t>(spec.width)) return text;
    size_t padding = static_cast<size_t>(spec.width) - length;
    char align = spec.align != 0 ? spec.align : defaultAlign;
    if (align == '<') return text + std::string(padding, spec.fill);
    if (align == '>') return std::string(padding, spec.fill) + text;
    size_t left = padding / 2;
    return std::string(left, spec.fill) + text + std::string(padding - left, spec.fill);
}

std::string formatNumber(const Value& value, const Spec& spec, const std::string& specText) {
    bool negative = false;
    std::string digits;
    char type = spec.type;
    if (type == 0 && spec.precision >= 0) type = 'f';  // {x:.2} means two decimals

    if (type == 'd' || type == 'x' || type == 'X' || type == 'o' || type == 'b') {
        if (!value.isInt()) badSpec(specText, std::string("'") + type + "' needs an integer, got " + aTypeName(value));
        negative = value.isNegativeInt();
        uint64_t magnitude = negative ? uint64_t(0) - value.asUInt() : value.asUInt();
        int base = type == 'd' ? 10 : (type == 'o' ? 8 : (type == 'b' ? 2 : 16));
        digits = toBase(magnitude, base, type == 'X');
    } else if (type == 0) {
        if (value.isInt()) {
            negative = value.isNegativeInt();
            digits = toBase(negative ? uint64_t(0) - value.asUInt() : value.asUInt(), 10, false);
        } else {
            double d = value.asFloat();
            negative = std::signbit(d) && !std::isnan(d);
            digits = formatFloat(std::fabs(d), value.numKind() == NumKind::F32);
        }
    } else {
        double d = value.toDouble();
        negative = std::signbit(d) && !std::isnan(d);
        double magnitude = std::fabs(d);
        int precision = spec.precision >= 0 ? spec.precision : 6;
        char buffer[512];
        if (std::isnan(magnitude) || std::isinf(magnitude)) {
            digits = std::isnan(magnitude) ? "nan" : "inf";
        } else if (type == '%') {
            std::snprintf(buffer, sizeof buffer, "%.*f", precision, magnitude * 100);
            digits = std::string(buffer) + "%";
        } else {
            char format[8] = {'%', '.', '*', type, '\0'};
            if (type == 'F') format[3] = 'f';
            if (magnitude >= 1e100 && (type == 'f' || type == 'F')) format[3] = 'e';  // keep the buffer bounded
            std::snprintf(buffer, sizeof buffer, format, precision, magnitude);
            digits = buffer;
        }
    }

    if (spec.grouping) digits = group(digits);
    std::string sign = negative ? "-" : (spec.sign == '+' ? "+" : (spec.sign == ' ' ? " " : ""));

    if (spec.zeroPad && spec.align == 0 && spec.width > 0) {
        size_t length = sign.size() + utf8Length(digits);
        if (length < static_cast<size_t>(spec.width)) digits = std::string(static_cast<size_t>(spec.width) - length, '0') + digits;
        return sign + digits;
    }
    return pad(sign + digits, spec, '>');
}

}  // namespace

std::string formatValue(const Value& value, const std::string& specText) {
    Spec spec = parseSpec(specText);

    if (spec.type == 'c') {
        char32_t code = 0;
        if (value.isChar()) {
            code = value.asChar();
        } else {
            int64_t number = 0;
            if (!toInt64(value, number) || number < 0 || number > 0x10FFFF ||
                !isValidCodePoint(static_cast<char32_t>(number))) {
                badSpec(specText, "'c' needs a char or a valid character code");
            }
            code = static_cast<char32_t>(number);
        }
        return pad(encodeUtf8(code), spec, '<');
    }

    if (value.isNumber() && spec.type != 's') return formatNumber(value, spec, specText);
    if (spec.type != 0 && spec.type != 's') {
        bool integerType = std::string("dxXob").find(spec.type) != std::string::npos;
        badSpec(specText, std::string("'") + spec.type + "' needs " + (integerType ? "an integer" : "a number") + ", got " +
                              aTypeName(value));
    }

    std::string text = toString(value);
    if (spec.precision >= 0 && utf8Length(text) > static_cast<size_t>(spec.precision)) {
        text = text.substr(0, utf8Offset(text, static_cast<size_t>(spec.precision)));
    }
    return pad(text, spec, '<');
}

std::string formatTemplate(const std::string& tpl, const std::vector<Value>& args, size_t first) {
    std::string out;
    size_t next = first;
    for (size_t i = 0; i < tpl.size(); ++i) {
        char c = tpl[i];
        if (c == '}') {
            if (i + 1 < tpl.size() && tpl[i + 1] == '}') {
                out += '}';
                ++i;
                continue;
            }
            throw RuntimeError("format(): single '}' in the template (write '}}' for a literal brace)");
        }
        if (c != '{') {
            out += c;
            continue;
        }
        if (i + 1 < tpl.size() && tpl[i + 1] == '{') {
            out += '{';
            ++i;
            continue;
        }
        size_t close = tpl.find('}', i);
        if (close == std::string::npos) throw RuntimeError("format(): '{' without a matching '}'");
        std::string field = tpl.substr(i + 1, close - i - 1);
        size_t colon = field.find(':');
        std::string position = field.substr(0, colon);
        std::string spec = colon == std::string::npos ? "" : field.substr(colon + 1);

        size_t index;
        if (position.empty()) {
            index = next++;
        } else {
            for (char digit : position) {
                if (!std::isdigit(static_cast<unsigned char>(digit))) {
                    throw RuntimeError("format(): invalid field {" + field + "} (use {}, {0} or {:spec})");
                }
            }
            index = first + static_cast<size_t>(std::stoul(position));
        }
        if (index >= args.size()) {
            throw RuntimeError("format(): the template needs more arguments than the " +
                               std::to_string(args.size() - first) + " given");
        }
        out += spec.empty() ? toString(args[index]) : formatValue(args[index], spec);
        i = close;
    }
    return out;
}

}  // namespace ascend

#ifndef ASCEND_STDLIB_HPP
#define ASCEND_STDLIB_HPP

#include "value.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ascend {

class Interpreter;

// Defines the builtin functions (print, len, range, push, ...) and constants
// (PI, E, ...) in the interpreter's builtin scope.
void registerStandardLibrary(Interpreter& interpreter);

// The integers produced by range(start, stop, step).
struct RangeSpec {
    int64_t start = 0;
    int64_t step = 1;
    uint64_t count = 0;

    int64_t at(uint64_t i) const {
        // Computed with unsigned (wrapping) arithmetic; the result is always in range.
        return static_cast<int64_t>(static_cast<uint64_t>(start) + i * static_cast<uint64_t>(step));
    }
};

// Validates range() arguments: range(stop), range(start, stop) or range(start, stop, step).
RangeSpec makeRange(const std::vector<Value>& args);

// Formats one value with a format spec like ">8", ".2f" or "08x" (format.cpp).
std::string formatValue(const Value& value, const std::string& spec);

// format("{} is {:.1f}", args...): fills each {} with the next argument,
// starting at args[first] (format.cpp).
std::string formatTemplate(const std::string& tpl, const std::vector<Value>& args, size_t first);

// JSON conversion (json.cpp).
Value jsonParse(Interpreter& interpreter, const std::string& text);
std::string jsonStringify(const Value& value, int indent);

// Reads a whole file. Returns false if it can't be opened.
bool readTextFile(const std::string& path, std::string& out);

}  // namespace ascend

#endif  // ASCEND_STDLIB_HPP

#ifndef ASCEND_STDLIB_HPP
#define ASCEND_STDLIB_HPP

#include "value.hpp"

#include <cstdint>
#include <vector>

namespace ascend {

class Interpreter;

// Defines the builtin functions (print, len, range, push, ...) and constants
// (PI, E) in the interpreter's global scope.
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

}  // namespace ascend

#endif  // ASCEND_STDLIB_HPP

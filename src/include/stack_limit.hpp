#ifndef ASCEND_STACK_LIMIT_HPP
#define ASCEND_STACK_LIMIT_HPP

#include <cstddef>
#include <cstdint>

namespace ascend {

// Tracks how much of the current thread's stack is in use, so very deep
// recursion (in a program, or in deeply nested code being parsed) is
// reported as an error instead of crashing the process.
class StackLimit {
public:
    // Measures from the current position, keeping a safety margin below the
    // real end of the stack.
    StackLimit();

    bool exceeded() const;
    size_t budget() const { return budget_; }

private:
    uintptr_t base_;
    size_t budget_;
};

// Bytes of stack left between the current position and the end of the
// current thread's stack (estimated from the stack size limit where the
// platform can't tell exactly).
size_t availableStack();

}  // namespace ascend

#endif  // ASCEND_STACK_LIMIT_HPP

#ifndef ASCEND_BIG_STACK_HPP
#define ASCEND_BIG_STACK_HPP

#include <cstddef>
#include <functional>

namespace ascend {

// Runs `body` on a new thread with a large stack and returns its result, so
// Ascend programs can recurse deeply (each Ascend call uses a few KB of the
// C++ stack). The memory is reserved, not used, until the stack grows. If the
// thread can't be created, `body` runs on the current thread instead.
int runWithLargeStack(const std::function<int()>& body, size_t bytes = size_t(512) * 1024 * 1024);

// The size of the current thread's stack when it was started by
// runWithLargeStack, or 0.
size_t knownThreadStackSize();

}  // namespace ascend

#endif  // ASCEND_BIG_STACK_HPP

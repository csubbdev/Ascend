#include "stack_limit.hpp"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace ascend {

namespace {

constexpr size_t kKiB = 1024;
constexpr size_t kMiB = 1024 * kKiB;

// Address of the current stack frame.
uintptr_t stackPosition() {
#if defined(__GNUC__) || defined(__clang__)
    return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
#elif defined(_MSC_VER)
    return reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
#else
    volatile char marker = 0;
    return reinterpret_cast<uintptr_t>(&marker);
#endif
}

}  // namespace

size_t availableStack() {
#if defined(_WIN32)
    // The exact bounds of this thread's stack (Windows 8 and later).
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    uintptr_t here = stackPosition();
    return here > low ? static_cast<size_t>(here - low) : 0;
#elif defined(__unix__) || defined(__APPLE__)
    // The main thread's stack can grow up to the soft limit; the interpreter
    // starts near the top of it.
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY) {
        return static_cast<size_t>(limit.rlim_cur);
    }
    return 8 * kMiB;
#else
    return 1 * kMiB;  // assume the smallest common default
#endif
}

StackLimit::StackLimit() : base_(stackPosition()) {
    size_t available = availableStack();
    // Leave room for the frames between two checks and for deep library
    // calls (formatting a nested list, sorting, ...).
    size_t margin = std::max<size_t>(256 * kKiB, available / 8);
    budget_ = available > 2 * margin ? available - margin : available / 2;
}

bool StackLimit::exceeded() const {
    uintptr_t here = stackPosition();
    size_t used = here > base_ ? here - base_ : base_ - here;
    return used > budget_;
}

}  // namespace ascend

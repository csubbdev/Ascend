#include "big_stack.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace ascend {

namespace {

thread_local size_t threadStackSize = 0;

struct ThreadTask {
    const std::function<int()>* body;
    size_t bytes;
    int result = 0;
};

void runTask(ThreadTask& task) {
    threadStackSize = task.bytes;
    task.result = (*task.body)();
}

}  // namespace

size_t knownThreadStackSize() { return threadStackSize; }

#if defined(_WIN32)

namespace {
DWORD WINAPI threadMain(LPVOID parameter) {
    runTask(*static_cast<ThreadTask*>(parameter));
    return 0;
}
}  // namespace

int runWithLargeStack(const std::function<int()>& body, size_t bytes) {
    ThreadTask task{&body, bytes};
    HANDLE thread = CreateThread(nullptr, bytes, threadMain, &task, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (thread == nullptr) return body();
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return task.result;
}

#else

namespace {
void* threadMain(void* parameter) {
    runTask(*static_cast<ThreadTask*>(parameter));
    return nullptr;
}
}  // namespace

int runWithLargeStack(const std::function<int()>& body, size_t bytes) {
    ThreadTask task{&body, bytes};
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return body();
    pthread_t thread;
    bool started = pthread_attr_setstacksize(&attributes, bytes) == 0 &&
                   pthread_create(&thread, &attributes, threadMain, &task) == 0;
    pthread_attr_destroy(&attributes);
    if (!started) return body();
    pthread_join(thread, nullptr);
    return task.result;
}

#endif

}  // namespace ascend

#include "stack_limit.hpp"
#include "test_framework.hpp"

#include <exception>
#include <iostream>
#include <string>

// Runs every registered test. Pass a substring to run only matching tests.
//
// Each test's name is printed (and flushed) before it runs, so if a test
// crashes the process, the log shows which one it was.
int main(int argc, char* argv[]) {
    std::string filter = argc > 1 ? argv[1] : "";
    std::cout << "stack available: " << ascend::availableStack() / 1024 << " KiB" << std::endl;
    int passed = 0;
    int failed = 0;
    for (const auto& test : asctest::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        std::cout << "[ RUN  ] " << test.name << std::endl;
        try {
            test.body();
            ++passed;
        } catch (const asctest::Failure& failure) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n  " << failure.message << std::endl;
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n  unexpected exception: " << error.what() << std::endl;
        }
    }
    std::cout << passed << " passed, " << failed << " failed" << std::endl;
    return failed == 0 ? 0 : 1;
}

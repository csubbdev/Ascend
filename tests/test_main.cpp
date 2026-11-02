#include "test_framework.hpp"

#include <exception>
#include <iostream>
#include <string>

// Runs every registered test. Pass a substring to run only matching tests.
int main(int argc, char* argv[]) {
    std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0;
    int failed = 0;
    for (const auto& test : asctest::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        try {
            test.body();
            ++passed;
        } catch (const asctest::Failure& failure) {
            ++failed;
            std::cerr << "FAIL " << test.name << "\n  " << failure.message << "\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << test.name << "\n  unexpected exception: " << error.what() << "\n";
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

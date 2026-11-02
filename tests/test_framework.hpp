#ifndef ASCEND_TEST_FRAMEWORK_HPP
#define ASCEND_TEST_FRAMEWORK_HPP

// A minimal dependency-free test framework.
//
//   TEST(addition_works) {
//       CHECK(1 + 1 == 2);
//       CHECK_EQ(run("print(1 + 1)"), "2\n");
//   }

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace asctest {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*body)()) { registry().push_back({name, body}); }
};

struct Failure {
    std::string message;
};

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* actualText, const char* expectedText,
                const char* file, int line) {
    if (actual == expected) return;
    std::ostringstream out;
    out << file << ':' << line << ": CHECK_EQ(" << actualText << ", " << expectedText << ") failed\n"
        << "    actual:   " << actual << "\n"
        << "    expected: " << expected;
    throw Failure{out.str()};
}

}  // namespace asctest

#define TEST(name)                                                  \
    static void name();                                             \
    static ::asctest::Registrar name##_registrar(#name, &name);     \
    static void name()

#define CHECK(condition)                                                                              \
    do {                                                                                              \
        if (!(condition)) {                                                                           \
            throw ::asctest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +         \
                                     ": CHECK(" #condition ") failed"};                               \
        }                                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected) ::asctest::checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#endif  // ASCEND_TEST_FRAMEWORK_HPP

cmake_minimum_required(VERSION 3.10)

project(ascend VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Everything except main() lives in a library so the tests can use it too.
add_library(ascend_core STATIC
    src/error.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic_analyzer.cpp
    src/value.cpp
    src/interpreter.cpp
    src/stdlib.cpp
    src/runner.cpp
    src/repl.cpp
)
target_include_directories(ascend_core PUBLIC src/include)

if(MSVC)
    target_compile_options(ascend_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ascend_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Deeply recursive Ascend programs need more stack than Windows' default 1 MB.
function(ascend_set_stack_size target)
    if(MSVC)
        set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " /STACK:8388608")
    elseif(MINGW)
        set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--stack,8388608")
    endif()
endfunction()

add_executable(ascend src/main.cpp)
target_link_libraries(ascend PRIVATE ascend_core)
ascend_set_stack_size(ascend)
install(TARGETS ascend RUNTIME DESTINATION bin)

option(ASCEND_BUILD_TESTS "Build the Ascend test suite" ON)
if(ASCEND_BUILD_TESTS)
    enable_testing()

    add_executable(ascend_tests
        tests/test_main.cpp
        tests/lexer_tests.cpp
        tests/parser_tests.cpp
        tests/interpreter_tests.cpp
    )
    target_link_libraries(ascend_tests PRIVATE ascend_core)
    ascend_set_stack_size(ascend_tests)
    add_test(NAME unit_tests COMMAND ascend_tests)

    # Every example program must run without errors.
    file(GLOB ASCEND_EXAMPLES ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.asc)
    foreach(example ${ASCEND_EXAMPLES})
        get_filename_component(example_name ${example} NAME_WE)
        add_test(NAME example_${example_name} COMMAND ascend ${example})
    endforeach()
endif()

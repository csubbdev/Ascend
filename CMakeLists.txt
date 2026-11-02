cmake_minimum_required(VERSION 3.10)

project(ascend VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The sources and tests contain UTF-8 text; MSVC otherwise assumes the system code page.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Everything except main() lives in a library so the tests can use it too.
add_library(ascend_core STATIC
    src/error.cpp
    src/types.cpp
    src/utf8.cpp
    src/numeric.cpp
    src/typecheck.cpp
    src/stack_limit.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic_analyzer.cpp
    src/value.cpp
    src/interpreter.cpp
    src/stdlib.cpp
    src/format.cpp
    src/json.cpp
    src/runner.cpp
    src/repl.cpp
    src/process.cpp
    src/packages.cpp
    src/package_manager.cpp
)
target_include_directories(ascend_core PUBLIC src/include)

# GCC 8 keeps std::filesystem in a separate library.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(ascend_core PUBLIC stdc++fs)
endif()

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

# The package manager.
add_executable(apkg src/apkg_main.cpp)
target_link_libraries(apkg PRIVATE ascend_core)

install(TARGETS ascend apkg RUNTIME DESTINATION bin)

option(ASCEND_BUILD_TESTS "Build the Ascend test suite" ON)
if(ASCEND_BUILD_TESTS)
    enable_testing()

    add_executable(ascend_tests
        tests/test_main.cpp
        tests/lexer_tests.cpp
        tests/parser_tests.cpp
        tests/interpreter_tests.cpp
        tests/language_tests.cpp
        tests/package_tests.cpp
    )
    target_link_libraries(ascend_tests PRIVATE ascend_core)
    # The package tests install the packages in this repository's registry.
    target_compile_definitions(ascend_tests PRIVATE ASCEND_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    ascend_set_stack_size(ascend_tests)
    add_test(NAME unit_tests COMMAND ascend_tests)

    # Every example program must run without errors.
    file(GLOB ASCEND_EXAMPLES ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.asc)
    foreach(example ${ASCEND_EXAMPLES})
        get_filename_component(example_name ${example} NAME_WE)
        add_test(NAME example_${example_name} COMMAND ascend ${example})
    endforeach()
endif()

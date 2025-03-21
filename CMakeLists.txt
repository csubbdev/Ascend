cmake_minimum_required(VERSION 3.10)

project(ascend)

set(SOURCES
    src/ast.cpp
    src/error.cpp
    src/interpreter.cpp
    src/lexer.cpp
    src/main.cpp
    src/parser.cpp
    src/repl.cpp
    src/semantic_analyzer.cpp
    src/stdlib.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

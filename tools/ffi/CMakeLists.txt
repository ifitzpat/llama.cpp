cmake_minimum_required(VERSION 3.14)
project(llama_simple C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Options
option(LLAMA_SIMPLE_BUILD_TESTS "Build tests for llama_simple" ON)
option(LLAMA_SIMPLE_BUILD_EXAMPLES "Build examples for llama_simple" OFF)

# Find llama.cpp
# Assume we're in tools/ffi, so llama.cpp is at ../..
set(LLAMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLAMA_ROOT}/include
    ${LLAMA_ROOT}/common
    ${LLAMA_ROOT}/ggml/include
)

# Link directories (build artifacts)
link_directories(
    ${LLAMA_ROOT}/build/src
    ${LLAMA_ROOT}/build/common
    ${LLAMA_ROOT}/build/ggml/src
)

# llama_simple library
add_library(llama_simple SHARED
    llama_simple.cpp
)

target_link_libraries(llama_simple
    llama
    common
)

set_target_properties(llama_simple PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER llama_simple.h
)

# Install
install(TARGETS llama_simple
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)

# Tests
if(LLAMA_SIMPLE_BUILD_TESTS)
    enable_testing()

    add_executable(test_llama_simple
        tests/test_llama_simple.c
    )

    target_link_libraries(test_llama_simple
        llama_simple
    )

    add_test(NAME test_llama_simple
        COMMAND test_llama_simple
    )
endif()

# Examples
if(LLAMA_SIMPLE_BUILD_EXAMPLES)
    add_executable(example_simple
        examples/simple.c
    )

    target_link_libraries(example_simple
        llama_simple
    )
endif()

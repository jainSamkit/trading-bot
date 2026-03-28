cmake_minimum_required(VERSION 3.20)
project(trading_bit CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Omit -march=native and -flto (Docker/CI-friendly; LTO can fail in some container link setups).
option(PORTABLE_RELEASE "Release: -O3 only (no -march=native/-flto)" OFF)

# ── Build types ────────────────────────────────────────────────────────────────
# Release:          cmake -DCMAKE_BUILD_TYPE=Release ..
# Docker/CI:        cmake -DPORTABLE_RELEASE=ON ...
# Debug:            cmake -DCMAKE_BUILD_TYPE=Debug ..
# ThreadSanitizer:  cmake -DCMAKE_BUILD_TYPE=ThreadSanitizer ..

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(PORTABLE_RELEASE)
        add_compile_options(-O3 -funroll-loops)
        add_compile_definitions(NDEBUG)
        message(STATUS "Release (portable): -O3 -funroll-loops (no native/flto)")
    else()
        add_compile_options(-O3 -march=native -flto -funroll-loops)
        add_compile_definitions(NDEBUG)
        message(STATUS "Release build: -O3 -march=native -flto")
    endif()

elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Debug build with AddressSanitizer + UBSan")

elseif(CMAKE_BUILD_TYPE STREQUAL "ThreadSanitizer")
    add_compile_options(-O1 -g -fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer build")
endif()

# ── Include dirs ───────────────────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/src)

# ── Dependencies ───────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)

find_library(URING_LIB uring)
if(NOT URING_LIB)
    message(WARNING "liburing not found — io_uring targets will be skipped")
endif()

include(FetchContent)
FetchContent_Declare(
    simdjson
    URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.12.2.zip
)
FetchContent_MakeAvailable(simdjson)

# ── Compiler warnings ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# ── Sources (match tree under src/) ────────────────────────────────────────────
add_executable(trading_bot
    src/main.cpp
    src/parser/delta_parser.cpp
)

target_link_libraries(trading_bot PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson
)
if(TARGET Boost::headers)
    target_link_libraries(trading_bot PRIVATE Boost::headers)
else()
    target_include_directories(trading_bot PRIVATE ${Boost_INCLUDE_DIRS})
endif()

# ── Usage ──────────────────────────────────────────────────────────────────────
# mkdir build && cd build
# cmake -DCMAKE_BUILD_TYPE=Release ..
# cmake --build . -j$(nproc)
# ./trading_bot

# ── Tests (opt-in) ────────────────────────────────────────────────────────────
# cmake -DBUILD_TESTS=ON ...
# cmake --build build -j$(nproc)
# ctest --test-dir build --output-on-failure
option(BUILD_TESTS "Build test suite (requires network for gtest download)" OFF)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(trading_bot_tests
        tests/test_ws_parser.cpp
        tests/test_session.cpp
    )
    target_include_directories(trading_bot_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
    )
    target_link_libraries(trading_bot_tests PRIVATE
        GTest::gtest_main
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson
    )
    if(TARGET Boost::headers)
        target_link_libraries(trading_bot_tests PRIVATE Boost::headers)
    else()
        target_include_directories(trading_bot_tests PRIVATE ${Boost_INCLUDE_DIRS})
    endif()

    include(GoogleTest)
    gtest_discover_tests(trading_bot_tests)
endif()

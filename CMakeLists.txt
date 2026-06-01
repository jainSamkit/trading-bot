cmake_minimum_required(VERSION 3.20)
project(trading_bit CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile_commands.json so clangd / IDE tooling sees the same standard + flags
# as the actual build. Without this, clangd analyzes headers in isolation with its own
# default standard (often C++14) and emits spurious "C++17 extension" warnings.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Omit -march=native and -flto (Docker/CI-friendly; LTO can fail in some container link setups).
option(PORTABLE_RELEASE "Release: -O3 only (no -march=native/-flto)" OFF)

# Force clock_gettime backend for environments where TSC is unreliable (Docker-on-Mac,
# ARM, virtualized hosts with non-invariant TSC). Production AWS Linux x86_64 uses
# rdtscp by default (this option OFF).
option(LATENCY_FORCE_CLOCK_GETTIME "Force clock_gettime backend in latency module" OFF)
if(LATENCY_FORCE_CLOCK_GETTIME)
    add_compile_definitions(LATENCY_FORCE_CLOCK_GETTIME=1)
    message(STATUS "Latency backend: clock_gettime (forced)")
else()
    message(STATUS "Latency backend: rdtscp (compile-time default; auto-aborts if TSC unreliable)")
endif()

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

FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.1.zip
)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE ON)
set(HTTPLIB_REQUIRE_OPENSSL ON)
FetchContent_MakeAvailable(httplib)

# ── Compiler warnings ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# ── Sources (match tree under src/) ────────────────────────────────────────────
set(BOT_SOURCES
    src/transport/http_client.cpp
    src/delta_exchange/rest_client.cpp
    src/delta_exchange/ws_client.cpp
    src/delta_exchange/oms_ws_client.cpp
    src/delta_exchange/api/product/product.cpp
    src/delta_exchange/api/order/order.cpp
    src/delta_exchange/api/position/position.cpp
    src/delta_exchange/api/wallet/wallet.cpp
    src/delta_exchange/api/fill/fill.cpp
    src/market_state/latency_stats.cpp
    src/market_state/market_state.cpp
    src/oms/oms_manager.cpp
)

add_executable(trading_bot src/main.cpp ${BOT_SOURCES})

add_executable(oms_test tests/main_oms_test.cpp ${BOT_SOURCES})

# ── Standalone tools ───────────────────────────────────────────────────────────
# shm_inspect: attaches to /trading_bot_state and prints live snapshots + trades.
# No bot sources needed — pure header-only consumer of SharedState.
#
# Currently STALE: written against the pre-venue-aware SharedState layout
# (commit 5ebab94 changed market_state to a [venue][instrument] 2D array and
# removed SharedState::MAX_INSTRUMENTS). Gate it behind an option so the bot
# build isn't blocked. Flip ON only after updating tools/shm_inspect.cpp to
# the new layout.
option(BUILD_SHM_INSPECT "Build the (currently stale) shm_inspect debugging tool" OFF)
if(BUILD_SHM_INSPECT)
    add_executable(shm_inspect tools/shm_inspect.cpp)
    target_link_libraries(shm_inspect PRIVATE Threads::Threads rt)
endif()

# ── Latency module smoke test ─────────────────────────────────────────────────
# Tiny binary that exercises src/latency/clock.hpp end-to-end. Run:
#   cmake --build build --target latency_smoke
#   ./build/latency_smoke
# Validates calibrate(), now_cycles(), cycles_to_ns() on the local CPU.
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_clock_smoke.cpp)
    add_executable(latency_smoke tests/test_clock_smoke.cpp)
    target_link_libraries(latency_smoke PRIVATE Threads::Threads)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_histogram.cpp)
    add_executable(latency_histogram tests/test_histogram.cpp)
    target_link_libraries(latency_histogram PRIVATE Threads::Threads)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_registry.cpp)
    add_executable(latency_registry tests/test_registry.cpp)
    target_link_libraries(latency_registry PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        httplib::httplib
    )
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_span.cpp)
    add_executable(latency_span tests/test_span.cpp)
    target_link_libraries(latency_span PRIVATE Threads::Threads)
endif()

foreach(tgt trading_bot oms_test)
    target_link_libraries(${tgt} PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson
        httplib::httplib
    )
    if(TARGET Boost::headers)
        target_link_libraries(${tgt} PRIVATE Boost::headers)
    else()
        target_include_directories(${tgt} PRIVATE ${Boost_INCLUDE_DIRS})
    endif()
endforeach()

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
        tests/test_l2_parsing.cpp
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

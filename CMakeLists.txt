cmake_minimum_required(VERSION 3.25)
project(cynamoDB VERSION 2.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

include(FetchContent)

# simdjson
set(SIMDJSON_IMPLEMENTATION_HASWELL ON CACHE BOOL "Enable Haswell implementation" FORCE)
set(SIMDJSON_IMPLEMENTATION_ARM64 ON CACHE BOOL "Enable ARM64 implementation" FORCE)
FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG v3.10.1
)
FetchContent_MakeAvailable(simdjson)

# Catch2
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
# Boost
find_package(Boost REQUIRED)

add_library(cynamodb_core STATIC 
    src/http/server.cpp 
    src/auth/sigv4.cpp
    src/api/dispatcher.cpp
    src/json/serializer.cpp
    src/json/json_writer.cpp
    src/utils/buffer_pool.cpp
    src/utils/chaos_engine.cpp
    src/core/memory_manager.cpp
    src/core/scheduler.cpp
    src/engine/table_manager.cpp
    src/engine/capacity/manager.cpp
    src/engine/memory_engine.cpp
    src/engine/item_validator.cpp
    src/engine/lsm/memtable.cpp
    src/engine/lsm/manifest.cpp
    src/engine/lsm/compaction_manager.cpp
    src/engine/lsm/merging_iterator.cpp
    src/engine/lsm/gsi_manager.cpp
    src/engine/lsm/lsi_manager.cpp
    src/engine/lsm/wal.cpp
    src/engine/lsm/lsm_engine.cpp
    src/engine/lsm/sstable.cpp
    src/expressions/lexer.cpp
    src/expressions/parser.cpp
    src/expressions/evaluator.cpp
    src/expressions/partiql/parser.cpp
    src/observability/metrics.cpp
    src/engine/recovery/recovery_manager.cpp
    src/engine/transactions/manager.cpp
    src/streams/manager.cpp
    src/backups/manager.cpp
)
target_include_directories(cynamodb_core PUBLIC include)
target_link_libraries(cynamodb_core PUBLIC simdjson::simdjson Boost::boost)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
    if(IPO_SUPPORTED)
        set_property(TARGET cynamodb_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

add_executable(cynamodb src/main.cpp)
target_link_libraries(cynamodb PRIVATE cynamodb_core)

add_executable(bench_performance tests/bench_performance.cpp)
target_link_libraries(bench_performance PRIVATE cynamodb_core)

enable_testing()
add_subdirectory(tests)

# Fuzzing
option(ENABLE_FUZZING "Enable fuzzing targets" OFF)
include(fuzzing.cmake)
add_fuzz_target(fuzz_json tests/fuzz_json.cpp)
add_fuzz_target(fuzz_expressions tests/fuzz_expressions.cpp)
add_fuzz_target(fuzz_sigv4 tests/fuzz_sigv4.cpp)

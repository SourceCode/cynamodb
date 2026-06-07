cmake_minimum_required(VERSION 3.25)
project(cynamoDB VERSION 2.4.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Strict warning flags for OUR code only. Third-party dependencies fetched via
# FetchContent (Catch2, simdjson) must not be held to -Werror, otherwise a
# deprecation warning in a newer compiler breaks the whole build.
if(MSVC)
    set(CYNAMODB_WARNING_FLAGS /W4 /WX /permissive-)
else()
    set(CYNAMODB_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
    # simdjson's public headers (included by our TUs) use a UDL syntax that newer
    # compilers flag as deprecated. It is not a bug in our code, so keep it a
    # warning instead of an error while leaving -Werror on for everything else.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Wno-error=deprecated-literal-operator
        CYNAMODB_HAS_NO_ERROR_DEPRECATED_LITERAL_OPERATOR)
    if(CYNAMODB_HAS_NO_ERROR_DEPRECATED_LITERAL_OPERATOR)
        list(APPEND CYNAMODB_WARNING_FLAGS -Wno-error=deprecated-literal-operator)
    endif()
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
    src/api/handlers.cpp
    src/json/serializer.cpp
    src/json/json_writer.cpp
    src/utils/buffer_pool.cpp
    src/utils/chaos_engine.cpp
    src/core/memory_manager.cpp
    src/core/scheduler.cpp
    src/engine/table_manager.cpp
    src/engine/capacity/manager.cpp
    src/engine/memory_engine.cpp
    src/engine/key_codec.cpp
    src/engine/item_validator.cpp
    src/engine/lsm/memtable.cpp
    src/engine/lsm/manifest.cpp
    src/engine/lsm/compaction_manager.cpp
    src/engine/lsm/merging_iterator.cpp
    src/engine/lsm/gsi_manager.cpp
    src/engine/lsm/lsi_manager.cpp
    src/engine/lsm/wal.cpp
    src/engine/lsm/record_codec.cpp
    src/engine/lsm/lsm_engine.cpp
    src/engine/lsm/sstable.cpp
    src/expressions/lexer.cpp
    src/expressions/parser.cpp
    src/expressions/evaluator.cpp
    src/expressions/update_expression.cpp
    src/expressions/partiql/parser.cpp
    src/observability/metrics.cpp
    src/engine/recovery/recovery_manager.cpp
    src/engine/transactions/manager.cpp
    src/streams/manager.cpp
    src/backups/manager.cpp
)
target_include_directories(cynamodb_core PUBLIC include)
target_link_libraries(cynamodb_core PUBLIC simdjson::simdjson Boost::boost)
target_compile_options(cynamodb_core PRIVATE ${CYNAMODB_WARNING_FLAGS})

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
    if(IPO_SUPPORTED)
        set_property(TARGET cynamodb_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

add_executable(cynamodb src/main.cpp)
target_link_libraries(cynamodb PRIVATE cynamodb_core)
target_compile_options(cynamodb PRIVATE ${CYNAMODB_WARNING_FLAGS})

add_executable(bench_performance tests/bench_performance.cpp)
target_link_libraries(bench_performance PRIVATE cynamodb_core)
target_compile_options(bench_performance PRIVATE ${CYNAMODB_WARNING_FLAGS})

enable_testing()
add_subdirectory(tests)

# Fuzzing
option(ENABLE_FUZZING "Enable fuzzing targets" OFF)
include(fuzzing.cmake)
add_fuzz_target(fuzz_json tests/fuzz_json.cpp)
add_fuzz_target(fuzz_expressions tests/fuzz_expressions.cpp)
add_fuzz_target(fuzz_sigv4 tests/fuzz_sigv4.cpp)

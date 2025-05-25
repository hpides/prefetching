cmake_minimum_required(VERSION 3.10)

project(Prefetch)

set(CMAKE_CXX_STANDARD 20)


message(STATUS "ARCH: ${CMAKE_SYSTEM_PROCESSOR}")
if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    add_definitions(-DX86_64)
elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    add_definitions(-DAARCH64)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_custom_target(generate_asm
        COMMAND ${CMAKE_CXX_COMPILER} -S -fverbose-asm -o prefetch_latency.s ${CMAKE_SOURCE_DIR}/src/benchmark/prefetch_latency.cpp
        DEPENDS ${CMAKE_SOURCE_DIR}/src/benchmark/prefetch_latency.cpp
        COMMENT "Generating assembly code for prefetch_latency.cpp"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Set the architecture flag based on processor type
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # No direct -march equivalent for MSVC, use appropriate architecture-specific options if necessary
    # Add any MSVC specific flags here if required
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH}) # To allow CMake to locate our Find*.cmake files


set(DEFAULT_LIB_DIRS $ENV{HOME}/local /opt/local /usr/local /usr)
find_package(Boost REQUIRED)
find_package(Numa REQUIRED)


add_subdirectory(third_party)
include(FetchContent)
FetchContent_Declare(
  perf-cpp-external
  GIT_REPOSITORY "https://github.com/tratori/perf-cpp/"
  GIT_TAG "v0.8.3"
)
FetchContent_MakeAvailable(perf-cpp-external)

target_compile_definitions(perf-cpp PUBLIC
  NO_PERF_SAMPLE_WEIGHT_STRUCT
  NO_PERF_SAMPLE_DATA_PAGE_SIZE
  NO_PERF_SAMPLE_CODE_PAGE_SIZE
)

add_subdirectory(src)
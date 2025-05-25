include_directories(
    ${perf-cpp-external_SOURCE_DIR}/include/
)

add_library(config config.cpp)

add_subdirectory(lib)
add_subdirectory(benchmark)
add_subdirectory(test)
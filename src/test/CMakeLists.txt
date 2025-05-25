include_directories(
    ${PROJECT_SOURCE_DIR}/src/lib
)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark)

add_executable(test_memory_allocator_pmr test_memory_allocator_pmr.cpp)

target_link_libraries(test_memory_allocator_pmr prefetching)

add_executable(test_coroutine_thread_switching test_coroutine_thread_switching.cpp)

include_directories(
    ${PROJECT_SOURCE_DIR}/src/lib
    utils
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark)

add_executable(hashmap_benchmark hashmap_benchmark.cpp)

target_link_libraries(hashmap_benchmark hashmap prefetching)

add_executable(prefetch_latency prefetch_latency.cpp)

target_link_libraries(prefetch_latency utils nlohmann_json::nlohmann_json)

add_executable(random_read_benchmark random_read_benchmark.cpp)

target_link_libraries(random_read_benchmark random_access)

add_executable(tree_simulation tree_simulation_benchmark.cpp)

target_link_libraries(tree_simulation prefetching)

add_executable(lfb_size lfb_size_benchmark.cpp)

target_link_libraries(lfb_size prefetching)

add_executable(pc pointer_chasing.cpp)

target_link_libraries(pc prefetching)

add_executable(memory_latencies memory_latencies.cpp)

target_link_libraries(memory_latencies prefetching tinymembench_lib)
target_include_directories(memory_latencies PUBLIC ${PROJECT_SOURCE_DIR}/third_party/tinymembench)

add_executable(lfb_full_behavior lfb_full_behavior.cpp)

target_link_libraries(lfb_full_behavior prefetching)

add_executable(lfb_size_smt lfb_size_benchmark_smt.cpp)

target_link_libraries(lfb_size_smt prefetching)

add_executable(prefetch_locality_behavior prefetch_locality_behavior.cpp)

target_link_libraries(prefetch_locality_behavior prefetching)

add_executable(lfb_scale_benchmark lfb_scale_benchmark.cpp)

target_link_libraries(lfb_scale_benchmark prefetching)

add_executable(cssl cssl.cpp)

target_link_libraries(cssl skiplist)

add_executable(btree_benchmark btree_benchmark.cpp)

target_link_libraries(btree_benchmark prefetching)

add_executable(lfb_reliability lfb_reliability.cpp)

target_link_libraries(lfb_reliability prefetching)

add_executable(rob_pressure rob_pressure.cpp)

target_link_libraries(rob_pressure prefetching)

add_executable(binary_search binary_search.cpp)

target_link_libraries(binary_search prefetching)

add_executable(materialization materialization.cpp)

target_link_libraries(materialization prefetching)

add_executable(test_perf test_perf.cpp)
target_link_libraries(test_perf prefetching)

add_executable(hash_join hash_join.cpp)
target_link_libraries(hash_join prefetching hash_join_balkensen)

add_executable(test_ca_memory_placement test_ca_memory_placement.cpp)
target_link_libraries(test_ca_memory_placement prefetching)


add_executable(cache_size_nvlink cache_size_nvlink.cpp)

target_link_libraries(cache_size_nvlink prefetching)
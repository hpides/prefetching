add_subdirectory(utils)
add_subdirectory(numa)
add_subdirectory(profiling)
add_subdirectory(HashJoin)

include_directories(utils)

add_library(prefetching prefetching.cpp)
target_link_libraries(prefetching PUBLIC utils perf_manager prefetching_numa config)

add_library(hashmap hashmap.cpp)
target_include_directories(hashmap SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/../../third_party/jemalloc/include)
target_link_libraries(hashmap PRIVATE prefetching)
add_library(random_access random_access.cpp)
target_link_libraries(random_access PRIVATE prefetching)

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
endif()
add_library(skiplist skiplist.cpp)
target_link_libraries(skiplist PRIVATE prefetching)

include_directories(BTree)


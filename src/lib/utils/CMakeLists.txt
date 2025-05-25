add_library(utils utils.cpp profiler.cpp runtime_config.cpp simple_continuous_allocator.cpp)
target_link_libraries(utils nlohmann_json::nlohmann_json cxxopts numa)

# Perf Configs

`originally_generated` contains csvs generated using `https://github.com/jmuehlig/perf-cpp` via
the `create_perf_list.py` (see `create_perf_list.sh` in this repository).

`generic_selections` contains a selected subset of perf events for the
used systems. More specifically, interesting memory and cache related counters.

Additionally, in `perf_manager.hpp`, we choose a subset of default counters to be profiled on all systems.
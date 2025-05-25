# "Changelog"

    - Added locking mechanism for ARM systems
    - adjusted timing calls, so that whole hash join is measured, and we get separate timings for build and probe
    - split "npo_thread" into two (probe & build) for easier profiling
    - set PREFETCHING_DISTANCE to 32
    - remove some printf statements to make logging more beautiful
    - moved swpf and naive implementation to same source file for easier code deduplication
    - re-added the proposed prefetch statements to the build phase
    - fix mem leak by destroying pthread attr

Related to `src/benchmark/hash_join.cpp`;

    - add callbacks for profiling and time measurements
    - add callbacks for custom memory allocation
    - add parameter through which cpu id thread placement can be configured


# Benchmarks — Google Benchmark suite. Gated by SOUXMAR_BUILD_BENCHMARKS
# (off by default; enabled by the perf-regression nightly CI).
#
# Each .cpp produces a self-contained benchmark target so individual
# benches can be run in isolation. Tag every target with the
# `souxmar.bench` folder for IDE grouping.

find_package(benchmark CONFIG REQUIRED)

# Reusable helper — keeps target setup boilerplate out of every bench.
function(souxmar_add_benchmark TARGET_NAME)
  add_executable(${TARGET_NAME} ${ARGN})
  target_link_libraries(${TARGET_NAME}
    PRIVATE
      souxmar::core
      souxmar::plugin
      souxmar::pipeline
      souxmar::warnings
      souxmar::sanitizers
      benchmark::benchmark
      benchmark::benchmark_main
  )
  set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "benchmarks")
endfunction()

souxmar_add_benchmark(bench_mesh_construction
  bench_mesh_construction.cpp
)

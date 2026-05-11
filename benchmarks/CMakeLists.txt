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

# Sprint 7 push 3 — mmap vs heap buffer round-trip (ADR-0006 v2).
souxmar_add_benchmark(bench_mmap_buffer
  bench_mmap_buffer.cpp
)

# Sprint 9 push 6 — per-face-tag sparse-map (ADR-0012).
souxmar_add_benchmark(bench_face_tag
  bench_face_tag.cpp
)

# Sprint 9 push 7 — plugin-dispatch hot path. ENGINEERING_PRACTICES.md
# § Performance budgets: "Plugin call overhead (no-op tool) < 20 µs
# (warm)".
souxmar_add_benchmark(bench_plugin_dispatch
  bench_plugin_dispatch.cpp
)

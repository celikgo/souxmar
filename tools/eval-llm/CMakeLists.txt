# souxmar-eval-llm — Sprint 10 push 9 LLM-driven eval runner. Sibling
# of souxmar-eval; see the file-top comment in main.cpp for the why.

find_package(fmt      CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

add_executable(souxmar_eval_llm
  main.cpp
)
add_executable(souxmar::eval_llm ALIAS souxmar_eval_llm)

set_target_properties(souxmar_eval_llm PROPERTIES
  OUTPUT_NAME souxmar-eval-llm
  FOLDER      "cli"
)

target_link_libraries(souxmar_eval_llm
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
    souxmar::ai
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
    yaml-cpp::yaml-cpp
)

install(TARGETS souxmar_eval_llm
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

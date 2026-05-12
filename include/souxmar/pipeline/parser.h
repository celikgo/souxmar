// SPDX-License-Identifier: Apache-2.0
//
// Pipeline YAML parser.
//
// Public format documented in docs/ARCHITECTURE.md. The parser is strict by
// design — a malformed pipeline file is a hard failure, never a silent
// guess. Errors carry source location when yaml-cpp surfaces it.

#pragma once

#include "souxmar/pipeline/pipeline.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace souxmar::pipeline {

struct ParseError {
  std::string message;
  std::optional<std::size_t> line;    // 1-based, when available
  std::optional<std::size_t> column;  // 1-based, when available
};

using ParseResult = std::variant<Pipeline, ParseError>;

[[nodiscard]] ParseResult parse_pipeline(std::string_view yaml_source);
[[nodiscard]] ParseResult parse_pipeline_file(const std::filesystem::path& path);

}  // namespace souxmar::pipeline

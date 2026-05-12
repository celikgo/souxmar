// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/parser.h"

#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace souxmar::pipeline {

namespace {

ParseError make_error(std::string msg,
                      std::optional<std::size_t> line = std::nullopt,
                      std::optional<std::size_t> column = std::nullopt) {
  return ParseError{std::move(msg), line, column};
}

ParseError from_mark(std::string msg, const YAML::Mark& mark) {
  // yaml-cpp uses 0-based lines/columns; expose as 1-based to humans.
  std::optional<std::size_t> line =
      mark.line >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(mark.line + 1))
                     : std::nullopt;
  std::optional<std::size_t> column =
      mark.column >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(mark.column + 1))
                       : std::nullopt;
  return make_error(std::move(msg), line, column);
}

// Recognise the StageRef shorthand `{ from: stage_id }`.
// Returns std::nullopt if the node is not a StageRef in disguise.
std::optional<StageRef> recognise_stage_ref(const YAML::Node& node) {
  if (!node.IsMap() || node.size() != 1)
    return std::nullopt;
  if (!node["from"])
    return std::nullopt;
  const auto& from = node["from"];
  if (!from.IsScalar())
    return std::nullopt;
  return StageRef{from.as<std::string>()};
}

// Convert a yaml-cpp node into a souxmar Value.
std::variant<Value, ParseError> to_value(const YAML::Node& node) {
  if (auto sr = recognise_stage_ref(node)) {
    if (sr->stage_id.empty()) {
      return from_mark("`from:` must reference a non-empty stage id", node.Mark());
    }
    return Value::stage_ref(std::move(sr->stage_id));
  }

  switch (node.Type()) {
    case YAML::NodeType::Null:
      return Value::null_value();

    case YAML::NodeType::Scalar: {
      // yaml-cpp does not carry a typed scalar back; we sniff bool/number/string.
      const auto raw = node.Scalar();
      if (raw == "true" || raw == "True")
        return Value::boolean(true);
      if (raw == "false" || raw == "False")
        return Value::boolean(false);
      try {
        // yaml-cpp's converter handles integer + scientific notation.
        const auto d = node.as<double>();
        // Make sure it round-trips the textual form (rules out a number that
        // looks like a string we've been shown by accident, e.g. "1.2.3").
        std::ostringstream check;
        check << d;
        // ... but allow textual exponent / leading zeros etc. via a permissive
        // policy: any conversion from numeric-looking text becomes a Number.
        if (raw.find_first_not_of("0123456789-+.eE_") == std::string::npos) {
          return Value::number(d);
        }
      } catch (const YAML::TypedBadConversion<double>&) {
        // not a number — fall through
      }
      return Value::string(raw);
    }

    case YAML::NodeType::Sequence: {
      std::vector<Value> items;
      items.reserve(node.size());
      for (const auto& child : node) {
        auto child_val = to_value(child);
        if (auto* err = std::get_if<ParseError>(&child_val))
          return *err;
        items.push_back(std::move(std::get<Value>(child_val)));
      }
      return Value::list(std::move(items));
    }

    case YAML::NodeType::Map: {
      std::map<std::string, Value> fields;
      for (auto it = node.begin(); it != node.end(); ++it) {
        if (!it->first.IsScalar()) {
          return from_mark("map keys must be scalars", it->first.Mark());
        }
        auto key = it->first.as<std::string>();
        auto val = to_value(it->second);
        if (auto* err = std::get_if<ParseError>(&val))
          return *err;
        fields.emplace(std::move(key), std::move(std::get<Value>(val)));
      }
      return Value::map(std::move(fields));
    }

    case YAML::NodeType::Undefined:
      return from_mark("undefined / unparsable YAML node", node.Mark());
  }
  return from_mark("unhandled YAML node type", node.Mark());
}

ParseResult parse_yaml_root(const YAML::Node& root) {
  if (!root.IsMap()) {
    return from_mark("pipeline root must be a mapping", root.Mark());
  }

  Pipeline p;

  // `version` is required.
  if (auto v = root["version"]) {
    if (!v.IsScalar()) {
      return from_mark("`version` must be an integer scalar", v.Mark());
    }
    try {
      const auto n = v.as<int64_t>();
      if (n < 1 || n > std::numeric_limits<std::int32_t>::max()) {
        return from_mark(fmt::format("`version` out of range (got {})", n), v.Mark());
      }
      p.version = static_cast<std::int32_t>(n);
    } catch (const YAML::TypedBadConversion<int64_t>&) {
      return from_mark("`version` must be an integer", v.Mark());
    }
  } else {
    return make_error("missing required field 'version'");
  }

  if (p.version != 1) {
    return make_error(fmt::format(
        "pipeline `version` = {} not supported by this parser (only v1 known)", p.version));
  }

  // `stages` is required and must be a sequence.
  auto stages_node = root["stages"];
  if (!stages_node) {
    return make_error("missing required field 'stages'");
  }
  if (!stages_node.IsSequence()) {
    return from_mark("`stages` must be a sequence", stages_node.Mark());
  }
  if (stages_node.size() == 0) {
    return from_mark("`stages` must list at least one stage", stages_node.Mark());
  }

  std::unordered_set<std::string> seen_ids;
  p.stages.reserve(stages_node.size());
  for (const auto& stage_node : stages_node) {
    if (!stage_node.IsMap()) {
      return from_mark("each stage must be a mapping", stage_node.Mark());
    }

    Stage stage;

    if (auto id = stage_node["id"]) {
      if (!id.IsScalar()) {
        return from_mark("`id` must be a scalar", id.Mark());
      }
      stage.id = id.as<std::string>();
      if (stage.id.empty()) {
        return from_mark("`id` must be non-empty", id.Mark());
      }
    } else {
      return from_mark("stage missing required `id`", stage_node.Mark());
    }

    if (!seen_ids.insert(stage.id).second) {
      return from_mark(fmt::format("duplicate stage id '{}'", stage.id), stage_node.Mark());
    }

    if (auto plug = stage_node["plugin"]) {
      if (!plug.IsScalar()) {
        return from_mark("`plugin` must be a scalar", plug.Mark());
      }
      stage.plugin = plug.as<std::string>();
      if (stage.plugin.empty()) {
        return from_mark("`plugin` must be non-empty", plug.Mark());
      }
    } else {
      return from_mark(fmt::format("stage '{}' missing required `plugin`", stage.id),
                       stage_node.Mark());
    }

    if (auto input = stage_node["input"]) {
      auto v = to_value(input);
      if (auto* err = std::get_if<ParseError>(&v))
        return *err;
      stage.input = std::move(std::get<Value>(v));
      if (stage.input.kind() != Value::Kind::Map) {
        return from_mark(fmt::format("stage '{}' input must be a mapping", stage.id), input.Mark());
      }
    } else {
      // Empty input — produce an empty map so downstream code can treat input
      // uniformly.
      stage.input = Value::map({});
    }

    p.stages.push_back(std::move(stage));
  }

  return p;
}

}  // namespace

ParseResult parse_pipeline(std::string_view yaml_source) {
  YAML::Node root;
  try {
    root = YAML::Load(std::string{yaml_source});
  } catch (const YAML::ParserException& e) {
    return from_mark(fmt::format("YAML parse error: {}", e.msg), e.mark);
  } catch (const YAML::Exception& e) {
    return make_error(fmt::format("YAML error: {}", e.what()));
  }
  return parse_yaml_root(root);
}

ParseResult parse_pipeline_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_error(fmt::format("cannot open pipeline file '{}'", path.string()));
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return parse_pipeline(buf.str());
}

}  // namespace souxmar::pipeline

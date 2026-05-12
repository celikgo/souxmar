// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/value.h"

#include <fmt/core.h>

#include <stdexcept>
#include <utility>

namespace souxmar::pipeline {

namespace {

[[noreturn]] void wrong_kind(Value::Kind expected, Value::Kind actual) {
  throw std::runtime_error(
      fmt::format("Value: requested as {}, but holds {}", kind_name(expected), kind_name(actual)));
}

}  // namespace

std::string_view kind_name(Value::Kind k) noexcept {
  switch (k) {
    case Value::Kind::Null:
      return "null";
    case Value::Kind::Bool:
      return "bool";
    case Value::Kind::Number:
      return "number";
    case Value::Kind::String:
      return "string";
    case Value::Kind::Stage:
      return "stage-ref";
    case Value::Kind::List:
      return "list";
    case Value::Kind::Map:
      return "map";
  }
  return "?";
}

Value::Value() = default;
Value::~Value() = default;
Value::Value(const Value&) = default;
Value::Value(Value&&) noexcept = default;
Value& Value::operator=(const Value&) = default;
Value& Value::operator=(Value&&) noexcept = default;

Value Value::null_value() {
  Value v;
  v.kind_ = Kind::Null;
  v.data_ = std::monostate{};
  return v;
}

Value Value::boolean(bool b) {
  Value v;
  v.kind_ = Kind::Bool;
  v.data_ = b;
  return v;
}

Value Value::number(double n) {
  Value v;
  v.kind_ = Kind::Number;
  v.data_ = n;
  return v;
}

Value Value::string(std::string s) {
  Value v;
  v.kind_ = Kind::String;
  v.data_ = std::move(s);
  return v;
}

Value Value::stage_ref(std::string stage_id) {
  Value v;
  v.kind_ = Kind::Stage;
  v.data_ = StageRef{std::move(stage_id)};
  return v;
}

Value Value::list(std::vector<Value> items) {
  Value v;
  v.kind_ = Kind::List;
  v.data_ = std::move(items);
  return v;
}

Value Value::map(std::map<std::string, Value> fields) {
  Value v;
  v.kind_ = Kind::Map;
  v.data_ = std::move(fields);
  return v;
}

Value::Kind Value::kind() const noexcept {
  return kind_;
}

bool Value::as_bool() const {
  if (kind_ != Kind::Bool)
    wrong_kind(Kind::Bool, kind_);
  return std::get<bool>(data_);
}

double Value::as_number() const {
  if (kind_ != Kind::Number)
    wrong_kind(Kind::Number, kind_);
  return std::get<double>(data_);
}

std::string_view Value::as_string() const {
  if (kind_ != Kind::String)
    wrong_kind(Kind::String, kind_);
  return std::get<std::string>(data_);
}

const StageRef& Value::as_stage() const {
  if (kind_ != Kind::Stage)
    wrong_kind(Kind::Stage, kind_);
  return std::get<StageRef>(data_);
}

std::span<const Value> Value::as_list() const {
  if (kind_ != Kind::List)
    wrong_kind(Kind::List, kind_);
  const auto& vec = std::get<std::vector<Value>>(data_);
  return {vec.data(), vec.size()};
}

const std::map<std::string, Value>& Value::as_map() const {
  if (kind_ != Kind::Map)
    wrong_kind(Kind::Map, kind_);
  return std::get<std::map<std::string, Value>>(data_);
}

const bool* Value::try_bool() const noexcept {
  return std::get_if<bool>(&data_);
}

const double* Value::try_number() const noexcept {
  return std::get_if<double>(&data_);
}

const std::string* Value::try_string() const noexcept {
  return std::get_if<std::string>(&data_);
}

const StageRef* Value::try_stage() const noexcept {
  return std::get_if<StageRef>(&data_);
}

const std::vector<Value>* Value::try_list() const noexcept {
  return std::get_if<std::vector<Value>>(&data_);
}

const std::map<std::string, Value>* Value::try_map() const noexcept {
  return std::get_if<std::map<std::string, Value>>(&data_);
}

const Value* Value::find(std::string_view key) const noexcept {
  const auto* m = try_map();
  if (!m)
    return nullptr;
  auto it = m->find(std::string{key});
  return it != m->end() ? &it->second : nullptr;
}

bool Value::operator==(const Value& other) const {
  if (kind_ != other.kind_)
    return false;
  return data_ == other.data_;
}

}  // namespace souxmar::pipeline

// ============================================================================
// YAML ↔ Value
// ============================================================================
//
// We delegate parsing to yaml-cpp (already a dep) and emission to a small
// hand-rolled writer so the output is stable across yaml-cpp versions
// (their emitter has stylistic drift between releases).

#include <yaml-cpp/yaml.h>

#include <sstream>

namespace souxmar::pipeline {

namespace {

Value yaml_node_to_value(const YAML::Node& n) {
  switch (n.Type()) {
    case YAML::NodeType::Null:
    case YAML::NodeType::Undefined:
      return Value::null_value();
    case YAML::NodeType::Scalar: {
      // Try the typed conversions in priority order: bool, int, double,
      // string. yaml-cpp's tag-free scalars are stringly-typed; the
      // conversion attempts are how we recover types deterministically.
      bool b;
      if (YAML::convert<bool>::decode(n, b))
        return Value::boolean(b);
      try {
        double d = n.as<double>();
        return Value::number(d);
      } catch (const YAML::TypedBadConversion<double>&) {}
      return Value::string(n.as<std::string>());
    }
    case YAML::NodeType::Sequence: {
      std::vector<Value> items;
      items.reserve(n.size());
      for (const auto& item : n)
        items.push_back(yaml_node_to_value(item));
      return Value::list(std::move(items));
    }
    case YAML::NodeType::Map: {
      // Single-key `{from: <stage_id>}` is the StageRef sugar, mirroring
      // the pipeline parser.
      if (n.size() == 1 && n["from"] && n["from"].IsScalar()) {
        return Value::stage_ref(n["from"].as<std::string>());
      }
      std::map<std::string, Value> entries;
      for (const auto& kv : n) {
        entries.emplace(kv.first.as<std::string>(), yaml_node_to_value(kv.second));
      }
      return Value::map(std::move(entries));
    }
  }
  return Value::null_value();
}

// Quote a string scalar conservatively — wrap in double quotes if it
// contains any character that might confuse a plain YAML scalar. Escape
// embedded quotes + backslashes. Good enough for tool I/O; full
// YAML-compliant escaping is a Sprint 5 hardening task.
std::string quote_if_needed(std::string_view s) {
  bool needs = s.empty();
  for (char c : s) {
    if (c == ':' || c == '#' || c == '\n' || c == '"' || c == '\'' || c == '{' || c == '}'
        || c == '[' || c == ']' || c == ',' || c == '&' || c == '*' || c == '!' || c == '|'
        || c == '>' || c == '%' || c == '@' || c == '`' || c == '\\' || c < 0x20) {
      needs = true;
      break;
    }
  }
  if (!needs
      && (s == "true" || s == "false" || s == "null" || s == "~" || s == "yes" || s == "no"
          || s == "on" || s == "off")) {
    needs = true;
  }
  if (!needs)
    return std::string(s);
  std::string out;
  out.reserve(s.size() + 2);
  out += '"';
  for (char c : s) {
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else
      out += c;
  }
  out += '"';
  return out;
}

void emit_indented(std::ostringstream& os, const Value& v, int indent);

void emit_indented(std::ostringstream& os, const Value& v, int indent) {
  const std::string pad(static_cast<std::size_t>(indent), ' ');
  switch (v.kind()) {
    case Value::Kind::Null:
      os << "null";
      return;
    case Value::Kind::Bool:
      os << (v.as_bool() ? "true" : "false");
      return;
    case Value::Kind::Number: {
      // Use a stable representation; integers render without a trailing
      // ".0", non-integers preserve enough precision for round-trip.
      double d = v.as_number();
      if (d == static_cast<double>(static_cast<long long>(d))) {
        os << static_cast<long long>(d);
      } else {
        os << fmt::format("{}", d);
      }
      return;
    }
    case Value::Kind::String:
      os << quote_if_needed(v.as_string());
      return;
    case Value::Kind::Stage:
      os << "{from: " << quote_if_needed(v.as_stage().stage_id) << "}";
      return;
    case Value::Kind::List: {
      const auto items = v.as_list();
      if (items.empty()) {
        os << "[]";
        return;
      }
      for (const auto& item : items) {
        os << "\n" << pad << "- ";
        emit_indented(os, item, indent + 2);
      }
      return;
    }
    case Value::Kind::Map: {
      const auto& m = v.as_map();
      if (m.empty()) {
        os << "{}";
        return;
      }
      for (const auto& [k, child] : m) {
        os << "\n" << pad << quote_if_needed(k) << ": ";
        emit_indented(os, child, indent + 2);
      }
      return;
    }
  }
}

}  // namespace

Value parse_value_yaml(std::string_view yaml_source) {
  YAML::Node root = YAML::Load(std::string(yaml_source));
  return yaml_node_to_value(root);
}

std::string emit_value_yaml(const Value& value) {
  std::ostringstream os;
  // emit_indented prepends a newline for List/Map; strip the leading
  // newline so the output is friendly when captured into a string + printed.
  emit_indented(os, value, 0);
  auto s = os.str();
  if (!s.empty() && s.front() == '\n')
    s.erase(0, 1);
  return s;
}

}  // namespace souxmar::pipeline

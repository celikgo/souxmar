// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/value.h"

#include <fmt/core.h>

#include <stdexcept>
#include <utility>

namespace souxmar::pipeline {

namespace {

[[noreturn]] void wrong_kind(Value::Kind expected, Value::Kind actual) {
  throw std::runtime_error(fmt::format(
      "Value: requested as {}, but holds {}",
      kind_name(expected), kind_name(actual)));
}

}  // namespace

std::string_view kind_name(Value::Kind k) noexcept {
  switch (k) {
    case Value::Kind::Null:   return "null";
    case Value::Kind::Bool:   return "bool";
    case Value::Kind::Number: return "number";
    case Value::Kind::String: return "string";
    case Value::Kind::Stage:  return "stage-ref";
    case Value::Kind::List:   return "list";
    case Value::Kind::Map:    return "map";
  }
  return "?";
}

Value::Value()  = default;
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

Value::Kind Value::kind() const noexcept { return kind_; }

bool Value::as_bool() const {
  if (kind_ != Kind::Bool) wrong_kind(Kind::Bool, kind_);
  return std::get<bool>(data_);
}

double Value::as_number() const {
  if (kind_ != Kind::Number) wrong_kind(Kind::Number, kind_);
  return std::get<double>(data_);
}

std::string_view Value::as_string() const {
  if (kind_ != Kind::String) wrong_kind(Kind::String, kind_);
  return std::get<std::string>(data_);
}

const StageRef& Value::as_stage() const {
  if (kind_ != Kind::Stage) wrong_kind(Kind::Stage, kind_);
  return std::get<StageRef>(data_);
}

std::span<const Value> Value::as_list() const {
  if (kind_ != Kind::List) wrong_kind(Kind::List, kind_);
  const auto& vec = std::get<std::vector<Value>>(data_);
  return {vec.data(), vec.size()};
}

const std::map<std::string, Value>& Value::as_map() const {
  if (kind_ != Kind::Map) wrong_kind(Kind::Map, kind_);
  return std::get<std::map<std::string, Value>>(data_);
}

const bool*   Value::try_bool()   const noexcept { return std::get_if<bool>(&data_); }
const double* Value::try_number() const noexcept { return std::get_if<double>(&data_); }
const std::string* Value::try_string() const noexcept { return std::get_if<std::string>(&data_); }
const StageRef* Value::try_stage() const noexcept { return std::get_if<StageRef>(&data_); }
const std::vector<Value>* Value::try_list() const noexcept {
  return std::get_if<std::vector<Value>>(&data_);
}
const std::map<std::string, Value>* Value::try_map() const noexcept {
  return std::get_if<std::map<std::string, Value>>(&data_);
}

const Value* Value::find(std::string_view key) const noexcept {
  const auto* m = try_map();
  if (!m) return nullptr;
  auto it = m->find(std::string{key});
  return it != m->end() ? &it->second : nullptr;
}

bool Value::operator==(const Value& other) const {
  if (kind_ != other.kind_) return false;
  return data_ == other.data_;
}

}  // namespace souxmar::pipeline

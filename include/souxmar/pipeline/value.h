// SPDX-License-Identifier: Apache-2.0
//
// Value — the typed-tree representation of pipeline-stage inputs.
//
// Pipeline YAML decodes into a tree of Values. The tree carries only data
// the orchestrator manipulates: scalars, lists, maps, and StageRef (a
// reference to another stage's output). Heavyweight types (Geometry, Mesh,
// Field) never appear in Value — they live in the runner's result store
// and are referenced by StageRef.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::pipeline {

// Reference to another stage's output, e.g. `{ from: import }` in YAML.
struct StageRef {
  std::string stage_id;

  [[nodiscard]] bool operator==(const StageRef&) const noexcept = default;
};

class Value {
 public:
  enum class Kind : std::uint8_t {
    Null = 0,
    Bool = 1,
    Number = 2,
    String = 3,
    Stage = 4,  // StageRef
    List = 5,
    Map = 6,
  };

  // ---- Construction ----

  Value();  // Null
  ~Value();
  Value(const Value&);
  Value(Value&&) noexcept;
  Value& operator=(const Value&);
  Value& operator=(Value&&) noexcept;

  static Value null_value();
  static Value boolean(bool b);
  static Value number(double n);
  static Value string(std::string s);
  static Value stage_ref(std::string stage_id);
  static Value list(std::vector<Value> items);
  static Value map(std::map<std::string, Value> fields);

  // ---- Inspection ----

  [[nodiscard]] Kind kind() const noexcept;

  [[nodiscard]] bool is(Kind k) const noexcept {
    return kind() == k;
  }

  // Throws std::runtime_error if the active kind does not match.
  [[nodiscard]] bool as_bool() const;
  [[nodiscard]] double as_number() const;
  [[nodiscard]] std::string_view as_string() const;
  [[nodiscard]] const StageRef& as_stage() const;
  [[nodiscard]] std::span<const Value> as_list() const;
  [[nodiscard]] const std::map<std::string, Value>& as_map() const;

  // Convenience: try-get returns nullptr if wrong kind.
  [[nodiscard]] const bool* try_bool() const noexcept;
  [[nodiscard]] const double* try_number() const noexcept;
  [[nodiscard]] const std::string* try_string() const noexcept;
  [[nodiscard]] const StageRef* try_stage() const noexcept;
  [[nodiscard]] const std::vector<Value>* try_list() const noexcept;
  [[nodiscard]] const std::map<std::string, Value>* try_map() const noexcept;

  // Convenience: Map field access. Returns nullptr if not a Map or no such key.
  [[nodiscard]] const Value* find(std::string_view key) const noexcept;

  // ---- Comparison / printing ----

  [[nodiscard]] bool operator==(const Value& other) const;

 private:
  Kind kind_{Kind::Null};
  std::variant<std::monostate,
               bool,
               double,
               std::string,
               StageRef,
               std::vector<Value>,
               std::map<std::string, Value>>
      data_;
};

[[nodiscard]] std::string_view kind_name(Value::Kind k) noexcept;

// Generic YAML ↔ Value helpers — used by the CLI agent shim and tests
// that want to feed Python-like literal trees into tools. Strict by
// design: unknown YAML constructs throw via the underlying yaml-cpp.
// StageRef is recognised as the `{ from: <stage_id> }` shorthand for
// parity with the pipeline parser.
[[nodiscard]] Value parse_value_yaml(std::string_view yaml_source);
[[nodiscard]] std::string emit_value_yaml(const Value& value);

}  // namespace souxmar::pipeline

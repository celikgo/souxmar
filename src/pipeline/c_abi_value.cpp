// SPDX-License-Identifier: Apache-2.0
//
// C ABI implementation of souxmar_value_* — opaque pointer cast to
// souxmar::pipeline::Value*. Lives in libsouxmar-pipeline because that's
// where Value is defined.

#include "souxmar/pipeline/value.h"

#include "souxmar-c/value.h"

#include <string>

namespace {

const souxmar::pipeline::Value* as_cpp(const souxmar_value_t* p) noexcept {
  return reinterpret_cast<const souxmar::pipeline::Value*>(p);
}

const souxmar_value_t* as_c(const souxmar::pipeline::Value* p) noexcept {
  return reinterpret_cast<const souxmar_value_t*>(p);
}

}  // namespace

extern "C" {

uint8_t souxmar_value_kind(const souxmar_value_t* value) {
  if (!value)
    return SOUXMAR_VK_NULL;
  return static_cast<uint8_t>(as_cpp(value)->kind());
}

int souxmar_value_as_bool(const souxmar_value_t* value) {
  if (!value)
    return 0;
  const auto* b = as_cpp(value)->try_bool();
  return b ? (*b ? 1 : 0) : 0;
}

double souxmar_value_as_number(const souxmar_value_t* value) {
  if (!value)
    return 0.0;
  const auto* n = as_cpp(value)->try_number();
  return n ? *n : 0.0;
}

const char* souxmar_value_as_string(const souxmar_value_t* value) {
  if (!value)
    return nullptr;
  const auto* s = as_cpp(value)->try_string();
  return s ? s->c_str() : nullptr;
}

const char* souxmar_value_as_stage(const souxmar_value_t* value) {
  if (!value)
    return nullptr;
  const auto* s = as_cpp(value)->try_stage();
  return s ? s->stage_id.c_str() : nullptr;
}

size_t souxmar_value_list_size(const souxmar_value_t* value) {
  if (!value)
    return 0;
  const auto* l = as_cpp(value)->try_list();
  return l ? l->size() : 0;
}

const souxmar_value_t* souxmar_value_list_at(const souxmar_value_t* value, size_t index) {
  if (!value)
    return nullptr;
  const auto* l = as_cpp(value)->try_list();
  if (!l || index >= l->size())
    return nullptr;
  return as_c(&(*l)[index]);
}

const souxmar_value_t* souxmar_value_map_get(const souxmar_value_t* value, const char* key) {
  if (!value || !key)
    return nullptr;
  const auto* m = as_cpp(value)->try_map();
  if (!m)
    return nullptr;
  auto it = m->find(std::string{key});
  return it != m->end() ? as_c(&it->second) : nullptr;
}

size_t souxmar_value_map_size(const souxmar_value_t* value) {
  if (!value)
    return 0;
  const auto* m = as_cpp(value)->try_map();
  return m ? m->size() : 0;
}

}  // extern "C"

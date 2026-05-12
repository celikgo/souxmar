// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/field.h"

#include "souxmar-c/field.h"

#include <string>

namespace {

souxmar::core::Field* as_cpp(souxmar_field_t* p) noexcept {
  return reinterpret_cast<souxmar::core::Field*>(p);
}

const souxmar::core::Field* as_cpp(const souxmar_field_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::Field*>(p);
}

souxmar_field_t* as_c(souxmar::core::Field* p) noexcept {
  return reinterpret_cast<souxmar_field_t*>(p);
}

}  // namespace

extern "C" {

souxmar_field_t* souxmar_field_new(const char* name,
                                   uint8_t location,
                                   uint8_t kind,
                                   size_t count,
                                   size_t num_time_steps) {
  if (!name)
    return nullptr;
  try {
    return as_c(new souxmar::core::Field{
        std::string(name),
        static_cast<souxmar::core::FieldLocation>(location),
        static_cast<souxmar::core::FieldKind>(kind),
        count,
        num_time_steps,
    });
  } catch (...) {
    return nullptr;
  }
}

void souxmar_field_free(souxmar_field_t* field) {
  delete as_cpp(field);
}

const char* souxmar_field_name(const souxmar_field_t* field) {
  if (!field)
    return nullptr;
  // Field::name() returns string_view to the impl's stored std::string, so
  // .data() is null-terminated as long as the field exists.
  return as_cpp(field)->name().data();
}

uint8_t souxmar_field_location(const souxmar_field_t* field) {
  if (!field)
    return 0;
  return static_cast<uint8_t>(as_cpp(field)->location());
}

uint8_t souxmar_field_kind(const souxmar_field_t* field) {
  if (!field)
    return 0;
  return static_cast<uint8_t>(as_cpp(field)->kind());
}

uint8_t souxmar_field_components(const souxmar_field_t* field) {
  return field ? as_cpp(field)->components() : 0;
}

size_t souxmar_field_count(const souxmar_field_t* field) {
  return field ? as_cpp(field)->count() : 0;
}

size_t souxmar_field_num_time_steps(const souxmar_field_t* field) {
  return field ? as_cpp(field)->num_time_steps() : 0;
}

double* souxmar_field_data(souxmar_field_t* field) {
  return field ? as_cpp(field)->data().data() : nullptr;
}

const double* souxmar_field_data_const(const souxmar_field_t* field) {
  return field ? as_cpp(field)->data().data() : nullptr;
}

size_t souxmar_field_data_size(const souxmar_field_t* field) {
  return field ? as_cpp(field)->data().size() : 0;
}

}  // extern "C"

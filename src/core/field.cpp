// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/field.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace souxmar::core {

class Field::Impl {
 public:
  std::string         name;
  FieldLocation       location;
  FieldKind           kind;
  std::size_t         count;
  std::size_t         num_time_steps;
  std::vector<double> data;
};

Field::Field(std::string    name,
             FieldLocation  location,
             FieldKind      kind,
             std::size_t    count,
             std::size_t    num_time_steps)
    : impl_(std::make_unique<Impl>()) {
  if (num_time_steps == 0) {
    throw std::invalid_argument("Field: num_time_steps must be >= 1");
  }
  impl_->name           = std::move(name);
  impl_->location       = location;
  impl_->kind           = kind;
  impl_->count          = count;
  impl_->num_time_steps = num_time_steps;
  impl_->data.assign(count * souxmar::core::components(kind) * num_time_steps, 0.0);
}

Field::~Field() = default;
Field::Field(Field&&) noexcept = default;
Field& Field::operator=(Field&&) noexcept = default;

std::string_view Field::name() const noexcept     { return impl_->name; }
FieldLocation    Field::location() const noexcept { return impl_->location; }
FieldKind        Field::kind() const noexcept     { return impl_->kind; }
std::uint8_t     Field::components() const noexcept {
  return souxmar::core::components(impl_->kind);
}
std::size_t Field::count() const noexcept           { return impl_->count; }
std::size_t Field::num_time_steps() const noexcept  { return impl_->num_time_steps; }

std::span<double>       Field::data() noexcept       { return impl_->data; }
std::span<const double> Field::data() const noexcept { return impl_->data; }

std::span<double> Field::step(std::size_t time_index) {
  if (time_index >= impl_->num_time_steps) {
    throw std::out_of_range("Field::step: time index out of range");
  }
  const auto stride = impl_->count * components();
  return {impl_->data.data() + time_index * stride, stride};
}

std::span<const double> Field::step(std::size_t time_index) const {
  if (time_index >= impl_->num_time_steps) {
    throw std::out_of_range("Field::step: time index out of range");
  }
  const auto stride = impl_->count * components();
  return {impl_->data.data() + time_index * stride, stride};
}

std::span<double> Field::at(std::size_t location_index, std::size_t time_index) {
  if (location_index >= impl_->count) {
    throw std::out_of_range("Field::at: location index out of range");
  }
  auto s = step(time_index);
  return {s.data() + location_index * components(), components()};
}

std::span<const double> Field::at(std::size_t location_index, std::size_t time_index) const {
  if (location_index >= impl_->count) {
    throw std::out_of_range("Field::at: location index out of range");
  }
  auto s = step(time_index);
  return {s.data() + location_index * components(), components()};
}

}  // namespace souxmar::core

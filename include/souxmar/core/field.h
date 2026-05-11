// SPDX-License-Identifier: Apache-2.0
//
// Field — DOF data over a Mesh: scalar / vector / tensor, at a chosen
// location (nodal / cell / face / Gauss-point), optionally over time.
//
// Storage is contiguous: a flat double buffer of size
//   count * components(kind) * num_time_steps.
// Stride is { components, components*count } for (location, time) — i.e.
// "all locations at time 0, all locations at time 1, ..." — which matches
// VTK and FEniCSx conventions and amortises the most common access pattern
// (whole-field read at a single time).

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace souxmar::core {

enum class FieldLocation : std::uint8_t {
  Nodal       = 0,  // values at mesh nodes
  Cell        = 1,  // one value per cell
  Face        = 2,  // one value per cell face
  GaussPoint  = 3,  // values at quadrature points (per-cell quadrature rule)
};

enum class FieldKind : std::uint8_t {
  Scalar = 0,  // 1 component
  Vector = 1,  // 3 components
  Tensor = 2,  // 9 components (full 3x3, row-major)
};

[[nodiscard]] constexpr std::uint8_t components(FieldKind k) noexcept {
  switch (k) {
    case FieldKind::Scalar: return 1;
    case FieldKind::Vector: return 3;
    case FieldKind::Tensor: return 9;
  }
  return 1;
}

class Field {
 public:
  // Allocates `count * components(kind) * num_time_steps` doubles, zero-initialised.
  // Throws std::bad_alloc on allocation failure.
  Field(std::string    name,
        FieldLocation  location,
        FieldKind      kind,
        std::size_t    count,
        std::size_t    num_time_steps = 1);

  ~Field();

  Field(Field&&) noexcept;
  Field& operator=(Field&&) noexcept;

  Field(const Field&)            = delete;
  Field& operator=(const Field&) = delete;

  // -------- Metadata --------

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] FieldLocation    location() const noexcept;
  [[nodiscard]] FieldKind        kind() const noexcept;
  [[nodiscard]] std::uint8_t     components() const noexcept;
  [[nodiscard]] std::size_t      count() const noexcept;
  [[nodiscard]] std::size_t      num_time_steps() const noexcept;

  // -------- Data access --------

  // Whole-field span. Size = count * components * num_time_steps.
  [[nodiscard]] std::span<double>       data() noexcept;
  [[nodiscard]] std::span<const double> data() const noexcept;

  // One time step. Size = count * components.
  [[nodiscard]] std::span<double>       step(std::size_t time_index);
  [[nodiscard]] std::span<const double> step(std::size_t time_index) const;

  // One location at one time step. Size = components.
  [[nodiscard]] std::span<double>       at(std::size_t location_index, std::size_t time_index = 0);
  [[nodiscard]] std::span<const double> at(std::size_t location_index, std::size_t time_index = 0) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core

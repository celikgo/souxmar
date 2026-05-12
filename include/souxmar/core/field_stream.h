// SPDX-License-Identifier: Apache-2.0
//
// FieldStream — derived renderer-friendly view over a Field.
//
// Computes per-component min/max in f64 (preserves source precision for
// the colormap legend) and exposes the field values as an f32 SoA
// (renderer-friendly, one down-cast on the C side instead of two).
//
// Operates on the field's first time step. Multi-frame transient
// playback gets its own surface in v1.9 (timeseries.h, ADR-0042).
//
// Eager construction; the Field is not retained after the constructor
// returns. The documented C ABI contract says freeing the field while
// the stream is open is UB (preserves forward-compat with a future
// lazy-recompute impl) — the current eager impl is independent.
//
// See docs/rfcs/0002-field-stream-protocol.md and ADR-0038.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace souxmar::core {

class Field;

class FieldStream {
 public:
  explicit FieldStream(const Field& field);
  ~FieldStream();

  FieldStream(FieldStream&&) noexcept;
  FieldStream& operator=(FieldStream&&) noexcept;

  FieldStream(const FieldStream&)            = delete;
  FieldStream& operator=(const FieldStream&) = delete;

  // Number of locations (one value-per-component-per-location).
  [[nodiscard]] std::size_t  count()       const noexcept;
  [[nodiscard]] std::uint8_t components()  const noexcept;

  // Per-component min/max in f64. Size of each span = components().
  [[nodiscard]] std::span<const double> range_min() const noexcept;
  [[nodiscard]] std::span<const double> range_max() const noexcept;

  // Borrowed string; empty if the field has no units attached.
  [[nodiscard]] std::string_view units() const noexcept;

  // f32 SoA of all values for the first time step.
  // Size = count() * components().
  [[nodiscard]] std::span<const float> values() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core

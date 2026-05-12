// SPDX-License-Identifier: Apache-2.0
//
// FieldStream — per-component min/max cache + f32 SoA down-cast.
//
// Sprint 27 PR 1 (real implementation; replaces the stub landed in
// the post-v1.0 ABI scaffold commit).
//
// Algorithm:
//   1. Read the first time step's contiguous SoA from the Field.
//   2. Walk it once computing per-component min/max in f64 (preserves
//      source precision; the legend reads these via range_min/_max).
//   3. Down-cast the f64 buffer to f32 in one pass for the renderer-
//      side SoA upload. The f32 buffer is what the bridge ships to JS.
//
// Units are not on the Field handle today — RFC-002 Open Q1 (a
// future v1.5.1 ratchet adds souxmar_field_units accessor); for now,
// units() returns an empty string and consumers fall back to
// "" / "unitless".

#include "souxmar/core/field_stream.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "souxmar/core/field.h"

namespace souxmar::core {

class FieldStream::Impl {
 public:
  std::size_t       count       = 0;
  std::uint8_t      components  = 0;
  std::vector<double> rmin;       // size = components
  std::vector<double> rmax;       // size = components
  std::vector<float>  values_f32; // size = count * components
  std::string         units;

  explicit Impl(const Field& field) {
    count      = field.count();
    components = field.components();
    units      = "";  // RFC-002 Open Q1 follow-up

    if (count == 0 || components == 0) {
      return;
    }

    const auto src = field.step(0);  // first time step; size = count * components
    const std::size_t expected = count * components;
    // Defensive: if the field's storage shape ever diverges from
    // (count * components), we'd silently scribble; check + bail.
    if (src.size() != expected) {
      return;
    }

    // Init min/max per component from the first location.
    rmin.assign(components, std::numeric_limits<double>::infinity());
    rmax.assign(components, -std::numeric_limits<double>::infinity());

    values_f32.resize(expected);
    for (std::size_t i = 0; i < count; ++i) {
      for (std::uint8_t c = 0; c < components; ++c) {
        const double v = src[i * components + c];
        if (v < rmin[c]) rmin[c] = v;
        if (v > rmax[c]) rmax[c] = v;
        values_f32[i * components + c] = static_cast<float>(v);
      }
    }
  }
};

FieldStream::FieldStream(const Field& field)
    : impl_(std::make_unique<Impl>(field)) {}

FieldStream::~FieldStream()                                  = default;
FieldStream::FieldStream(FieldStream&&) noexcept            = default;
FieldStream& FieldStream::operator=(FieldStream&&) noexcept = default;

std::size_t  FieldStream::count()      const noexcept { return impl_->count; }
std::uint8_t FieldStream::components() const noexcept { return impl_->components; }

std::span<const double> FieldStream::range_min() const noexcept { return impl_->rmin; }
std::span<const double> FieldStream::range_max() const noexcept { return impl_->rmax; }

std::string_view FieldStream::units() const noexcept { return impl_->units; }

std::span<const float> FieldStream::values() const noexcept { return impl_->values_f32; }

}  // namespace souxmar::core

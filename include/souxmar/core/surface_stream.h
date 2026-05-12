// SPDX-License-Identifier: Apache-2.0
//
// SurfaceStream — derived renderer-friendly view over a Mesh.
//
// Computes the outer shell (boundary faces of 3D cells, plus 2D cells as
// their own surface), triangulates quads into pairs, computes
// area-weighted per-vertex normals, and exposes the result as SoA buffers
// suitable for direct upload into Three.js BufferGeometry.
//
// Eager construction: everything is computed at construction time and
// stored. The Mesh is *not* retained — the documented C ABI contract
// says freeing the mesh while the stream is open is UB (preserves
// forward-compat with a future lazy-cache impl), but the current eager
// impl is independent of mesh lifetime after the constructor returns.
//
// See docs/rfcs/0001-viewport-renderer.md and ADR-0037.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace souxmar::core {

class Mesh;

class SurfaceStream {
 public:
  explicit SurfaceStream(const Mesh& mesh);
  ~SurfaceStream();

  SurfaceStream(SurfaceStream&&) noexcept;
  SurfaceStream& operator=(SurfaceStream&&) noexcept;

  SurfaceStream(const SurfaceStream&) = delete;
  SurfaceStream& operator=(const SurfaceStream&) = delete;

  [[nodiscard]] std::size_t vertex_count() const noexcept;
  [[nodiscard]] std::size_t triangle_count() const noexcept;

  [[nodiscard]] std::array<double, 3> bounds_min() const noexcept;
  [[nodiscard]] std::array<double, 3> bounds_max() const noexcept;

  // SoA buffers. positions/normals are 3 * vertex_count floats;
  // indices is 3 * triangle_count uint32; face_ids is triangle_count
  // uint32; vertex_ids is vertex_count uint64 (the mesh node index
  // each exported vertex came from).
  [[nodiscard]] std::span<const float> positions() const noexcept;
  [[nodiscard]] std::span<const float> normals() const noexcept;
  [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
  [[nodiscard]] std::span<const std::uint32_t> face_ids() const noexcept;
  [[nodiscard]] std::span<const std::uint64_t> vertex_ids() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core

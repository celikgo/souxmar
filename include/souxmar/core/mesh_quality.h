// SPDX-License-Identifier: Apache-2.0
//
// Mesh-quality metrics — pure geometric functions over an element's node
// coordinates. Used by:
//   * the in-tree `postproc.mesh_quality` plugin (computes a per-cell
//     field over a whole mesh through the C ABI),
//   * the `query_mesh_quality` agent tool (summarises an already-
//     computed field),
//   * unit tests that pin the algorithms in isolation.
//
// These functions are NOT part of the stable C ABI — they are an in-tree
// C++ helper. Out-of-tree plugins authoring quality metrics either roll
// their own math or vendor this header.
//
// The metric numeric ids are STABLE — they pin the component layout of
// the Field the postproc plugin emits, and they appear in tool outputs.
// Appending new metrics is allowed; renumbering is forbidden.

#pragma once

#include "souxmar/core/element_type.h"
#include "souxmar/core/field.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace souxmar::core::quality {

enum class Metric : std::uint8_t {
  SignedVolume = 0,    // 3D: signed volume; 2D: signed area. NaN otherwise.
  EdgeRatio = 1,       // max_edge_length / min_edge_length. ≥ 1 for non-degenerate.
  MinDihedralDeg = 2,  // 3D: minimum interior dihedral angle, degrees.
                       // 2D / 1D: NaN.
};

inline constexpr std::size_t kNumMetrics = 3;

// One element. `nodes_xyz` carries exactly num_nodes(type) coordinate
// triples (row-major). NaN for any (type, metric) we don't yet support
// or for a degenerate input.
[[nodiscard]] double evaluate(ElementType type,
                              Metric metric,
                              std::span<const std::array<double, 3>> nodes_xyz) noexcept;

// All three metrics in one pass — slightly cheaper than three `evaluate`
// calls because edge-length computations amortise.
//
// `out` must have at least kNumMetrics doubles; entries beyond
// kNumMetrics are not touched.
void evaluate_all(ElementType type,
                  std::span<const std::array<double, 3>> nodes_xyz,
                  std::span<double> out) noexcept;

// Aggregates over a whole-mesh quality Field — for the agent tool's
// summary path. NaN entries (unsupported element types, degenerate
// cells) are excluded from min/max/mean; the (finite, total) counts
// surface this to the caller.
struct Stats {
  double min = std::numeric_limits<double>::quiet_NaN();
  double max = std::numeric_limits<double>::quiet_NaN();
  double mean = std::numeric_limits<double>::quiet_NaN();
  std::size_t finite_count = 0;
  std::size_t total_count = 0;
};

struct Report {
  std::array<Stats, kNumMetrics> per_metric{};
  // Counts of cells flagged by simple downstream-facing thresholds.
  // These are advisory — the caller decides what's actually
  // unacceptable — but they're the obvious first things to surface to a
  // human looking at the summary.
  std::size_t cells_inverted = 0;         // SignedVolume < 0
  std::size_t cells_extreme_aspect = 0;   // EdgeRatio > 20
  std::size_t cells_sliver_dihedral = 0;  // MinDihedralDeg < 5  (or > 175)
  std::size_t cells_unsupported = 0;      // any NaN across the metrics
};

// `quality_field_data` is the flat buffer the postproc plugin produced:
// `num_cells * kNumMetrics` doubles, single time step, row-major
// `[cell, metric]`. Unsupported entries are NaN; both `min/max/mean` and
// the threshold counters skip them.
[[nodiscard]] Report summarise(std::span<const double> quality_field_data,
                               std::size_t num_cells) noexcept;

// Convenience: the canonical short name a tool / log emits for a metric.
[[nodiscard]] constexpr const char* metric_name(Metric m) noexcept {
  switch (m) {
    case Metric::SignedVolume:
      return "signed_volume";
    case Metric::EdgeRatio:
      return "edge_ratio";
    case Metric::MinDihedralDeg:
      return "min_dihedral_deg";
  }
  return "unknown";
}

}  // namespace souxmar::core::quality

namespace souxmar::core {
class Mesh;
}

namespace souxmar::core::quality {

// Compute a per-cell scalar Field of one quality metric for a whole
// mesh. The returned Field has count == mesh.num_cells(), FieldKind
// Scalar, FieldLocation Cell, and is named after the metric (per
// `metric_name`). Cells of unsupported element types receive NaN.
// Negative SignedVolume entries flag inverted cells; the renderer can
// color these red. Sprint 26 uses this as a FieldStream input for
// "color by quality" without re-implementing the cell walk in the UI.
[[nodiscard]] Field compute_field(const Mesh& mesh, Metric metric);

}  // namespace souxmar::core::quality

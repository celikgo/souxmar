// SPDX-License-Identifier: Apache-2.0
//
// fem-elasticity — small-strain linear isotropic elasticity, solved by
// actually discretising the thing.
//
// Registers `solver.elasticity.fem`. This is the first capability in the
// default build that forms a stiffness matrix, assembles it, applies
// boundary conditions and solves a linear system. Everything else under
// `solver.*` that ships always-on is a closed-form stand-in — see
// docs/CAPABILITIES.md, which is honest about it, and docs/PHYSICS.md,
// whose headline ("There is no stiffness matrix, no assembly, no linear
// solve") this plugin is written to falsify.
//
// It is deliberately NOT registered as `solver.elasticity.linear`: the
// registry rejects a duplicate capability id and elasticity-stub is also
// always-on. The two ids coexist the way `solver.heat.linear` and
// `solver.heat.fenicsx` do, and the stub's closed form is now something
// this solver is tested against rather than a substitute for it.
//
// ---------------------------------------------------------------------
// What it computes
//
//   Find u such that   ∫ ε(v)ᵀ D ε(u) dV = ∫ v·t dA   for all admissible v
//
// Small-strain, linear isotropic Hooke's law, no geometric or material
// nonlinearity, no contact, no dynamics. Voigt ordering throughout is
// (xx, yy, zz, xy, yz, zx) with *engineering* shear strain γ = 2ε, which
// is why the lower diagonal of D carries μ and not 2μ.
//
//   λ = Eν / ((1+ν)(1−2ν))        μ = E / (2(1+ν))
//
// Elements: Tet4 (constant strain, one-point exact) and Hex8 (trilinear,
// 2×2×2 Gauss). Both are standard isoparametric displacement elements;
// see Zienkiewicz & Taylor, *The Finite Element Method*, 7th ed., ch. 6.
//
// ---------------------------------------------------------------------
// What it is NOT, and which way it is wrong
//
//   * **Hex8 shear-locks.** A trilinear hexahedron in bending is far too
//     stiff when the element is slender: it cannot represent the linear
//     bending strain without spurious shear. A one-element-through-thickness
//     cantilever will under-predict tip deflection substantially. Refine
//     through the thickness, or wait for the incompatible-modes / reduced
//     integration variant. The Tet4 is worse still — constant strain means
//     constant stress per element, so bending needs a genuinely fine mesh.
//   * **Near-incompressible locking.** As ν → 0.5, λ → ∞ and both elements
//     lock volumetrically. Above ν ≈ 0.45 the answer is not to be trusted.
//   * **No stress output.** The solver vtable returns exactly one Field and
//     the useful one is displacement. Von Mises stress belongs in a
//     `postproc.stress.*` stage that reads this field back.
//   * Not a substitute for a verified commercial code, and no part of this
//     is a qualification, an approval or a permit to build anything.
//
// ---------------------------------------------------------------------
// Determinism
//
// The cross-platform determinism gate requires byte-identical output on
// Linux, macOS and Windows, and docs/PHYSICS.md § determinism prefers a
// fixed iteration count over a tolerance exit — because a tolerance exit
// can take a *different* number of iterations once a libm last bit
// differs, and then the answer differs too.
//
// That rule's premise does not hold here, and the same page says why:
// "add/multiply/sqrt ... are correctly rounded IEEE-754 operations". This
// solver calls no libm function at all — assembly, the conjugate-gradient
// loop and the convergence test are add, multiply, divide and compare on
// doubles, every one of them correctly rounded and therefore identical on
// every IEEE-754 platform. The residual test compares *squared* norms
// specifically so that not even sqrt is needed. Given a fixed operation
// order, every platform takes the same number of iterations and produces
// the same bits.
//
// Two things are load-bearing for that and are easy to lose:
//   1. `-ffp-contract=off` (see this plugin's CMakeLists.txt). Without it
//      the compiler may fuse a*b+c into an FMA, which is *more* accurate
//      and therefore *different* — and it fuses on arm64 where it has a
//      single instruction and often not on x86-64, which is precisely a
//      macOS-vs-Linux divergence.
//   2. Fixed reduction order. Every sum below runs over a container whose
//      order is derived from the mesh, never from a hash map or a pointer.
//      The CSR sparsity pattern is built by sorting node ids, not by
//      insertion order into an unordered_set.
//
// ---------------------------------------------------------------------
// Inputs (souxmar_value_t map)
//
//   mesh            : {from: <stage>}   — Tet4 and/or Hex8 cells
//   youngs_modulus  : number, Pa        (default 210e9, structural steel)
//   poisson_ratio   : number            (default 0.3)
//
//   fix             : list of Dirichlet selectors. At least one is
//                     required — without it the stiffness matrix is
//                     singular (six rigid-body modes) and the solve is
//                     meaningless rather than merely inaccurate.
//     - plane      : "x" | "y" | "z"    — select nodes on a coordinate plane
//       at         : number | "min" | "max"   (default "min")
//       tol        : number             (default 1e-9 × bounding-box diagonal)
//       components : subset of "xyz"    (default "xyz")
//       value      : [ux, uy, uz]       (default [0, 0, 0])
//     - face_tag   : integer            — select nodes of faces carrying
//                                         this tag (mesher.am.layered stamps
//                                         10=−X 11=+X 12=−Y 13=+Y 14=−Z 15=+Z)
//       components, value as above
//
//   traction        : list of Neumann selectors, same `plane`/`face_tag`
//                     geometry, plus:
//       vector     : [tx, ty, tz]       — surface traction in Pa
//
// Output: nodal vector Field "displacement", 3 components, 1 time step.

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMaxElementNodes = 8;  // Hex8 is the widest we accept
constexpr std::size_t kMaxElementDof = 24;   // 8 nodes × 3

// ---------------------------------------------------------------------------
// Value-bag readers. Every one of these is total: a missing or wrong-kinded
// entry yields the default rather than an error, because the alternative is a
// solver that refuses to run on a pipeline someone wrote by hand with one
// key misspelled — and the required keys are checked explicitly below.
// ---------------------------------------------------------------------------

const souxmar_value_t* map_get(const souxmar_value_t* v, const char* key) {
  if (v == nullptr || souxmar_value_kind(v) != SOUXMAR_VK_MAP)
    return nullptr;
  return souxmar_value_map_get(v, key);
}

double read_number(const souxmar_value_t* v, const char* key, double dv) {
  const souxmar_value_t* e = map_get(v, key);
  if (e == nullptr || souxmar_value_kind(e) != SOUXMAR_VK_NUMBER)
    return dv;
  return souxmar_value_as_number(e);
}

std::string_view read_string(const souxmar_value_t* v, const char* key, const char* dv) {
  const souxmar_value_t* e = map_get(v, key);
  if (e == nullptr || souxmar_value_kind(e) != SOUXMAR_VK_STRING)
    return dv;
  const char* s = souxmar_value_as_string(e);
  return s != nullptr ? std::string_view(s) : std::string_view(dv);
}

// Read a 3-vector from `key`. Returns false when the key is absent or is not
// a 3-element list of numbers; `out` is untouched in that case.
bool read_vec3(const souxmar_value_t* v, const char* key, double out[3]) {
  const souxmar_value_t* e = map_get(v, key);
  if (e == nullptr || souxmar_value_kind(e) != SOUXMAR_VK_LIST)
    return false;
  if (souxmar_value_list_size(e) != 3)
    return false;
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(e, i);
    if (c == nullptr || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER)
      return false;
    out[i] = souxmar_value_as_number(c);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

struct Material {
  // Voigt (xx, yy, zz, xy, yz, zx) with engineering shear strain.
  double D[6][6]{};
};

Material make_material(double E, double nu) {
  const double lambda = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
  const double mu = E / (2.0 * (1.0 + nu));
  Material m;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j)
      m.D[i][j] = lambda;
    m.D[i][i] = lambda + 2.0 * mu;
  }
  // γ, not ε: the engineering shear strain already carries the factor of two,
  // so the shear block is μ. Writing 2μ here is the single most common way to
  // get an elasticity D matrix wrong and it passes a uniaxial test unnoticed.
  for (int i = 3; i < 6; ++i)
    m.D[i][i] = mu;
  return m;
}

// ---------------------------------------------------------------------------
// Element kinematics
// ---------------------------------------------------------------------------

// Invert a 3×3 in place-ish. Returns the determinant; `inv` is only valid
// when the determinant is non-zero.
double invert3(const double J[3][3], double inv[3][3]) {
  const double c00 = J[1][1] * J[2][2] - J[1][2] * J[2][1];
  const double c01 = J[1][2] * J[2][0] - J[1][0] * J[2][2];
  const double c02 = J[1][0] * J[2][1] - J[1][1] * J[2][0];
  const double det = J[0][0] * c00 + J[0][1] * c01 + J[0][2] * c02;
  if (det == 0.0)
    return 0.0;
  const double r = 1.0 / det;
  inv[0][0] = c00 * r;
  inv[1][0] = c01 * r;
  inv[2][0] = c02 * r;
  inv[0][1] = (J[0][2] * J[2][1] - J[0][1] * J[2][2]) * r;
  inv[1][1] = (J[0][0] * J[2][2] - J[0][2] * J[2][0]) * r;
  inv[2][1] = (J[0][1] * J[2][0] - J[0][0] * J[2][1]) * r;
  inv[0][2] = (J[0][1] * J[1][2] - J[0][2] * J[1][1]) * r;
  inv[1][2] = (J[0][2] * J[1][0] - J[0][0] * J[1][2]) * r;
  inv[2][2] = (J[0][0] * J[1][1] - J[0][1] * J[1][0]) * r;
  return det;
}

// Hex8 reference-coordinate signs, in souxmar's node order: v0..v3 the
// ζ = −1 face CCW seen from +ζ, v4..v7 the ζ = +1 face with v[i+4] stacked
// above v[i]. Identical to VTK_HEXAHEDRON, which is why vtu-writer can map
// the type straight across.
constexpr double kHexSign[8][3] = {
    {-1, -1, -1},
    {+1, -1, -1},
    {+1, +1, -1},
    {-1, +1, -1},
    {-1, -1, +1},
    {+1, -1, +1},
    {+1, +1, +1},
    {-1, +1, +1},
};

// 2-point Gauss-Legendre on [-1, 1]. Written as a division rather than a
// literal so the value is the correctly-rounded double nearest 1/√3 on every
// platform; `std::sqrt` is correctly rounded by IEEE-754, unlike most of libm.
const double kGauss2 = 1.0 / std::sqrt(3.0);

// Natural-coordinate shape derivatives dN_i/dξ_r at (xi, eta, zeta).
// Returns the node count, or 0 for an element type this solver cannot handle.
std::size_t shape_derivatives(uint16_t element_type,
                              double xi,
                              double eta,
                              double zeta,
                              double dN[kMaxElementNodes][3]) {
  if (element_type == SOUXMAR_ET_TET4) {
    // N0 = 1 − ξ − η − ζ, N1 = ξ, N2 = η, N3 = ζ. Constant, so the sample
    // point is irrelevant — which is exactly why one Gauss point is exact.
    dN[0][0] = -1.0;
    dN[0][1] = -1.0;
    dN[0][2] = -1.0;
    dN[1][0] = +1.0;
    dN[1][1] = 0.0;
    dN[1][2] = 0.0;
    dN[2][0] = 0.0;
    dN[2][1] = +1.0;
    dN[2][2] = 0.0;
    dN[3][0] = 0.0;
    dN[3][1] = 0.0;
    dN[3][2] = +1.0;
    return 4;
  }
  if (element_type == SOUXMAR_ET_HEX8) {
    // N_i = ⅛(1 + ξξ_i)(1 + ηη_i)(1 + ζζ_i)
    for (std::size_t i = 0; i < 8; ++i) {
      const double sx = kHexSign[i][0], sy = kHexSign[i][1], sz = kHexSign[i][2];
      dN[i][0] = 0.125 * sx * (1.0 + eta * sy) * (1.0 + zeta * sz);
      dN[i][1] = 0.125 * (1.0 + xi * sx) * sy * (1.0 + zeta * sz);
      dN[i][2] = 0.125 * (1.0 + xi * sx) * (1.0 + eta * sy) * sz;
    }
    return 8;
  }
  return 0;
}

// One quadrature contribution: build B from the spatial shape derivatives and
// accumulate w·detJ·BᵀDB into `ke` (row-major, n*3 square).
//
// B is never materialised as a 6×(3n) array. Each node contributes a 6×3
// block whose only non-zeros are the three derivatives, so DB for node j is
// computed directly and contracted against node i's block. That is ~6× less
// arithmetic than a dense B multiply and, more importantly for this file, it
// keeps the operation order obvious and fixed.
void accumulate_gauss_point(const Material& mat,
                            const double dNdx[kMaxElementNodes][3],
                            std::size_t n,
                            double weight,
                            double ke[kMaxElementDof * kMaxElementDof]) {
  const std::size_t ndof = 3 * n;
  for (std::size_t j = 0; j < n; ++j) {
    const double bx = dNdx[j][0], by = dNdx[j][1], bz = dNdx[j][2];

    // Column c of B for node j, as a 6-vector, for c = 0, 1, 2.
    const double Bj[3][6] = {
        {bx, 0.0, 0.0, by, 0.0, bz},
        {0.0, by, 0.0, bx, bz, 0.0},
        {0.0, 0.0, bz, 0.0, by, bx},
    };

    // DBj[c] = D · Bj[c]
    double DBj[3][6];
    for (int c = 0; c < 3; ++c) {
      for (int r = 0; r < 6; ++r) {
        double s = 0.0;
        for (int k = 0; k < 6; ++k)
          s += mat.D[r][k] * Bj[c][k];
        DBj[c][r] = s;
      }
    }

    for (std::size_t i = 0; i < n; ++i) {
      const double ax = dNdx[i][0], ay = dNdx[i][1], az = dNdx[i][2];
      const double Bi[3][6] = {
          {ax, 0.0, 0.0, ay, 0.0, az},
          {0.0, ay, 0.0, ax, az, 0.0},
          {0.0, 0.0, az, 0.0, ay, ax},
      };
      for (int a = 0; a < 3; ++a) {
        for (int c = 0; c < 3; ++c) {
          double s = 0.0;
          for (int r = 0; r < 6; ++r)
            s += Bi[a][r] * DBj[c][r];
          ke[(3 * i + a) * ndof + (3 * j + c)] += weight * s;
        }
      }
    }
  }
}

// Element stiffness for Tet4 / Hex8. Returns false when the element is
// degenerate (non-positive Jacobian) or the type is unsupported.
bool element_stiffness(uint16_t element_type,
                       const double coords[kMaxElementNodes][3],
                       const Material& mat,
                       std::size_t n,
                       double ke[kMaxElementDof * kMaxElementDof]) {
  const std::size_t ndof = 3 * n;
  std::fill(ke, ke + ndof * ndof, 0.0);

  // (xi, eta, zeta, weight) per rule. The tet's reference volume is 1/6, so
  // its single point carries that weight and the sample location is arbitrary
  // (the integrand is constant).
  double pts[8][4];
  std::size_t npts = 0;
  if (element_type == SOUXMAR_ET_TET4) {
    pts[0][0] = 0.25;
    pts[0][1] = 0.25;
    pts[0][2] = 0.25;
    pts[0][3] = 1.0 / 6.0;
    npts = 1;
  } else if (element_type == SOUXMAR_ET_HEX8) {
    for (int a = 0; a < 2; ++a) {
      for (int b = 0; b < 2; ++b) {
        for (int c = 0; c < 2; ++c) {
          pts[npts][0] = (a == 0 ? -kGauss2 : kGauss2);
          pts[npts][1] = (b == 0 ? -kGauss2 : kGauss2);
          pts[npts][2] = (c == 0 ? -kGauss2 : kGauss2);
          pts[npts][3] = 1.0;
          ++npts;
        }
      }
    }
  } else {
    return false;
  }

  for (std::size_t g = 0; g < npts; ++g) {
    double dN[kMaxElementNodes][3];
    if (shape_derivatives(element_type, pts[g][0], pts[g][1], pts[g][2], dN) != n) {
      return false;
    }
    // J[r][c] = ∂x_c/∂ξ_r
    double J[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (std::size_t i = 0; i < n; ++i) {
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
          J[r][c] += dN[i][r] * coords[i][c];
      }
    }
    double Jinv[3][3];
    const double detJ = invert3(J, Jinv);
    // A non-positive Jacobian is an inverted or collapsed cell. Integrating
    // it anyway produces a matrix that is not positive definite and a CG that
    // wanders; refusing names the mesh as the problem.
    if (!(detJ > 0.0))
      return false;

    double dNdx[kMaxElementNodes][3];
    for (std::size_t i = 0; i < n; ++i) {
      for (int c = 0; c < 3; ++c) {
        dNdx[i][c] = Jinv[c][0] * dN[i][0] + Jinv[c][1] * dN[i][1] + Jinv[c][2] * dN[i][2];
      }
    }
    accumulate_gauss_point(mat, dNdx, n, pts[g][3] * detJ, ke);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Face tables — the plugin-side copy.
//
// souxmar::core::face_node_table() is the host-side source of truth
// (include/souxmar/core/face_topology.h), but plugins load independently of
// libsouxmar-core and cannot link it. openfoam-solver carries the same copy
// for the same reason; ADR-0012's pre-mortem records the duplication as
// accepted. These must stay identical to src/core/face_topology.cpp, and the
// face ORDER is load-bearing: souxmar_mesh_face_tag indexes into it.
// ---------------------------------------------------------------------------

struct LocalFace {
  std::uint8_t count;
  std::uint8_t idx[4];
};

constexpr LocalFace kTet4Faces[4] = {
    {3, {1, 2, 3, 0}},  // opposite v0
    {3, {0, 3, 2, 0}},  // opposite v1
    {3, {0, 1, 3, 0}},  // opposite v2
    {3, {0, 2, 1, 0}},  // opposite v3
};

constexpr LocalFace kHex8Faces[6] = {
    {4, {0, 3, 2, 1}},  // −z
    {4, {4, 5, 6, 7}},  // +z
    {4, {0, 1, 5, 4}},  // −y
    {4, {3, 7, 6, 2}},  // +y
    {4, {0, 4, 7, 3}},  // −x
    {4, {1, 2, 6, 5}},  // +x
};

std::size_t face_table(uint16_t element_type, const LocalFace** out) {
  if (element_type == SOUXMAR_ET_TET4) {
    *out = kTet4Faces;
    return 4;
  }
  if (element_type == SOUXMAR_ET_HEX8) {
    *out = kHex8Faces;
    return 6;
  }
  *out = nullptr;
  return 0;
}

// ---------------------------------------------------------------------------
// Boundary-condition selectors
// ---------------------------------------------------------------------------

enum class SelectorKind { Plane, FaceTag };

struct Selector {
  SelectorKind kind = SelectorKind::Plane;
  int axis = 0;               // Plane: 0=x, 1=y, 2=z
  double at = 0.0;            // Plane: resolved coordinate
  double tol = 0.0;           // Plane: half-width of the selection slab
  std::int32_t face_tag = 0;  // FaceTag
  bool comp[3] = {true, true, true};
  double value[3] = {0.0, 0.0, 0.0};  // Dirichlet: prescribed u
  double traction[3] = {0.0, 0.0, 0.0};
};

bool node_on_plane(const Selector& s, const double* p) {
  const double d = p[s.axis] - s.at;
  return d <= s.tol && d >= -s.tol;
}

// Parse the shared geometry half of a selector. `bbox` is
// {xmin,ymin,zmin,xmax,ymax,zmax}; `diag_tol` the default plane tolerance.
bool parse_selector_geometry(const souxmar_value_t* e,
                             const double bbox[6],
                             double diag_tol,
                             Selector& out) {
  const souxmar_value_t* tag = map_get(e, "face_tag");
  if (tag != nullptr && souxmar_value_kind(tag) == SOUXMAR_VK_NUMBER) {
    out.kind = SelectorKind::FaceTag;
    out.face_tag = static_cast<std::int32_t>(souxmar_value_as_number(tag));
    return true;
  }

  const std::string_view plane = read_string(e, "plane", "");
  if (plane == "x")
    out.axis = 0;
  else if (plane == "y")
    out.axis = 1;
  else if (plane == "z")
    out.axis = 2;
  else
    return false;

  out.kind = SelectorKind::Plane;
  // `at` accepts "min"/"max" as well as a number so a pipeline can clamp the
  // end of a beam without the author having to know the mesh extent — which
  // for a mesher that meshes a bounding box is not knowable from the YAML.
  const souxmar_value_t* at = map_get(e, "at");
  if (at != nullptr && souxmar_value_kind(at) == SOUXMAR_VK_NUMBER) {
    out.at = souxmar_value_as_number(at);
  } else {
    const std::string_view which = read_string(e, "at", "min");
    out.at = (which == "max") ? bbox[3 + out.axis] : bbox[out.axis];
  }
  out.tol = read_number(e, "tol", diag_tol);
  return true;
}

void parse_components(const souxmar_value_t* e, Selector& out) {
  const std::string_view comps = read_string(e, "components", "xyz");
  out.comp[0] = comps.find('x') != std::string_view::npos;
  out.comp[1] = comps.find('y') != std::string_view::npos;
  out.comp[2] = comps.find('z') != std::string_view::npos;
}

// ---------------------------------------------------------------------------
// Sparse matrix — CSR, symmetric storage not exploited (the elimination below
// breaks symmetry of the *stored* pattern anyway, and full storage keeps the
// matrix-vector product a single straight loop).
// ---------------------------------------------------------------------------

struct Csr {
  std::vector<std::size_t> row_start;  // ndof + 1
  std::vector<std::uint32_t> col;
  std::vector<double> val;

  [[nodiscard]] std::size_t rows() const {
    return row_start.empty() ? 0 : row_start.size() - 1;
  }

  // Index of (r, c) within the row, or npos. The row's columns are sorted by
  // construction, so this is a binary search.
  [[nodiscard]] std::size_t find(std::size_t r, std::uint32_t c) const {
    const auto lo = col.begin() + static_cast<std::ptrdiff_t>(row_start[r]);
    const auto hi = col.begin() + static_cast<std::ptrdiff_t>(row_start[r + 1]);
    const auto it = std::lower_bound(lo, hi, c);
    if (it == hi || *it != c)
      return static_cast<std::size_t>(-1);
    return static_cast<std::size_t>(it - col.begin());
  }

  void multiply(const std::vector<double>& x, std::vector<double>& y) const {
    for (std::size_t r = 0; r < rows(); ++r) {
      double s = 0.0;
      for (std::size_t k = row_start[r]; k < row_start[r + 1]; ++k) {
        s += val[k] * x[col[k]];
      }
      y[r] = s;
    }
  }
};

// ---------------------------------------------------------------------------
// Jacobi-preconditioned conjugate gradient.
//
// Fixed operation order, no libm, squared-norm convergence test. Returns the
// iteration count actually taken; `x` holds the solution.
// ---------------------------------------------------------------------------

std::size_t solve_pcg(const Csr& A,
                      const std::vector<double>& b,
                      double rel_tol,
                      std::size_t max_iterations,
                      std::vector<double>& x) {
  const std::size_t n = b.size();
  x.assign(n, 0.0);

  std::vector<double> minv(n, 1.0);
  for (std::size_t r = 0; r < n; ++r) {
    const std::size_t d = A.find(r, static_cast<std::uint32_t>(r));
    const double diag = (d == static_cast<std::size_t>(-1)) ? 0.0 : A.val[d];
    // A zero diagonal means an unconstrained, unconnected dof. Leaving the
    // preconditioner at 1 there is harmless: the row is zero, so the residual
    // is zero, and the dof stays at its initial value.
    minv[r] = (diag != 0.0) ? 1.0 / diag : 1.0;
  }

  // x starts at zero, so r = b − Ax = b.
  std::vector<double> r = b;
  std::vector<double> z(n), p(n), Ap(n);

  double bb = 0.0;
  for (std::size_t i = 0; i < n; ++i)
    bb += b[i] * b[i];
  if (bb == 0.0)
    return 0;  // zero load — zero displacement, exactly.

  for (std::size_t i = 0; i < n; ++i)
    z[i] = minv[i] * r[i];
  p = z;
  double rz = 0.0;
  for (std::size_t i = 0; i < n; ++i)
    rz += r[i] * z[i];

  const double tol2 = rel_tol * rel_tol * bb;

  std::size_t it = 0;
  for (; it < max_iterations; ++it) {
    A.multiply(p, Ap);
    double pAp = 0.0;
    for (std::size_t i = 0; i < n; ++i)
      pAp += p[i] * Ap[i];
    // Non-positive curvature on a symmetric positive-definite matrix means
    // rounding has caught up with us; continuing only adds noise.
    if (!(pAp > 0.0))
      break;

    const double alpha = rz / pAp;
    for (std::size_t i = 0; i < n; ++i)
      x[i] += alpha * p[i];
    for (std::size_t i = 0; i < n; ++i)
      r[i] -= alpha * Ap[i];

    double rr = 0.0;
    for (std::size_t i = 0; i < n; ++i)
      rr += r[i] * r[i];
    if (rr <= tol2) {
      ++it;
      break;
    }

    for (std::size_t i = 0; i < n; ++i)
      z[i] = minv[i] * r[i];
    double rz_next = 0.0;
    for (std::size_t i = 0; i < n; ++i)
      rz_next += r[i] * z[i];
    if (rz == 0.0)
      break;
    const double beta = rz_next / rz;
    for (std::size_t i = 0; i < n; ++i)
      p[i] = z[i] + beta * p[i];
    rz = rz_next;
  }
  return it;
}

// ---------------------------------------------------------------------------
// The solve
// ---------------------------------------------------------------------------

souxmar_status_t fem_solve(const souxmar_mesh_t* mesh,
                           const souxmar_value_t* inputs,
                           const souxmar_solver_options_t* options,
                           souxmar_field_t** out_field,
                           void* /*user_data*/) {
  if (mesh == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (out_field == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  if (num_nodes == 0 || num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes or no cells");
  }

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (coords == nullptr || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  const double E = read_number(inputs, "youngs_modulus", 210e9);
  const double nu = read_number(inputs, "poisson_ratio", 0.3);
  if (!(E > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "youngs_modulus must be positive");
  }
  // ν = 0.5 is exactly incompressible and λ is a division by zero; ν ≤ −1
  // gives a non-positive-definite D. Both are refused rather than clamped:
  // silently changing a material constant produces a plausible answer to a
  // question nobody asked.
  if (!(nu > -1.0 && nu < 0.5)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "poisson_ratio must lie in (-1, 0.5)");
  }
  const Material mat = make_material(E, nu);

  // Bounding box, for resolving "min"/"max" and the default plane tolerance.
  double bbox[6] = {coords[0], coords[1], coords[2], coords[0], coords[1], coords[2]};
  for (std::size_t i = 1; i < num_nodes; ++i) {
    for (int c = 0; c < 3; ++c) {
      const double v = coords[i * 3 + static_cast<std::size_t>(c)];
      if (v < bbox[c])
        bbox[c] = v;
      if (v > bbox[3 + c])
        bbox[3 + c] = v;
    }
  }
  const double dx = bbox[3] - bbox[0], dy = bbox[4] - bbox[1], dz = bbox[5] - bbox[2];
  const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double default_tol = (diag > 0.0) ? 1e-9 * diag : 1e-12;

  // ---- Sparsity pattern -------------------------------------------------
  // Node adjacency first, then expand each entry to a 3×3 block. Sorting the
  // neighbour lists (rather than relying on insertion order) is what makes
  // the pattern, and therefore the accumulation order, platform-independent.
  std::vector<std::vector<std::uint32_t>> adj(num_nodes);
  std::vector<std::uint64_t> cell_nodes(kMaxElementNodes);

  for (std::size_t c = 0; c < num_cells; ++c) {
    const uint16_t et = souxmar_mesh_cell_type(mesh, c);
    if (et != SOUXMAR_ET_TET4 && et != SOUXMAR_ET_HEX8) {
      return souxmar_status_error(
          SOUXMAR_E_NOT_IMPLEMENTED,
          "solver.elasticity.fem handles Tet4 and Hex8 cells only; this mesh "
          "contains another element type");
    }
    const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
    if (n == 0 || n > kMaxElementNodes) {
      return souxmar_status_error(SOUXMAR_E_INTERNAL, "cell reports an impossible node count");
    }
    const auto st = souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), n);
    if (st.code != SOUXMAR_OK)
      return st;
    for (std::size_t i = 0; i < n; ++i) {
      if (cell_nodes[i] >= num_nodes) {
        return souxmar_status_error(SOUXMAR_E_INTERNAL, "cell references a node out of range");
      }
      for (std::size_t j = 0; j < n; ++j) {
        adj[cell_nodes[i]].push_back(static_cast<std::uint32_t>(cell_nodes[j]));
      }
    }
  }
  for (auto& a : adj) {
    std::sort(a.begin(), a.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
  }

  const std::size_t ndof = 3 * num_nodes;
  Csr A;
  A.row_start.resize(ndof + 1, 0);
  std::size_t nnz = 0;
  for (std::size_t i = 0; i < num_nodes; ++i) {
    for (int a = 0; a < 3; ++a) {
      A.row_start[3 * i + static_cast<std::size_t>(a)] = nnz;
      nnz += 3 * adj[i].size();
    }
  }
  A.row_start[ndof] = nnz;
  A.col.resize(nnz);
  A.val.assign(nnz, 0.0);
  {
    std::size_t k = 0;
    for (std::size_t i = 0; i < num_nodes; ++i) {
      for (int a = 0; a < 3; ++a) {
        for (std::uint32_t j : adj[i]) {
          for (int b = 0; b < 3; ++b)
            A.col[k++] = 3 * j + static_cast<std::uint32_t>(b);
        }
      }
    }
  }

  // ---- Assembly ---------------------------------------------------------
  std::vector<double> rhs(ndof, 0.0);
  {
    std::vector<double> ke(kMaxElementDof * kMaxElementDof);
    double xe[kMaxElementNodes][3];
    for (std::size_t c = 0; c < num_cells; ++c) {
      const uint16_t et = souxmar_mesh_cell_type(mesh, c);
      const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
      const auto st = souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), n);
      if (st.code != SOUXMAR_OK)
        return st;
      for (std::size_t i = 0; i < n; ++i) {
        for (int d = 0; d < 3; ++d) {
          xe[i][d] = coords[cell_nodes[i] * 3 + static_cast<std::size_t>(d)];
        }
      }
      if (!element_stiffness(et, xe, mat, n, ke.data())) {
        return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                    "element has a non-positive Jacobian — the mesh contains an "
                                    "inverted or degenerate cell");
      }
      const std::size_t edof = 3 * n;
      for (std::size_t i = 0; i < n; ++i) {
        for (int a = 0; a < 3; ++a) {
          const std::size_t row = 3 * cell_nodes[i] + static_cast<std::size_t>(a);
          for (std::size_t j = 0; j < n; ++j) {
            for (int b = 0; b < 3; ++b) {
              const auto colid =
                  static_cast<std::uint32_t>(3 * cell_nodes[j] + static_cast<std::size_t>(b));
              const std::size_t k = A.find(row, colid);
              if (k == static_cast<std::size_t>(-1)) {
                return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                            "assembly targeted an entry outside the pattern");
              }
              A.val[k] += ke[(3 * i + static_cast<std::size_t>(a)) * edof
                             + (3 * j + static_cast<std::size_t>(b))];
            }
          }
        }
      }
    }
  }

  // ---- Neumann: surface traction ----------------------------------------
  // Consistent nodal loads, f_i = ∫ N_i t dA over each selected boundary
  // face. Not lumped: for a Quad4 face the two are the same only when the
  // face is a parallelogram, and silently lumping would make the answer
  // mesh-shape dependent in a way nothing downstream could see.
  const souxmar_value_t* tractions = map_get(inputs, "traction");
  if (tractions != nullptr && souxmar_value_kind(tractions) == SOUXMAR_VK_LIST) {
    const std::size_t count = souxmar_value_list_size(tractions);
    for (std::size_t s = 0; s < count; ++s) {
      const souxmar_value_t* e = souxmar_value_list_at(tractions, s);
      Selector sel;
      if (!parse_selector_geometry(e, bbox, default_tol, sel)) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "traction selector needs either `plane: x|y|z` or `face_tag: <int>`");
      }
      if (!read_vec3(e, "vector", sel.traction)) {
        return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                    "traction selector needs `vector: [tx, ty, tz]`");
      }

      for (std::size_t c = 0; c < num_cells; ++c) {
        const uint16_t et = souxmar_mesh_cell_type(mesh, c);
        const LocalFace* faces = nullptr;
        const std::size_t nfaces = face_table(et, &faces);
        if (nfaces == 0)
          continue;
        const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
        const auto st = souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), n);
        if (st.code != SOUXMAR_OK)
          return st;

        for (std::size_t f = 0; f < nfaces; ++f) {
          const LocalFace& lf = faces[f];
          bool selected = true;
          if (sel.kind == SelectorKind::FaceTag) {
            selected = souxmar_mesh_face_tag(mesh, c, static_cast<std::uint8_t>(f)) == sel.face_tag;
          } else {
            for (std::size_t v = 0; v < lf.count && selected; ++v) {
              const std::size_t nid = cell_nodes[lf.idx[v]];
              selected = node_on_plane(sel, &coords[nid * 3]);
            }
          }
          if (!selected)
            continue;

          double fx[4][3];
          for (std::size_t v = 0; v < lf.count; ++v) {
            const std::size_t nid = cell_nodes[lf.idx[v]];
            for (int d = 0; d < 3; ++d)
              fx[v][d] = coords[nid * 3 + static_cast<std::size_t>(d)];
          }

          // Per-node shape-function integrals ∫N_i dA over the face.
          double w[4] = {0.0, 0.0, 0.0, 0.0};
          if (lf.count == 3) {
            // Planar triangle: ∫N_i dA = A/3 exactly for linear N.
            double u[3], v2[3], cr[3];
            for (int d = 0; d < 3; ++d) {
              u[d] = fx[1][d] - fx[0][d];
              v2[d] = fx[2][d] - fx[0][d];
            }
            cr[0] = u[1] * v2[2] - u[2] * v2[1];
            cr[1] = u[2] * v2[0] - u[0] * v2[2];
            cr[2] = u[0] * v2[1] - u[1] * v2[0];
            const double area = 0.5 * std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
            w[0] = w[1] = w[2] = area / 3.0;
          } else {
            // Bilinear quad: 2×2 Gauss over the surface, with the surface
            // Jacobian |∂x/∂ξ × ∂x/∂η|. Exact for a parallelogram and correct
            // (not merely close) for a general bilinear face.
            constexpr double qs[4][2] = {{-1, -1}, {+1, -1}, {+1, +1}, {-1, +1}};
            for (int a = 0; a < 2; ++a) {
              for (int b = 0; b < 2; ++b) {
                const double xi = (a == 0 ? -kGauss2 : kGauss2);
                const double eta = (b == 0 ? -kGauss2 : kGauss2);
                double N[4], dNx[4], dNe[4];
                for (int i = 0; i < 4; ++i) {
                  N[i] = 0.25 * (1.0 + xi * qs[i][0]) * (1.0 + eta * qs[i][1]);
                  dNx[i] = 0.25 * qs[i][0] * (1.0 + eta * qs[i][1]);
                  dNe[i] = 0.25 * (1.0 + xi * qs[i][0]) * qs[i][1];
                }
                double t1[3] = {0, 0, 0}, t2[3] = {0, 0, 0};
                for (int i = 0; i < 4; ++i) {
                  for (int d = 0; d < 3; ++d) {
                    t1[d] += dNx[i] * fx[i][d];
                    t2[d] += dNe[i] * fx[i][d];
                  }
                }
                const double cr[3] = {t1[1] * t2[2] - t1[2] * t2[1],
                                      t1[2] * t2[0] - t1[0] * t2[2],
                                      t1[0] * t2[1] - t1[1] * t2[0]};
                const double dA = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
                for (int i = 0; i < 4; ++i)
                  w[i] += N[i] * dA;
              }
            }
          }

          for (std::size_t v = 0; v < lf.count; ++v) {
            const std::size_t nid = cell_nodes[lf.idx[v]];
            for (int d = 0; d < 3; ++d) {
              rhs[nid * 3 + static_cast<std::size_t>(d)] += w[v] * sel.traction[d];
            }
          }
        }
      }
    }
  }

  // ---- Dirichlet --------------------------------------------------------
  std::vector<std::uint8_t> fixed(ndof, 0);
  std::vector<double> prescribed(ndof, 0.0);

  const souxmar_value_t* fixes = map_get(inputs, "fix");
  if (fixes == nullptr || souxmar_value_kind(fixes) != SOUXMAR_VK_LIST
      || souxmar_value_list_size(fixes) == 0) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "solver.elasticity.fem requires at least one `fix:` selector — with no "
        "Dirichlet constraint the stiffness matrix is singular (six rigid-body "
        "modes) and any answer would be arbitrary");
  }
  {
    const std::size_t count = souxmar_value_list_size(fixes);
    for (std::size_t s = 0; s < count; ++s) {
      const souxmar_value_t* e = souxmar_value_list_at(fixes, s);
      Selector sel;
      if (!parse_selector_geometry(e, bbox, default_tol, sel)) {
        return souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "fix selector needs either `plane: x|y|z` or `face_tag: <int>`");
      }
      parse_components(e, sel);
      read_vec3(e, "value", sel.value);

      if (sel.kind == SelectorKind::Plane) {
        for (std::size_t i = 0; i < num_nodes; ++i) {
          if (!node_on_plane(sel, &coords[i * 3]))
            continue;
          for (int d = 0; d < 3; ++d) {
            if (!sel.comp[d])
              continue;
            fixed[i * 3 + static_cast<std::size_t>(d)] = 1;
            prescribed[i * 3 + static_cast<std::size_t>(d)] = sel.value[d];
          }
        }
      } else {
        for (std::size_t c = 0; c < num_cells; ++c) {
          const uint16_t et = souxmar_mesh_cell_type(mesh, c);
          const LocalFace* faces = nullptr;
          const std::size_t nfaces = face_table(et, &faces);
          if (nfaces == 0)
            continue;
          const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
          const auto st = souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), n);
          if (st.code != SOUXMAR_OK)
            return st;
          for (std::size_t f = 0; f < nfaces; ++f) {
            if (souxmar_mesh_face_tag(mesh, c, static_cast<std::uint8_t>(f)) != sel.face_tag) {
              continue;
            }
            for (std::size_t v = 0; v < faces[f].count; ++v) {
              const std::size_t nid = cell_nodes[faces[f].idx[v]];
              for (int d = 0; d < 3; ++d) {
                if (!sel.comp[d])
                  continue;
                fixed[nid * 3 + static_cast<std::size_t>(d)] = 1;
                prescribed[nid * 3 + static_cast<std::size_t>(d)] = sel.value[d];
              }
            }
          }
        }
      }
    }
  }

  std::size_t num_fixed = 0;
  for (std::size_t i = 0; i < ndof; ++i)
    num_fixed += fixed[i];
  if (num_fixed == 0) {
    return souxmar_status_error(
        SOUXMAR_E_INVALID_ARGUMENT,
        "the `fix:` selectors matched no nodes — check `plane`/`at`/`tol` "
        "against the mesh extent, or `face_tag` against what the mesher stamped");
  }

  // Symmetric elimination. Move each constrained column's contribution to the
  // right-hand side, then zero the row and the column and put 1 on the
  // diagonal. Keeping the matrix symmetric matters: CG is only valid on a
  // symmetric positive-definite operator, and the asymmetric shortcut (zero
  // the row only) silently turns CG into a method that does not converge.
  for (std::size_t r = 0; r < ndof; ++r) {
    if (fixed[r])
      continue;
    for (std::size_t k = A.row_start[r]; k < A.row_start[r + 1]; ++k) {
      const std::uint32_t c = A.col[k];
      if (!fixed[c])
        continue;
      rhs[r] -= A.val[k] * prescribed[c];
      A.val[k] = 0.0;
    }
  }
  for (std::size_t r = 0; r < ndof; ++r) {
    if (!fixed[r])
      continue;
    for (std::size_t k = A.row_start[r]; k < A.row_start[r + 1]; ++k) {
      A.val[k] = (A.col[k] == r) ? 1.0 : 0.0;
    }
    rhs[r] = prescribed[r];
  }

  // ---- Solve ------------------------------------------------------------
  double tol = 1e-12;
  if (options != nullptr && options->tolerance > 0.0)
    tol = options->tolerance;
  std::size_t max_it = 10 * ndof;
  if (max_it > 200000)
    max_it = 200000;
  if (options != nullptr && options->max_iterations > 0) {
    max_it = static_cast<std::size_t>(options->max_iterations);
  }

  std::vector<double> u;
  solve_pcg(A, rhs, tol, max_it, u);

  // ---- Emit -------------------------------------------------------------
  souxmar_field_t* field = souxmar_field_new("displacement",
                                             SOUXMAR_FL_NODAL,
                                             SOUXMAR_FK_VECTOR,
                                             num_nodes,
                                             /*num_time_steps=*/1);
  if (field == nullptr) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double* data = souxmar_field_data(field);
  if (data == nullptr || souxmar_field_data_size(field) != ndof) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "souxmar_field_data buffer size mismatch");
  }
  std::memcpy(data, u.data(), ndof * sizeof(double));
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &fem_solve,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT int souxmar_plugin_register_v1(souxmar_registry_t* registry,
                                                                const souxmar_host_info_t* host) {
  if (host == nullptr || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_solver(
      registry, "solver.elasticity.fem", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}

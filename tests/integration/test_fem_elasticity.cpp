// SPDX-License-Identifier: Apache-2.0
//
// Integration tests for `solver.elasticity.fem` — the first capability in
// the default build that assembles a stiffness matrix and solves it.
//
// The load-bearing test here is the first one, and it is worth saying why.
// A displacement-based finite element is only usable if it can reproduce a
// state of constant strain *exactly*, on any admissible mesh. That is the
// patch test, and the `validating-solver` skill puts it at Level 2 with the
// blunt instruction: "Patch test failure means the element is non-conforming.
// Do not ship." Nearly every way of getting an element formulation wrong —
// a transposed Jacobian, engineering-vs-tensor shear in D, a mis-signed
// shape derivative, a quadrature weight off by the reference volume — breaks
// it, and almost nothing else catches all of those.
//
// The form used is the traction patch test: roller supports on three
// mutually perpendicular planes (which removes all six rigid-body modes and
// nothing else) and a uniform normal traction on one face. The exact
// solution is uniform uniaxial stress, whose displacement field is linear,
// and a linear field lies in both the Tet4 and the Hex8 space. So the
// discrete answer is not "close to" the analytic one — Galerkin orthogonality
// makes it *equal*, to solver tolerance, on a mesh of any size. Anything
// looser than that is a bug.
//
// It also closes the loop elasticity-stub's own header asks for: that
// closed form is "what a real linear-elasticity solver should converge to on
// a uniaxial-load mesh", and test 2 asserts the two agree.
//
// Meshes are built here rather than taken from a mesher plugin. The point is
// to control node coordinates exactly and to keep the test's meaning
// independent of another plugin's conventions; test 3 drives the real
// mesher → solver path through the dispatcher to cover the wiring.

#include "souxmar/core/field.h"
#include "souxmar/core/geometry.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

#include "souxmar-c/geometry.h"
#include "souxmar-c/mesh.h"

#include "test_config.h"
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

constexpr double kE = 210e9;  // structural steel
constexpr double kNu = 0.3;

plugin::LoadedPlugin load_by_id(plugin::PluginLoader& loader,
                                const plugin::DiscoveryReport& report,
                                const std::string& want_id) {
  for (const auto& d : report.loaded) {
    if (d.manifest.id != want_id)
      continue;
    auto r = loader.load(d);
    if (auto* e = std::get_if<plugin::LoadError>(&r)) {
      throw std::runtime_error("load failed for " + want_id + ": " + e->message);
    }
    return std::move(std::get<plugin::LoadedPlugin>(r));
  }
  throw std::runtime_error("plugin not discovered: " + want_id);
}

std::shared_ptr<core::Mesh> wrap(souxmar_mesh_t* raw) {
  return std::shared_ptr<core::Mesh>(reinterpret_cast<core::Mesh*>(raw), [](core::Mesh* p) {
    souxmar_mesh_free(reinterpret_cast<souxmar_mesh_t*>(p));
  });
}

std::size_t node_index(std::size_t i,
                       std::size_t j,
                       std::size_t k,
                       std::size_t nx,
                       std::size_t ny) {
  return (k * (ny + 1) + j) * (nx + 1) + i;
}

// Structured grid of nodes over [0,Lx]×[0,Ly]×[0,Lz], nx·ny·nz cells.
void add_grid_nodes(souxmar_mesh_t* m,
                    std::size_t nx,
                    std::size_t ny,
                    std::size_t nz,
                    double Lx,
                    double Ly,
                    double Lz) {
  for (std::size_t k = 0; k <= nz; ++k) {
    for (std::size_t j = 0; j <= ny; ++j) {
      for (std::size_t i = 0; i <= nx; ++i) {
        const double p[3] = {Lx * static_cast<double>(i) / static_cast<double>(nx),
                             Ly * static_cast<double>(j) / static_cast<double>(ny),
                             Lz * static_cast<double>(k) / static_cast<double>(nz)};
        souxmar_mesh_add_node(m, p);
      }
    }
  }
}

// The eight corners of cell (i,j,k) in souxmar's Hex8 order: v0..v3 the
// z-low face CCW seen from +z, v4..v7 stacked above.
std::array<std::uint64_t, 8> hex_corners(std::size_t i,
                                         std::size_t j,
                                         std::size_t k,
                                         std::size_t nx,
                                         std::size_t ny) {
  return {
      node_index(i, j, k, nx, ny),
      node_index(i + 1, j, k, nx, ny),
      node_index(i + 1, j + 1, k, nx, ny),
      node_index(i, j + 1, k, nx, ny),
      node_index(i, j, k + 1, nx, ny),
      node_index(i + 1, j, k + 1, nx, ny),
      node_index(i + 1, j + 1, k + 1, nx, ny),
      node_index(i, j + 1, k + 1, nx, ny),
  };
}

std::shared_ptr<core::Mesh> make_hex_box(std::size_t nx,
                                         std::size_t ny,
                                         std::size_t nz,
                                         double Lx,
                                         double Ly,
                                         double Lz) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  add_grid_nodes(m, nx, ny, nz, Lx, Ly, Lz);
  for (std::size_t k = 0; k < nz; ++k) {
    for (std::size_t j = 0; j < ny; ++j) {
      for (std::size_t i = 0; i < nx; ++i) {
        const auto c = hex_corners(i, j, k, nx, ny);
        souxmar_mesh_add_cell(m, SOUXMAR_ET_HEX8, c.data(), 8, /*tag=*/-1, nullptr);
      }
    }
  }
  return wrap(m);
}

// Kuhn's 6-tetrahedron subdivision about the main diagonal v0–v6. Every tet
// comes out positively oriented, and because each hex uses the same diagonal,
// neighbouring hexes agree on how the shared quad face is split — so the
// tetrahedral mesh is conforming, not just space-filling.
constexpr std::array<std::array<std::size_t, 4>, 6> kHexToTets = {{
    {{0, 1, 2, 6}},
    {{0, 2, 3, 6}},
    {{0, 3, 7, 6}},
    {{0, 7, 4, 6}},
    {{0, 4, 5, 6}},
    {{0, 5, 1, 6}},
}};

std::shared_ptr<core::Mesh> make_tet_box(std::size_t nx,
                                         std::size_t ny,
                                         std::size_t nz,
                                         double Lx,
                                         double Ly,
                                         double Lz) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  add_grid_nodes(m, nx, ny, nz, Lx, Ly, Lz);
  for (std::size_t k = 0; k < nz; ++k) {
    for (std::size_t j = 0; j < ny; ++j) {
      for (std::size_t i = 0; i < nx; ++i) {
        const auto c = hex_corners(i, j, k, nx, ny);
        for (const auto& t : kHexToTets) {
          const std::uint64_t nodes[4] = {c[t[0]], c[t[1]], c[t[2]], c[t[3]]};
          souxmar_mesh_add_cell(m, SOUXMAR_ET_TET4, nodes, 4, /*tag=*/-1, nullptr);
        }
      }
    }
  }
  return wrap(m);
}

struct Harness {
  plugin::Registry registry;
  std::unique_ptr<plugin::PluginLoader> loader;
  std::vector<plugin::LoadedPlugin> held;
  std::unique_ptr<pipeline::RegistryDispatcher> dispatcher;

  explicit Harness(const std::vector<std::string>& plugin_ids) {
    const fs::path root = fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
    const auto discovery = plugin::discover_plugins({root});
    if (discovery.loaded.empty())
      throw std::runtime_error("no plugins discovered under " + root.string());
    loader = std::make_unique<plugin::PluginLoader>(registry, "test-host/0.0.0");
    for (const auto& id : plugin_ids)
      held.push_back(load_by_id(*loader, discovery, id));
    dispatcher = std::make_unique<pipeline::RegistryDispatcher>(registry);
  }
};

pipeline::Value num(double v) {
  return pipeline::Value::number(v);
}

pipeline::Value str(const std::string& v) {
  return pipeline::Value::string(v);
}

pipeline::Value vec3(double a, double b, double c) {
  return pipeline::Value::list({num(a), num(b), num(c)});
}

// One `fix:` entry: roller on `plane` at `at`, constraining `components`.
pipeline::Value roller(const std::string& plane,
                       const std::string& at,
                       const std::string& components) {
  std::map<std::string, pipeline::Value> m;
  m.emplace("plane", str(plane));
  m.emplace("at", str(at));
  m.emplace("components", str(components));
  return pipeline::Value::map(std::move(m));
}

pipeline::Value traction_on(const std::string& plane,
                            const std::string& at,
                            double tx,
                            double ty,
                            double tz) {
  std::map<std::string, pipeline::Value> m;
  m.emplace("plane", str(plane));
  m.emplace("at", str(at));
  m.emplace("vector", vec3(tx, ty, tz));
  return pipeline::Value::map(std::move(m));
}

// Dispatch solver.elasticity.fem against `mesh` with the given fix/traction
// lists, returning the produced Field (or nullptr, with `err` set).
std::shared_ptr<pipeline::StageOutput> run_fem(Harness& h,
                                               const std::shared_ptr<core::Mesh>& mesh,
                                               std::vector<pipeline::Value> fixes,
                                               std::vector<pipeline::Value> tractions,
                                               std::string* err = nullptr) {
  auto mesh_so = std::make_shared<pipeline::StageOutput>();
  mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
  mesh_so->mesh = mesh;
  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__mesh__", std::static_pointer_cast<void>(mesh_so));

  std::map<std::string, pipeline::Value> in;
  in.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  in.emplace("youngs_modulus", num(kE));
  in.emplace("poisson_ratio", num(kNu));
  if (!fixes.empty())
    in.emplace("fix", pipeline::Value::list(std::move(fixes)));
  if (!tractions.empty())
    in.emplace("traction", pipeline::Value::list(std::move(tractions)));

  pipeline::DispatchContext ctx{
      "solver.elasticity.fem", pipeline::Value::map(std::move(in)), upstream};
  auto dr = h.dispatcher->dispatch(ctx);
  if (auto* e = std::get_if<pipeline::DispatchError>(&dr)) {
    if (err != nullptr)
      *err = e->message;
    return nullptr;
  }
  auto payload = std::get<pipeline::DispatchSuccess>(dr);
  return std::static_pointer_cast<pipeline::StageOutput>(payload);
}

// The uniaxial patch: roller on the three min planes, uniform tension on
// x = max. Asserts every node matches the analytic constant-strain field.
void expect_uniaxial_exact(Harness& h,
                           const std::shared_ptr<core::Mesh>& mesh,
                           double sigma,
                           double Lx) {
  std::vector<pipeline::Value> fixes;
  fixes.push_back(roller("x", "min", "x"));
  fixes.push_back(roller("y", "min", "y"));
  fixes.push_back(roller("z", "min", "z"));
  std::vector<pipeline::Value> tract;
  tract.push_back(traction_on("x", "max", sigma, 0.0, 0.0));

  std::string err;
  auto out = run_fem(h, mesh, fixes, tract, &err);
  ASSERT_NE(out, nullptr) << err;
  ASSERT_EQ(out->kind, pipeline::StageOutput::Kind::Field);
  const auto& field = *out->field;
  ASSERT_EQ(field.count(), mesh->num_nodes());

  const double eps = sigma / kE;
  // Scale the tolerance to the largest displacement in the model, so the
  // assertion means "seven digits of the analytic answer" independently of
  // the load and the geometry rather than a magic absolute number.
  const double scale = eps * Lx;
  const double tol = 1e-7 * scale;

  const auto nodes = mesh->nodes_flat();
  for (std::size_t i = 0; i < mesh->num_nodes(); ++i) {
    const auto u = field.at(i, 0);
    const double x = nodes[i * 3 + 0], y = nodes[i * 3 + 1], z = nodes[i * 3 + 2];
    EXPECT_NEAR(u[0], eps * x, tol) << "u_x at node " << i;
    EXPECT_NEAR(u[1], -kNu * eps * y, tol) << "u_y at node " << i;
    EXPECT_NEAR(u[2], -kNu * eps * z, tol) << "u_z at node " << i;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The patch test. If either of these fails, the element is broken.
// ---------------------------------------------------------------------------

TEST(FemElasticity, Hex8ReproducesConstantStrainExactly) {
  Harness h({"dev.souxmar.examples.fem-elasticity"});
  ASSERT_NE(h.registry.find_solver("solver.elasticity.fem"), nullptr);
  // Deliberately not a cube and deliberately not uniform in all directions:
  // a patch test on a single perfect cube can pass with a Jacobian bug that a
  // non-unit, non-cubic cell exposes.
  auto mesh = make_hex_box(3, 2, 2, 0.6, 0.2, 0.3);
  expect_uniaxial_exact(h, mesh, /*sigma=*/1.0e6, /*Lx=*/0.6);
}

TEST(FemElasticity, Tet4ReproducesConstantStrainExactly) {
  Harness h({"dev.souxmar.examples.fem-elasticity"});
  auto mesh = make_tet_box(3, 2, 2, 0.6, 0.2, 0.3);
  expect_uniaxial_exact(h, mesh, /*sigma=*/1.0e6, /*Lx=*/0.6);
}

// ---------------------------------------------------------------------------
// 2. The FEM answer equals elasticity-stub's closed form.
//
// elasticity_stub.cpp's header calls its own output "the analytical bar
// solution a real linear-elasticity solver should converge to". This asserts
// that claim against the real solver rather than leaving it as a comment.
// ---------------------------------------------------------------------------

TEST(FemElasticity, AgreesWithTheClosedFormStubOnAUniaxialBar) {
  Harness h({"dev.souxmar.examples.fem-elasticity", "dev.souxmar.examples.elasticity-stub"});
  auto mesh = make_hex_box(4, 2, 2, 1.0, 0.25, 0.25);

  const double sigma = 2.1e6;

  std::vector<pipeline::Value> fixes{
      roller("x", "min", "x"), roller("y", "min", "y"), roller("z", "min", "z")};
  std::vector<pipeline::Value> tract{traction_on("x", "max", sigma, 0.0, 0.0)};
  std::string err;
  auto fem = run_fem(h, mesh, fixes, tract, &err);
  ASSERT_NE(fem, nullptr) << err;

  // The stub, dispatched over the same mesh with the same numbers.
  auto mesh_so = std::make_shared<pipeline::StageOutput>();
  mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
  mesh_so->mesh = mesh;
  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__mesh__", std::static_pointer_cast<void>(mesh_so));
  std::map<std::string, pipeline::Value> sin_;
  sin_.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  sin_.emplace("load_magnitude", num(sigma));
  sin_.emplace("youngs_modulus", num(kE));
  sin_.emplace("poisson_ratio", num(kNu));
  pipeline::DispatchContext sctx{
      "solver.elasticity.linear", pipeline::Value::map(std::move(sin_)), upstream};
  auto sdr = h.dispatcher->dispatch(sctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(sdr));
  const auto* stub =
      static_cast<const pipeline::StageOutput*>(std::get<pipeline::DispatchSuccess>(sdr).get());

  const double tol = 1e-7 * (sigma / kE) * 1.0;
  for (std::size_t i = 0; i < mesh->num_nodes(); ++i) {
    const auto a = fem->field->at(i, 0);
    const auto b = stub->field->at(i, 0);
    for (std::size_t c = 0; c < 3; ++c) {
      EXPECT_NEAR(a[c], b[c], tol) << "component " << c << " node " << i;
    }
  }
}

// ---------------------------------------------------------------------------
// 3. The real pipeline path: grid-mesher → fem solver, through the dispatcher.
// ---------------------------------------------------------------------------

TEST(FemElasticity, SolvesAMeshProducedByTheGridMesher) {
  Harness h({"dev.souxmar.examples.fem-elasticity", "dev.souxmar.examples.grid-mesher"});

  auto* g = souxmar_geometry_new();
  const double corners[8][3] = {
      {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
  for (const auto& c : corners)
    souxmar_geometry_add_vertex(g, c);
  auto geom = std::shared_ptr<core::Geometry>(
      reinterpret_cast<core::Geometry*>(g),
      [](core::Geometry* p) { souxmar_geometry_free(reinterpret_cast<souxmar_geometry_t*>(p)); });

  auto geom_so = std::make_shared<pipeline::StageOutput>();
  geom_so->kind = pipeline::StageOutput::Kind::Geometry;
  geom_so->geometry = geom;
  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__geom__", std::static_pointer_cast<void>(geom_so));

  std::map<std::string, pipeline::Value> min_;
  min_.emplace("geometry", pipeline::Value::stage_ref("__geom__"));
  min_.emplace("target_size", num(0.25));
  pipeline::DispatchContext mctx{
      "mesher.tetra.grid", pipeline::Value::map(std::move(min_)), upstream};
  auto mdr = h.dispatcher->dispatch(mctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mdr));
  auto mesh_payload = std::get<pipeline::DispatchSuccess>(mdr);
  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(mesh_payload.get());
  ASSERT_EQ(mesh_out->kind, pipeline::StageOutput::Kind::Mesh);

  // Same exactness bar as the hand-built meshes — the mesher's node ordering
  // and 5-tets-per-hex split must not change the answer by one part in 1e7.
  expect_uniaxial_exact(h, mesh_out->mesh, /*sigma=*/1.0e6, /*Lx=*/1.0);
}

// ---------------------------------------------------------------------------
// 4. Bending converges toward Euler–Bernoulli from below.
//
// Asserts the trend, not a percentage. Both elements shear-lock, so a
// tolerance on the coarse mesh would either be vacuous or wrong; what has to
// be true is that refinement moves the answer monotonically toward the beam
// solution and gets meaningfully close.
// ---------------------------------------------------------------------------

TEST(FemElasticity, CantileverTipDeflectionConvergesTowardBeamTheory) {
  Harness h({"dev.souxmar.examples.fem-elasticity"});

  const double L = 1.0, H = 0.1, W = 0.1;
  const double sigma = 1.0e6;      // shear traction on the tip face
  const double P = sigma * H * W;  // total tip load
  const double I = W * H * H * H / 12.0;
  const double delta_eb = P * L * L * L / (3.0 * kE * I);

  std::vector<double> tip;
  for (std::size_t n : {2u, 4u, 8u}) {
    auto mesh = make_hex_box(n * 8, n, n, L, H, W);
    std::vector<pipeline::Value> fixes{roller("x", "min", "xyz")};
    std::vector<pipeline::Value> tract{traction_on("x", "max", 0.0, 0.0, -sigma)};
    std::string err;
    auto out = run_fem(h, mesh, fixes, tract, &err);
    ASSERT_NE(out, nullptr) << err;

    // Largest downward deflection anywhere on the tip face.
    const auto nodes = mesh->nodes_flat();
    double worst = 0.0;
    for (std::size_t i = 0; i < mesh->num_nodes(); ++i) {
      if (std::abs(nodes[i * 3 + 0] - L) > 1e-12)
        continue;
      worst = std::min(worst, out->field->at(i, 0)[2]);
    }
    tip.push_back(-worst);
  }

  for (std::size_t i = 0; i + 1 < tip.size(); ++i) {
    EXPECT_GT(tip[i + 1], tip[i]) << "refinement must soften a locking element, not stiffen it";
    EXPECT_LT(tip[i + 1], delta_eb * 1.5)
        << "overshooting beam theory by 50% is not shear locking, it is a bug";
  }
  // Beam theory itself neglects shear deflection, so the converged 3-D answer
  // is legitimately a little above it; what would be wrong is staying far below.
  EXPECT_GT(tip.back(), 0.80 * delta_eb)
      << "finest mesh reached " << tip.back() << " m against a beam-theory " << delta_eb << " m";
}

// ---------------------------------------------------------------------------
// 5. Determinism — the same input twice must give bit-identical output.
// ---------------------------------------------------------------------------

TEST(FemElasticity, RepeatedSolvesAreBitIdentical) {
  Harness h({"dev.souxmar.examples.fem-elasticity"});
  auto mesh = make_tet_box(3, 2, 2, 0.6, 0.2, 0.3);
  std::vector<pipeline::Value> fixes{
      roller("x", "min", "x"), roller("y", "min", "y"), roller("z", "min", "z")};
  std::vector<pipeline::Value> tract{traction_on("x", "max", 1.0e6, 0.0, 0.0)};

  std::string err;
  auto a = run_fem(h, mesh, fixes, tract, &err);
  ASSERT_NE(a, nullptr) << err;
  std::vector<pipeline::Value> fixes2{
      roller("x", "min", "x"), roller("y", "min", "y"), roller("z", "min", "z")};
  std::vector<pipeline::Value> tract2{traction_on("x", "max", 1.0e6, 0.0, 0.0)};
  auto b = run_fem(h, mesh, fixes2, tract2, &err);
  ASSERT_NE(b, nullptr) << err;

  const auto da = a->field->data();
  const auto db = b->field->data();
  ASSERT_EQ(da.size(), db.size());
  EXPECT_EQ(0, std::memcmp(da.data(), db.data(), da.size() * sizeof(double)))
      << "two identical solves diverged — something in assembly or CG depends "
         "on address order or uninitialised memory";
}

// ---------------------------------------------------------------------------
// 6. Refusals. Each of these would otherwise produce a plausible wrong answer.
// ---------------------------------------------------------------------------

TEST(FemElasticity, RefusesToSolveWithoutADirichletConstraint) {
  Harness h({"dev.souxmar.examples.fem-elasticity"});
  auto mesh = make_hex_box(2, 1, 1, 0.4, 0.2, 0.2);
  std::string err;
  auto out = run_fem(h, mesh, {}, {traction_on("x", "max", 1.0e6, 0.0, 0.0)}, &err);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(err.find("fix"), std::string::npos) << err;
}

TEST(FemElasticity, RefusesASelectorThatMatchesNoNode) {
  Harness h({"dev.souxmar.examples.fem-elasticity"});
  auto mesh = make_hex_box(2, 1, 1, 0.4, 0.2, 0.2);
  std::map<std::string, pipeline::Value> bad;
  bad.emplace("plane", str("x"));
  bad.emplace("at", num(999.0));  // nowhere near the mesh
  std::string err;
  auto out = run_fem(h, mesh, {pipeline::Value::map(std::move(bad))}, {}, &err);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(err.find("matched no nodes"), std::string::npos) << err;
}

TEST(FemElasticity, RefusesAnElementTypeItCannotIntegrate) {
  Harness h({"dev.souxmar.examples.fem-elasticity", "dev.souxmar.examples.hello-mesher"});
  // A Tri3 surface mesh: valid, but there is no 2-D element in this solver.
  souxmar_mesh_t* raw = souxmar_mesh_new();
  const double p0[3] = {0, 0, 0}, p1[3] = {1, 0, 0}, p2[3] = {0, 1, 0};
  souxmar_mesh_add_node(raw, p0);
  souxmar_mesh_add_node(raw, p1);
  souxmar_mesh_add_node(raw, p2);
  const std::uint64_t tri[3] = {0, 1, 2};
  souxmar_mesh_add_cell(raw, SOUXMAR_ET_TRI3, tri, 3, -1, nullptr);

  std::string err;
  auto out = run_fem(h, wrap(raw), {roller("x", "min", "xyz")}, {}, &err);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(err.find("Tet4 and Hex8"), std::string::npos) << err;
}

// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/registry_dispatcher.h"

#include "souxmar/plugin/guard.h"

#include "souxmar-c/mesher.h"
#include "souxmar-c/postproc.h"
#include "souxmar-c/reader.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/writer.h"

#include <fmt/core.h>

#include <filesystem>
#include <system_error>
#include <utility>

namespace souxmar::pipeline {

namespace {

// Get a typed StageOutput pointer from the upstream map. Returns nullptr
// if the entry is absent OR its kind doesn't match.
const StageOutput* upstream_as(const std::map<std::string, std::shared_ptr<void>>& upstream,
                               const std::string& stage_id,
                               StageOutput::Kind expected) {
  auto it = upstream.find(stage_id);
  if (it == upstream.end())
    return nullptr;
  const auto* so = static_cast<const StageOutput*>(it->second.get());
  if (!so || so->kind != expected)
    return nullptr;
  return so;
}

// Extract a StageRef from an input map's named key, if present.
std::optional<std::string> extract_stage_ref(const Value& input, std::string_view key) {
  const auto* v = input.find(key);
  if (!v || v->kind() != Value::Kind::Stage)
    return std::nullopt;
  return v->as_stage().stage_id;
}

DispatchResult dispatch_mesher(plugin::Registry& registry, const DispatchContext& ctx) {
  const auto* entry = registry.find_mesher(ctx.capability_id);
  if (!entry) {
    return DispatchError{fmt::format("no mesher capability registered as '{}'", ctx.capability_id)};
  }

  // Optional `geometry` upstream handle.
  const souxmar_geometry_t* c_geometry = nullptr;
  if (auto stage_id = extract_stage_ref(ctx.inputs, "geometry")) {
    const auto* up = upstream_as(ctx.upstream_outputs, *stage_id, StageOutput::Kind::Geometry);
    if (!up) {
      return DispatchError{
          fmt::format("stage referenced by 'geometry' ({}) did not produce a Geometry", *stage_id)};
    }
    c_geometry = reinterpret_cast<const souxmar_geometry_t*>(up->geometry.get());
  }

  // Read scalar option keys from the input bag.
  souxmar_mesher_options_t options{0.0, 0, 1, -1};
  if (const auto* v = ctx.inputs.find("target_size"); v && v->kind() == Value::Kind::Number) {
    options.target_size = v->as_number();
  }
  if (const auto* v = ctx.inputs.find("optimize"); v && v->kind() == Value::Kind::Bool) {
    options.optimize = v->as_bool() ? 1 : 0;
  }
  if (const auto* v = ctx.inputs.find("element_order"); v && v->kind() == Value::Kind::Number) {
    options.element_order = static_cast<std::int32_t>(v->as_number());
  }

  souxmar_mesh_t* out_mesh = nullptr;
  souxmar_status_t status{SOUXMAR_E_INTERNAL, "uninitialised", nullptr};

  const auto guard = plugin::guard_call(
      [&] { status = entry->vtable->mesh_fn(c_geometry, &options, &out_mesh, entry->user_data); });
  if (guard.outcome != plugin::GuardOutcome::Ok) {
    if (out_mesh)
      souxmar_mesh_free(out_mesh);
    return DispatchError{fmt::format("mesher '{}' raised: {}", ctx.capability_id, guard.detail)};
  }
  if (status.code != SOUXMAR_OK) {
    if (out_mesh)
      souxmar_mesh_free(out_mesh);
    return DispatchError{fmt::format("mesher '{}' returned error {}: {}",
                                     ctx.capability_id,
                                     status.code,
                                     status.message ? status.message : "(no message)")};
  }
  if (!out_mesh) {
    return DispatchError{fmt::format("mesher '{}' returned OK but no mesh", ctx.capability_id)};
  }

  auto out = std::make_shared<StageOutput>();
  out->kind = StageOutput::Kind::Mesh;
  // Take ownership via shared_ptr with custom deleter that calls
  // souxmar_mesh_free, so plugins that allocated through the C ABI are
  // freed through the same path on stage-output destruction.
  out->mesh = std::shared_ptr<souxmar::core::Mesh>(
      reinterpret_cast<souxmar::core::Mesh*>(out_mesh),
      [](souxmar::core::Mesh* m) { souxmar_mesh_free(reinterpret_cast<souxmar_mesh_t*>(m)); });
  return std::static_pointer_cast<void>(out);
}

DispatchResult dispatch_solver(plugin::Registry& registry, const DispatchContext& ctx) {
  const auto* entry = registry.find_solver(ctx.capability_id);
  if (!entry) {
    return DispatchError{fmt::format("no solver capability registered as '{}'", ctx.capability_id)};
  }

  // Required `mesh` upstream handle.
  auto mesh_stage = extract_stage_ref(ctx.inputs, "mesh");
  if (!mesh_stage) {
    return DispatchError{fmt::format("solver '{}' input is missing required 'mesh: {{from: ...}}'",
                                     ctx.capability_id)};
  }
  const auto* up = upstream_as(ctx.upstream_outputs, *mesh_stage, StageOutput::Kind::Mesh);
  if (!up) {
    return DispatchError{
        fmt::format("stage referenced by 'mesh' ({}) did not produce a Mesh", *mesh_stage)};
  }
  const auto* c_mesh = reinterpret_cast<const souxmar_mesh_t*>(up->mesh.get());

  souxmar_solver_options_t options{-1, 0.0, 0};
  if (const auto* v = ctx.inputs.find("tolerance"); v && v->kind() == Value::Kind::Number) {
    options.tolerance = v->as_number();
  }
  if (const auto* v = ctx.inputs.find("max_iterations"); v && v->kind() == Value::Kind::Number) {
    options.max_iterations = static_cast<std::int32_t>(v->as_number());
  }
  if (const auto* v = ctx.inputs.find("random_seed"); v && v->kind() == Value::Kind::Number) {
    options.random_seed = static_cast<std::int64_t>(v->as_number());
  }

  // Pass the whole stage input bag through as the value handle. The plugin
  // is free to inspect any non-mesh keys it wants (bcs, materials, ...).
  const auto* c_inputs = reinterpret_cast<const souxmar_value_t*>(&ctx.inputs);

  souxmar_field_t* out_field = nullptr;
  souxmar_status_t status{SOUXMAR_E_INTERNAL, "uninitialised", nullptr};

  const auto guard = plugin::guard_call([&] {
    status = entry->vtable->solve_fn(c_mesh, c_inputs, &options, &out_field, entry->user_data);
  });
  if (guard.outcome != plugin::GuardOutcome::Ok) {
    if (out_field)
      souxmar_field_free(out_field);
    return DispatchError{fmt::format("solver '{}' raised: {}", ctx.capability_id, guard.detail)};
  }
  if (status.code != SOUXMAR_OK) {
    if (out_field)
      souxmar_field_free(out_field);
    return DispatchError{fmt::format("solver '{}' returned error {}: {}",
                                     ctx.capability_id,
                                     status.code,
                                     status.message ? status.message : "(no message)")};
  }
  if (!out_field) {
    return DispatchError{fmt::format("solver '{}' returned OK but no field", ctx.capability_id)};
  }

  auto out = std::make_shared<StageOutput>();
  out->kind = StageOutput::Kind::Field;
  out->field = std::shared_ptr<souxmar::core::Field>(
      reinterpret_cast<souxmar::core::Field*>(out_field),
      [](souxmar::core::Field* f) { souxmar_field_free(reinterpret_cast<souxmar_field_t*>(f)); });
  return std::static_pointer_cast<void>(out);
}

DispatchResult dispatch_writer(plugin::Registry& registry, const DispatchContext& ctx) {
  const auto* entry = registry.find_writer(ctx.capability_id);
  if (!entry) {
    return DispatchError{fmt::format("no writer capability registered as '{}'", ctx.capability_id)};
  }

  // Required `mesh` upstream handle.
  auto mesh_stage = extract_stage_ref(ctx.inputs, "mesh");
  if (!mesh_stage) {
    return DispatchError{fmt::format("writer '{}' input is missing required 'mesh: {{from: ...}}'",
                                     ctx.capability_id)};
  }
  const auto* mesh_up = upstream_as(ctx.upstream_outputs, *mesh_stage, StageOutput::Kind::Mesh);
  if (!mesh_up) {
    return DispatchError{
        fmt::format("stage referenced by 'mesh' ({}) did not produce a Mesh", *mesh_stage)};
  }
  const auto* c_mesh = reinterpret_cast<const souxmar_mesh_t*>(mesh_up->mesh.get());

  // Optional `field` upstream handle.
  const souxmar_field_t* c_field = nullptr;
  if (auto field_stage = extract_stage_ref(ctx.inputs, "field")) {
    const auto* field_up =
        upstream_as(ctx.upstream_outputs, *field_stage, StageOutput::Kind::Field);
    if (!field_up) {
      return DispatchError{
          fmt::format("stage referenced by 'field' ({}) did not produce a Field", *field_stage)};
    }
    c_field = reinterpret_cast<const souxmar_field_t*>(field_up->field.get());
  }

  // Pass the whole stage input bag (path, format flags, etc.).
  const auto* c_inputs = reinterpret_cast<const souxmar_value_t*>(&ctx.inputs);

  souxmar_status_t status{SOUXMAR_E_INTERNAL, "uninitialised", nullptr};
  const auto guard = plugin::guard_call(
      [&] { status = entry->vtable->write_fn(c_mesh, c_field, c_inputs, entry->user_data); });
  if (guard.outcome != plugin::GuardOutcome::Ok) {
    return DispatchError{fmt::format("writer '{}' raised: {}", ctx.capability_id, guard.detail)};
  }
  if (status.code != SOUXMAR_OK) {
    return DispatchError{fmt::format("writer '{}' returned error {}: {}",
                                     ctx.capability_id,
                                     status.code,
                                     status.message ? status.message : "(no message)")};
  }

  auto out = std::make_shared<StageOutput>();
  out->kind = StageOutput::Kind::Path;
  if (const auto* p = ctx.inputs.find("path"); p && p->kind() == Value::Kind::String) {
    out->path = std::string(p->as_string());
  }
  return std::static_pointer_cast<void>(out);
}

DispatchResult dispatch_postproc(plugin::Registry& registry, const DispatchContext& ctx) {
  const auto* entry = registry.find_postproc(ctx.capability_id);
  if (!entry) {
    return DispatchError{
        fmt::format("no postproc capability registered as '{}'", ctx.capability_id)};
  }

  // Required `mesh` upstream handle.
  auto mesh_stage = extract_stage_ref(ctx.inputs, "mesh");
  if (!mesh_stage) {
    return DispatchError{fmt::format(
        "postproc '{}' input is missing required 'mesh: {{from: ...}}'", ctx.capability_id)};
  }
  const auto* mesh_up = upstream_as(ctx.upstream_outputs, *mesh_stage, StageOutput::Kind::Mesh);
  if (!mesh_up) {
    return DispatchError{
        fmt::format("stage referenced by 'mesh' ({}) did not produce a Mesh", *mesh_stage)};
  }
  const auto* c_mesh = reinterpret_cast<const souxmar_mesh_t*>(mesh_up->mesh.get());

  // Required `field` upstream handle — postproc is field-in / field-out by
  // definition; missing input field is a hard error, not silent NULL.
  auto field_stage = extract_stage_ref(ctx.inputs, "field");
  if (!field_stage) {
    return DispatchError{fmt::format(
        "postproc '{}' input is missing required 'field: {{from: ...}}'", ctx.capability_id)};
  }
  const auto* field_up = upstream_as(ctx.upstream_outputs, *field_stage, StageOutput::Kind::Field);
  if (!field_up) {
    return DispatchError{
        fmt::format("stage referenced by 'field' ({}) did not produce a Field", *field_stage)};
  }
  const auto* c_field = reinterpret_cast<const souxmar_field_t*>(field_up->field.get());

  souxmar_postproc_options_t options{-1, 0.0, 0};
  if (const auto* v = ctx.inputs.find("tolerance"); v && v->kind() == Value::Kind::Number) {
    options.tolerance = v->as_number();
  }
  if (const auto* v = ctx.inputs.find("max_iterations"); v && v->kind() == Value::Kind::Number) {
    options.max_iterations = static_cast<std::int32_t>(v->as_number());
  }
  if (const auto* v = ctx.inputs.find("random_seed"); v && v->kind() == Value::Kind::Number) {
    options.random_seed = static_cast<std::int64_t>(v->as_number());
  }

  // Pass the whole stage input bag through — the plugin may inspect
  // non-mesh / non-field keys (e.g. component-selection flags).
  const auto* c_inputs = reinterpret_cast<const souxmar_value_t*>(&ctx.inputs);

  souxmar_field_t* out_field = nullptr;
  souxmar_status_t status{SOUXMAR_E_INTERNAL, "uninitialised", nullptr};
  const auto guard = plugin::guard_call([&] {
    status = entry->vtable->compute_fn(
        c_mesh, c_field, c_inputs, &options, &out_field, entry->user_data);
  });
  if (guard.outcome != plugin::GuardOutcome::Ok) {
    if (out_field)
      souxmar_field_free(out_field);
    return DispatchError{fmt::format("postproc '{}' raised: {}", ctx.capability_id, guard.detail)};
  }
  if (status.code != SOUXMAR_OK) {
    if (out_field)
      souxmar_field_free(out_field);
    return DispatchError{fmt::format("postproc '{}' returned error {}: {}",
                                     ctx.capability_id,
                                     status.code,
                                     status.message ? status.message : "(no message)")};
  }
  if (!out_field) {
    return DispatchError{fmt::format("postproc '{}' returned OK but no field", ctx.capability_id)};
  }

  auto out = std::make_shared<StageOutput>();
  out->kind = StageOutput::Kind::Field;
  out->field = std::shared_ptr<souxmar::core::Field>(
      reinterpret_cast<souxmar::core::Field*>(out_field),
      [](souxmar::core::Field* f) { souxmar_field_free(reinterpret_cast<souxmar_field_t*>(f)); });
  return std::static_pointer_cast<void>(out);
}

DispatchResult dispatch_reader(plugin::Registry& registry, const DispatchContext& ctx) {
  const auto* entry = registry.find_reader(ctx.capability_id);
  if (!entry) {
    return DispatchError{fmt::format("no reader capability registered as '{}'", ctx.capability_id)};
  }

  // Required `path` input.
  const auto* path_v = ctx.inputs.find("path");
  if (!path_v || path_v->kind() != Value::Kind::String) {
    return DispatchError{
        fmt::format("reader '{}' input is missing required string `path`", ctx.capability_id)};
  }
  const std::string path_str(path_v->as_string());

  const auto* c_inputs = reinterpret_cast<const souxmar_value_t*>(&ctx.inputs);

  souxmar_mesh_t* out_mesh = nullptr;
  souxmar_geometry_t* out_geometry = nullptr;
  souxmar_status_t status{SOUXMAR_E_INTERNAL, "uninitialised", nullptr};

  const auto guard = plugin::guard_call([&] {
    status = entry->vtable->read_fn(path_str.c_str(),
                                    c_inputs,
                                    /*options=*/nullptr,
                                    &out_mesh,
                                    &out_geometry,
                                    entry->user_data);
  });
  if (guard.outcome != plugin::GuardOutcome::Ok) {
    if (out_mesh)
      souxmar_mesh_free(out_mesh);
    if (out_geometry)
      souxmar_geometry_free(out_geometry);
    return DispatchError{fmt::format("reader '{}' raised: {}", ctx.capability_id, guard.detail)};
  }
  if (status.code != SOUXMAR_OK) {
    if (out_mesh)
      souxmar_mesh_free(out_mesh);
    if (out_geometry)
      souxmar_geometry_free(out_geometry);
    return DispatchError{fmt::format("reader '{}' returned error {}: {}",
                                     ctx.capability_id,
                                     status.code,
                                     status.message ? status.message : "(no message)")};
  }
  if ((out_mesh == nullptr) == (out_geometry == nullptr)) {
    // Both NULL → nothing produced; both set → ambiguous. Either is a
    // plugin contract violation; clean up and surface the error.
    if (out_mesh)
      souxmar_mesh_free(out_mesh);
    if (out_geometry)
      souxmar_geometry_free(out_geometry);
    return DispatchError{
        fmt::format("reader '{}' must fill exactly one of out_mesh / out_geometry "
                    "(got mesh={}, geometry={})",
                    ctx.capability_id,
                    out_mesh != nullptr,
                    out_geometry != nullptr)};
  }

  auto out = std::make_shared<StageOutput>();
  if (out_mesh) {
    out->kind = StageOutput::Kind::Mesh;
    out->mesh = std::shared_ptr<souxmar::core::Mesh>(
        reinterpret_cast<souxmar::core::Mesh*>(out_mesh),
        [](souxmar::core::Mesh* m) { souxmar_mesh_free(reinterpret_cast<souxmar_mesh_t*>(m)); });
  } else {
    out->kind = StageOutput::Kind::Geometry;
    out->geometry = std::shared_ptr<souxmar::core::Geometry>(
        reinterpret_cast<souxmar::core::Geometry*>(out_geometry), [](souxmar::core::Geometry* g) {
          souxmar_geometry_free(reinterpret_cast<souxmar_geometry_t*>(g));
        });
  }
  return std::static_pointer_cast<void>(out);
}

}  // namespace

RegistryDispatcher::RegistryDispatcher(plugin::Registry& registry) : registry_(registry) {}

std::string RegistryDispatcher::plugin_version(std::string_view capability_id) {
  if (const auto* e = registry_.find(capability_id); e) {
    return fmt::format("abi{}", e->abi_version);
  }
  return {};
}

std::string RegistryDispatcher::plugin_id(std::string_view capability_id) {
  if (const auto* e = registry_.find(capability_id); e) {
    return e->plugin_id;
  }
  return std::string(capability_id);
}

::souxmar::plugin::ThreadingModel RegistryDispatcher::plugin_threading(
    std::string_view capability_id) {
  if (auto m = registry_.find_threading(capability_id); m.has_value()) {
    return *m;
  }
  // Conservative default: SingleThreaded. The parallel runner over-
  // serialises an unknown capability rather than risking concurrent calls
  // into a plugin that did not declare itself reentrant.
  return ::souxmar::plugin::ThreadingModel::SingleThreaded;
}

DispatchResult RegistryDispatcher::dispatch(const DispatchContext& ctx) {
  // Route by namespace prefix.
  if (ctx.capability_id.starts_with("mesher.")) {
    return dispatch_mesher(registry_, ctx);
  }
  if (ctx.capability_id.starts_with("solver.")) {
    return dispatch_solver(registry_, ctx);
  }
  if (ctx.capability_id.starts_with("writer.")) {
    return dispatch_writer(registry_, ctx);
  }
  if (ctx.capability_id.starts_with("postproc.")) {
    return dispatch_postproc(registry_, ctx);
  }
  if (ctx.capability_id.starts_with("reader.")) {
    return dispatch_reader(registry_, ctx);
  }
  return DispatchError{
      fmt::format("unsupported capability namespace for '{}' "
                  "(known namespaces: mesher.*, solver.*, writer.*, postproc.*, reader.*)",
                  ctx.capability_id)};
}

// ---- StageOutput on-disk serialization -----------------------------------

std::optional<std::vector<std::uint8_t>> serialize_stage_output(
    const std::shared_ptr<void>& payload) {
  if (!payload)
    return std::nullopt;
  const auto* so = static_cast<const StageOutput*>(payload.get());
  if (so->kind != StageOutput::Kind::Path) {
    // Mesh/Geometry/Field need plugin-side serializers — defer to Sprint 5.
    return std::nullopt;
  }

  const auto& path = so->path;
  std::vector<std::uint8_t> out;
  out.reserve(1 + 8 + path.size());
  out.push_back(static_cast<std::uint8_t>(StageOutput::Kind::Path));
  const std::uint64_t len = path.size();
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
  }
  out.insert(out.end(), path.begin(), path.end());
  return out;
}

std::shared_ptr<void> deserialize_stage_output(std::span<const std::uint8_t> blob) {
  if (blob.size() < 9)
    return nullptr;
  if (blob[0] != static_cast<std::uint8_t>(StageOutput::Kind::Path))
    return nullptr;

  std::uint64_t len = 0;
  for (int i = 0; i < 8; ++i) {
    len |= static_cast<std::uint64_t>(blob[1 + i]) << (8 * i);
  }
  if (blob.size() != 9 + len)
    return nullptr;

  // The on-disk artifact this Path output points at must still exist;
  // otherwise a "cached" hit would resolve to a missing file and confuse
  // downstream consumers.
  std::string path(reinterpret_cast<const char*>(blob.data() + 9), static_cast<std::size_t>(len));
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return nullptr;
  }

  auto out = std::make_shared<StageOutput>();
  out->kind = StageOutput::Kind::Path;
  out->path = std::move(path);
  return std::static_pointer_cast<void>(out);
}

}  // namespace souxmar::pipeline

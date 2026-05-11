// SPDX-License-Identifier: Apache-2.0
//
// pysouxmar — pybind11 bindings for the souxmar C++ libraries.
//
// Surface goals (Sprint 4 push 1):
//   * Idiomatic Python: dataclass-like read-only structs, snake_case methods.
//   * Pipeline inputs travel as native Python objects (dict/list/str/int/
//     float/bool/None plus a typed StageRef sentinel) — the C++-side Value
//     tree is a pure implementation detail.
//   * No raw pointers / opaque handles in the public surface. Errors are
//     Python exceptions; C++ status variants are unwrapped at the boundary.
//   * Lifetime safety: PluginLoader keeps a reference to the Registry so
//     accidental re-ordering at the Python level cannot dangle.
//
// Anything that needs the C ABI (Mesh, Geometry, Field handles) is reached
// only through pipeline runs — the runner returns Python-friendly summaries
// instead of raw shared_ptr<void> payloads. Direct handle access lands in
// Sprint 5 once we have plugin-side serialization.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "souxmar/version.h"

#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/pipeline.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"

#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/manifest.h"
#include "souxmar/plugin/registry.h"

namespace py = pybind11;
using namespace souxmar;
namespace fs = std::filesystem;

// ============================================================================
// Value ↔ Python conversion
// ============================================================================
//
// Mapping:
//   Null    <->  None
//   Bool    <->  bool
//   Number  <->  float (always — C++ side is double; ints survive the
//                round-trip as long as they fit in a double's mantissa)
//   String  <->  str
//   Stage   <->  StageRef(stage_id=...)  (Python-side dataclass-ish wrapper)
//   List    <->  list
//   Map     <->  dict
//
// We expose StageRef as a pybind11 class so it round-trips losslessly.

namespace {

py::object value_to_py(const pipeline::Value& v) {
  using Kind = pipeline::Value::Kind;
  switch (v.kind()) {
    case Kind::Null:   return py::none();
    case Kind::Bool:   return py::bool_(v.as_bool());
    case Kind::Number: return py::float_(v.as_number());
    case Kind::String: return py::str(std::string(v.as_string()));
    case Kind::Stage: {
      // Construct a Python-side StageRef via py::cast. The class is
      // registered before this function is callable.
      return py::cast(v.as_stage());
    }
    case Kind::List: {
      py::list out;
      for (const auto& item : v.as_list()) out.append(value_to_py(item));
      return out;
    }
    case Kind::Map: {
      py::dict out;
      for (const auto& [k, child] : v.as_map()) {
        out[py::str(k)] = value_to_py(child);
      }
      return out;
    }
  }
  return py::none();
}

pipeline::Value py_to_value(const py::handle& obj) {
  // Order matters: bool is a subclass of int in Python, so check it first.
  if (obj.is_none()) return pipeline::Value::null_value();
  if (py::isinstance<py::bool_>(obj)) {
    return pipeline::Value::boolean(obj.cast<bool>());
  }
  if (py::isinstance<py::int_>(obj)) {
    return pipeline::Value::number(static_cast<double>(obj.cast<long long>()));
  }
  if (py::isinstance<py::float_>(obj)) {
    return pipeline::Value::number(obj.cast<double>());
  }
  if (py::isinstance<py::str>(obj)) {
    return pipeline::Value::string(obj.cast<std::string>());
  }
  // Try StageRef before generic dict/sequence — a registered class with
  // attribute lookup is the cheapest probe.
  if (py::isinstance<pipeline::StageRef>(obj)) {
    return pipeline::Value::stage_ref(obj.cast<pipeline::StageRef>().stage_id);
  }
  if (py::isinstance<py::dict>(obj)) {
    // YAML-style {from: <stage>} sugar mirrors what the YAML parser accepts —
    // a plain dict with a single string-valued "from" key is treated as a
    // StageRef. This keeps Python pipelines readable without forcing users
    // to import StageRef.
    auto d = obj.cast<py::dict>();
    if (d.size() == 1 && d.contains("from")) {
      auto v = d["from"];
      if (py::isinstance<py::str>(v)) {
        return pipeline::Value::stage_ref(v.cast<std::string>());
      }
    }
    std::map<std::string, pipeline::Value> m;
    for (const auto& kv : d) {
      m.emplace(py::str(kv.first).cast<std::string>(), py_to_value(kv.second));
    }
    return pipeline::Value::map(std::move(m));
  }
  if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
    std::vector<pipeline::Value> items;
    for (const auto& item : obj) items.push_back(py_to_value(item));
    return pipeline::Value::list(std::move(items));
  }
  throw py::type_error(
      "pysouxmar: cannot convert Python value of type '" +
      py::str(py::type::of(obj).attr("__name__")).cast<std::string>() +
      "' to a pipeline Value (expected None/bool/int/float/str/StageRef/dict/list)");
}

// ============================================================================
// StageOutput → Python summary
// ============================================================================
//
// The runner's payload is shared_ptr<void> wrapping a StageOutput. Surfacing
// the raw handle to Python is unsafe (no destructor coordination) and
// uninteresting (no Python code can do anything with a raw Mesh*). Instead
// we return a small Python dict per stage: {"kind": "mesh"|"path"|...,
// optional payload-specific fields}.

py::dict stage_output_to_py(const std::shared_ptr<void>& payload) {
  py::dict out;
  if (!payload) {
    out["kind"] = py::none();
    return out;
  }
  const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
  switch (so->kind) {
    case pipeline::StageOutput::Kind::None:
      out["kind"] = "none";
      break;
    case pipeline::StageOutput::Kind::Mesh:
      out["kind"] = "mesh";
      // Mesh has no Python-readable accessors yet; expose presence only.
      // Sprint 5 plumbs num_nodes / num_cells / element histogram once the
      // C ABI accessors are wrapped here.
      break;
    case pipeline::StageOutput::Kind::Geometry:
      out["kind"] = "geometry";
      break;
    case pipeline::StageOutput::Kind::Field:
      out["kind"] = "field";
      break;
    case pipeline::StageOutput::Kind::Path:
      out["kind"] = "path";
      out["path"] = so->path;
      break;
  }
  return out;
}

}  // namespace

// ============================================================================
// Module
// ============================================================================

PYBIND11_MODULE(_pysouxmar, m) {
  m.doc() = "pysouxmar — Python bindings for souxmar (CAE platform)";

  // ---- Version + ABI -----------------------------------------------------

  m.def("version", []() { return std::string(version_string()); });
  m.def("version_tuple", []() {
    const auto v = ::souxmar::version();
    return py::make_tuple(v.major, v.minor, v.patch);
  });
  m.def("abi_version", []() { return ::souxmar::abi_version(); });

  // ---- Pipeline data model ----------------------------------------------

  py::class_<pipeline::StageRef>(m, "StageRef",
      "Reference to another stage's output. In YAML this is the\n"
      "`{ from: stage_id }` shorthand; in Python construct it directly\n"
      "or pass `{'from': 'stage_id'}` and the binding will convert.")
      .def(py::init<std::string>(), py::arg("stage_id"))
      .def_readonly("stage_id", &pipeline::StageRef::stage_id)
      .def("__repr__", [](const pipeline::StageRef& r) {
        return "StageRef('" + r.stage_id + "')";
      })
      .def("__eq__", [](const pipeline::StageRef& a, const pipeline::StageRef& b) {
        return a == b;
      });

  py::class_<pipeline::Stage>(m, "Stage")
      .def_readonly("id",     &pipeline::Stage::id)
      .def_readonly("plugin", &pipeline::Stage::plugin)
      .def_property_readonly("input", [](const pipeline::Stage& s) {
        return value_to_py(s.input);
      })
      .def("__repr__", [](const pipeline::Stage& s) {
        return "Stage(id='" + s.id + "', plugin='" + s.plugin + "')";
      });

  py::class_<pipeline::Pipeline>(m, "Pipeline")
      .def_readonly("version", &pipeline::Pipeline::version)
      .def_readonly("stages",  &pipeline::Pipeline::stages)
      .def("__len__", [](const pipeline::Pipeline& p) { return p.stages.size(); })
      .def("__repr__", [](const pipeline::Pipeline& p) {
        return "Pipeline(version=" + std::to_string(p.version) +
               ", stages=" + std::to_string(p.stages.size()) + ")";
      });

  // Note: pipeline::ParseError is a struct (not derived from std::exception),
  // so we surface it as a Python ValueError carrying the message + line info.
  // The name "ParseError" is re-exported from the Python facade as an alias
  // of ValueError for documentation purposes.
  m.def("parse_pipeline",
      [](const std::string& yaml_source) {
        auto r = pipeline::parse_pipeline(yaml_source);
        if (auto* err = std::get_if<pipeline::ParseError>(&r)) {
          std::string msg = err->message;
          if (err->line)   msg += " (line " + std::to_string(*err->line) + ")";
          if (err->column) msg += ", column " + std::to_string(*err->column);
          throw py::value_error(msg);
        }
        return std::get<pipeline::Pipeline>(std::move(r));
      },
      py::arg("yaml_source"),
      "Parse a pipeline YAML string. Raises ValueError on parse failure.");

  m.def("parse_pipeline_file",
      [](const fs::path& path) {
        auto r = pipeline::parse_pipeline_file(path);
        if (auto* err = std::get_if<pipeline::ParseError>(&r)) {
          std::string msg = err->message + " [" + path.string() + "]";
          if (err->line)   msg += " (line " + std::to_string(*err->line) + ")";
          if (err->column) msg += ", column " + std::to_string(*err->column);
          throw py::value_error(msg);
        }
        return std::get<pipeline::Pipeline>(std::move(r));
      },
      py::arg("path"),
      "Parse a pipeline YAML file. Raises ValueError on parse failure or "
      "file-not-found.");

  // ---- Manifest + discovery ---------------------------------------------

  py::enum_<plugin::ThreadingModel>(m, "ThreadingModel")
      .value("Reentrant",        plugin::ThreadingModel::Reentrant)
      .value("SingleThreaded",   plugin::ThreadingModel::SingleThreaded)
      .value("InternalParallel", plugin::ThreadingModel::InternalParallel);

  py::class_<plugin::Manifest>(m, "Manifest")
      .def_readonly("id",                  &plugin::Manifest::id)
      .def_readonly("name",                &plugin::Manifest::name)
      .def_readonly("version",             &plugin::Manifest::version)
      .def_readonly("abi",                 &plugin::Manifest::abi)
      .def_readonly("license",             &plugin::Manifest::license)
      .def_readonly("homepage",            &plugin::Manifest::homepage)
      .def_readonly("binary_file",         &plugin::Manifest::binary_file)
      .def_readonly("capabilities",        &plugin::Manifest::capabilities)
      .def_readonly("threading",           &plugin::Manifest::threading)
      .def_readonly("souxmar_version_constraint",
                                           &plugin::Manifest::souxmar_version_constraint)
      .def("__repr__", [](const plugin::Manifest& mf) {
        return "Manifest(id='" + mf.id + "', version='" + mf.version + "')";
      });

  py::class_<plugin::DiscoveredPlugin>(m, "DiscoveredPlugin")
      .def_readonly("manifest_path", &plugin::DiscoveredPlugin::manifest_path)
      .def_readonly("binary_path",   &plugin::DiscoveredPlugin::binary_path)
      .def_readonly("manifest",      &plugin::DiscoveredPlugin::manifest);

  py::class_<plugin::DiscoveryRejection>(m, "DiscoveryRejection")
      .def_readonly("candidate_path", &plugin::DiscoveryRejection::candidate_path)
      .def_readonly("reason",         &plugin::DiscoveryRejection::reason);

  py::class_<plugin::DiscoveryReport>(m, "DiscoveryReport")
      .def_readonly("loaded",   &plugin::DiscoveryReport::loaded)
      .def_readonly("rejected", &plugin::DiscoveryReport::rejected);

  py::class_<plugin::DiscoveryOptions>(m, "DiscoveryOptions")
      .def(py::init<>())
      .def_readwrite("include_env_path",       &plugin::DiscoveryOptions::include_env_path)
      .def_readwrite("include_install_prefix", &plugin::DiscoveryOptions::include_install_prefix)
      .def_readwrite("include_user_prefix",    &plugin::DiscoveryOptions::include_user_prefix)
      .def_readwrite("include_cwd",            &plugin::DiscoveryOptions::include_cwd)
      .def_readwrite("install_prefix",         &plugin::DiscoveryOptions::install_prefix);

  m.def("default_search_paths", &plugin::default_search_paths,
      py::arg("options") = plugin::DiscoveryOptions{});

  m.def("discover_plugins",
      [](const std::vector<fs::path>& search_paths) {
        return plugin::discover_plugins(search_paths);
      },
      py::arg("search_paths"));

  m.def("discover_plugins",
      [](const plugin::DiscoveryOptions& opts) {
        return plugin::discover_plugins(opts);
      },
      py::arg("options"));

  // ---- Registry + loader ------------------------------------------------

  py::enum_<plugin::CapabilityKind>(m, "CapabilityKind")
      .value("Mesher", plugin::CapabilityKind::Mesher)
      .value("Solver", plugin::CapabilityKind::Solver)
      .value("Writer", plugin::CapabilityKind::Writer);

  py::class_<plugin::Registry>(m, "Registry",
      "Capability registry — owned by the Python side; outlives every\n"
      "PluginLoader and LoadedPlugin you create against it.")
      .def(py::init<>())
      .def("size", &plugin::Registry::size)
      .def("__len__", &plugin::Registry::size)
      .def("list_capabilities", &plugin::Registry::list_capabilities)
      .def("list_capabilities_in_namespace",
           &plugin::Registry::list_capabilities_in_namespace,
           py::arg("namespace_prefix"));

  // Same note as ParseError: LoadError is a struct, surfaced as ValueError
  // carrying binary_path + message.
  py::class_<plugin::LoadedPlugin>(m, "LoadedPlugin",
      "Move-only RAII handle. Drop it and the registry forgets the plugin's\n"
      "capabilities + the OS module is closed.")
      .def_property_readonly("manifest",    &plugin::LoadedPlugin::manifest)
      .def_property_readonly("binary_path", &plugin::LoadedPlugin::binary_path);

  py::class_<plugin::PluginLoader>(m, "PluginLoader")
      // The Registry must outlive the loader and every loaded plugin —
      // pybind11's `keep_alive` ties the Python object lifetimes so a
      // collected Registry cannot leave the loader dangling.
      .def(py::init<plugin::Registry&, std::string>(),
           py::arg("registry"), py::arg("host_version"),
           py::keep_alive<1, 2>())
      .def("load",
           [](plugin::PluginLoader& self, const plugin::DiscoveredPlugin& d) {
             auto r = self.load(d);
             if (auto* err = std::get_if<plugin::LoadError>(&r)) {
               throw py::value_error("plugin load failed [" +
                                     err->binary_path.string() + "]: " + err->message);
             }
             return std::move(std::get<plugin::LoadedPlugin>(r));
           },
           py::arg("discovered"),
           "Load a discovered plugin into the registry. Raises ValueError "
           "if the binary cannot be opened or the registration call returns "
           "an error. Returns a LoadedPlugin RAII handle; drop it to unload.");

  // ---- Cache ------------------------------------------------------------

  py::class_<pipeline::ContentHash>(m, "ContentHash")
      .def_property_readonly("hex", &pipeline::ContentHash::hex)
      .def("__repr__", [](const pipeline::ContentHash& h) {
        return "ContentHash('" + h.hex() + "')";
      })
      .def("__eq__", [](const pipeline::ContentHash& a,
                        const pipeline::ContentHash& b) { return a == b; });

  py::class_<pipeline::Cache>(m, "Cache",
      "In-process content-addressed payload store.")
      .def(py::init<>())
      .def("size",     &pipeline::Cache::size)
      .def("__len__",  &pipeline::Cache::size)
      .def("clear",    &pipeline::Cache::clear)
      .def("contains", &pipeline::Cache::contains, py::arg("key"));

  py::class_<pipeline::DiskCache, std::shared_ptr<pipeline::DiskCache>>(m, "DiskCache",
      "Directory-backed byte-blob KV store keyed by ContentHash. Used by\n"
      "`run_pipeline` when `RunOptions.disk_cache_dir` is set, so cached\n"
      "stages survive across processes.")
      .def(py::init<fs::path>(), py::arg("dir"))
      .def_property_readonly("dir", &pipeline::DiskCache::dir)
      .def("contains", &pipeline::DiskCache::contains, py::arg("key"))
      .def_static("default_dir", &pipeline::DiskCache::default_dir,
                  py::arg("override_path") = fs::path{});

  // ---- Runner ------------------------------------------------------------

  py::class_<pipeline::DispatchError>(m, "DispatchError")
      .def_readonly("message", &pipeline::DispatchError::message);

  py::enum_<pipeline::StageRunResult::Status>(m, "StageStatus")
      .value("Cached",   pipeline::StageRunResult::Status::Cached)
      .value("Executed", pipeline::StageRunResult::Status::Executed)
      .value("Failed",   pipeline::StageRunResult::Status::Failed)
      .value("Skipped",  pipeline::StageRunResult::Status::Skipped);

  py::class_<pipeline::StageRunResult>(m, "StageRunResult")
      .def_readonly("stage_id",     &pipeline::StageRunResult::stage_id)
      .def_readonly("status",       &pipeline::StageRunResult::status)
      .def_readonly("content_hash", &pipeline::StageRunResult::content_hash)
      .def_readonly("error",        &pipeline::StageRunResult::error)
      .def("__repr__", [](const pipeline::StageRunResult& s) {
        std::string st;
        switch (s.status) {
          case pipeline::StageRunResult::Status::Cached:   st = "Cached"; break;
          case pipeline::StageRunResult::Status::Executed: st = "Executed"; break;
          case pipeline::StageRunResult::Status::Failed:   st = "Failed"; break;
          case pipeline::StageRunResult::Status::Skipped:  st = "Skipped"; break;
        }
        return "StageRunResult(stage_id='" + s.stage_id + "', status=" + st + ")";
      });

  py::enum_<pipeline::RunResult::Status>(m, "RunStatus")
      .value("Success",          pipeline::RunResult::Status::Success)
      .value("ValidationFailed", pipeline::RunResult::Status::ValidationFailed)
      .value("StageFailed",      pipeline::RunResult::Status::StageFailed);

  py::class_<pipeline::RunResult>(m, "RunResult")
      .def_readonly("status",            &pipeline::RunResult::status)
      .def_readonly("validation_errors", &pipeline::RunResult::validation_errors)
      .def_readonly("stage_results",     &pipeline::RunResult::stage_results)
      .def_property_readonly("outputs", [](const pipeline::RunResult& r) {
        py::dict out;
        for (const auto& [stage_id, payload] : r.outputs) {
          out[py::str(stage_id)] = stage_output_to_py(payload);
        }
        return out;
      });

  py::class_<pipeline::RunOptions>(m, "RunOptions")
      .def(py::init<>())
      .def_readwrite("use_cache",            &pipeline::RunOptions::use_cache)
      .def_readwrite("stop_on_first_failure",&pipeline::RunOptions::stop_on_first_failure)
      // Convenience: instead of exposing the C++ DiskBacking with its
      // raw callbacks, we accept a directory path here and the binding
      // wires the Path-StageOutput round-trip on the user's behalf.
      .def_property("disk_cache_dir",
          [](const pipeline::RunOptions& o) -> py::object {
            if (!o.disk_backing || !o.disk_backing->cache) return py::none();
            return py::cast(o.disk_backing->cache->dir());
          },
          [](pipeline::RunOptions& o, py::object v) {
            if (v.is_none()) {
              o.disk_backing.reset();
              return;
            }
            auto cache = std::make_shared<pipeline::DiskCache>(v.cast<fs::path>());
            pipeline::DiskBacking b;
            b.cache       = std::move(cache);
            b.serialize   = &pipeline::serialize_stage_output;
            b.deserialize = &pipeline::deserialize_stage_output;
            o.disk_backing = std::move(b);
          });

  // Plain IDispatcher base — registered first so RegistryDispatcher can
  // declare it as a base class. Sprint 4 push 2 lifts a Python trampoline
  // off this so user code can subclass IDispatcher and provide a custom
  // routing strategy.
  py::class_<pipeline::IDispatcher>(m, "IDispatcher");

  py::class_<pipeline::RegistryDispatcher, pipeline::IDispatcher>(
      m, "RegistryDispatcher",
      "IDispatcher implementation that routes capability ids to their\n"
      "registered C ABI vtables. Pass to `run_pipeline`.")
      .def(py::init<plugin::Registry&>(), py::arg("registry"),
           py::keep_alive<1, 2>());

  m.def("run_pipeline",
        [](const pipeline::Pipeline&    p,
           pipeline::RegistryDispatcher& d,
           pipeline::Cache&             c,
           const pipeline::RunOptions&  opts) {
          return pipeline::run_pipeline(p, d, c, opts);
        },
        py::arg("pipeline"),
        py::arg("dispatcher"),
        py::arg("cache"),
        py::arg("options") = pipeline::RunOptions{},
        "Run a pipeline through the supplied dispatcher and cache. Returns\n"
        "a RunResult — inspect `.status`, `.stage_results`, `.outputs`.");
}

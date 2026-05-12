// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater per-user state file. See
// include/souxmar/update/update_state.h for the contract and
// ADR-0013 § "Replay-after-rollback attack" for the threat model
// that motivates the max_version_ever_seen field.

#include "souxmar/update/update_state.h"

#include <toml++/toml.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace souxmar::update {

namespace fs = std::filesystem;

namespace {

std::filesystem::path home_dir() {
#if defined(_WIN32)
  if (const char* v = std::getenv("USERPROFILE"); v && *v)
    return v;
  return {};
#else
  if (const char* v = std::getenv("HOME"); v && *v)
    return v;
  return {};
#endif
}

std::string read_string_field(const toml::table& tbl, const char* key) {
  const auto* v = tbl.get(key);
  if (!v)
    return {};
  auto sv = v->value<std::string>();
  return sv ? *sv : std::string{};
}

}  // namespace

std::filesystem::path default_update_state_path() {
  if (const char* v = std::getenv("SOUXMAR_UPDATE_STATE"); v && *v) {
    return fs::path{v};
  }
#if defined(_WIN32)
  if (const char* v = std::getenv("LOCALAPPDATA"); v && *v) {
    return fs::path{v} / "souxmar" / "update-state.toml";
  }
#elif defined(__APPLE__)
  if (auto h = home_dir(); !h.empty()) {
    return h / "Library" / "Application Support" / "souxmar" / "update-state.toml";
  }
#else
  if (const char* v = std::getenv("XDG_STATE_HOME"); v && *v) {
    return fs::path{v} / "souxmar" / "update-state.toml";
  }
  if (auto h = home_dir(); !h.empty()) {
    return h / ".local" / "state" / "souxmar" / "update-state.toml";
  }
#endif
  std::error_code ec;
  return fs::temp_directory_path(ec) / "souxmar-update-state.toml";
}

std::string render_update_state(const UpdateState& s) {
  // Hand-rolled TOML — small and exact. Avoids tomlplusplus's emitter,
  // which sorts table keys and we want a canonical field order for
  // diff-friendliness across releases.
  std::ostringstream o;
  o << "# souxmar auto-updater per-user state. Schema is locked by\n"
       "# ADR-0013; do not hand-edit unless you also bump `schema`.\n"
       "schema                    = "
    << kUpdateStateSchemaV1
    << "\n"
       "current_installed_version = \""
    << s.current_installed_version
    << "\"\n"
       "max_version_ever_seen     = \""
    << s.max_version_ever_seen
    << "\"\n"
       "last_check_at             = \""
    << s.last_check_at
    << "\"\n"
       "last_apply_at             = \""
    << s.last_apply_at << "\"\n";
  return o.str();
}

UpdateStateLoadResult load_update_state(const fs::path& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return UpdateState{};  // fresh-install defaults
  }
  toml::table root;
  try {
    root = toml::parse_file(path.string());
  } catch (const toml::parse_error& e) {
    UpdateStateLoadError err;
    err.message = path.string() + ": " + std::string{e.description()};
    return err;
  } catch (const std::exception& e) {
    UpdateStateLoadError err;
    err.message = path.string() + ": " + e.what();
    return err;
  }

  const auto* schema_node = root.get("schema");
  if (!schema_node) {
    return UpdateStateLoadError{path.string() + ": missing required field 'schema'"};
  }
  const auto schema_val = schema_node->value<std::int64_t>();
  if (!schema_val) {
    return UpdateStateLoadError{path.string() + ": 'schema' must be an integer"};
  }
  if (*schema_val != static_cast<std::int64_t>(kUpdateStateSchemaV1)) {
    return UpdateStateLoadError{path.string() + ": unsupported state-file schema "
                                + std::to_string(*schema_val) + " (this client understands "
                                + std::to_string(kUpdateStateSchemaV1) + ")"};
  }

  UpdateState s;
  s.schema = kUpdateStateSchemaV1;
  s.current_installed_version = read_string_field(root, "current_installed_version");
  s.max_version_ever_seen = read_string_field(root, "max_version_ever_seen");
  s.last_check_at = read_string_field(root, "last_check_at");
  s.last_apply_at = read_string_field(root, "last_apply_at");
  return s;
}

bool save_update_state(const fs::path& path, const UpdateState& s) {
  std::error_code ec;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent, ec);
    if (ec && !fs::is_directory(parent))
      return false;
  }

  thread_local std::mt19937_64 rng{std::random_device{}()};
  const auto tmp = path.string() + ".tmp." + std::to_string(rng());
  {
    std::ofstream sink(tmp, std::ios::binary | std::ios::trunc);
    if (!sink.is_open())
      return false;
    const auto rendered = render_update_state(s);
    sink.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
    if (!sink.good()) {
      sink.close();
      fs::remove(tmp, ec);
      return false;
    }
  }

  fs::rename(tmp, path, ec);
  if (ec) {
    // POSIX rename is atomic across the same filesystem; on Windows
    // it can fail if the target is locked. Fall back to copy-and-
    // delete; still safe — the next save retries the rename path.
    fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
    fs::remove(tmp, ec);
    if (ec)
      return false;
  }
  return true;
}

}  // namespace souxmar::update

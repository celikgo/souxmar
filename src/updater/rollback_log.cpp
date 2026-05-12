// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater rollback / apply event log. See
// include/souxmar/update/rollback_log.h for the format and contract.

#include "souxmar/update/rollback_log.h"

#include <toml++/toml.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace souxmar::update {

namespace fs = std::filesystem;

namespace {

std::string read_string_field(const toml::table& tbl, const char* key) {
  const auto* v = tbl.get(key);
  if (!v)
    return {};
  auto sv = v->value<std::string>();
  return sv ? *sv : std::string{};
}

std::string_view type_to_string(RollbackEventType t) noexcept {
  switch (t) {
    case RollbackEventType::Apply:
      return "apply";
    case RollbackEventType::Rollback:
      return "rollback";
  }
  return "unknown";
}

}  // namespace

std::string_view to_string(RollbackEventType t) noexcept {
  return type_to_string(t);
}

std::string render_rollback_log(const std::vector<RollbackEvent>& events) {
  std::ostringstream o;
  o << "# souxmar auto-updater rollback / apply event log. Schema is\n"
       "# locked by ADR-0013; append-only — do not hand-edit unless\n"
       "# you also bump `schema`.\n"
       "schema = "
    << kRollbackLogSchemaV1 << "\n\n";
  for (const auto& e : events) {
    o << "[[event]]\n"
         "timestamp       = \""
      << e.timestamp
      << "\"\n"
         "type            = \""
      << type_to_string(e.type)
      << "\"\n"
         "from_version    = \""
      << e.from_version
      << "\"\n"
         "to_version      = \""
      << e.to_version << "\"\n";
    // The artifact + key fields are emitted unconditionally so
    // downstream parsers can rely on the field set; empty strings
    // mean "not applicable" (e.g., rollback events).
    o << "artifact_sha256 = \"" << e.artifact_sha256
      << "\"\n"
         "public_key_id   = \""
      << e.public_key_id << "\"\n\n";
  }
  return o.str();
}

RollbackLogLoadResult load_rollback_log(const fs::path& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return std::vector<RollbackEvent>{};
  }

  toml::table root;
  try {
    root = toml::parse_file(path.string());
  } catch (const toml::parse_error& e) {
    return RollbackLogLoadError{path.string() + ": " + std::string{e.description()}};
  } catch (const std::exception& e) {
    return RollbackLogLoadError{path.string() + ": " + e.what()};
  }

  const auto* schema_node = root.get("schema");
  if (!schema_node) {
    return RollbackLogLoadError{path.string() + ": missing required field 'schema'"};
  }
  const auto schema_val = schema_node->value<std::int64_t>();
  if (!schema_val) {
    return RollbackLogLoadError{path.string() + ": 'schema' must be an integer"};
  }
  if (*schema_val != static_cast<std::int64_t>(kRollbackLogSchemaV1)) {
    return RollbackLogLoadError{path.string() + ": unsupported rollback-log schema "
                                + std::to_string(*schema_val) + " (this client understands "
                                + std::to_string(kRollbackLogSchemaV1) + ")"};
  }

  std::vector<RollbackEvent> events;
  const auto* arr = root.get_as<toml::array>("event");
  if (arr) {
    events.reserve(arr->size());
    std::size_t idx = 0;
    for (const auto& node : *arr) {
      const auto* tbl = node.as_table();
      if (!tbl) {
        return RollbackLogLoadError{path.string() + ": [[event]] entry #" + std::to_string(idx)
                                    + " is not a table"};
      }
      RollbackEvent e;
      e.timestamp = read_string_field(*tbl, "timestamp");
      e.from_version = read_string_field(*tbl, "from_version");
      e.to_version = read_string_field(*tbl, "to_version");
      e.artifact_sha256 = read_string_field(*tbl, "artifact_sha256");
      e.public_key_id = read_string_field(*tbl, "public_key_id");

      const std::string type_str = read_string_field(*tbl, "type");
      if (type_str == "apply")
        e.type = RollbackEventType::Apply;
      else if (type_str == "rollback")
        e.type = RollbackEventType::Rollback;
      else {
        return RollbackLogLoadError{path.string() + ": [[event]] #" + std::to_string(idx)
                                    + " has unknown type '" + type_str + "'"};
      }
      events.push_back(std::move(e));
      ++idx;
    }
  }
  return events;
}

bool append_rollback_event(const fs::path& path, const RollbackEvent& event) {
  // Read existing (if any), append, write atomically. Linear in N
  // events, see the header comment for the bounded-N argument.
  std::vector<RollbackEvent> events;
  auto loaded = load_rollback_log(path);
  if (auto* err = std::get_if<RollbackLogLoadError>(&loaded)) {
    // A corrupt log is a load-bearing diagnostic — fail-loud rather
    // than silently truncating. The caller surfaces this and bails.
    (void)err;
    return false;
  }
  events = std::move(std::get<std::vector<RollbackEvent>>(loaded));
  events.push_back(event);

  std::error_code ec;
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path(), ec);
    if (ec && !fs::is_directory(path.parent_path()))
      return false;
  }

  thread_local std::mt19937_64 rng{std::random_device{}()};
  const fs::path tmp = path.string() + ".tmp." + std::to_string(rng());
  {
    std::ofstream sink(tmp, std::ios::binary | std::ios::trunc);
    if (!sink.is_open())
      return false;
    const auto rendered = render_rollback_log(events);
    sink.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
    if (!sink.good()) {
      sink.close();
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
    fs::remove(tmp, ec);
    if (ec)
      return false;
  }
  return true;
}

std::string find_rollback_target(const std::vector<RollbackEvent>& events,
                                 const std::string& current_version) {
  if (current_version.empty())
    return {};
  // Walk in reverse. The first Apply event whose to_version names the
  // current install is the one that put us where we are; its
  // from_version is the rollback target. Rollback events are skipped
  // — they're audit, not state-restoration choices. A rollback that
  // happened "after" the apply we're looking for is still part of
  // the path back, and the rollback's *from_version* records what
  // version we left, which is what we want to avoid going to again.
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    if (it->type == RollbackEventType::Apply && it->to_version == current_version
        && !it->from_version.empty()) {
      return it->from_version;
    }
  }
  return {};
}

}  // namespace souxmar::update

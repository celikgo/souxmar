// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-c-bridge — auto_updater_menu surface. Sprint 15 push 4.
//
// Read-only update-status query. The desktop's "Apply update"
// button shells out to the existing CLI `souxmar update apply`
// path; this surface only exists to render the *current state*
// in the desktop menu.

#include "souxmar-c-bridge/updater.h"

#include "souxmar/update/install_layout.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>

struct souxmar_bridge_update_status_t {
  int32_t state = SOUXMAR_BRIDGE_US_UNKNOWN;
  std::string current_version;
  std::string available_version;
  std::string detail;
};

namespace {

souxmar_bridge_update_status_t* new_status(int32_t state,
                                           std::string current,
                                           std::string available,
                                           std::string detail) {
  auto* out = new (std::nothrow) souxmar_bridge_update_status_t;
  if (!out)
    return nullptr;
  out->state = state;
  out->current_version = std::move(current);
  out->available_version = std::move(available);
  out->detail = std::move(detail);
  return out;
}

void set_err(char** out_err, std::string_view msg) {
  if (!out_err)
    return;
  *out_err = static_cast<char*>(std::malloc(msg.size() + 1));
  if (*out_err) {
    std::memcpy(*out_err, msg.data(), msg.size());
    (*out_err)[msg.size()] = '\0';
  }
}

}  // namespace

extern "C" souxmar_bridge_update_status_t* souxmar_bridge_update_status_read(
    const char* target_root_c,
    char** out_err) {
  if (out_err)
    *out_err = nullptr;
  if (target_root_c == nullptr || target_root_c[0] == '\0') {
    set_err(out_err, "target_root is NULL or empty");
    return nullptr;
  }

  std::filesystem::path target_root(target_root_c);
  std::error_code ec;
  if (!std::filesystem::is_directory(target_root, ec)) {
    // Fresh install on a machine that hasn't run the updater
    // yet — return Unknown rather than NULL so the menu can
    // render "No update history; check again to see updates."
    return new_status(SOUXMAR_BRIDGE_US_UNKNOWN,
                      std::string{},
                      std::string{},
                      "No update install layout at this path. The auto-updater "
                      "has not yet run for this build.");
  }

  souxmar::update::InstallLayout layout(target_root);
  const auto current = layout.read_current_version();

  // Sprint 15 push 4 — we don't yet have a cached "available
  // version" surface on the engine side (the fetch + verify
  // path runs once per `souxmar update check`; its result isn't
  // persisted in a queryable form). Returning UpToDate as the
  // default state when current is known, Unknown otherwise.
  // Sprint 16+ adds a `last_known_available` field to
  // update_state.toml; until then, the desktop menu shows
  // "Current version vX.Y.Z" + "Check for updates" button which
  // triggers the CLI shell-out.
  if (current.empty()) {
    return new_status(SOUXMAR_BRIDGE_US_UNKNOWN,
                      std::string{},
                      std::string{},
                      "No current.txt in the install layout. Either this is a "
                      "fresh install or the layout is corrupted.");
  }

  std::string detail = "Current version " + current +
                       ". Click 'Check for updates' to query the "
                       "release manifest.";
  return new_status(SOUXMAR_BRIDGE_US_UP_TO_DATE, current, std::string{}, std::move(detail));
}

extern "C" int32_t souxmar_bridge_update_state(const souxmar_bridge_update_status_t* s) {
  return s ? s->state : SOUXMAR_BRIDGE_US_UNKNOWN;
}

extern "C" const char* souxmar_bridge_update_current_version(
    const souxmar_bridge_update_status_t* s) {
  return s ? s->current_version.c_str() : "";
}

extern "C" const char* souxmar_bridge_update_available_version(
    const souxmar_bridge_update_status_t* s) {
  return s ? s->available_version.c_str() : "";
}

extern "C" const char* souxmar_bridge_update_detail(const souxmar_bridge_update_status_t* s) {
  return s ? s->detail.c_str() : "";
}

extern "C" void souxmar_bridge_update_status_free(souxmar_bridge_update_status_t* s) {
  delete s;
}

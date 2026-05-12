/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar-c-bridge — auto_updater_menu surface. Sprint 15 push 4.
 *
 * Third real FFI surface (after pipeline.h's pipeline_introspection
 * in Sprint 13 push 3 + provider.h's provider_call in Sprint 14
 * push 4). Routes the desktop app's "Check for updates" menu
 * through the engine's auto-updater state machine (Sprint 10
 * push 6's `souxmar::update::*`).
 *
 * Surface today: read-only status query. The desktop client's
 * menu fetches the current install + available-update status,
 * renders a one-line summary, lets the user click "Apply update"
 * → which routes through the existing CLI `souxmar update apply`
 * path on the host process (not through this bridge). That
 * decision (don't double-implement apply / rollback through FFI)
 * is documented inline below.
 *
 * Stability: bridge ABI bumps 2 → 3 with this push. Old desktop
 * builds linked against v2 cross-check the byte and refuse the
 * call cleanly per ADR-0018 § 5.
 */

#ifndef SOUXMAR_C_BRIDGE_UPDATER_H
#define SOUXMAR_C_BRIDGE_UPDATER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Update-status response handle. Caller frees via
 * souxmar_bridge_update_status_free. */
typedef struct souxmar_bridge_update_status_t souxmar_bridge_update_status_t;

/* Update state. Mirrors `souxmar::update::ApplyOutcome` collapsed
 * to a smaller set suitable for the desktop's menu surface.
 *   0  unknown / state file missing
 *   1  current install up to date
 *   2  update available, not yet downloaded
 *   3  update staged, ready to apply
 *   4  refused by replay-defence (a previously-seen newer
 *      version is the floor)
 *   5  install layout corrupted; manual recovery
 */
#define SOUXMAR_BRIDGE_US_UNKNOWN 0
#define SOUXMAR_BRIDGE_US_UP_TO_DATE 1
#define SOUXMAR_BRIDGE_US_AVAILABLE 2
#define SOUXMAR_BRIDGE_US_STAGED 3
#define SOUXMAR_BRIDGE_US_REFUSED 4
#define SOUXMAR_BRIDGE_US_CORRUPTED 5

/* Read the auto-updater's view of the given target_root and
 * return a status summary. The target_root is the install
 * directory the updater manages (per Sprint 10 push 7's
 * install_layout — `<target_root>/{current.txt, previous.txt,
 * versions/<v>/payload, ...}`).
 *
 * Returns NULL only on catastrophic error (out-of-memory, invalid
 * target_root path), with *out_err populated.
 *
 * On success, the response handle carries:
 *   - the current installed version (string, may be empty if
 *     state is fresh)
 *   - the latest available version (string, may be empty if the
 *     state file has no fetched manifest)
 *   - the state code (one of SOUXMAR_BRIDGE_US_*)
 *   - a human-readable detail string the desktop menu can show
 *     verbatim
 *
 * **Read-only**. The desktop's "Apply update" click does NOT
 * go through this surface — it shells out to `souxmar update
 * apply` on the host system, same path the CLI exercises. That
 * decision is intentional: re-implementing apply / rollback
 * through FFI would duplicate the v1.3-frozen state-machine
 * surface ADR-0014's key-rotation procedure depends on; the
 * shell-out keeps the auto-updater's single point of truth
 * single. */
souxmar_bridge_update_status_t* souxmar_bridge_update_status_read(const char* target_root,
                                                                  char** out_err);

/* Accessors. Strings remain valid until the handle is freed. */
int32_t souxmar_bridge_update_state(const souxmar_bridge_update_status_t* s);
const char* souxmar_bridge_update_current_version(const souxmar_bridge_update_status_t* s);
const char* souxmar_bridge_update_available_version(const souxmar_bridge_update_status_t* s);
const char* souxmar_bridge_update_detail(const souxmar_bridge_update_status_t* s);

/* Release a status handle. Safe to call with NULL. */
void souxmar_bridge_update_status_free(souxmar_bridge_update_status_t* s);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOUXMAR_C_BRIDGE_UPDATER_H */

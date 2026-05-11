/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar plugin C ABI — common preamble.
 *
 * The C ABI is the stable contract between souxmar 1.x and every plugin
 * built against it. See ADR-0001 for the rationale and docs/PLUGIN_SDK.md
 * for the consumer-facing reference.
 *
 * ======================================================================
 * STATUS: ABI v1 frozen FINAL (Sprint 7 push 1, 2026-05-11).
 *
 * Every header in include/souxmar-c/ is locked for the entire 1.x
 * release series. Binary-breaking changes require a major version bump
 * (souxmar 2.0) with a one-major-overlap deprecation cycle. See
 * docs/adr/0008-abi-v1-final-freeze.md for the binding declaration +
 * the inventory of headers under lock.
 *
 * Post-freeze rules (the ratchet, unchanged from the soak period):
 *   - Additive minor surfaces are allowed and bump
 *     SOUXMAR_ABI_VERSION_MINOR monotonically.
 *   - Bug fixes to comments/docs/non-load-bearing details are allowed.
 *   - Anything else requires a Tier-3 ADR per docs/GOVERNANCE.md.
 *
 * Plugin authors targeting v1: this is the contract. Build against the
 * abi_version_minor floor you need; the host advertises its minor in
 * host_info.abi_version_minor. Conformance check C004 catches every
 * "v1.N plugin on a v1.M host where M < N" case.
 * ======================================================================
 */

#ifndef SOUXMAR_ABI_H
#define SOUXMAR_ABI_H

#include <stdint.h>
#include <stddef.h>

#define SOUXMAR_ABI_VERSION_MAJOR 1
/* MINOR bumps record additive surface additions. The history:
 *   v1.0  Sprint 5 push 6 — initial freeze-candidate surface.
 *   v1.1  Sprint 6 push 4 — `reader.*` capability + souxmar-c/reader.h.
 *   v1.2  Sprint 7 push 3 — `souxmar_buffer_new_mmap` + flag constants;
 *                            v2 of ADR-0006 (mmap-backed buffer backing).
 *   v1.3  Sprint 9 push 2 — per-face-tag surface
 *                            (`souxmar_mesh_cell_face_count`,
 *                            `souxmar_mesh_face_tag`,
 *                            `souxmar_mesh_set_face_tag`,
 *                            `SOUXMAR_FACE_UNTAGGED`); ADR-0012.
 *
 * Future minor bumps land via ADR plus the
 * `Ratchet: additive minor surface (ADR-0008)` commit marker checked
 * by scripts/check-frozen-headers.sh. */
#define SOUXMAR_ABI_VERSION_MINOR 3

/* ---- Symbol export ---------------------------------------------------- */

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(SOUXMAR_BUILD_PLUGIN)
    #define SOUXMAR_PLUGIN_EXPORT __declspec(dllexport)
  #else
    #define SOUXMAR_PLUGIN_EXPORT __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define SOUXMAR_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
  #define SOUXMAR_PLUGIN_EXPORT
#endif

/* ---- C linkage scoping helpers --------------------------------------- */

#ifdef __cplusplus
  #define SOUXMAR_C_BEGIN extern "C" {
  #define SOUXMAR_C_END   }
#else
  #define SOUXMAR_C_BEGIN
  #define SOUXMAR_C_END
#endif

#endif /* SOUXMAR_ABI_H */

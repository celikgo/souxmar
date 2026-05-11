/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar plugin C ABI — common preamble.
 *
 * The C ABI is the stable contract between souxmar 1.x and every plugin
 * built against it. See ADR-0001 for the rationale and docs/PLUGIN_SDK.md
 * for the consumer-facing reference.
 *
 * The ABI version is independent of the souxmar release version. ABI v1 is
 * frozen for the entire 1.x release series; binary-breaking changes require
 * a major bump per docs/GOVERNANCE.md.
 *
 * ----------------------------------------------------------------------
 * STATUS: ABI v1 frozen-candidate (Sprint 5 push 6, 2026-05-11).
 *
 * Two-sprint soak period; formal freeze target 2026-06-08. During soak:
 * additive minor surfaces are allowed (these bump SOUXMAR_ABI_VERSION_MINOR);
 * breaking changes to any v1 surface cancel the candidacy and reset the
 * soak. See docs/adr/0007-abi-v1-freeze-candidate.md for the full
 * mechanics + the list of headers under freeze.
 *
 * Plugin authors targeting ABI v1: it is safe to build now. The candidate
 * surface will not break before formal freeze — additive minor changes
 * are forward-compatible by construction (zero-init of unknown fields).
 * ----------------------------------------------------------------------
 */

#ifndef SOUXMAR_ABI_H
#define SOUXMAR_ABI_H

#include <stdint.h>
#include <stddef.h>

#define SOUXMAR_ABI_VERSION_MAJOR 1
#define SOUXMAR_ABI_VERSION_MINOR 0

/* Set during the freeze-candidate soak; removed at formal freeze. Host
 * tooling and plugin authors can branch on this macro to surface
 * "you're building against an un-frozen candidate" warnings if useful.
 *
 * Target removal date: 2026-06-08 (unless soak is cancelled). */
#define SOUXMAR_ABI_FREEZE_CANDIDATE 1

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

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
 */

#ifndef SOUXMAR_ABI_H
#define SOUXMAR_ABI_H

#include <stdint.h>
#include <stddef.h>

#define SOUXMAR_ABI_VERSION_MAJOR 1
#define SOUXMAR_ABI_VERSION_MINOR 0

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

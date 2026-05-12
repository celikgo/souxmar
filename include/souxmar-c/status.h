/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar status / error reporting across the C ABI.
 *
 * Numeric values are STABLE — they appear in plugin source, in caches, and
 * in audit logs. Add new codes by APPENDING; never renumber an existing one.
 */

#ifndef SOUXMAR_STATUS_H
#define SOUXMAR_STATUS_H

#include "souxmar-c/abi.h"

SOUXMAR_C_BEGIN

typedef int32_t souxmar_status_code_t;

#define SOUXMAR_OK 0                 /* success                            */
#define SOUXMAR_E_INVALID_ARGUMENT 1 /* caller-supplied argument invalid   */
#define SOUXMAR_E_OUT_OF_MEMORY 2
#define SOUXMAR_E_NOT_FOUND 3 /* lookup failed                      */
#define SOUXMAR_E_INTERNAL 4  /* generic implementation failure     */
#define SOUXMAR_E_NOT_IMPLEMENTED 5
#define SOUXMAR_E_PLUGIN_FAULT 6 /* host caught a plugin signal/SEH    */
#define SOUXMAR_E_ABI_MISMATCH 7 /* plugin targets unsupported ABI     */
#define SOUXMAR_E_EMPTY_INPUT 8
#define SOUXMAR_E_PLUGIN_REJECTED 9 /* plugin's own pre-condition failed  */
#define SOUXMAR_E_IO 10
#define SOUXMAR_E_TIMEOUT 11
#define SOUXMAR_E_CANCELLED 12
/* v1.7 — sketch solver result codes (ADR-0040). */
#define SOUXMAR_E_UNDERCONSTRAINED 13 /* sketch has remaining DoF        */
#define SOUXMAR_E_OVERCONSTRAINED 14  /* constraints are redundant        */
#define SOUXMAR_E_NO_CONVERGENCE 15   /* solver gave up (time-limit etc.) */
/* v1.8 — design.yaml replay failure (ADR-0041). */
#define SOUXMAR_E_DANGLING_REFERENCE 16 /* feature input no longer resolves */

/* Status carries the code plus optional human-readable message + detail.
 * Strings are owned by the producer; consumer must treat them as const
 * and may not free or modify them. They remain valid until the next call
 * into the same plugin / host function. */
typedef struct souxmar_status {
  souxmar_status_code_t code;
  const char* message; /* UTF-8, may be NULL                  */
  const char* detail;  /* optional secondary info, may be NULL */
} souxmar_status_t;

/* Convenience constructor — guaranteed-OK status. */
static inline souxmar_status_t souxmar_status_ok(void) {
  souxmar_status_t s;
  s.code = SOUXMAR_OK;
  s.message = (const char*)0;
  s.detail = (const char*)0;
  return s;
}

/* Convenience constructor for an error with a (caller-owned) message.
 * The string lifetime is the caller's responsibility per the ownership
 * rules above. */
static inline souxmar_status_t souxmar_status_error(souxmar_status_code_t code,
                                                    const char* message) {
  souxmar_status_t s;
  s.code = code;
  s.message = message;
  s.detail = (const char*)0;
  return s;
}

SOUXMAR_C_END

#endif /* SOUXMAR_STATUS_H */

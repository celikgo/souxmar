/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar-c-bridge — minimal C ABI exported by libsouxmar-c-bridge.so
 * for the Rust `souxmar-bridge` crate to call. Sprint 13 push 3 lands
 * the first real FFI surface: pipeline introspection.
 *
 * Why a separate library, not just exposing the plugin C ABI?
 *
 *   - The plugin C ABI (souxmar-c/) is for plugins authored *against*
 *     souxmar — its surface is centred on what a plugin needs to
 *     register meshers / writers / etc. against a host.
 *
 *   - The bridge C ABI is for the desktop's Rust side calling *into*
 *     the engine — its surface is centred on what the workbench
 *     panels need (parse a pipeline, introspect a project, route an
 *     agent call). The two have nearly disjoint readerships.
 *
 *   - Two separate libraries means a plugin doesn't accidentally
 *     link the bridge and a desktop build doesn't accidentally
 *     bring the plugin-host ABI's transitive deps.
 *
 * Stability: every function declared here is on the **bridge ABI
 * contract**. The contract is Tier-2 (see ADR-0016) — adding a new
 * function is non-breaking; changing or removing a signature
 * requires the BridgeFeatureSet protocol-version bump *and* a
 * deprecation window. The version number is exposed via
 * souxmar_bridge_abi_version().
 *
 * Memory ownership: handles created by this library are freed by
 * the matching `*_free` call. Strings returned via out-pointers
 * are valid until the next call on the same handle, OR until
 * the handle is freed — whichever comes first. The Rust side's
 * cbindgen-derived wrappers (in souxmar-bridge/src/ffi.rs) own
 * copies eagerly to dodge the lifetime question.
 */

#ifndef SOUXMAR_C_BRIDGE_PIPELINE_H
#define SOUXMAR_C_BRIDGE_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the bridge ABI version. Bumps on every breaking change
 * to the C signatures declared in this file. The Rust side
 * cross-checks at startup and refuses to call into a mismatched
 * library. */
uint32_t souxmar_bridge_abi_version(void);

/* ---- Pipeline introspection ----------------------------------- */

/* Opaque handle to a parsed pipeline. Created by
 * souxmar_bridge_pipeline_parse(); freed by
 * souxmar_bridge_pipeline_free(). */
typedef struct souxmar_bridge_pipeline_t souxmar_bridge_pipeline_t;

/* Parse a souxmar pipeline YAML document. Returns NULL if parsing
 * fails; an error message is written to `*out_err` (caller frees
 * via souxmar_bridge_free_string()). On success, `*out_err` is set
 * to NULL.
 *
 * The returned handle is owned by the caller and must be freed
 * via souxmar_bridge_pipeline_free(). */
souxmar_bridge_pipeline_t* souxmar_bridge_pipeline_parse(const char* yaml, char** out_err);

/* Number of stages in the parsed pipeline. */
uint32_t souxmar_bridge_pipeline_stage_count(const souxmar_bridge_pipeline_t* p);

/* Retrieve the i-th stage's id + plugin id. The strings are
 * borrowed from the pipeline handle and remain valid until the
 * handle is freed.
 *
 * Returns 0 on success, -1 if `i` is out of range or `p` is NULL.
 * On failure, `*out_id` and `*out_plugin` are left untouched. */
int32_t souxmar_bridge_pipeline_stage_at(const souxmar_bridge_pipeline_t* p,
                                         uint32_t i,
                                         const char** out_id,
                                         const char** out_plugin);

/* Release a pipeline handle. Safe to call with NULL. */
void souxmar_bridge_pipeline_free(souxmar_bridge_pipeline_t* p);

/* Release a string previously returned through `out_err`. Safe to
 * call with NULL. */
void souxmar_bridge_free_string(char* s);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOUXMAR_C_BRIDGE_PIPELINE_H */

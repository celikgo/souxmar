/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar capability registration.
 *
 * The plugin populates the host's capability registry by calling one
 * souxmar_registry_add_<kind>() per capability it provides. Each registration
 * function takes a vtable defined in the corresponding capability header
 * (mesher.h, solver.h, ...).
 *
 * At Sprint 1 only the mesher namespace exists; subsequent sprints add
 * solver, element, reader, writer, and postproc.
 */

#ifndef SOUXMAR_REGISTRY_H
#define SOUXMAR_REGISTRY_H

#include "souxmar-c/abi.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

/* Forward-declared so registration functions can take a vtable pointer
 * without pulling in the capability header. */
struct souxmar_mesher_vtable;
struct souxmar_solver_vtable;
struct souxmar_writer_vtable;
struct souxmar_postproc_vtable;
struct souxmar_reader_vtable;   /* Sprint 6 push 4 — ABI minor v1.1. */

/* Register a mesher capability, identified by `capability_id`
 * (e.g. "mesher.tetra.example"). The vtable's ABI version is checked
 * by the host before any function pointer is invoked.
 *
 * The vtable pointer must remain valid for the lifetime of the plugin
 * (i.e. point to static storage or to memory the plugin owns until
 * its destroy_fn is called).
 *
 * `user_data` is opaque to the host and passed back to vtable functions. */
souxmar_status_t souxmar_registry_add_mesher(
    souxmar_registry_t*                  registry,
    const char*                          capability_id,
    const struct souxmar_mesher_vtable*  vtable,
    void*                                user_data);

souxmar_status_t souxmar_registry_add_solver(
    souxmar_registry_t*                  registry,
    const char*                          capability_id,
    const struct souxmar_solver_vtable*  vtable,
    void*                                user_data);

souxmar_status_t souxmar_registry_add_writer(
    souxmar_registry_t*                  registry,
    const char*                          capability_id,
    const struct souxmar_writer_vtable*  vtable,
    void*                                user_data);

souxmar_status_t souxmar_registry_add_postproc(
    souxmar_registry_t*                    registry,
    const char*                            capability_id,
    const struct souxmar_postproc_vtable*  vtable,
    void*                                  user_data);

/* Sprint 6 push 4 — readers. The plugin's read_fn produces a Mesh
 * or a Geometry per the format it consumes. Registration is the same
 * shape as the other namespaces; the dispatcher routes the produced
 * handle to the matching downstream slot. */
souxmar_status_t souxmar_registry_add_reader(
    souxmar_registry_t*                  registry,
    const char*                          capability_id,
    const struct souxmar_reader_vtable*  vtable,
    void*                                user_data);

SOUXMAR_C_END

#endif /* SOUXMAR_REGISTRY_H */

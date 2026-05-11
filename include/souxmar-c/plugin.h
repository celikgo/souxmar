/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar plugin entry point.
 *
 * Every plugin exports exactly one symbol:
 *
 *   SOUXMAR_PLUGIN_EXPORT
 *   int souxmar_plugin_register_v1(souxmar_registry_t* registry,
 *                                  const souxmar_host_info_t* host);
 *
 * Return 0 on success, non-zero on fatal init failure.
 */

#ifndef SOUXMAR_PLUGIN_H
#define SOUXMAR_PLUGIN_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

/* Opaque registry handle, owned by the host. The plugin only ever passes it
 * back into souxmar_registry_add_*() functions defined in registry.h. */
typedef struct souxmar_registry souxmar_registry_t;

/* Information the host shares with the plugin at registration time.
 * The plugin uses host->abi_version_minor to detect host capabilities and
 * gracefully downgrade against older hosts (additive minor bumps). */
typedef struct souxmar_host_info {
  int32_t      abi_version_major;   /* SOUXMAR_ABI_VERSION_MAJOR of the host */
  int32_t      abi_version_minor;   /* SOUXMAR_ABI_VERSION_MINOR of the host */
  const char*  host_version_string; /* e.g. "1.2.3"                          */
  uint64_t     host_capabilities;   /* bitmask reserved for ABI v1 minors    */
} souxmar_host_info_t;

/* Plugin entry-point function-pointer type. The host's loader resolves
 * "souxmar_plugin_register_v1" via dlsym/GetProcAddress and casts to this. */
typedef int (*souxmar_plugin_register_v1_fn)(souxmar_registry_t*         registry,
                                             const souxmar_host_info_t*  host);

SOUXMAR_C_END

#endif /* SOUXMAR_PLUGIN_H */

// SPDX-License-Identifier: Apache-2.0
//
// hello-mesher — the canonical reference plugin. The smallest possible
// thing that exercises the full plugin SDK contract: a single exported
// `souxmar_plugin_register_v1`, a vtable, a registration call.
//
// This plugin's `mesh_fn` is a placeholder that returns
// SOUXMAR_E_NOT_IMPLEMENTED — a real mesh implementation needs the host-side
// Mesh accessor C ABI which lands in Sprint 3. The point of this plugin is
// to exercise *registration*, the load handshake, and crash isolation.

#include "souxmar-c/abi.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"

namespace {

souxmar_status_t hello_mesh(const souxmar_geometry_t*       /*geometry*/,
                            const souxmar_mesher_options_t* /*options*/,
                            souxmar_mesh_t**                /*out_mesh*/,
                            void*                           /*user_data*/) {
  return souxmar_status_error(
      SOUXMAR_E_NOT_IMPLEMENTED,
      "hello-mesher is a registration-only reference plugin; "
      "no mesh is produced. See examples/plugins/hello-mesher/README.md.");
}

constexpr souxmar_mesher_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,  // abi_version
    &hello_mesh,                // mesh_fn
    nullptr,                    // destroy_fn
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (host == nullptr || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;  // ABI mismatch — refuse to register against an older host
  }

  const souxmar_status_t s =
      souxmar_registry_add_mesher(registry,
                                  "mesher.tetra.hello",
                                  &kVtable,
                                  /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}

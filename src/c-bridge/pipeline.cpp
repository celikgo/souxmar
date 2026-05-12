// SPDX-License-Identifier: Apache-2.0
//
// libsouxmar-c-bridge — C ABI implementation. Sprint 13 push 3.
//
// Bridges libsouxmar-pipeline's C++ API behind the C signatures
// declared in include/souxmar-c-bridge/pipeline.h. The opaque
// handle wraps a `souxmar::pipeline::Pipeline` and an `std::string`
// arena holding stage id / plugin strings the C side returns by
// pointer.
//
// Strings returned through `out_id` / `out_plugin` borrow from the
// arena and remain valid until the handle is freed. The Rust side
// copies eagerly (`CStr::to_owned`) so it doesn't have to track
// our lifetime.

#include "souxmar-c-bridge/pipeline.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <variant>
#include <vector>

#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/pipeline.h"

namespace {

// One stage entry as the C side sees it. The strings here are
// stable for the lifetime of the parent souxmar_bridge_pipeline_t
// because we never resize the vector after construction.
struct StageEntry {
  std::string id;
  std::string plugin;
};

}  // namespace

// Opaque to the C side; the layout is private to this translation
// unit.
struct souxmar_bridge_pipeline_t {
  std::vector<StageEntry> stages;
};

extern "C" uint32_t souxmar_bridge_abi_version(void) {
  // Bumps on every signature-breaking change *or surface-growth*
  // change in the bridge headers. Tied to the
  // BridgeFeatureSet.bridge_protocol_version on the Rust side —
  // they advance together to keep a single name for "this build's
  // bridge surface."
  //
  // v1 (Sprint 13 push 3): pipeline introspection surface.
  // v2 (Sprint 14 push 4): + provider_call surface.
  // v3 (Sprint 15 push 4): + auto_updater_menu surface.
  return 3;
}

extern "C"
souxmar_bridge_pipeline_t*
souxmar_bridge_pipeline_parse(const char* yaml, char** out_err) {
  if (out_err) *out_err = nullptr;
  if (yaml == nullptr) {
    if (out_err) {
      const char* msg = "yaml pointer is NULL";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err) std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }

  // Parse via the existing pipeline parser. The parser returns a
  // variant<Pipeline, ParseError>; we surface ParseError messages
  // through out_err.
  auto result = souxmar::pipeline::parse_pipeline(std::string(yaml));
  if (auto* err = std::get_if<souxmar::pipeline::ParseError>(&result)) {
    if (out_err) {
      const auto& m = err->message;
      *out_err = static_cast<char*>(std::malloc(m.size() + 1));
      if (*out_err) std::memcpy(*out_err, m.c_str(), m.size() + 1);
    }
    return nullptr;
  }

  const auto& pipe = std::get<souxmar::pipeline::Pipeline>(result);

  auto* out = new (std::nothrow) souxmar_bridge_pipeline_t;
  if (out == nullptr) {
    if (out_err) {
      const char* msg = "bridge: out of memory";
      *out_err = static_cast<char*>(std::malloc(std::strlen(msg) + 1));
      if (*out_err) std::memcpy(*out_err, msg, std::strlen(msg) + 1);
    }
    return nullptr;
  }
  out->stages.reserve(pipe.stages.size());
  for (const auto& s : pipe.stages) {
    out->stages.push_back({s.id, s.plugin});
  }
  return out;
}

extern "C"
uint32_t
souxmar_bridge_pipeline_stage_count(const souxmar_bridge_pipeline_t* p) {
  if (p == nullptr) return 0;
  return static_cast<uint32_t>(p->stages.size());
}

extern "C"
int32_t
souxmar_bridge_pipeline_stage_at(const souxmar_bridge_pipeline_t* p,
                                 uint32_t                          i,
                                 const char**                      out_id,
                                 const char**                      out_plugin) {
  if (p == nullptr) return -1;
  if (i >= p->stages.size()) return -1;
  const auto& s = p->stages[i];
  if (out_id)     *out_id     = s.id.c_str();
  if (out_plugin) *out_plugin = s.plugin.c_str();
  return 0;
}

extern "C" void souxmar_bridge_pipeline_free(souxmar_bridge_pipeline_t* p) {
  delete p;
}

extern "C" void souxmar_bridge_free_string(char* s) {
  std::free(s);
}

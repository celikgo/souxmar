// SPDX-License-Identifier: Apache-2.0
//
// Embedded trust store — populated from the generated
// include/souxmar/update/embedded_trust.h.

#include "souxmar/update/embedded_trust.h"

#include <string>
#include <string_view>

namespace souxmar::update {

namespace {

// The dev-only key id is a load-bearing constant — release CI greps
// for it to refuse-to-publish on builds that didn't override the
// embedded key. Keep it spelled out in one place.
inline constexpr std::string_view kDevKeyId = "souxmar-dev-key";

}  // namespace

bool build_uses_dev_key() noexcept {
  return std::string_view{kEmbeddedReleaseKeyId} == kDevKeyId;
}

TrustStore embedded_trust_store() {
  TrustStore ts;
  // Embedded release key. If the build was generated against the dev
  // key (local devs / CI smoke), that key lives in this slot —
  // build_uses_dev_key() lets release pipelines reject those builds
  // before they ever produce artefacts.
  (void)ts.add_hex(std::string(kEmbeddedReleaseKeyId),
                   std::string_view{kEmbeddedReleasePubkeyHex});
  return ts;
}

}  // namespace souxmar::update

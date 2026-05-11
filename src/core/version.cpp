// SPDX-License-Identifier: Apache-2.0

#include "souxmar/version.h"

#include "souxmar/build_config.h"

namespace souxmar {

Version version() noexcept {
  return Version{SOUXMAR_VERSION_MAJOR, SOUXMAR_VERSION_MINOR, SOUXMAR_VERSION_PATCH};
}

std::string_view version_string() noexcept {
  return SOUXMAR_VERSION_STRING;
}

std::uint32_t abi_version() noexcept {
  return SOUXMAR_ABI_VERSION;
}

}  // namespace souxmar

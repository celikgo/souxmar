// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/topology.h"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace souxmar::core {

class Topology::Impl {
 public:
  // One counter per kind. Values themselves do not matter — only the count.
  std::vector<std::int32_t> per_kind[4];

  struct Key {
    TopologyKind  kind;
    std::uint32_t index;
    bool operator==(const Key&) const noexcept = default;
  };
  struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
      return std::hash<std::uint64_t>{}(
          (static_cast<std::uint64_t>(k.kind) << 32) | k.index);
    }
  };

  std::unordered_map<Key, EntityTag,   KeyHash> tags;
  std::unordered_map<Key, std::string, KeyHash> names;
};

Topology::Topology() : impl_(std::make_unique<Impl>()) {}
Topology::~Topology() = default;
Topology::Topology(Topology&&) noexcept = default;
Topology& Topology::operator=(Topology&&) noexcept = default;

std::size_t Topology::count(TopologyKind kind) const noexcept {
  return impl_->per_kind[static_cast<std::size_t>(kind)].size();
}

EntityTag Topology::tag(TopologyRef ref) const noexcept {
  if (auto it = impl_->tags.find(Impl::Key{ref.kind, ref.index}); it != impl_->tags.end()) {
    return it->second;
  }
  return EntityTag{};
}

std::optional<std::string_view> Topology::name(TopologyRef ref) const {
  if (auto it = impl_->names.find(Impl::Key{ref.kind, ref.index}); it != impl_->names.end()) {
    return std::string_view{it->second};
  }
  return std::nullopt;
}

std::uint32_t Topology::add_entity(TopologyKind kind) {
  auto& bucket = impl_->per_kind[static_cast<std::size_t>(kind)];
  const auto idx = static_cast<std::uint32_t>(bucket.size());
  bucket.push_back(0);
  return idx;
}

void Topology::set_tag(TopologyRef ref, EntityTag tag) {
  if (ref.index >= count(ref.kind)) {
    throw std::out_of_range("Topology::set_tag: ref out of range for kind");
  }
  impl_->tags[Impl::Key{ref.kind, ref.index}] = tag;
}

void Topology::set_name(TopologyRef ref, std::string name) {
  if (ref.index >= count(ref.kind)) {
    throw std::out_of_range("Topology::set_name: ref out of range for kind");
  }
  impl_->names[Impl::Key{ref.kind, ref.index}] = std::move(name);
}

bool Topology::empty() const noexcept {
  for (const auto& bucket : impl_->per_kind) {
    if (!bucket.empty()) return false;
  }
  return true;
}

}  // namespace souxmar::core

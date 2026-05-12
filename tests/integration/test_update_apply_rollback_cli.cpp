// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 7 — end-to-end integration test for the full
// `souxmar update apply` -> `souxmar update rollback` cycle.
//
// What this proves:
//   1. `souxmar update apply` with a valid manifest + signature +
//      matching artifact writes versions/<v>/payload, flips
//      current.txt, and appends an apply event to rollback.log.
//   2. The per-user state file gains current_installed_version +
//      max_version_ever_seen + last_apply_at.
//   3. `souxmar update rollback` against the same install layout
//      reverts current.txt to the prior version and appends a
//      rollback event.
//   4. The replay-defence floor (max_version_ever_seen) does *not*
//      drop on rollback — a subsequent `apply` of an older manifest
//      would be refused by the gate.

#include "souxmar/update/install_layout.h"
#include "souxmar/update/rollback_log.h"
#include "souxmar/update/update_state.h"

#include "test_config.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <sodium.h>

namespace fs = std::filesystem;

namespace {

std::string shell_quote(const fs::path& p) {
#if defined(_WIN32)
  return "\"" + p.string() + "\"";
#else
  return "'" + p.string() + "'";
#endif
}

int run_cli(const std::string& full_cmd) {
  std::fflush(nullptr);
  const int rc = std::system(full_cmd.c_str());
#if defined(_WIN32)
  return rc;
#else
  if (rc < 0)
    return rc;
  return (rc >> 8) & 0xFF;
#endif
}

fs::path tmp_dir(std::string_view tag) {
  std::random_device rd;
  auto base = fs::temp_directory_path()
              / ("souxmar-update-apply-test-" + std::string(tag) + "-" + std::to_string(rd()));
  fs::create_directories(base);
  return base;
}

struct Keypair {
  std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES> pk{};
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> sk{};
};

Keypair make_keypair() {
  if (sodium_init() < 0)
    ADD_FAILURE() << "sodium_init failed";
  std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
  for (std::size_t i = 0; i < seed.size(); ++i) {
    seed[i] = static_cast<std::uint8_t>(i * 11 + 3);
  }
  Keypair k;
  crypto_sign_seed_keypair(k.pk.data(), k.sk.data(), seed.data());
  return k;
}

std::string hex_encode(const std::uint8_t* p, std::size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(n * 2);
  for (std::size_t i = 0; i < n; ++i) {
    out[2 * i + 0] = kHex[(p[i] >> 4) & 0x0F];
    out[2 * i + 1] = kHex[p[i] & 0x0F];
  }
  return out;
}

std::string sign_hex(const std::string& message,
                     const std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES>& sk) {
  std::array<std::uint8_t, crypto_sign_BYTES> sig{};
  unsigned long long sig_len = 0;
  crypto_sign_detached(sig.data(),
                       &sig_len,
                       reinterpret_cast<const std::uint8_t*>(message.data()),
                       message.size(),
                       sk.data());
  return hex_encode(sig.data(), crypto_sign_BYTES);
}

std::string sha256_hex_of(const std::string& bytes) {
  std::array<std::uint8_t, crypto_hash_sha256_BYTES> d{};
  crypto_hash_sha256(d.data(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
  return hex_encode(d.data(), d.size());
}

void write_file(const fs::path& p, std::string_view content) {
  std::ofstream sink(p, std::ios::binary | std::ios::trunc);
  sink.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_file(const fs::path& p) {
  std::ifstream src(p, std::ios::binary);
  std::ostringstream buf;
  buf << src.rdbuf();
  return buf.str();
}

const char* kManifestTemplate = R"toml(
schema       = 1
generated_at = "2026-05-11T14:00:00Z"

[channel]
name       = "stable"
expires_at = "2026-12-31T00:00:00Z"

[release]
version              = "{VERSION}"
released_at          = "2026-05-10T10:00:00Z"
min_previous_version = "0.0.0"
rollback_target      = "0.0.0"
notes_url            = "https://souxmar.dev/releases/{VERSION}"
mandatory            = false

[[artifact]]
os     = "linux"
arch   = "x86_64"
url    = "https://dl.souxmar.dev/{VERSION}/linux-x86_64.tar.zst"
sha256 = "{SHA256}"
size   = {SIZE}

[signing]
algorithm     = "ed25519"
public_key_id = "release-test"
)toml";

std::string replace_all(std::string s, std::string_view needle, std::string_view repl) {
  std::size_t pos = 0;
  while ((pos = s.find(needle, pos)) != std::string::npos) {
    s.replace(pos, needle.size(), repl);
    pos += repl.size();
  }
  return s;
}

class UpdateApplyRollbackCli : public ::testing::Test {
 protected:
  void SetUp() override {
    work_ = tmp_dir("workdir");
    target_root_ = tmp_dir("target-root");
    state_path_ = work_ / "update-state.toml";
    kp_ = make_keypair();
    pubkey_hex_ = hex_encode(kp_.pk.data(), kp_.pk.size());
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(work_, ec);
    fs::remove_all(target_root_, ec);
  }

  struct Bundle {
    fs::path manifest;
    fs::path signature;
    fs::path artifact;
  };

  Bundle write_signed(const std::string& version, const std::string& payload) {
    const fs::path artifact = work_ / ("artifact-" + version + ".bin");
    write_file(artifact, payload);

    std::string mt = replace_all(kManifestTemplate, "{VERSION}", version);
    mt = replace_all(std::move(mt), "{SHA256}", sha256_hex_of(payload));
    mt = replace_all(std::move(mt), "{SIZE}", std::to_string(payload.size()));
    const fs::path manifest = work_ / ("manifest-" + version + ".toml");
    write_file(manifest, mt);

    const fs::path signature = work_ / ("manifest-" + version + ".sig");
    write_file(signature, sign_hex(mt, kp_.sk) + "\n");
    return {manifest, signature, artifact};
  }

  std::string apply_cmd(const Bundle& b,
                        const std::string& current_version,
                        const fs::path& log_out) const {
    return shell_quote(SOUXMAR_TEST_CLI_BINARY) + " update apply"
           + " --trusted-key release-test=" + pubkey_hex_ + " --platform linux/x86_64" + " --state "
           + shell_quote(state_path_) + " --target-root " + shell_quote(target_root_)
           + " --manifest " + shell_quote(b.manifest) + " --signature " + shell_quote(b.signature)
           + " --artifact " + shell_quote(b.artifact) + " --current-version " + current_version
           + " --as-of 2026-05-12T00:00:00Z" + " > " + shell_quote(log_out) + " 2>&1";
  }

  std::string rollback_cmd(const fs::path& log_out) const {
    return shell_quote(SOUXMAR_TEST_CLI_BINARY) + " update rollback" + " --target-root "
           + shell_quote(target_root_) + " --state " + shell_quote(state_path_)
           + " --as-of 2026-05-12T01:00:00Z" + " > " + shell_quote(log_out) + " 2>&1";
  }

  fs::path work_;
  fs::path target_root_;
  fs::path state_path_;
  Keypair kp_{};
  std::string pubkey_hex_;
};

}  // namespace

TEST_F(UpdateApplyRollbackCli, ApplyThenRollbackFullCycle) {
  using namespace souxmar::update;

  // 1. Apply 0.8.5 onto a fresh target root.
  const auto b1 = write_signed("0.8.5", "payload-of-0.8.5-bytes");
  ASSERT_EQ(run_cli(apply_cmd(b1, "0.0.0", work_ / "apply-1.log")), 0)
      << read_file(work_ / "apply-1.log");

  // 2. Apply 0.9.0.
  const auto b2 = write_signed("0.9.0", "payload-of-0.9.0-bytes");
  ASSERT_EQ(run_cli(apply_cmd(b2, "0.8.5", work_ / "apply-2.log")), 0)
      << read_file(work_ / "apply-2.log");

  // 3. Assert install-layout state via the real APIs (not by re-
  //    parsing CLI stdout — the wire-format would lock the test to
  //    text shape, the contracts are what we want to verify).
  InstallLayout layout(target_root_);
  EXPECT_EQ(layout.read_current_version(), "0.9.0");
  EXPECT_EQ(layout.read_previous_version(), "0.8.5");
  EXPECT_TRUE(layout.has_version_payload("0.9.0"));
  EXPECT_TRUE(layout.has_version_payload("0.8.5"));

  // 4. Assert state file bumped.
  auto loaded = load_update_state(state_path_);
  ASSERT_TRUE(std::holds_alternative<UpdateState>(loaded));
  {
    const auto& s = std::get<UpdateState>(loaded);
    EXPECT_EQ(s.current_installed_version, "0.9.0");
    EXPECT_EQ(s.max_version_ever_seen, "0.9.0");
    EXPECT_FALSE(s.last_apply_at.empty());
  }

  // 5. Assert rollback log has two apply events.
  {
    auto log = load_rollback_log(layout.rollback_log_path());
    ASSERT_TRUE(std::holds_alternative<std::vector<RollbackEvent>>(log));
    const auto& evs = std::get<std::vector<RollbackEvent>>(log);
    ASSERT_EQ(evs.size(), 2u);
    EXPECT_EQ(evs[0].type, RollbackEventType::Apply);
    EXPECT_EQ(evs[0].to_version, "0.8.5");
    EXPECT_EQ(evs[1].type, RollbackEventType::Apply);
    EXPECT_EQ(evs[1].to_version, "0.9.0");
  }

  // 6. Rollback.
  ASSERT_EQ(run_cli(rollback_cmd(work_ / "rollback.log")), 0) << read_file(work_ / "rollback.log");

  EXPECT_EQ(layout.read_current_version(), "0.8.5");
  loaded = load_update_state(state_path_);
  ASSERT_TRUE(std::holds_alternative<UpdateState>(loaded));
  {
    const auto& s = std::get<UpdateState>(loaded);
    EXPECT_EQ(s.current_installed_version, "0.8.5");
    // Replay-defence floor must *not* drop.
    EXPECT_EQ(s.max_version_ever_seen, "0.9.0")
        << "rollback must not lower the replay-defence floor";
  }
  auto log = load_rollback_log(layout.rollback_log_path());
  ASSERT_TRUE(std::holds_alternative<std::vector<RollbackEvent>>(log));
  const auto& evs = std::get<std::vector<RollbackEvent>>(log);
  ASSERT_EQ(evs.size(), 3u);
  EXPECT_EQ(evs[2].type, RollbackEventType::Rollback);
  EXPECT_EQ(evs[2].from_version, "0.9.0");
  EXPECT_EQ(evs[2].to_version, "0.8.5");
}

TEST_F(UpdateApplyRollbackCli, ApplyRejectsArtifactSha256Mismatch) {
  // Sign a manifest for one payload but pass a different artifact on
  // --artifact. The pre-flight signature check passes (the manifest
  // is genuine); apply_update's sha256 check refuses.
  const auto b = write_signed("0.9.0", "expected payload");
  // Overwrite the artifact file with different bytes.
  write_file(b.artifact, "tampered payload");

  const int rc = run_cli(apply_cmd(b, "0.8.5", work_ / "out.log"));
  const auto out = read_file(work_ / "out.log");
  EXPECT_NE(rc, 0) << out;
  EXPECT_NE(out.find("artifact-hash-mismatch"), std::string::npos) << out;
  // Install layout untouched.
  EXPECT_TRUE(souxmar::update::InstallLayout(target_root_).read_current_version().empty());
}

TEST_F(UpdateApplyRollbackCli, RollbackOnFreshRootFails) {
  const int rc = run_cli(rollback_cmd(work_ / "out.log"));
  const auto out = read_file(work_ / "out.log");
  EXPECT_EQ(rc, 75 /* kExitUpdateRefused */) << out;
  EXPECT_NE(out.find("no-current-install"), std::string::npos) << out;
}

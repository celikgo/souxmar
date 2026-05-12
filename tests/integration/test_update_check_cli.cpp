// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 6 — integration test for `souxmar update check` and
// `souxmar update apply --dry-run`.
//
// What this proves end-to-end:
//   1. The CLI parses --manifest / --signature / --trusted-key /
//      --as-of / --platform / --current-version flags without
//      reordering pain.
//   2. A correctly-signed, in-window manifest produces an "apply"
//      decision with exit code 0 and prints the artifact URL +
//      sha256 + size on stdout (or a JSON object under --json).
//   3. A tampered manifest body fails verification with exit code 76
//      (kExitSignatureBad).
//   4. An expired-as-of-`--as-of` manifest is refused with exit code 75
//      (kExitUpdateRefused) and the canonical "expired" reason string.
//   5. `apply` without --dry-run prints the "lands in push 7" message
//      and exits non-zero — locks in the deferral so a future "I'll
//      just wire the download here" change has to update this test
//      first.
//
// Signature material is generated on the fly with libsodium so the
// test doesn't ship a fixture .toml + .sig pair (rotating those would
// be a maintenance trap). The test seeds the keypair deterministically
// so a flake never falls out of CSPRNG variation.

#include "souxmar/update/verifier.h"

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
  // std::system's return convention varies. On POSIX, WEXITSTATUS()
  // would normalise but pulling sys/wait.h into a test file is heavy.
  // The exit codes we assert against are small positive ints; the
  // platform shells we run on (bash, zsh, Windows cmd) propagate them
  // verbatim through the high byte. For POSIX we shift; for Windows
  // we take the value as-is.
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
              / ("souxmar-update-cli-test-" + std::string(tag) + "-" + std::to_string(rd()));
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
  // Deterministic seed — see test_update_verifier.cpp.
  std::array<std::uint8_t, crypto_sign_SEEDBYTES> seed{};
  for (std::size_t i = 0; i < seed.size(); ++i) {
    seed[i] = static_cast<std::uint8_t>(i * 7 + 1);
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

const char* kManifestTemplate = R"toml(
schema       = 1
generated_at = "2026-05-11T14:00:00Z"

[channel]
name       = "stable"
expires_at = "{EXPIRES}"

[release]
version              = "{VERSION}"
released_at          = "2026-05-10T10:00:00Z"
min_previous_version = "0.8.0"
rollback_target      = "0.8.4"
notes_url            = "https://souxmar.dev/releases/{VERSION}"
mandatory            = false

[[artifact]]
os     = "linux"
arch   = "x86_64"
url    = "https://dl.souxmar.dev/{VERSION}/linux-x86_64.tar.zst"
sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 48217600

[[artifact]]
os     = "macos"
arch   = "aarch64"
url    = "https://dl.souxmar.dev/{VERSION}/macos-aarch64.tar.zst"
sha256 = "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
size   = 47900800

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

class UpdateCheckCli : public ::testing::Test {
 protected:
  void SetUp() override {
    work_ = tmp_dir("workdir");
    kp_ = make_keypair();
    pubkey_hex_ = hex_encode(kp_.pk.data(), kp_.pk.size());
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(work_, ec);
  }

  // Write a manifest with the given version + expiry; sign it; return
  // the paths to the manifest + signature.
  struct Pair {
    fs::path manifest;
    fs::path signature;
  };

  Pair write_signed(const std::string& version, const std::string& expires_at) {
    auto manifest_text = replace_all(kManifestTemplate, "{VERSION}", version);
    manifest_text = replace_all(std::move(manifest_text), "{EXPIRES}", expires_at);
    const auto manifest = work_ / ("manifest-" + version + ".toml");
    write_file(manifest, manifest_text);

    const auto sig_hex = sign_hex(manifest_text, kp_.sk);
    const auto signature = work_ / ("manifest-" + version + ".sig");
    write_file(signature, sig_hex + "\n");
    return {manifest, signature};
  }

  std::string cli_base() const {
    return shell_quote(SOUXMAR_TEST_CLI_BINARY) + " update check"
           + " --trusted-key release-test=" + pubkey_hex_ + " --platform linux/x86_64" + " --state "
           + shell_quote(work_ / "no-such-state.toml");
  }

  fs::path work_;
  Keypair kp_{};
  std::string pubkey_hex_;
};

}  // namespace

TEST_F(UpdateCheckCli, ValidManifestProducesApplyDecision) {
  const auto p = write_signed("0.9.0", "2026-12-31T00:00:00Z");
  const auto log = work_ / "out.log";
  const std::string cmd = cli_base() + " --manifest " + shell_quote(p.manifest) + " --signature "
                          + shell_quote(p.signature) + " --current-version 0.8.5"
                          + " --as-of 2026-05-12T00:00:00Z" + " > " + shell_quote(log) + " 2>&1";

  const int rc = run_cli(cmd);
  const auto out = read_file(log);
  ASSERT_EQ(rc, 0) << "CLI output:\n" << out;
  EXPECT_NE(out.find("update available"), std::string::npos) << out;
  EXPECT_NE(out.find("-> 0.9.0"), std::string::npos) << out;
  EXPECT_NE(out.find("linux-x86_64.tar.zst"), std::string::npos) << out;
}

TEST_F(UpdateCheckCli, AlreadyUpToDateExitsZeroWithMessage) {
  const auto p = write_signed("0.9.0", "2026-12-31T00:00:00Z");
  const auto log = work_ / "out.log";
  const std::string cmd = cli_base() + " --manifest " + shell_quote(p.manifest) + " --signature "
                          + shell_quote(p.signature)
                          + " --current-version 1.0.0"  // ahead of offered
                          + " --as-of 2026-05-12T00:00:00Z" + " > " + shell_quote(log) + " 2>&1";

  const int rc = run_cli(cmd);
  const auto out = read_file(log);
  ASSERT_EQ(rc, 0) << "CLI output:\n" << out;
  EXPECT_NE(out.find("already up-to-date"), std::string::npos) << out;
}

TEST_F(UpdateCheckCli, TamperedManifestFailsVerification) {
  const auto p = write_signed("0.9.0", "2026-12-31T00:00:00Z");
  // Tamper after signing — flip one digit in the version string.
  auto manifest_text = read_file(p.manifest);
  auto pos = manifest_text.find("\"0.9.0\"");
  ASSERT_NE(pos, std::string::npos);
  manifest_text[pos + 4] = '1';  // "0.9.0" -> "0.9.1"
  write_file(p.manifest, manifest_text);

  const auto log = work_ / "out.log";
  const std::string cmd = cli_base() + " --manifest " + shell_quote(p.manifest) + " --signature "
                          + shell_quote(p.signature) + " --current-version 0.8.5"
                          + " --as-of 2026-05-12T00:00:00Z" + " > " + shell_quote(log) + " 2>&1";

  const int rc = run_cli(cmd);
  const auto out = read_file(log);
  EXPECT_EQ(rc, 76 /* kExitSignatureBad */) << out;
  EXPECT_NE(out.find("signature verification failed"), std::string::npos) << out;
}

TEST_F(UpdateCheckCli, ExpiredManifestRefused) {
  // expires_at *before* --as-of — the apply gate must reject.
  const auto p = write_signed("0.9.0", "2026-04-01T00:00:00Z");
  const auto log = work_ / "out.log";
  const std::string cmd = cli_base() + " --manifest " + shell_quote(p.manifest) + " --signature "
                          + shell_quote(p.signature) + " --current-version 0.8.5"
                          + " --as-of 2026-05-12T00:00:00Z" + " > " + shell_quote(log) + " 2>&1";

  const int rc = run_cli(cmd);
  const auto out = read_file(log);
  EXPECT_EQ(rc, 75 /* kExitUpdateRefused */) << out;
  EXPECT_NE(out.find("expired"), std::string::npos) << out;
}

TEST_F(UpdateCheckCli, JsonModeEmitsStructuredOutput) {
  const auto p = write_signed("0.9.0", "2026-12-31T00:00:00Z");
  const auto log = work_ / "out.log";
  const std::string cmd = cli_base() + " --manifest " + shell_quote(p.manifest) + " --signature "
                          + shell_quote(p.signature) + " --current-version 0.8.5"
                          + " --as-of 2026-05-12T00:00:00Z" + " --json" + " > " + shell_quote(log)
                          + " 2>&1";

  const int rc = run_cli(cmd);
  const auto out = read_file(log);
  ASSERT_EQ(rc, 0) << out;
  EXPECT_NE(out.find("\"status\":\"apply\""), std::string::npos) << out;
  EXPECT_NE(out.find("\"version\":\"0.9.0\""), std::string::npos) << out;
  EXPECT_NE(out.find("\"os\":\"linux\""), std::string::npos) << out;
  EXPECT_NE(out.find("\"arch\":\"x86_64\""), std::string::npos) << out;
}

TEST_F(UpdateCheckCli, ApplyWithoutDryRunIsNotYetImplemented) {
  // Locks in that `apply` without --dry-run errors with a guidance
  // message until push 7 wires the downloader. A future patch that
  // accidentally enables `apply` would have to update this test
  // first.
  const auto p = write_signed("0.9.0", "2026-12-31T00:00:00Z");
  const auto log = work_ / "out.log";

  std::string base = shell_quote(SOUXMAR_TEST_CLI_BINARY) + " update apply"
                     + " --trusted-key release-test=" + pubkey_hex_ + " --platform linux/x86_64"
                     + " --state " + shell_quote(work_ / "no-such-state.toml") + " --manifest "
                     + shell_quote(p.manifest) + " --signature " + shell_quote(p.signature)
                     + " --current-version 0.8.5" + " --as-of 2026-05-12T00:00:00Z" + " > "
                     + shell_quote(log) + " 2>&1";

  const int rc = run_cli(base);
  const auto out = read_file(log);
  EXPECT_NE(rc, 0) << "expected non-zero exit; output:\n" << out;
  EXPECT_NE(out.find("push 7"), std::string::npos) << "expected guidance message; output:\n" << out;
}

TEST_F(UpdateCheckCli, ApplyWithDryRunMatchesCheck) {
  const auto p = write_signed("0.9.0", "2026-12-31T00:00:00Z");
  const auto log = work_ / "out.log";

  std::string cmd = shell_quote(SOUXMAR_TEST_CLI_BINARY) + " update apply --dry-run"
                    + " --trusted-key release-test=" + pubkey_hex_ + " --platform linux/x86_64"
                    + " --state " + shell_quote(work_ / "no-such-state.toml") + " --manifest "
                    + shell_quote(p.manifest) + " --signature " + shell_quote(p.signature)
                    + " --current-version 0.8.5" + " --as-of 2026-05-12T00:00:00Z" + " > "
                    + shell_quote(log) + " 2>&1";

  const int rc = run_cli(cmd);
  const auto out = read_file(log);
  ASSERT_EQ(rc, 0) << out;
  EXPECT_NE(out.find("update available"), std::string::npos) << out;
}

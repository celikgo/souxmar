// SPDX-License-Identifier: Apache-2.0
//
// CLI smoke test — exercise the souxmar binary against the cantilever-beam
// example end-to-end.
//
// What this proves:
//   1. The CLI binary actually links and runs.
//   2. `souxmar plugin list` discovers the in-tree plugins.
//   3. `souxmar run` parses YAML, loads plugins, dispatches stages, writes
//      a VTU file to disk.
//   4. A second `souxmar run` with the same cache dir hits the disk cache
//      for the writer stage (no re-execution needed).
//
// The CLI is invoked via std::system; this is a smoke test, not a full
// option-coverage suite (those live in the unit tests against the
// dispatcher / cache directly).

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "test_config.h"

namespace fs = std::filesystem;

namespace {

std::string shell_quote(const fs::path& p) {
#if defined(_WIN32)
  return "\"" + p.string() + "\"";
#else
  // Single-quote the POSIX way; embedded single quotes are not expected in
  // build paths but we'd still rather know if they appear.
  return "'" + p.string() + "'";
#endif
}

// Run a CLI command and capture exit code. We let stdout/stderr flow
// through to the test runner — if a test fails, the CLI's diagnostics
// land in the gtest output for debugging.
int run_cli(const std::string& full_cmd) {
  std::fflush(nullptr);
  return std::system(full_cmd.c_str());
}

fs::path tmp_dir(std::string_view tag) {
  std::random_device rd;
  auto base = fs::temp_directory_path() /
              ("souxmar-cli-test-" + std::string(tag) + "-" + std::to_string(rd()));
  fs::create_directories(base);
  return base;
}

class CliSmokeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    workdir_  = tmp_dir("workdir");
    cachedir_ = tmp_dir("cache");
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(workdir_, ec);
    fs::remove_all(cachedir_, ec);
  }

  // discover_plugins walks immediate subdirectories of a search root —
  // pass the shared parent of all in-tree example plugins (e.g. the
  // build/.../examples/plugins/ directory) and let discovery find each.
  fs::path plugins_root() const {
    return fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  }

  fs::path workdir_;
  fs::path cachedir_;
};

TEST_F(CliSmokeTest, PluginListEnumeratesInTreePlugins) {
  // Stream the CLI's stdout to a captured file so we can assert on contents.
  const auto out_log = workdir_ / "plugin-list.txt";
  std::ostringstream cmd;
  cmd << shell_quote(SOUXMAR_TEST_CLI_BINARY) << " plugin list"
      << " --plugin-path " << shell_quote(plugins_root())
      << " > " << shell_quote(out_log) << " 2>&1";

  ASSERT_EQ(run_cli(cmd.str()), 0) << "CLI exited non-zero";

  std::ifstream in(out_log);
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  EXPECT_NE(contents.find("dev.souxmar.examples.hello-mesher"), std::string::npos)
      << "plugin list missed hello-mesher; output was:\n" << contents;
  EXPECT_NE(contents.find("dev.souxmar.examples.vtu-writer"), std::string::npos)
      << "plugin list missed vtu-writer; output was:\n" << contents;
  EXPECT_NE(contents.find("writer.vtu"), std::string::npos)
      << "plugin list missed writer.vtu capability; output was:\n" << contents;
}

TEST_F(CliSmokeTest, RunCantileverExampleProducesVtuOutput) {
  const auto pipeline_src = fs::path(SOUXMAR_TEST_SOURCE_ROOT) /
                            "examples/cantilever-beam/pipeline.yaml";
  ASSERT_TRUE(fs::exists(pipeline_src)) << pipeline_src;

  // Copy to workdir so the relative `path: cantilever.vtu` resolves there.
  const auto pipeline_local = workdir_ / "pipeline.yaml";
  fs::copy_file(pipeline_src, pipeline_local);

  std::ostringstream cmd;
  cmd << "cd " << shell_quote(workdir_) << " && "
      << shell_quote(SOUXMAR_TEST_CLI_BINARY) << " run pipeline.yaml"
      << " --plugin-path " << shell_quote(plugins_root())
      << " --cache-dir "  << shell_quote(cachedir_)
      << " > run1.log 2>&1";
  const int rc1 = run_cli(cmd.str());

  // Surface CLI output if the test fails.
  std::ifstream log1(workdir_ / "run1.log");
  std::string log1_contents((std::istreambuf_iterator<char>(log1)), {});
  ASSERT_EQ(rc1, 0) << "CLI exited non-zero. Output:\n" << log1_contents;

  const auto vtu = workdir_ / "cantilever.vtu";
  ASSERT_TRUE(fs::exists(vtu)) << "VTU file not produced at " << vtu;

  std::ifstream vin(vtu);
  std::string vtu_contents((std::istreambuf_iterator<char>(vin)), {});
  EXPECT_NE(vtu_contents.find("<VTKFile type=\"UnstructuredGrid\""), std::string::npos)
      << "VTU header missing; file:\n" << vtu_contents.substr(0, 256);
  EXPECT_NE(vtu_contents.find("NumberOfPoints=\"4\""), std::string::npos)
      << "expected 4 points (unit tet); got:\n" << vtu_contents.substr(0, 512);
  EXPECT_NE(vtu_contents.find("NumberOfCells=\"1\""), std::string::npos)
      << "expected 1 cell (unit tet); got:\n" << vtu_contents.substr(0, 512);
}

TEST_F(CliSmokeTest, ReRunHitsDiskCacheForWriterStage) {
  const auto pipeline_src = fs::path(SOUXMAR_TEST_SOURCE_ROOT) /
                            "examples/cantilever-beam/pipeline.yaml";
  const auto pipeline_local = workdir_ / "pipeline.yaml";
  fs::copy_file(pipeline_src, pipeline_local);

  const std::string base =
      "cd " + shell_quote(workdir_) + " && " +
      shell_quote(SOUXMAR_TEST_CLI_BINARY) + " run pipeline.yaml" +
      " --plugin-path " + shell_quote(plugins_root()) +
      " --cache-dir " + shell_quote(cachedir_);

  ASSERT_EQ(run_cli(base + " > run1.log 2>&1"), 0);
  ASSERT_EQ(run_cli(base + " > run2.log 2>&1"), 0);

  std::ifstream r2(workdir_ / "run2.log");
  std::string r2_contents((std::istreambuf_iterator<char>(r2)), {});
  // The writer stage should be a CACHED hit on the second run — its output
  // is a Path StageOutput which the disk cache knows how to round-trip.
  EXPECT_NE(r2_contents.find("[CACHED  ] write"), std::string::npos)
      << "writer stage was not cached on rerun; second-run output:\n" << r2_contents;
}

}  // namespace

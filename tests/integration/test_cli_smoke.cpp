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

#include "test_config.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>

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

// `cd` into a directory, portably.
//
// On Windows, `cd C:\path` does not change the *drive* — it sets the current
// directory of C: and leaves the process wherever it was. These tests put
// their workdir under the temp directory, which on a GitHub runner is on C:
// while the checkout is on D:, so the CLI then ran from the wrong drive,
// could not find pipeline.yaml, and exited 64 with its log written somewhere
// nobody looked. `/d` changes drive and directory together.
std::string cd_to(const fs::path& dir) {
#if defined(_WIN32)
  return "cd /d " + shell_quote(dir);
#else
  return "cd " + shell_quote(dir);
#endif
}

// Run a CLI command and capture exit code. We let stdout/stderr flow
// through to the test runner — if a test fails, the CLI's diagnostics
// land in the gtest output for debugging.
int run_cli(const std::string& full_cmd) {
  std::fflush(nullptr);
  // cmd.exe eats the outer quotes. std::system runs `cmd /c <string>`, and
  // when that string starts with a double quote cmd strips the first and
  // last one before parsing — so a command built as
  //     "C:\path\souxmar.exe" plugin list --plugin-path "C:\..." > "C:\..."
  // arrives as
  //     C:\path\souxmar.exe" plugin list --plugin-path "C:\..." > "C:\...
  // and fails with "The filename, directory name, or volume label syntax is
  // incorrect." Wrapping the whole command in one more pair of quotes gives
  // cmd the pair it intends to remove and leaves the real ones intact. This
  // is why every CLI integration test failed on Windows while the same
  // commands worked by hand.
#if defined(_WIN32)
  return std::system(("\"" + full_cmd + "\"").c_str());
#else
  return std::system(full_cmd.c_str());
#endif
}

fs::path tmp_dir(std::string_view tag) {
  std::random_device rd;
  auto base = fs::temp_directory_path()
              / ("souxmar-cli-test-" + std::string(tag) + "-" + std::to_string(rd()));
  fs::create_directories(base);
  return base;
}

class CliSmokeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    workdir_ = tmp_dir("workdir");
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
      << " --plugin-path " << shell_quote(plugins_root()) << " > " << shell_quote(out_log)
      << " 2>&1";

  ASSERT_EQ(run_cli(cmd.str()), 0) << "CLI exited non-zero";

  std::ifstream in(out_log);
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  EXPECT_NE(contents.find("dev.souxmar.examples.hello-mesher"), std::string::npos)
      << "plugin list missed hello-mesher; output was:\n"
      << contents;
  EXPECT_NE(contents.find("dev.souxmar.examples.vtu-writer"), std::string::npos)
      << "plugin list missed vtu-writer; output was:\n"
      << contents;
  EXPECT_NE(contents.find("writer.vtu"), std::string::npos)
      << "plugin list missed writer.vtu capability; output was:\n"
      << contents;
}

TEST_F(CliSmokeTest, RunCantileverExampleProducesVtuOutput) {
  const auto pipeline_src =
      fs::path(SOUXMAR_TEST_SOURCE_ROOT) / "examples/cantilever-beam/pipeline.yaml";
  ASSERT_TRUE(fs::exists(pipeline_src)) << pipeline_src;

  // Copy to workdir so the relative `path: cantilever.vtu` resolves there.
  const auto pipeline_local = workdir_ / "pipeline.yaml";
  fs::copy_file(pipeline_src, pipeline_local);

  std::ostringstream cmd;
  cmd << cd_to(workdir_) << " && " << shell_quote(SOUXMAR_TEST_CLI_BINARY) << " run pipeline.yaml"
      << " --plugin-path " << shell_quote(plugins_root()) << " --cache-dir "
      << shell_quote(cachedir_) << " > run1.log 2>&1";
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
      << "VTU header missing; file:\n"
      << vtu_contents.substr(0, 256);
  EXPECT_NE(vtu_contents.find("NumberOfPoints=\"4\""), std::string::npos)
      << "expected 4 points (unit tet); got:\n"
      << vtu_contents.substr(0, 512);
  EXPECT_NE(vtu_contents.find("NumberOfCells=\"1\""), std::string::npos)
      << "expected 1 cell (unit tet); got:\n"
      << vtu_contents.substr(0, 512);
}

// Every shipped example must run from a working directory that is not its
// own. This is the user story the examples' own headers document — `souxmar
// run examples/<name>/pipeline.yaml` from the repo root — and until reader
// paths became pipeline-relative it was the one invocation that could not
// work: five of the ten resolved `path: cube.stl` against the process cwd and
// exited 70.
//
// Nothing caught that, and the reason is worth recording. Every other reader
// test builds its YAML in a string with an absolute path interpolated in, so
// the relative-path branch was unreachable from any test. The one test above
// that runs a real example copies pipeline.yaml into the workdir first —
// which makes the pipeline directory and the cwd the same directory, and
// collapses the exact distinction the bug lived in.
//
// Asserting over the whole corpus rather than one example is deliberate: it
// also fails for a *newly added* broken example, which a count-based gate
// cannot.
// Two pipelines, the same relative input filename, different bytes behind it,
// one shared cache. Each must get its own answer.
//
// The stage key is built from the capability id, the plugin version, the
// declared input tree and the upstream hashes. A reader's declared input is a
// *path*, and a path is not what a reader reads — so before the key folded in
// a digest of the file's contents, these two runs produced identical hashes
// and the second was served the first one's output, reported as `[CACHED]`.
// Not a crash and not a diff: a plausible, wrong answer.
//
// The same key change makes an in-place edit of an input invalidate its
// stage, which it previously did not.
TEST_F(CliSmokeTest, ReaderStageKeyFollowsFileContentsNotTheFileName) {
  const auto src = fs::path(SOUXMAR_TEST_SOURCE_ROOT) / "examples/stl-cube";
  ASSERT_TRUE(fs::exists(src / "pipeline.yaml")) << src;

  // Same pipeline and same input *name* in both, different geometry.
  const auto a = workdir_ / "a";
  const auto b = workdir_ / "b";
  for (const auto& d : {a, b}) {
    fs::create_directories(d);
    fs::copy_file(src / "pipeline.yaml", d / "pipeline.yaml");
    fs::copy_file(src / "cube.stl", d / "cube.stl");
  }
  {
    // Perturb b's geometry. Any real byte difference will do; this keeps the
    // file a valid ASCII STL so the reader still succeeds and the test is
    // about the cache rather than about error handling.
    std::ifstream in(b / "cube.stl");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const auto pos = text.find("vertex 1");
    ASSERT_NE(pos, std::string::npos) << "fixture changed shape; update this test";
    text.replace(pos, std::string("vertex 1").size(), "vertex 2");
    std::ofstream out(b / "cube.stl", std::ios::trunc);
    out << text;
  }

  // ONE working directory and one cache for both runs. Both matter, and the
  // reason is the whole shape of the defect:
  //
  //   * shared cache  — otherwise there is nothing to collide in;
  //   * shared cwd    — a writer's output is cached as a *path*, and a Path
  //                     entry is only rehydrated if the file it names still
  //                     exists. Run the two pipelines in separate
  //                     directories and B's write stage misses on
  //                     `fs::exists` and re-runs, producing the right answer
  //                     by accident and hiding the bug completely. That
  //                     accident is exactly why this survived: before reader
  //                     paths resolved against their pipeline file, these two
  //                     pipelines *could not* share a working directory.
  const auto rundir = workdir_ / "shared-cwd";
  const auto shared_cache = cachedir_ / "shared";

  const auto run = [&](const fs::path& pipeline_dir) {
    fs::create_directories(rundir);
    std::ostringstream cmd;
    cmd << cd_to(rundir) << " && " << shell_quote(SOUXMAR_TEST_CLI_BINARY) << " run "
        << shell_quote(pipeline_dir / "pipeline.yaml") << " --plugin-path "
        << shell_quote(plugins_root()) << " --cache-dir " << shell_quote(shared_cache)
        << " > run.log 2>&1";
    const int rc = run_cli(cmd.str());
    std::ifstream log(rundir / "run.log");
    const std::string log_text((std::istreambuf_iterator<char>(log)), {});
    EXPECT_EQ(rc, 0) << log_text;
    std::ifstream vtu(rundir / "cube.vtu");
    const std::string vtu_text((std::istreambuf_iterator<char>(vtu)), {});
    return std::pair<std::string, std::string>{vtu_text, log_text};
  };

  const auto [from_a, log_a] = run(a);
  ASSERT_FALSE(from_a.empty()) << log_a;

  const auto [from_b, log_b] = run(b);
  ASSERT_FALSE(from_b.empty()) << log_b;

  // The load-bearing assertion. With the key built from the path string
  // alone, B's stages are indistinguishable from A's and the writer is
  // served A's entry — the run prints `[CACHED  ] write` and leaves A's file
  // in place.
  EXPECT_EQ(log_b.find("[CACHED"), std::string::npos)
      << "a pipeline reading different bytes was served the previous run's "
         "cached output — the stage key saw only the shared filename "
         "'cube.stl':\n"
      << log_b;
  EXPECT_NE(from_a, from_b) << "B's output is byte-identical to A's:\n" << log_b;

  // ...and the cache must still work, or this would pass with caching off.
  // Same directory, same bytes, so this one is a legitimate hit.
  const auto [from_a_again, log_a2] = run(a);
  EXPECT_NE(log_a2.find("[CACHED"), std::string::npos)
      << "an unchanged re-run should hit the cache:\n"
      << log_a2;
  (void)from_a_again;  // a Path entry names a file B has since overwritten
}

TEST_F(CliSmokeTest, EveryShippedExampleRunsFromAForeignWorkingDirectory) {
  const fs::path examples_dir = fs::path(SOUXMAR_TEST_SOURCE_ROOT) / "examples";
  ASSERT_TRUE(fs::exists(examples_dir)) << examples_dir;

  // Only examples/<name>/pipeline.yaml. mesh-comparison and swap-mesher name
  // their files differently and need OpenCASCADE, which no default build has.
  std::vector<fs::path> pipelines;
  for (const auto& entry : fs::directory_iterator(examples_dir)) {
    if (!entry.is_directory())
      continue;
    const auto candidate = entry.path() / "pipeline.yaml";
    if (fs::exists(candidate))
      pipelines.push_back(candidate);
  }
  std::sort(pipelines.begin(), pipelines.end());
  ASSERT_FALSE(pipelines.empty()) << "no examples/*/pipeline.yaml found under " << examples_dir;

  for (const auto& pipeline : pipelines) {
    const std::string name = pipeline.parent_path().filename().string();

    // A fresh directory per example: outputs are cwd-relative by design, and
    // one example's artifacts must not satisfy another's assertions.
    const auto rundir = workdir_ / name;
    fs::create_directories(rundir);

    std::ostringstream cmd;
    cmd << cd_to(rundir) << " && " << shell_quote(SOUXMAR_TEST_CLI_BINARY) << " run "
        << shell_quote(pipeline) << " --plugin-path " << shell_quote(plugins_root())
        << " --cache-dir " << shell_quote(cachedir_ / name) << " > run.log 2>&1";
    const int rc = run_cli(cmd.str());

    std::ifstream log(rundir / "run.log");
    const std::string log_contents((std::istreambuf_iterator<char>(log)), {});
    EXPECT_EQ(rc, 0) << "example '" << name << "' failed from a foreign working directory.\n"
                     << "Pipeline: " << pipeline << "\nOutput:\n"
                     << log_contents;
  }
}

TEST_F(CliSmokeTest, ReRunHitsDiskCacheForWriterStage) {
  const auto pipeline_src =
      fs::path(SOUXMAR_TEST_SOURCE_ROOT) / "examples/cantilever-beam/pipeline.yaml";
  const auto pipeline_local = workdir_ / "pipeline.yaml";
  fs::copy_file(pipeline_src, pipeline_local);

  const std::string base = cd_to(workdir_) + " && " + shell_quote(SOUXMAR_TEST_CLI_BINARY)
                           + " run pipeline.yaml" + " --plugin-path " + shell_quote(plugins_root())
                           + " --cache-dir " + shell_quote(cachedir_);

  ASSERT_EQ(run_cli(base + " > run1.log 2>&1"), 0);
  ASSERT_EQ(run_cli(base + " > run2.log 2>&1"), 0);

  std::ifstream r2(workdir_ / "run2.log");
  std::string r2_contents((std::istreambuf_iterator<char>(r2)), {});
  // The writer stage should be a CACHED hit on the second run — its output
  // is a Path StageOutput which the disk cache knows how to round-trip.
  EXPECT_NE(r2_contents.find("[CACHED  ] write"), std::string::npos)
      << "writer stage was not cached on rerun; second-run output:\n"
      << r2_contents;
}

}  // namespace

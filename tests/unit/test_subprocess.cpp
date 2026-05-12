// SPDX-License-Identifier: Apache-2.0
//
// Sprint 8 push 1 — subprocess harness tests.
//
// The tests dispatch real child processes to the platform shell. We
// pin POSIX-side semantics directly (most of CI is Linux) and skip
// the Windows-specific cases on POSIX; an equivalent suite runs on
// the Windows leg of the CI matrix.

#include "souxmar/plugin/subprocess.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace souxmar::plugin;
using namespace std::chrono_literals;

namespace {

#if !defined(_WIN32)

TEST(Subprocess, ExitCodeRoundTrip) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "exit 42"};
  const auto r = run_subprocess(opts);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.exit_code, 42);
  EXPECT_EQ(r.fatal_signal, 0);
  EXPECT_FALSE(r.timed_out);
  EXPECT_FALSE(r.succeeded());
}

TEST(Subprocess, SucceededOnCleanZeroExit) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "true"};
  const auto r = run_subprocess(opts);
  EXPECT_TRUE(r.succeeded());
}

TEST(Subprocess, StdoutCapture) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "printf 'hello %s\\n' 'world'"};
  const auto r = run_subprocess(opts);
  ASSERT_TRUE(r.succeeded());
  EXPECT_EQ(r.stdout_bytes, "hello world\n");
  EXPECT_TRUE(r.stderr_bytes.empty());
  EXPECT_FALSE(r.stdout_truncated);
}

TEST(Subprocess, StderrCapture) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "printf 'oops\\n' 1>&2; exit 1"};
  const auto r = run_subprocess(opts);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stderr_bytes, "oops\n");
}

TEST(Subprocess, TimeoutKillsRunaway) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "sleep 5"};
  opts.timeout = 100ms;
  const auto r = run_subprocess(opts);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.timed_out);
  EXPECT_LT(r.duration, 2000ms) << "harness took too long to kill the child";
}

TEST(Subprocess, MissingBinaryReportsError) {
  SubprocessOptions opts;
  opts.argv = {"/this/binary/does/not/exist", "--help"};
  const auto r = run_subprocess(opts);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error_message.empty());
}

TEST(Subprocess, StdinPassThrough) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "cat"};
  opts.stdin_bytes = "the cat sat on the mat\n";
  const auto r = run_subprocess(opts);
  ASSERT_TRUE(r.succeeded());
  EXPECT_EQ(r.stdout_bytes, opts.stdin_bytes);
}

TEST(Subprocess, EnvOverlay) {
  SubprocessOptions opts;
  opts.argv = {"/bin/sh", "-c", "printf '%s' \"$SOUXMAR_TEST_OVERLAY\""};
  opts.env = {{"SOUXMAR_TEST_OVERLAY", "yes-please"}};
  const auto r = run_subprocess(opts);
  ASSERT_TRUE(r.succeeded());
  EXPECT_EQ(r.stdout_bytes, "yes-please");
}

TEST(Subprocess, MaxCaptureTruncates) {
  SubprocessOptions opts;
  // Print 200 bytes; cap at 50.
  opts.argv = {"/bin/sh", "-c", "printf 'A%.0s' $(seq 1 200)"};
  opts.max_capture_bytes = 50;
  const auto r = run_subprocess(opts);
  ASSERT_TRUE(r.succeeded());
  EXPECT_EQ(r.stdout_bytes.size(), 50u);
  EXPECT_TRUE(r.stdout_truncated);
}

TEST(Subprocess, FatalSignalSurfacesAsSignal) {
  SubprocessOptions opts;
  // POSIX shells exit 128+N when a child dies on signal N; we want
  // the *child* to die on a signal so the harness reports it via
  // fatal_signal. `kill -SEGV $$` does exactly that.
  opts.argv = {"/bin/sh", "-c", "kill -SEGV $$"};
  const auto r = run_subprocess(opts);
  EXPECT_TRUE(r.ok);
  EXPECT_NE(r.fatal_signal, 0) << "expected SIGSEGV (or similar) to surface via fatal_signal";
  EXPECT_FALSE(r.succeeded());
}

TEST(Subprocess, EmptyArgvRejected) {
  SubprocessOptions opts;
  const auto r = run_subprocess(opts);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error_message.empty());
}

TEST(FindExecutableOnPath, ResolvesShell) {
  // /bin/sh exists on every POSIX CI box we run; we expect either
  // an absolute lookup to succeed, or the PATH lookup to find `sh`.
  auto p = find_executable_on_path("sh");
  EXPECT_TRUE(p.has_value());
}

TEST(FindExecutableOnPath, MissingProgramReturnsNullopt) {
  EXPECT_FALSE(find_executable_on_path("this-program-does-not-exist-souxmar-test").has_value());
}

#endif  // !_WIN32

}  // namespace

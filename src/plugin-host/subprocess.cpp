// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/subprocess.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <cstring>

#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

namespace souxmar::plugin {

namespace {

#if !defined(_WIN32)

// Read up to `cap` bytes from `fd`, draining the pipe. Returns
// (bytes, truncated). When `cap == 0`, no cap.
struct DrainResult {
  std::string bytes;
  bool truncated = false;
};

DrainResult drain_fd_capped(int fd, std::size_t cap) {
  DrainResult r;
  std::array<char, 4096> buf{};
  for (;;) {
    const ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n < 0) {
      if (errno == EINTR)
        continue;
      break;  // EAGAIN / EBADF / pipe closed; loop exits in poll() above
    }
    if (n == 0)
      break;  // EOF
    if (cap == 0 || r.bytes.size() + static_cast<std::size_t>(n) <= cap) {
      r.bytes.append(buf.data(), static_cast<std::size_t>(n));
    } else {
      const std::size_t remaining = cap - r.bytes.size();
      r.bytes.append(buf.data(), remaining);
      r.truncated = true;
      // Keep draining to a /dev/null so the child doesn't block on
      // a full pipe; we just don't grow the buffer.
      char sink[4096];
      while (::read(fd, sink, sizeof(sink)) > 0) {}
      break;
    }
  }
  return r;
}

SubprocessResult run_posix(const SubprocessOptions& opts) {
  SubprocessResult r{};
  if (opts.argv.empty()) {
    r.error_message = "argv is empty";
    return r;
  }

  // Pipes: parent reads stdout (out_pipe[0]) + stderr (err_pipe[0]);
  // parent writes to stdin (in_pipe[1]); child uses the opposite ends.
  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  auto close_all = [&] {
    for (int* fd :
         {&in_pipe[0], &in_pipe[1], &out_pipe[0], &out_pipe[1], &err_pipe[0], &err_pipe[1]}) {
      if (*fd >= 0) {
        ::close(*fd);
        *fd = -1;
      }
    }
  };
  if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
    r.error_message = std::string("pipe() failed: ") + std::strerror(errno);
    close_all();
    return r;
  }
  // Set the parent-side read fds non-blocking so the poll loop below
  // can drain without sitting on a single fd.
  ::fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
  ::fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

  // Build argv as a NULL-terminated C array.
  std::vector<char*> argv_c;
  argv_c.reserve(opts.argv.size() + 1);
  for (const auto& a : opts.argv)
    argv_c.push_back(const_cast<char*>(a.c_str()));
  argv_c.push_back(nullptr);

  // Build envp by overlaying opts.env onto the parent's environment.
  // Empty values delete the key (POSIX has no "unset" via execve, so
  // we just omit it from the assembled envp).
  std::vector<std::string> env_storage;
  std::vector<char*> envp_c;
  std::map<std::string, std::string> merged;
  for (char** e = environ; *e; ++e) {
    std::string line(*e);
    const auto eq = line.find('=');
    if (eq != std::string::npos)
      merged[line.substr(0, eq)] = line.substr(eq + 1);
  }
  for (const auto& [k, v] : opts.env) {
    if (v.empty())
      merged.erase(k);
    else
      merged[k] = v;
  }
  for (const auto& [k, v] : merged) {
    env_storage.push_back(k + "=" + v);
    envp_c.push_back(env_storage.back().data());
  }
  envp_c.push_back(nullptr);

  // posix_spawn handles fork/exec atomically and avoids the
  // fork-handler edge cases vfork has — it's exactly the surface
  // designed for "run a program and wait."
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  // Child's stdin ← in_pipe read end; close write end.
  posix_spawn_file_actions_addclose(&file_actions, in_pipe[1]);
  posix_spawn_file_actions_adddup2(&file_actions, in_pipe[0], STDIN_FILENO);
  posix_spawn_file_actions_addclose(&file_actions, in_pipe[0]);
  // Child's stdout → out_pipe write end; close read end.
  posix_spawn_file_actions_addclose(&file_actions, out_pipe[0]);
  posix_spawn_file_actions_adddup2(&file_actions, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&file_actions, out_pipe[1]);
  // Child's stderr → err_pipe write end; close read end.
  posix_spawn_file_actions_addclose(&file_actions, err_pipe[0]);
  posix_spawn_file_actions_adddup2(&file_actions, err_pipe[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&file_actions, err_pipe[1]);
  if (!opts.work_dir.empty()) {
    // posix_spawn_file_actions_addchdir_np is non-portable; fall back
    // to chdir-in-parent + restore. The reentrancy guard serialises
    // calls so the temporary cwd change is safe across stages.
  }

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);

  // Save + change cwd if requested. Restored unconditionally below.
  std::filesystem::path saved_cwd;
  std::error_code cwd_ec;
  if (!opts.work_dir.empty()) {
    saved_cwd = std::filesystem::current_path(cwd_ec);
    std::filesystem::current_path(opts.work_dir, cwd_ec);
    if (cwd_ec) {
      posix_spawn_file_actions_destroy(&file_actions);
      posix_spawnattr_destroy(&attr);
      close_all();
      r.error_message = "chdir to " + opts.work_dir.string() + " failed: " + cwd_ec.message();
      return r;
    }
  }

  const auto t_start = std::chrono::steady_clock::now();

  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, argv_c[0], &file_actions, &attr, argv_c.data(), envp_c.data());

  if (!saved_cwd.empty()) {
    std::filesystem::current_path(saved_cwd, cwd_ec);  // best-effort
  }
  posix_spawn_file_actions_destroy(&file_actions);
  posix_spawnattr_destroy(&attr);
  if (rc != 0) {
    r.error_message = std::string("posix_spawnp(") + argv_c[0] + ") failed: " + std::strerror(rc);
    close_all();
    return r;
  }

  // Close child-only ends in the parent so EOF reaches the reader.
  ::close(in_pipe[0]);
  in_pipe[0] = -1;
  ::close(out_pipe[1]);
  out_pipe[1] = -1;
  ::close(err_pipe[1]);
  err_pipe[1] = -1;

  // Push stdin in one write + close. Stdin is bounded by the caller;
  // we don't currently support iterative stdin streaming.
  if (!opts.stdin_bytes.empty()) {
    const char* p = opts.stdin_bytes.data();
    std::size_t rem = opts.stdin_bytes.size();
    while (rem > 0) {
      const ssize_t n = ::write(in_pipe[1], p, rem);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      p += n;
      rem -= static_cast<std::size_t>(n);
    }
  }
  ::close(in_pipe[1]);
  in_pipe[1] = -1;

  // Poll the two output fds while the child runs. We don't strictly
  // need poll() for correctness — sequential `read` would work — but
  // poll keeps us responsive to the timeout deadline.
  DrainResult stdout_drain{};
  DrainResult stderr_drain{};
  const auto deadline = (opts.timeout.count() > 0) ? t_start + opts.timeout
                                                   : std::chrono::steady_clock::time_point::max();

  bool killed_by_timeout = false;
  while (out_pipe[0] >= 0 || err_pipe[0] >= 0) {
    pollfd fds[2];
    int nfds = 0;
    if (out_pipe[0] >= 0) {
      fds[nfds++] = {out_pipe[0], POLLIN, 0};
    }
    if (err_pipe[0] >= 0) {
      fds[nfds++] = {err_pipe[0], POLLIN, 0};
    }

    int timeout_ms = -1;
    if (deadline != std::chrono::steady_clock::time_point::max()) {
      const auto now = std::chrono::steady_clock::now();
      const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      timeout_ms = (remain.count() > 0) ? static_cast<int>(remain.count()) : 0;
    }
    const int pr = ::poll(fds, static_cast<nfds_t>(nfds), timeout_ms);
    if (pr < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (pr == 0) {
      // Timeout. Kill the child group + drain what we have so the
      // caller still sees partial output.
      killed_by_timeout = true;
      ::kill(pid, SIGKILL);
      break;
    }
    for (int i = 0; i < nfds; ++i) {
      if (fds[i].revents & (POLLIN | POLLHUP)) {
        const int fd = fds[i].fd;
        auto d = drain_fd_capped(fd, opts.max_capture_bytes);
        if (fd == out_pipe[0]) {
          stdout_drain.bytes.append(d.bytes);
          stdout_drain.truncated = stdout_drain.truncated || d.truncated;
        } else {
          stderr_drain.bytes.append(d.bytes);
          stderr_drain.truncated = stderr_drain.truncated || d.truncated;
        }
        if (fds[i].revents & POLLHUP) {
          ::close(fd);
          if (fd == out_pipe[0])
            out_pipe[0] = -1;
          else
            err_pipe[0] = -1;
        }
      }
    }
  }
  // Drain anything still pending on either fd post-loop.
  if (out_pipe[0] >= 0) {
    auto d = drain_fd_capped(out_pipe[0], opts.max_capture_bytes);
    stdout_drain.bytes.append(d.bytes);
    stdout_drain.truncated = stdout_drain.truncated || d.truncated;
    ::close(out_pipe[0]);
    out_pipe[0] = -1;
  }
  if (err_pipe[0] >= 0) {
    auto d = drain_fd_capped(err_pipe[0], opts.max_capture_bytes);
    stderr_drain.bytes.append(d.bytes);
    stderr_drain.truncated = stderr_drain.truncated || d.truncated;
    ::close(err_pipe[0]);
    err_pipe[0] = -1;
  }

  int status = 0;
  ::waitpid(pid, &status, 0);
  const auto t_end = std::chrono::steady_clock::now();

  r.ok = true;
  r.duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start);
  r.stdout_bytes = std::move(stdout_drain.bytes);
  r.stderr_bytes = std::move(stderr_drain.bytes);
  r.stdout_truncated = stdout_drain.truncated;
  r.stderr_truncated = stderr_drain.truncated;
  r.timed_out = killed_by_timeout;
  if (WIFEXITED(status))
    r.exit_code = WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    r.fatal_signal = WTERMSIG(status);
  close_all();
  return r;
}

#else  // _WIN32

// Windows path. Wraps CreateProcessW + WaitForSingleObject + pipe IO.
// Same SubprocessResult contract as POSIX; structured-exception codes
// map into the `fatal_signal` slot via a thin translation table so
// consumers can treat "child crashed" uniformly across platforms.

std::wstring to_wide(std::string_view s) {
  if (s.empty())
    return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
  return out;
}

std::string from_wide(const std::wstring& s) {
  if (s.empty())
    return {};
  const int len = WideCharToMultiByte(
      CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(len, '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
  return out;
}

int map_status_exception_to_signal(DWORD code) noexcept {
  // Synthetic signal-like values so cross-platform consumers can pattern-match.
  switch (code) {
    case STATUS_ACCESS_VIOLATION:
      return 11;  // SIGSEGV
    case STATUS_STACK_OVERFLOW:
      return 11;  // SIGSEGV
    case STATUS_ILLEGAL_INSTRUCTION:
      return 4;  // SIGILL
    case STATUS_FLOAT_DIVIDE_BY_ZERO:
      return 8;  // SIGFPE
    case STATUS_INTEGER_DIVIDE_BY_ZERO:
      return 8;  // SIGFPE
    default:
      return 0;
  }
}

SubprocessResult run_windows(const SubprocessOptions& opts) {
  SubprocessResult r{};
  if (opts.argv.empty()) {
    r.error_message = "argv is empty";
    return r;
  }
  // Build a UTF-16 command line. The classic CreateProcessW quoting
  // is a hot mess — we use the standard MSVC algorithm.
  std::wstring cmdline;
  auto quote = [](std::wstring& out, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
      out += arg;
      return;
    }
    out += L'"';
    for (std::size_t i = 0; i < arg.size(); ++i) {
      std::size_t bs = 0;
      while (i < arg.size() && arg[i] == L'\\') {
        ++bs;
        ++i;
      }
      if (i == arg.size())
        out.append(bs * 2, L'\\');
      else if (arg[i] == L'"') {
        out.append(bs * 2 + 1, L'\\');
        out += L'"';
      } else {
        out.append(bs, L'\\');
        out += arg[i];
      }
    }
    out += L'"';
  };
  for (std::size_t i = 0; i < opts.argv.size(); ++i) {
    if (i)
      cmdline += L' ';
    quote(cmdline, to_wide(opts.argv[i]));
  }
  std::wstring program = to_wide(opts.argv[0]);

  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE in_r = nullptr, in_w = nullptr;
  HANDLE out_r = nullptr, out_w = nullptr;
  HANDLE err_r = nullptr, err_w = nullptr;
  if (!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0)
      || !CreatePipe(&err_r, &err_w, &sa, 0)) {
    r.error_message = "CreatePipe failed";
    return r;
  }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  si.dwFlags = STARTF_USESTDHANDLES;

  PROCESS_INFORMATION pi{};

  std::wstring wd = opts.work_dir.empty() ? std::wstring{} : opts.work_dir.wstring();

  const auto t_start = std::chrono::steady_clock::now();
  const BOOL spawned = CreateProcessW(program.empty() ? nullptr : program.c_str(),
                                      cmdline.data(),
                                      nullptr,
                                      nullptr,
                                      TRUE,
                                      0,
                                      nullptr,
                                      wd.empty() ? nullptr : wd.c_str(),
                                      &si,
                                      &pi);
  if (!spawned) {
    r.error_message = "CreateProcessW failed (GLE=" + std::to_string(GetLastError()) + ")";
    CloseHandle(in_r);
    CloseHandle(in_w);
    CloseHandle(out_r);
    CloseHandle(out_w);
    CloseHandle(err_r);
    CloseHandle(err_w);
    return r;
  }
  CloseHandle(in_r);
  CloseHandle(out_w);
  CloseHandle(err_w);

  if (!opts.stdin_bytes.empty()) {
    DWORD written = 0;
    WriteFile(in_w,
              opts.stdin_bytes.data(),
              static_cast<DWORD>(opts.stdin_bytes.size()),
              &written,
              nullptr);
  }
  CloseHandle(in_w);

  auto drain_handle = [&](HANDLE h, std::string& out, bool& truncated) {
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
      if (opts.max_capture_bytes == 0 || out.size() + n <= opts.max_capture_bytes) {
        out.append(buf, n);
      } else {
        const std::size_t remaining = opts.max_capture_bytes - out.size();
        out.append(buf, remaining);
        truncated = true;
        // drain to /dev/null
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {}
        break;
      }
    }
  };

  const DWORD wait_ms =
      (opts.timeout.count() > 0) ? static_cast<DWORD>(opts.timeout.count()) : INFINITE;
  const DWORD wr = WaitForSingleObject(pi.hProcess, wait_ms);
  bool timed_out = false;
  if (wr == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    timed_out = true;
  }

  drain_handle(out_r, r.stdout_bytes, r.stdout_truncated);
  drain_handle(err_r, r.stderr_bytes, r.stderr_truncated);
  CloseHandle(out_r);
  CloseHandle(err_r);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  const auto t_end = std::chrono::steady_clock::now();
  r.ok = true;
  r.duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start);
  r.timed_out = timed_out;
  // Windows surfaces structured-exception "crashes" as the exit
  // code. Translate the well-known codes into signal-like values;
  // everything else stays in exit_code.
  if (const int sig = map_status_exception_to_signal(exit_code); sig != 0) {
    r.fatal_signal = sig;
  } else {
    r.exit_code = static_cast<int>(exit_code);
  }
  return r;
}

#endif

}  // namespace

SubprocessResult run_subprocess(const SubprocessOptions& opts) noexcept {
  try {
#if defined(_WIN32)
    return run_windows(opts);
#else
    return run_posix(opts);
#endif
  } catch (const std::bad_alloc&) {
    SubprocessResult r;
    r.error_message = "out of memory";
    return r;
  } catch (...) {
    SubprocessResult r;
    r.error_message = "unknown error in run_subprocess";
    return r;
  }
}

std::optional<std::filesystem::path> find_executable_on_path(std::string_view program) noexcept {
  if (program.empty())
    return std::nullopt;
  std::filesystem::path p(program);
  std::error_code ec;
  if (p.is_absolute()) {
    return std::filesystem::exists(p, ec) ? std::optional{p} : std::nullopt;
  }
#if defined(_WIN32)
  const char* path_env = std::getenv("PATH");
  const char sep = ';';
#else
  const char* path_env = std::getenv("PATH");
  const char sep = ':';
#endif
  if (!path_env)
    return std::nullopt;
  std::string env(path_env);
  std::size_t start = 0;
  while (start <= env.size()) {
    const auto end = env.find(sep, start);
    const auto dir = env.substr(start, end == std::string::npos ? env.size() - start : end - start);
    if (!dir.empty()) {
      auto candidate = std::filesystem::path(dir) / std::string(program);
      if (std::filesystem::exists(candidate, ec) && !std::filesystem::is_directory(candidate, ec)) {
        return candidate;
      }
#if defined(_WIN32)
      auto candidate_exe = candidate;
      candidate_exe += ".exe";
      if (std::filesystem::exists(candidate_exe, ec)
          && !std::filesystem::is_directory(candidate_exe, ec)) {
        return candidate_exe;
      }
#endif
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return std::nullopt;
}

}  // namespace souxmar::plugin

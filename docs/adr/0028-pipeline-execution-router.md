# ADR-0028: Pipeline ExecutionRouter — per-stage local vs. remote dispatch

- **Status:** Accepted
- **Date:** 2026-05-16 (Sprint 18 push 1)
- **Author:** souxmar platform team
- **Deciders:** platform, AI
- **Tier:** 2 (the runner contract is on a stable path; the
  ExecutionRouter is a new dispatcher injection point)
- **Affects:** `include/souxmar/pipeline/runner.h` (new
  `ExecutionRouter` parameter); `src/pipeline/registry_dispatcher.cpp`
  (forwards local stages unchanged); new
  `src/pipeline/remote_dispatcher.cpp` (Sprint 19 push 1
  implementation).

## Context

ADR-0027 named the wire-protocol for hosted compute offload via
`execution.target: "managed"` on a stage. Sprint 17 push 3's
service scaffold accepts the protocol. The engine-side router
that inspects the stage + decides local-dispatch vs.
remote-submit is the missing piece; ADR-0028 ratifies the
shape.

Three viable shapes named in the Sprint 17 retro:

1. **Routing decided at dispatch time.** Existing
   RegistryDispatcher gains a "skip to remote" branch.
2. **Routing decided at parse time.** Per-stage dispatcher
   selected during pipeline parse; the runner walks a list of
   `(stage, dispatcher)` pairs.
3. **Hybrid via plugin forwarding.** Local plugin's solver
   stub forwards to the remote service. Unmodified plugins
   participate.

## Decision

**Option 2 — routing decided at parse time, per-stage
dispatcher selected.**

```cpp
// New types in souxmar/pipeline/runner.h:

enum class StageExecutionTarget : std::uint8_t {
  Local   = 0,    // default
  Managed = 1,    // remote via compute-offload service
};

struct StageExecution {
  StageExecutionTarget  target           = StageExecutionTarget::Local;
  std::int64_t          timeout_seconds  = 3600;
  struct {
    std::int32_t  cpu_cores  = 1;
    std::int32_t  memory_gb  = 4;
  }                     capacity;
};

// IDispatcher gains a sibling type:
class IExecutionRouter {
 public:
  virtual ~IExecutionRouter() = default;
  // Pick the dispatcher for `stage`. The router owns the
  // dispatcher pool (registry-based local, remote, ...).
  virtual IDispatcher* dispatcher_for(const Stage& stage) = 0;
};
```

`run_pipeline()` now takes an `IExecutionRouter&` instead of
an `IDispatcher&`; the router's default impl (`DefaultLocalRouter`)
always returns the registry dispatcher. Managed-stage routing
arrives via `OffloadingRouter` which returns a
`RemoteDispatcher` for `target == Managed` stages.

### Reasons over option 1 / 3

- **Option 1** mixes the registry-walk + the remote-submit
  paths in one dispatcher class; the unit tests covering the
  registry dispatcher (Sprint 3+) would have to grow remote-
  submit knowledge. The contract surface bloats.
- **Option 3** requires every solver plugin to be aware of
  remote forwarding; plugin authors who didn't sign up for
  that get magic behaviour. Rejected.
- **Option 2** keeps the dispatcher contract clean. The
  RegistryDispatcher stays focused on its single
  responsibility. The router is the new abstraction; testing
  is straightforward (assert routing decisions per stage; assert
  the chosen dispatcher runs the stage).

### What the parser does

The pipeline parser already reads YAML; ADR-0028 extends it to
populate `Stage::execution` from the optional `execution:` block.
Absent block → `StageExecutionTarget::Local` (default).
Schema discriminator stays at v1 (additive Tier-0 — old
pipelines parse unchanged).

### What the dispatcher pool contains

At Sprint 18 ratification: one dispatcher (local). Sprint 19
push 1 adds `RemoteDispatcher` reading from `OffloadingRouter`'s
HTTP-client config. Sprint 22 public-beta exercises the full
path against the compute-offload scaffold's stub mode.

## Consequences

- `include/souxmar/pipeline/runner.h` declares `IExecutionRouter`
  + `DefaultLocalRouter`. Existing callers of
  `run_pipeline(p, dispatcher, cache)` keep their dispatcher
  by wrapping it: `DefaultLocalRouter router(&dispatcher);
  run_pipeline(p, router, cache);`.
- The CLI's `souxmar run` keeps its existing flag set; no new
  flag at Sprint 18.
- Sprint 19 push 1 ratchets in `OffloadingRouter` + the
  `RemoteDispatcher` class that wraps the compute-offload HTTP
  protocol.
- Existing integration tests pass without modification because
  `DefaultLocalRouter` is a 1:1 wrapper over a registry
  dispatcher.

## Risks

- **R-035 (router-of-dispatchers indirection cost).** Per-
  stage virtual dispatch through the router adds one indirection
  per stage. **Mitigation:** the cost is microseconds per
  stage; the eval suite's latency gate (Sprint 7+) catches
  any regression > 1ms.
- **R-036 (Stage::execution drift between desktop +
  engine).** A desktop client sets `execution.target =
  "managed"` against an older engine binary that doesn't know
  the field. **Mitigation:** the parser tolerates unknown
  fields (Tier-0 additive); older engines silently ignore +
  run locally — degraded behaviour, not crash.

— Sprint 18 push 1.

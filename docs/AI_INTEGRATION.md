# AI Integration

souxmar's chat panel is **agentic by design**: the AI is not a documentation lookup, it is a control surface. It can mesh a part, set a boundary condition, run a solve, and inspect a result by invoking the same C++ backend the user does. Everything the AI does, the user can see and undo.

This document covers the AI architecture, the BYOK (bring your own key) credential model, the agent's tool surface, privacy, and cost control.

## Design philosophy

1. **Chat is a thin shell over the backend.** The AI cannot do anything the user cannot do via the CLI or Python. It calls the same plugin host, the same orchestrator, the same files.
2. **Every AI action is auditable.** Tool invocations render as inline cards in the chat; every action also writes a line to `.souxmar/chat/audit.log`. Nothing the AI does is invisible.
3. **The user is in the loop for destructive actions.** Re-running a cached stage: silent. Overwriting a result file, deleting a pipeline, sending data to a cloud solver: explicit confirmation chip in the chat.
4. **Privacy is the default.** BYOK means the API call goes from the user's machine to the provider, with no souxmar server in the path. Conversation history stays on disk.
5. **The AI is optional.** The desktop app, CLI, and Python library all work fully without any AI provider configured.

## Provider model

souxmar supports three provider modes, configurable per project or globally:

### 1. Bring Your Own Key (BYOK) — free tier

The user supplies their own API credentials for one of the supported providers:

| `provider`            | Service                    | Key variable         | Notes                                       |
| --------------------- | -------------------------- | -------------------- | ------------------------------------------- |
| `anthropic`           | Anthropic (Claude)         | `ANTHROPIC_API_KEY`  | Messages API. Best agentic tool-use behaviour. |
| `openai`              | OpenAI (GPT)               | `OPENAI_API_KEY`     | |
| `grok`                | xAI (Grok)                 | `XAI_API_KEY`        | |
| `deepseek`            | DeepSeek                   | `DEEPSEEK_API_KEY`   | |
| `groq`                | Groq                       | `GROQ_API_KEY`       | |
| `mistral`             | Mistral                    | `MISTRAL_API_KEY`    | |
| `openrouter`          | OpenRouter                 | `OPENROUTER_API_KEY` | One key, many vendors. Prefix the model id. |
| `together`            | Together AI                | `TOGETHER_API_KEY`   | |
| `ollama`              | Ollama (local)             | *none*               | Air-gapped. Function calling required; capability degrades on smaller models. |
| `openai-compatible`   | Anything else              | `SOUXMAR_AI_API_KEY` | LM Studio, vLLM, llama.cpp. Supply `base_url`. |

Every one of these runs the full agent loop — the model receives the tool catalogue, calls
tools, and sees real results. `model` is always required; souxmar does not guess one.

API calls go directly from the desktop app to the provider. souxmar's servers are not in the path.

### Running the agent

The agent loop is `souxmar agent chat`. The model receives the tool catalogue, calls tools,
sees the real results, and keeps going until it answers:

```sh
souxmar agent chat "how many plugins are installed?" \
  --provider grok --model <model id> \
  --plugin-path build/dev/examples/plugins
```

```
agent: grok · model <id> · 24 tools · max 8 steps
  [ok] list_plugins → registry: 30 capabilities total

The engine reported: registry: 30 capabilities total
```

With no `--provider`, the project's `project.ai.toml` decides — so `souxmar agent chat "..."`
inside a configured project just works. `souxmar agent providers` lists everything selectable.

**Confirmation is the user's, not the model's.** A tool declared `ConfirmOnce` or
`ConfirmAlways` prompts on the terminal before it runs:

```
  the agent wants to run `solve`
  allow? [y/N]
```

Answering no returns a `DENIED` error *to the model*, which can then explain what it wanted
rather than stalling silently. `--yes` overrides every tool to automatic for one run — correct
for scripting, wrong for anything destructive you have not read.

**Step budget.** `--max-steps` (default 8) bounds provider round-trips. A model that loops
calling the same tool is a real failure mode and each step costs money, so the loop stops and
says the answer is incomplete rather than presenting a half-finished turn as a conclusion.

**Open models participate.** Tools are advertised to Ollama and to every OpenAI-compatible
service alike. This previously did not work at all: `ChatRequest::tool_names` was populated by
the eval runner and read by no provider, so no model was ever offered a souxmar tool and no
tool call could be elicited from any of them.

### In the desktop chat panel

The panel runs the same loop. Ask it something, and it calls tools against the open project:
each tool it ran appears under the reply with what the dispatcher returned, so a turn is never
silent about having changed something.

Confirmation could not be a terminal `[y/N]` here, so the loop **suspends** instead of blocking:
when the agent reaches a tool needing approval, the turn pauses, the panel shows a card naming
the tool and the arguments the model chose, and the composer is disabled until you answer.
Declining is a real answer rather than a cancel — the model is told, and gets a turn to explain
what it wanted.

The suspended session lives in the C bridge, keyed by project, so the mesh and field handles
earlier tools produced survive the pause. That is also what makes multi-turn work possible:
mesh in one message, solve in the next.

Requires the `real-ffi` build below. Without it the panel says so.

#### Connecting any OpenAI-compatible service

Most services — xAI (Grok), OpenAI, DeepSeek, Groq, Mistral, OpenRouter, Together — and every
local server that advertises an "OpenAI-compatible endpoint" (LM Studio, vLLM, llama.cpp's
server) speak the same `POST {base_url}/chat/completions` shape. souxmar implements that shape
once, so connecting one of them is configuration rather than a code change.

Put a `project.ai.toml` next to your `pipeline.yaml`:

```toml
schema   = 1
provider = "grok"                  # a preset id, or "openai-compatible"
model    = "<the model id your account can reach>"
```

then export the key in the environment souxmar runs in:

```sh
export XAI_API_KEY="xai-..."
```

The preset ids and the environment variable each one looks for:

| `provider`          | Endpoint                        | Key variable         |
| ------------------- | ------------------------------- | -------------------- |
| `grok`              | `https://api.x.ai/v1`           | `XAI_API_KEY`        |
| `openai`            | `https://api.openai.com/v1`     | `OPENAI_API_KEY`     |
| `deepseek`          | `https://api.deepseek.com/v1`   | `DEEPSEEK_API_KEY`   |
| `groq`              | `https://api.groq.com/openai/v1`| `GROQ_API_KEY`       |
| `mistral`           | `https://api.mistral.ai/v1`     | `MISTRAL_API_KEY`    |
| `openrouter`        | `https://openrouter.ai/api/v1`  | `OPENROUTER_API_KEY` |
| `together`          | `https://api.together.xyz/v1`   | `TOGETHER_API_KEY`   |
| `openai-compatible` | *you supply it*                 | `SOUXMAR_AI_API_KEY` |

Both are overridable, which is how you reach a service that has no preset, a self-hosted
server, or an endpoint that has moved:

```toml
schema   = 1
provider = "openai-compatible"
model    = "my-local-model"

[openai_compatible]
base_url    = "http://localhost:1234/v1"   # LM Studio, vLLM, llama.cpp, …
api_key_env = "MY_GATEWAY_TOKEN"           # omit entirely for a server with no auth
```

**No API key ever goes in this file.** `project.ai.toml` sits beside `pipeline.yaml` and gets
committed; the loader refuses a config containing `api_key`, `token` or `secret` rather than
reading one out of it. The key is read from the named environment variable, and is passed to
`curl` through a configuration on **stdin** rather than as an argument — so it does not appear
in `ps` output or `/proc/<pid>/cmdline`, and is never written to disk.

Model ids are deliberately not defaulted. Every service names and retires models on its own
schedule, and a stale built-in default produces a 404 that reads like a broken install, so
`model` is required and the error says so.

**Anthropic is reached separately.** Its Messages API is a different request and tool shape
(`input_schema`, top-level `system`, `x-api-key` auth, mandatory `max_tokens`), so it is not
reachable through the OpenAI-compatible provider — it has its own `AnthropicProvider`. From the
config side the difference is invisible: set `provider = "anthropic"` and export
`ANTHROPIC_API_KEY`. Override the endpoint or the key variable with an `[anthropic]` subtable,
the same way `[openai_compatible]` works:

```toml
schema   = 1
provider = "anthropic"
model    = "claude-sonnet-4-20250514"

[anthropic]                                    # optional
base_url    = "https://gateway.internal/anthropic/v1"
api_key_env = "WORK_ANTHROPIC_KEY"
```

`byok-anthropic` and `byok-openai` are the original spellings and still load; they resolve to
`anthropic` and `openai` respectively.

**Reaching it from the desktop app.** The chat panel talks to the engine through the C bridge,
which is only linked when the desktop is built with the `real-ffi` feature. That build needs a
configured CMake build directory and the location of the engine's third-party libraries:

```sh
cmake --build build/dev                    # engine + C bridge archives
cd src/desktop/src-tauri
SOUXMAR_BUILD_DIR=$PWD/../../../build/dev \
SOUXMAR_EXTRA_LINK_DIRS=/opt/homebrew/lib \
  cargo build --features real-ffi
```

Without `real-ffi` the shell still builds and runs, but `chat_send` returns
`FeatureNotWired` and the chat panel says so — no provider of any kind is reachable.

**Verifying your endpoint.** The unit-test binary carries an opt-in live check, which is the
same code path the app uses:

```sh
SOUXMAR_TEST_OPENAI_BASE_URL=https://api.x.ai/v1 \
SOUXMAR_TEST_OPENAI_KEY=$XAI_API_KEY \
SOUXMAR_TEST_OPENAI_MODEL=<model id> \
  ./build/dev/tests/unit/souxmar_unit_tests --gtest_filter='OpenAICompatibleLive.*'
```

### 2. Managed AI — Pro tier

souxmar bills the user a monthly subscription, the user does not handle keys, and the desktop app talks to a souxmar proxy that fans out to the underlying providers. Includes a token allowance per month and seamless model switching. See [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md).

### 3. Hybrid

A user with a Pro subscription can still register a BYOK provider for specific projects (e.g. a defence project that may not call out to managed services). The provider is a per-project setting; project files declare it in `project.souxmar.toml`.

## Credential storage

Keys are never stored in souxmar's project files, never in the conversation history, never in the cache. They are stored in the OS keychain:

| OS      | Backend                                                     |
| ------- | ----------------------------------------------------------- |
| macOS   | Keychain Services (`SecItem*`)                              |
| Windows | Credential Manager (DPAPI-protected)                        |
| Linux   | Secret Service (libsecret); fallback to encrypted file      |

The keychain entry is namespaced as `com.souxmar.api-key.<provider>` so a uninstall + reinstall preserves the credential and a `souxmar key forget --all` cleanly removes it.

A key never enters the React frontend's memory. The Tauri Rust shell holds it; outbound HTTPS calls to the provider are made from Rust; the frontend only receives streamed completion tokens.

## Agent tool surface

The agent has a strict set of typed tools. They are defined in `src/ai/tools/` as a versioned schema (the schema is part of the v1 contract — third parties can target it). Adding a tool requires an RFC.

Categories and examples:

| Category    | Tool                          | Effect                                                      |
| ----------- | ----------------------------- | ----------------------------------------------------------- |
| Read        | `read_geometry_summary`       | Returns face/edge/volume counts, bounding box, named tags.  |
| Read        | `query_field`                 | Returns min/max/mean of a field, optionally over a tag.     |
| Read        | `screenshot_viewport`         | Captures the current viewport as PNG; returns a thumbnail.  |
| Pipeline    | `propose_pipeline`            | Drafts a YAML pipeline; user must accept before it writes.  |
| Pipeline    | `apply_pipeline_diff`         | Applies a small diff to the active pipeline. Reversible.    |
| Mesh        | `mesh`                        | Calls `mesher.*` plugin. Long-running, streams progress.    |
| BC          | `set_bc`                      | Adds a Dirichlet/Neumann/Robin BC to a tagged entity.       |
| Material    | `set_material`                | Assigns a material (lookup or custom) to a tagged volume.   |
| Solve       | `solve`                       | Calls `solver.*` plugin. Confirmation required if runtime estimate > 60 s. |
| Postproc    | `compute_field`               | Calls `postproc.*` plugin (e.g. von Mises from stress).     |
| File        | `write_pipeline`              | Persists current pipeline. Confirmation required.           |
| File        | `export_results`              | Writes `.vtu` / `.xdmf`. Confirmation required.             |
| Plugin      | `list_plugins`                | Returns available plugins by capability.                    |
| Pipeline    | `propose_am_setup`            | Drafts a complete AM pipeline for a process + material + machine; stages the setup under session `manufacturing`. |
| Mesh        | `check_printability`          | Runs `solver.am.printability`; summarises score distribution + dominant blocker. |
| BC          | `set_build_orientation`       | Scores candidate build directions on support area / height / footprint; stages the winner. Confirmation required. |
| Mesh        | `estimate_build_cost`         | Mass, build time, energy, machine + material cost from the staged setup and mesh bounding box. |
| BC          | `apply_hydrostatic_load`      | Stages a depth-derived pressure load case (computes ρ·g·h). Confirmation required. |
| Field       | `check_marine_integrity`      | Collapse margin + corrosion / galvanic advisory summary. Advisory only, not class approval. |

Tools missing from this list (e.g. "delete file", "install plugin", "send to cloud") are deliberately absent. Adding them is an RFC, not a one-line change.

Each tool returns a structured JSON result the model can chain on, plus a human-readable summary the chat panel renders inline. Errors are returned as structured `{code, message, suggestion}` so the model can recover, not retry-loop.

## Confirmation policy

Every tool is annotated as one of:

- `auto` — runs immediately (read-only, or trivially reversible).
- `confirm-once` — first invocation in a session shows a chip; subsequent ones are silent.
- `confirm-always` — every invocation requires explicit click (file writes, exports, costly solves, sending data outside the machine).

The user can override per-tool in settings. There is no global "trust this AI" switch; that is by design.

## Context window management

The agent's context is constructed fresh per turn from:

1. **System prompt** — the souxmar agent persona and tool catalogue. Cached on the provider when supported (Anthropic prompt caching; OpenAI prompt caching).
2. **Project context** — `project.souxmar.toml`, the active pipeline YAML, and a summary of the current geometry/mesh/fields (entity counts, tag list, viewport pose). Refreshed each turn.
3. **Conversation history** — the prior user/assistant turns from this session.
4. **User message** — the current prompt.

For long sessions, older turns are summarised into a rolling memo to keep within the model's context window. The full transcript is preserved on disk; only the model's working set is trimmed.

souxmar caches the static parts of the prompt aggressively to keep BYOK costs down. With Anthropic's 5-minute cache, a typical interactive session reads ~80% of its prompt from cache.

## Cost and budget controls

- **Per-session token budget.** Default $1.00 / 200K tokens. Configurable. The chat shows a small running total and warns at 80%.
- **Per-tool runtime budget.** A `solve` tool that is going to take 30 minutes will not be silently launched; the model is required to surface its estimate, and the user confirms.
- **Model selector.** The user can pin a model (Claude Sonnet 4.6, GPT-5-mini, etc.) globally or per project. The default leans cheap-and-fast; the user can opt into the heavy-and-slow tier for harder problems.
- **Audit log.** `.souxmar/chat/audit.log` records every tool invocation with input hash, runtime, (for managed AI) token cost, and — on platforms where the host can query process-wide heap usage (Linux + glibc ≥ 2.33 today, via `mallinfo2`) — a per-call `heap_bytes_delta` field that surfaces tool-side memory growth. Useful as a leak indicator: a session with steadily growing deltas points at a plugin that owns memory it isn't releasing. The accounting is process-wide, so it's most accurate in single-threaded sessions (`max_workers=1`); multi-threaded runs surface aggregate deltas that mix sibling-thread allocations. `souxmar audit show` summarises a project's spend.

## Latency budgets

The performance contract for the agent path lives in [`ENGINEERING_PRACTICES.md`](ENGINEERING_PRACTICES.md) § Performance budgets. The two that govern the chat experience:

| Surface                                     | p95 budget |
| ------------------------------------------- | ---------- |
| First chat token (BYOK direct)              | < 800 ms   |
| First chat token (managed AI proxy)         | < 1200 ms  |

The Sprint 7 push 4 eval suite measures these in two ways: by *exercising the dispatcher path* the first-token waits on, and by *reporting aggregate percentiles* across every dispatched step. Run `souxmar-eval <evals-dir> --latency-output report.json` to write a per-tool + aggregate JSON the perf dashboard renders alongside the Google-Benchmark reports. `--max-p95-ms <N>` turns the aggregate p95 into a merge-blocking gate — used today in CI to catch dispatcher regressions (the scripted-eval p95 is microseconds; the 800 ms BYOK budget kicks in once the LLM provider integration lands and the eval drives real model calls).

Note on accuracy: the scripted eval runs `dispatch_tool` directly against the local plugin registry — no network, no LLM. The latency it reports is dispatcher + plugin overhead, which is the floor for the BYOK path. The LLM-side latency (provider round-trip + first-token delivery) is the dominant term in production and lands in the same JSON as a `provider_first_token_ms` field once the Sprint 12+ provider integration ships.

## Privacy and data flow

Three concentric trust boundaries, in order of least to most data leakage:

1. **No AI configured.** Nothing leaves the machine. Default state of a fresh install.
2. **BYOK to a provider.** The chat content + tool results that the agent inspects (e.g. mesh statistics, field summaries) are sent to the provider. **Geometry and mesh data are not sent unless the user explicitly invokes a tool that includes them** (e.g. `screenshot_viewport`, `dump_mesh_for_review`). Tools that send data are tagged with a "leaves machine" badge.
3. **Managed AI (Pro).** Same as BYOK, but the provider is reached through a souxmar proxy that handles billing. The proxy retains nothing beyond billing metadata. Detailed in [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md).

For sensitive work — defence, ITAR-controlled designs, NDA work — the recommended configuration is local provider (Ollama) or BYOK with tools that send data disabled in project settings.

## Failure modes the agent handles well

- A solver plugin segfaults: the host catches it (`SOUXMAR_E_PLUGIN_FAULT`), the agent receives a structured error, and surfaces a "the elasticity solver crashed on this mesh; do you want me to try a coarser mesh?" suggestion.
- A mesh fails quality gates: the agent inspects metrics and proposes specific refinement parameters, not generic "try again."
- BC tag not found: the agent lists the tags that exist, asks which one the user meant.

## Failure modes the agent does not handle and we are honest about

- **Hallucinated geometry.** If the user asks about a face that does not exist, the agent's answer is only as good as its `read_geometry_summary` discipline. Tool-grounded answers are reliable; freeform interpretation is not.
- **Numerical judgement.** The agent will not tell you whether a stress concentration is dangerous. It can compute the value; the engineer reads it.
- **Domain expertise beyond the model.** A small open-weights local model is fine for orchestration but will struggle with complex setups. The model selector is part of the user's responsibility.

## Roadmap items

These are deliberately out of scope at v1.0 and tracked under the AI workstream:

- Multi-step undo of an agent session ("revert the last five tool calls"). Currently each tool is individually reversible; bulk undo needs design.
- Voice input. Not a v1 priority.
- Multi-agent / agent-to-agent coordination. Probably never; one well-grounded agent beats two arguing agents in this domain.
- Automatic learning from the user's edits. We are explicitly not collecting "improve the model" data; if we ever do, it will be opt-in, project-scoped, and clearly signposted.

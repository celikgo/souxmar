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

| Provider              | Models                                          | Notes                                       |
| --------------------- | ----------------------------------------------- | ------------------------------------------- |
| Anthropic (Claude)    | Claude Opus 4.x, Sonnet 4.x, Haiku 4.x          | Default; best agentic tool-use behaviour.   |
| OpenAI                | GPT-5 family, GPT-4.1 family                    | Tool calling supported.                     |
| Local (Ollama / LM Studio) | Qwen, Llama, Mistral, etc.                | For air-gapped environments. Function calling required; capability degrades on smaller models. |

API calls go directly from the desktop app to the provider. souxmar's servers are not in the path.

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

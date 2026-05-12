# ADR-0020: Per-project AI provider override — `project.ai.toml` sibling file

- **Status:** Accepted
- **Date:** 2026-05-13 (Sprint 15 push 2)
- **Author:** souxmar platform team
- **Deciders:** desktop, AI, security review
- **Tier:** 2 (the on-disk file shape this ADR ratifies is a
  Tier-2 contract; adding optional keys is non-breaking,
  renaming / removing existing keys is a Tier-2 break with a
  deprecation cycle)
- **Affects:** new `include/souxmar/ai/provider_config.h` +
  `src/ai/provider_config.cpp`; `src/c-bridge/provider.cpp`
  consults the config instead of always-StubProvider;
  `.gitignore` adds `project.ai.toml`.

## Context

ADR-0019 introduced the Pro-tier managed-AI proxy + named the
`ai.provider` field that selects per-project provider routing
("managed" | "byok-anthropic" | "byok-openai" | "ollama"). It
deferred *where on disk* this field lives.

Sprint 14's retro queued the decision:

> The decision worth recording in a future ADR: **what's the
> per-project provider override format on disk?** Two viable
> shapes: A) inline in `project.souxmar.toml` under a
> top-level `[ai]` section, or B) separate `project.ai.toml`
> sibling.

Sprint 15 push 2 (this push) is the first push that **needs**
the answer. The bridge's `provider.cpp` (Sprint 14 push 4)
currently always routes to StubProvider. To swap in a real
per-project provider lookup, the engine needs to read
*something* off disk that names the configured provider.

## Decision

**We pick Option B — separate `project.ai.toml` sibling file.**

Concretely:

- Every project directory may contain a `project.ai.toml`
  alongside its `pipeline.yaml` / `project.souxmar.toml`.
- The file is **`.gitignore`d by default** — `project.ai.toml`
  is added to the in-tree `.gitignore` from this push.
- The file is optional. When absent, the engine defaults to
  StubProvider (the Sprint 14 behaviour) — the same fallback
  the Bridge wrapper's no-real-ffi path delivers.

### Schema

```toml
# project.ai.toml — sibling of pipeline.yaml. Per-project AI
# provider configuration. Optional; absent means default
# StubProvider.
#
# Per ADR-0020. Schema discriminator is mandatory.

schema = 1

# Provider selector. One of:
#   "stub"           — canned replies; for tests / dev only.
#   "byok-anthropic" — direct call to Anthropic with the
#                      OS-keychain-stored Anthropic key.
#   "byok-openai"    — direct call to OpenAI with the
#                      OS-keychain-stored OpenAI key.
#   "ollama"         — local Ollama daemon.
#   "managed"        — souxmar Pro-tier managed-AI proxy.
provider = "byok-anthropic"

# Provider-specific model id. Required for byok-anthropic and
# byok-openai; optional (with provider-specific defaults) for
# ollama and managed; ignored for stub.
model = "claude-sonnet-4-6"

# Optional provider-specific overrides. Each provider has its
# own optional subtable; absent → provider defaults apply.

[ollama]
# Default: http://localhost:11434
endpoint = "http://localhost:11434"

[managed]
# Default: https://proxy.souxmar.dev
endpoint = "https://proxy.souxmar.dev"
# Sprint 14 push 4's openapi.yaml schema is the contract.
```

### Reasons for Option B

1. **The file is secrets-adjacent.** `provider = "byok-anthropic"`
   tells anyone who reads the file that an Anthropic key is
   expected; the file is one keystroke away from a contributor
   accidentally pasting the key directly. Keeping it `.gitignore`d
   by default means the casual user does the right thing
   without thinking.

   `project.souxmar.toml` is checked into git (it's the
   project's pipeline definition); folding `[ai]` into it would
   require either the same `.gitignore`-by-default treatment
   (breaking project sharing) or a careful separation
   convention (relying on the user not to fill in the secret).

2. **Per-project provider config has a different lifecycle**
   than the pipeline. A user might prototype the pipeline once,
   then swap providers as they iterate (StubProvider during
   development, managed-AI for shipping the analysis to a
   reviewer). One-file-per-axis means git history doesn't see
   churn it doesn't care about.

3. **Cleaner separation between "project definition" and
   "execution config."** `project.souxmar.toml` says what the
   project *is*; `project.ai.toml` says *how* the AI runs.
   Future per-project execution config (Sprint 17+ — viewport
   rendering preferences, telemetry opt-in) lives in
   `project.exec.toml` or similar siblings.

### Reasons against Option B (rejected)

- **Two files where one would do.** Mild cost. The user-
  facing CLI (`souxmar run`) ignores this file entirely
  today; only the desktop chat panel + the agent dispatcher
  read it. The cost lands on people who actively configure
  the AI provider — exactly the group whose mental model
  benefits from the separation.

## Consequences

- New `include/souxmar/ai/provider_config.h` declaring
  `ProviderConfig` (provider kind + model + per-provider
  overrides) + `load_provider_config(project_dir)` returning
  a typed `variant<ProviderConfig, ProviderConfigError>`.
- `src/ai/provider_config.cpp` reads via toml++.
- `src/c-bridge/provider.cpp` consults the config — when
  the project dir contains a valid `project.ai.toml` selecting
  "ollama", the bridge invokes the existing `OllamaProvider`;
  otherwise it falls back to StubProvider (today). Sprint 15
  push 3's Anthropic forwarder adds the BYOK-anthropic case;
  Sprint 17's account portal wires the managed case.
- `.gitignore` adds `project.ai.toml`.
- `docs-site/guide/first-pipeline.md` gains a small "set up
  your provider" section pointing at the sibling file.

## Risks

- **R-019 (silent default-to-stub).** A user who creates a
  malformed `project.ai.toml` might unknowingly get StubProvider
  replies. **Mitigation:** `load_provider_config()` returns a
  typed error, and the bridge surfaces it through the chat
  panel ("project.ai.toml malformed: <detail>") — not silent
  fallback. **Likelihood: Med; Impact: Low.**
- **R-020 (config-file secret leakage).** A user who *removes*
  `project.ai.toml` from `.gitignore` and commits it might
  leak provider preferences (not keys — the keys live in the
  OS keychain). **Likelihood: Low; Impact: Low.** Mitigation:
  the schema deliberately stores no key material; the
  file is OK to share, just not by default.

## Migration

No existing project file format breaks. `project.ai.toml`
absence has always meant "use defaults"; this ADR ratifies that
*default* is still StubProvider. Sprint 15+ pushes that wire
real providers (Anthropic/Ollama/managed) only run when the
file *exists and selects them*.

## Related ADRs

- ADR-0003 (BYOK as AI default) — the policy this file
  selects between.
- ADR-0013 (signed update manifest) — same schema=1
  discriminator pattern.
- ADR-0019 (managed-AI proxy architecture) — names the
  values "managed" can mean.
- ADR-0015 (libsouxmar-crypto extraction) — `project.ai.toml`
  does NOT store keys; the OS keychain still owns those.

— Sprint 15 push 2.

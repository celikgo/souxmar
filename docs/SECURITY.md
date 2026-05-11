# SECURITY.md

souxmar's security posture is documented here. This file is the
operational counterpart to the architectural choices recorded in
ADR-0013 (signed update manifest) and ADR-0014 (release signing key
rotation). If you have found a security issue, see § "Reporting" at
the bottom.

## Trust boundaries

| Boundary | What's trusted | What isn't |
| -------- | -------------- | ---------- |
| The user's running binary | Anything in the process address space — same as any native app. | Plugin .so/.dylib/.dll files until the loader has read their manifest + DCO signature (Sprint 5+). |
| `souxmar update check`/`apply` | The bytes signed by a key in the embedded trust store. | Anything the CDN serves before the signature is verified. |
| Plugin marketplace | Conformance-passing plugins (badge surfaced in `souxmar plugin search`). | Author-uploaded binaries until conformance + sigstore identity check (Sprint 16). |
| AI provider integration | The configured BYOK endpoint (Anthropic, OpenAI, Ollama). | The user's prompt text — never logged outside `.souxmar/chat/audit.log` per ADR-0003. |

## Release signing

souxmar's release artefacts are signed with three independent
mechanisms. Each defends a different vector; together they let a user
on any of the three OSes verify "this binary came from souxmar's
release pipeline" without trusting any single root.

| Mechanism | What it protects | Where the key lives |
| --------- | ---------------- | ------------------- |
| ed25519 manifest signature (ADR-0013) | The user has the right version + the right bytes for it. | Offline HSM; CI worker holds only a build-pipeline-scoped sub-key. |
| Apple notarisation (macOS) | Gatekeeper accepts the bundle. | Apple Developer ID + app-specific password. |
| EV Authenticode (Windows) | SmartScreen reputation bypass; the user sees "souxmar Pty Ltd" not "unknown publisher". | Hardware-backed EV cert (eToken / YubiKey HSM). |
| GPG detached signature (Linux) | `apt` / `dnf` chain-of-trust. | Air-gapped signing host; ASCII-armored pubkey in `docs/releases/souxmar.asc`. |

### How a release is signed

The CI release workflow runs three parallel platform-specific jobs
followed by a single orchestrator job:

1. **Per-platform job** (macOS / Windows / Linux). Each builds the
   platform's artefact, then invokes:
   * macOS — `scripts/release/notarize-macos.sh <bundle.dmg>`. This
     submits to Apple's notarytool with bounded retry + 5-minute
     heartbeat logging (Apple's queue p99 can stall tens of
     minutes during release windows). On accept, the script staples
     the ticket so the bundle works offline.
   * Windows — `scripts/release/sign-windows.ps1 <installer.msi>`.
     Signs against the EV cert in the signing host's certificate
     store; verifies with `signtool verify /pa /v`.
   * Linux — `gpg --detach-sign --armor` against `$GPG_KEY_ID`.

   Each per-platform job uploads the signed artefact + a `*.sha256`
   sidecar.

2. **Orchestrator job** (`publish`). After all three per-platform
   jobs succeed:
   * Composes the `manifest.toml` (one `[[artifact]]` per platform,
     each pinned by `sha256` + `size`).
   * Signs the manifest with the ed25519 release key
     (`scripts/release/sign-manifest.sh`). The private key is
     fetched from the offline HSM via the air-gapped signing-host
     bridge — see § "Key rotation procedure".
   * Attaches `manifest.toml` + `manifest.toml.sig` to the GitHub
     release.
   * The auto-updater on user machines fetches both files from the
     release CDN and runs through the verifier (`souxmar::update`,
     introduced Sprint 10 push 5).

### Release signing key rotation

ed25519 signing keys are rotated yearly. The rotation procedure
deliberately produces a *separate* signed root-update event —
clients accept the new key by virtue of a new client release
embedding it, not by trusting an in-band rotation message. This
mirrors TUF's root-of-trust handling (ADR-0013 § "Pinned key id,
separate rotation event").

Procedure (per ADR-0014):

1. **T-90 days.** Generate the next year's keypair in the offline
   HSM. Record the public-key hex + key id (`release-YYYY`).
2. **T-60 days.** Open a PR that bumps the in-tree embedded
   `SOUXMAR_RELEASE_PUBKEY_HEX` + `SOUXMAR_RELEASE_PUBKEY_ID` to the
   next year's values. Merge it. Cut a release whose binaries embed
   the new key id; that release is still signed by the **current**
   key.
3. **T-30 days.** Every user has had ≥ 30 days to take the
   release with the new embedded trust root. Subsequent
   manifest-signing events use the new key.
4. **T+0.** Old key destruction: HSM operator zeroises the private
   half. Public key stays in `docs/releases/` indefinitely so an
   auditor can verify historical signatures.
5. **Emergency rotation.** If the current key is suspected
   compromised, T-60 → T+0 collapses; a hotfix release ships
   immediately embedding a new key + revoking the old via
   `revoked_key_ids = [...]` in the next manifest. Any client that
   hasn't taken the hotfix release will refuse the revoked key's
   future manifests.

### Build-time configuration

The CMake cache variable `SOUXMAR_RELEASE_PUBKEY_HEX` controls which
public key gets baked into the build's embedded trust store. The
release CI workflow overrides it via
`-DSOUXMAR_RELEASE_PUBKEY_HEX=$RELEASE_PUBKEY_HEX
 -DSOUXMAR_RELEASE_PUBKEY_ID=$RELEASE_PUBKEY_ID` at configure time;
the release workflow's pre-publish gate refuses to upload artefacts
when `build_uses_dev_key() == true`.

The default value compiled into developer builds is the
`souxmar-dev-key` placeholder — a *real but disposable* keypair
intended for local manifest-signing experiments. Regenerate yours via
`scripts/release/regenerate-dev-key.sh`. The corresponding private
key is gitignored.

## Plugin host hardening

The plugin host (`libsouxmar-plugin`) runs in-process plugins behind:
* A signal frame / SEH frame that catches segfaults and returns
  `SOUXMAR_E_PLUGIN_FAULT` to the caller (Sprint 2).
* A subprocess harness for adapters that link GPL'd code (Sprint 8
  push 1). OpenFOAM is the canonical user.
* Per-plugin heap accounting in the audit log (Sprint 9 push 9).
  Surfaces a leak indicator without blocking dispatch.

A malicious plugin can still:
* Corrupt the host process via shared mutable state (if any —
  none today; the v1 ABI is data-in/data-out per ADR-0001).
* Exhaust file descriptors / sockets.
* Burn CPU.

Mitigations for those vectors land Sprint 11+ when seccomp / job
objects enter the plugin loader.

## AI provider data handling

Per ADR-0003:
* BYOK is the free-tier default. Keys live in the OS keychain
  (Keychain Services on macOS, Credential Manager on Windows,
  libsecret on Linux), never in plaintext files.
* The audit log at `.souxmar/chat/audit.log` records *tool calls* —
  inputs + outputs + token counts. It does NOT record raw prompt
  text. A future ADR may revisit if compliance requirements push.

## Reporting

Send security reports privately to **security@souxmar.dev**. We
respond within 72 hours. Critical issues receive a CVE coordinated
through MITRE; we publish a post-mortem within 30 days of the fix
landing.

For non-vulnerability hardening suggestions, open a regular issue
or RFC per `docs/GOVERNANCE.md`.

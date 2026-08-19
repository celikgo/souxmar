# Security policy

## Reporting a vulnerability

**Use GitHub's private vulnerability reporting:**

👉 **https://github.com/celikgo/souxmar/security/advisories/new**

That form is enabled on this repository and goes straight to the maintainer
in a private advisory thread. It is the only reporting channel, and it works —
please do not open a public issue for a suspected vulnerability, and do not
email an address you found in an older revision of the docs.

If the form is unavailable to you for any reason, open a public issue saying
only *"I would like to report a security issue privately"* — with no detail —
and you will be contacted to arrange a channel.

### What to include

Whatever you have. The most useful report contains:

- the version or commit SHA you tested,
- your platform and how you built or obtained the binary,
- what an attacker gains, and
- the smallest reproduction you can manage — a pipeline YAML, a plugin, a
  malformed input file.

### What to expect

souxmar has **one maintainer**, so please calibrate accordingly:

| | |
| --- | --- |
| Acknowledgement | within 7 days |
| Initial assessment | within 14 days |
| Fix or a documented reason there will not be one | best effort, and you will be told which |
| Credit | in the advisory and the release notes, unless you ask otherwise |
| Coordinated disclosure | happy to; propose a date and it will be honoured where possible |

There is no bug-bounty programme and no payment.

## Supported versions

| Version | Supported |
| --- | --- |
| `0.9.x` | ✅ current |
| anything earlier | ❌ — no earlier version was ever released |

Fixes land on `master` and ship in the next tag. There is no long-term
support branch.

## Scope

**In scope** — anything that lets untrusted input cross a boundary it should
not:

- memory-safety bugs reachable from a mesh, geometry or pipeline file
  (`reader.*` parsers are the obvious surface),
- the plugin host's fault isolation and heap accounting
  (`src/plugin-host/`) failing to contain a hostile or broken plugin,
- the C ABI (`include/souxmar-c/`) being coerced into undefined behaviour by
  a conforming caller,
- the update verifier (`src/**/update/`) accepting an artefact it should
  reject,
- credential handling in the AI layer — a BYOK key leaking into a log, an
  audit file, a crash dump, or an outbound request to anywhere other than the
  configured provider endpoint.

**Out of scope** — things that are true by design and documented:

- **A loaded plugin is native code and runs with the host's privileges.** The
  plugin host isolates *faults*, not *malice*. Loading a plugin you do not
  trust is equivalent to running any other untrusted binary. A report that
  amounts to "a malicious plugin can do malicious things" is expected
  behaviour; a report that the host fails to contain a *crashing* plugin is
  not.
- **`services/` is not deployed.** The code under `services/` is an API
  scaffold with nothing running behind it, and every `*.souxmar.invalid`
  hostname in this repository is a placeholder on the RFC 6761 reserved TLD.
  There is no live endpoint to attack, so findings there are code review
  rather than vulnerabilities — still welcome, just as ordinary issues.
- Anything requiring the attacker to already have write access to the user's
  filesystem or process memory.
- Denial of service by feeding a deliberately enormous mesh to a mesher.

## Related documents

[`docs/SECURITY.md`](docs/SECURITY.md) covers the architectural side: trust
boundaries, the release-signing design ([ADR-0013](docs/adr/0013-signed-update-manifest.md),
[ADR-0014](docs/adr/0014-release-signing-key-rotation.md)), and credential
handling. Note that it describes the *designed* posture — read its status
notes for which parts are implemented today.

// SPDX-License-Identifier: Apache-2.0
//
// Typed Rust mirror of the `souxmar` CLI's `--json` output shapes.
// Sprint 17 push 1. ADR-0025.
//
// Every CLI subcommand that emits structured output is one variant
// of `CliOutput`. The desktop's `Command::output()` parse paths
// (per ADR-0022 — MVC-via-subprocess) feed the stdout through
// `serde_json::from_slice::<CliOutput>(...)`. A missing field
// defaults via `#[serde(default)]` so older CLI binaries
// round-trip cleanly against newer desktop builds (additive
// Tier-0 within a given schema).
//
// Bumping the schema is a Tier-2 deprecation cycle:
//   - schema=N+1 marks the removed field `#[serde(default)] +
//     #[deprecated]`.
//   - schema=N+2 removes it.
//
// Adding a new CLI subcommand: add a `kind` variant here.
// Bumping a field's type or renaming: bump the schema.

#![allow(dead_code)]

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum CliOutput {
    /// `souxmar agent list --json` (Sprint 13 push 2 / Sprint 17
    /// push 1 retrofit).
    AgentToolCatalogue {
        schema:           u32,
        contract_version: String,
        tool_count:       u32,
        tools:            Vec<AgentTool>,
    },
    /// `souxmar plugin install --json` (Sprint 16 push 4 /
    /// Sprint 17 push 1 retrofit).
    PluginInstallResult {
        schema: u32,
        status: String,
        code:   String,
        id:     String,
        #[serde(default)]
        version: String,
        #[serde(default)]
        paid:    bool,
        #[serde(default)]
        license_supplied: bool,
        #[serde(default)]
        detail:  String,
    },
    /// `souxmar update check --json` / `update apply --json` /
    /// `update rollback --json` (Sprint 10 push 6+; retrofit
    /// queued for Sprint 17 push 2).
    UpdateResult {
        schema: u32,
        status: String,
        #[serde(default)]
        from:   String,
        #[serde(default)]
        to:     String,
        #[serde(default)]
        detail: String,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentTool {
    pub name:         String,
    pub category:     String,
    pub confirmation: String,
    pub description:  String,
}

/// Parse a CLI `--json` stdout into a typed CliOutput. Returns
/// `None` for malformed JSON or unknown `kind` (the desktop
/// surfaces this as "the CLI binary is newer than this desktop
/// client supports — please upgrade").
pub fn parse(stdout: &[u8]) -> Option<CliOutput> {
    serde_json::from_slice(stdout).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plugin_install_round_trip() {
        let s = r#"{"schema":1,"kind":"plugin_install_result","status":"not_yet_wired","code":"sprint_17_pending","id":"com.acme.foo","version":">=1.0,<2.0","paid":true,"license_supplied":false,"detail":"..."}"#;
        let parsed: CliOutput = serde_json::from_str(s).unwrap();
        match parsed {
            CliOutput::PluginInstallResult { schema, id, paid, .. } => {
                assert_eq!(schema, 1);
                assert_eq!(id, "com.acme.foo");
                assert!(paid);
            }
            _ => panic!("wrong variant"),
        }
    }

    #[test]
    fn missing_optional_fields_default() {
        // Older CLI binary without `detail` field; must still parse.
        let s = r#"{"schema":1,"kind":"plugin_install_result","status":"error","code":"not_found","id":"x"}"#;
        let parsed: CliOutput = serde_json::from_str(s).unwrap();
        match parsed {
            CliOutput::PluginInstallResult { detail, version, paid, .. } => {
                assert!(detail.is_empty());
                assert!(version.is_empty());
                assert!(!paid);
            }
            _ => panic!("wrong variant"),
        }
    }

    #[test]
    fn unknown_kind_fails_clean() {
        let s = r#"{"schema":1,"kind":"future_command_from_a_newer_cli","x":1}"#;
        let parsed: Option<CliOutput> = serde_json::from_str(s).ok();
        assert!(parsed.is_none());
    }
}

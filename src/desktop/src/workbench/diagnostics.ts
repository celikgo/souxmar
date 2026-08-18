// SPDX-License-Identifier: Apache-2.0
//
// Diagnostics derived from things the workbench actually knows: the
// project's pipeline.yaml as it is on disk, the capability ids the
// plugin scan found, and the transcript of the last `souxmar run`.
//
// Every value here traces back to one of those three sources. The panels
// that consume this module (Inspector, Problems) previously rendered
// fixed content — a hard-coded cantilever stage list and a permanent
// "No problems detected." — so the rule for anything added below is that
// it must be false-negative-biased: report only what can be established
// from the inputs, and say "not checked" rather than "fine" when an
// input is missing.

/** One stage as written in pipeline.yaml. */
export interface ParsedStage {
  id:     string;
  plugin: string;
  /** 1-based line of the `- id:` row, for problem messages. */
  line:   number;
}

/** Stage execution status from the engine's own run output. */
export type StageStatus = "ok" | "cached" | "failed" | "skipped";

export interface Problem {
  severity: "error" | "warning";
  /** Short line shown in the Problems list. */
  message:  string;
  /** Where it came from — a file:line, or the engine. */
  origin:   string;
}

// ---------------------------------------------------------------------------
// pipeline.yaml
// ---------------------------------------------------------------------------

/**
 * Extract `id:` / `plugin:` pairs from the top-level `stages:` list.
 *
 * Deliberately the same shape-recognition the rest of the workbench uses
 * (YamlViewer's stage helpers): `- id:` opens a stage, a following
 * `plugin:` names it. Anything more would need a real YAML parser, and a
 * half-parser that guesses is how the panels ended up disagreeing with
 * the engine in the first place.
 */
export function parseStages(yaml: string): ParsedStage[] {
  const stages: ParsedStage[] = [];
  let cur: { id: string; line: number } | null = null;
  let plugin: string | null = null;

  const flush = () => {
    if (cur) stages.push({ id: cur.id, plugin: plugin ?? "", line: cur.line });
    cur = null;
    plugin = null;
  };

  yaml.split("\n").forEach((raw, i) => {
    const idMatch = /^\s*-\s+id:\s*(.+?)\s*(?:#.*)?$/.exec(raw);
    if (idMatch) {
      flush();
      cur = { id: idMatch[1].trim(), line: i + 1 };
      return;
    }
    const pluginMatch = /^\s*plugin:\s*(.+?)\s*(?:#.*)?$/.exec(raw);
    if (pluginMatch && cur) plugin = pluginMatch[1].trim();
  });
  flush();
  return stages;
}

/** True when the document carries the `version:` key the parser requires. */
export function hasVersionKey(yaml: string): boolean {
  return yaml.split("\n").some(line => /^version:\s*\S/.test(line));
}

// ---------------------------------------------------------------------------
// run transcript
// ---------------------------------------------------------------------------

/**
 * Per-stage status from `souxmar run` output.
 *
 * The engine prints one line per stage (src/cli/main.cpp, print_stage_line):
 *
 *     [OK      ] mesh  hash=851d6b93…
 *     [FAILED  ] solve  hash=…
 */
export function parseStageStatuses(lines: string[]): Record<string, StageStatus> {
  const out: Record<string, StageStatus> = {};
  const re = /\[(OK|CACHED|FAILED|SKIPPED)\s*\]\s+(\S+)\s+hash=/;
  for (const line of lines) {
    const m = re.exec(line);
    if (m) out[m[2]] = m[1].toLowerCase() as StageStatus;
  }
  return out;
}

// ---------------------------------------------------------------------------
// problems
// ---------------------------------------------------------------------------

export interface ProblemInputs {
  /** pipeline.yaml as read from disk; null when it could not be read. */
  yaml:         string | null;
  /** Why the read failed, when it did. */
  yamlError:    string | null;
  /** Capability ids found by the plugin scan; null when not yet loaded. */
  capabilities: string[] | null;
  /** Output lines of the most recent run, empty when nothing has run. */
  runLines:     string[];
}

/**
 * Checks that can be decided from the inputs alone. Each one below either
 * reproduces a rule the engine enforces (and so predicts a real failure)
 * or forwards a diagnostic the engine already emitted.
 */
export function collectProblems(inputs: ProblemInputs): Problem[] {
  const problems: Problem[] = [];
  const { yaml, yamlError, capabilities, runLines } = inputs;

  if (yamlError) {
    problems.push({
      severity: "error",
      message:  `Cannot read pipeline.yaml — ${yamlError}`,
      origin:   "pipeline.yaml",
    });
  }

  if (yaml !== null) {
    // The parser rejects a document with no `version:` outright
    // (src/pipeline/parser.cpp). Worth catching here because the
    // File → New project scaffold omits it.
    if (!hasVersionKey(yaml)) {
      problems.push({
        severity: "error",
        message:  "Missing `version:` — the pipeline parser rejects the file without it",
        origin:   "pipeline.yaml:1",
      });
    }

    const stages = parseStages(yaml);
    if (stages.length === 0) {
      problems.push({
        severity: "warning",
        message:  "No stages defined — a run would do nothing",
        origin:   "pipeline.yaml",
      });
    }

    const seen = new Set<string>();
    for (const stage of stages) {
      if (!stage.plugin) {
        problems.push({
          severity: "error",
          message:  `Stage \`${stage.id}\` has no \`plugin:\` — the parser requires one`,
          origin:   `pipeline.yaml:${stage.line}`,
        });
      } else if (capabilities && !capabilities.includes(stage.plugin)) {
        problems.push({
          severity: "error",
          message:  `No plugin provides \`${stage.plugin}\` (stage \`${stage.id}\`)`,
          origin:   `pipeline.yaml:${stage.line}`,
        });
      }
      if (seen.has(stage.id)) {
        problems.push({
          severity: "error",
          message:  `Duplicate stage id \`${stage.id}\` — stage ids must be unique`,
          origin:   `pipeline.yaml:${stage.line}`,
        });
      }
      seen.add(stage.id);
    }
  }

  // Anything the engine itself flagged on the last run. Forwarded
  // verbatim — the engine's wording is the authoritative one.
  for (const line of runLines) {
    const trimmed = line.trim();
    if (/^error:/i.test(trimmed)) {
      problems.push({ severity: "error", message: trimmed.replace(/^error:\s*/i, ""), origin: "last run" });
    } else if (/^warning:/i.test(trimmed)) {
      problems.push({ severity: "warning", message: trimmed.replace(/^warning:\s*/i, ""), origin: "last run" });
    }
  }

  return problems;
}

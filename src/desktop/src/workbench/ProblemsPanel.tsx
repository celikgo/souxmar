// SPDX-License-Identifier: Apache-2.0
//
// Problems tab. Runs the checks in diagnostics.ts against the open
// project and lists what they find.
//
// The tab previously rendered the fixed string "No problems detected."
// with nothing behind it — indistinguishable from a real all-clear, and
// it stayed that way for projects that could not run at all. The empty
// states below now distinguish "nothing to check" from "checked, found
// nothing", and the header says how many checks ran.

import { useEffect, useState } from "react";
import type { CSSProperties } from "react";
import { invokeCommand, type SolverCapability } from "../tauri/bridge";
import { collectProblems, type Problem } from "./diagnostics";

interface Props {
  projectId:   string;
  runLines:    string[];
  reloadToken: number;
}

export function ProblemsPanel({ projectId, runLines, reloadToken }: Props) {
  const [yaml, setYaml] = useState<string | null>(null);
  const [yamlErr, setYamlErr] = useState<string | null>(null);
  const [capabilities, setCapabilities] = useState<string[] | null>(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!projectId) {
      setYaml(null);
      setYamlErr(null);
      setCapabilities(null);
      return;
    }
    let cancelled = false;
    setLoading(true);
    (async () => {
      // pipeline.yaml — the subject of most of the checks.
      try {
        const bytes = await invokeCommand<number[]>("read_geometry_bytes", {
          projectPath: projectId,
          relPath:     "pipeline.yaml",
        });
        if (!cancelled) {
          setYaml(new TextDecoder("utf-8", { fatal: false }).decode(new Uint8Array(bytes)));
          setYamlErr(null);
        }
      } catch (err) {
        if (!cancelled) {
          setYaml(null);
          setYamlErr(String(err));
        }
      }

      // Capability ids, for the "no plugin provides this" check. On
      // failure we leave it null so that check is skipped rather than
      // reporting every stage as missing.
      try {
        const caps = await invokeCommand<SolverCapability[]>("list_capabilities", {
          projectPath: projectId,
        });
        if (!cancelled) setCapabilities(caps.map(c => c.capability));
      } catch {
        if (!cancelled) setCapabilities(null);
      }

      if (!cancelled) setLoading(false);
    })();
    return () => {
      cancelled = true;
    };
  }, [projectId, reloadToken]);

  if (!projectId) {
    return <Empty>Open a project to check its pipeline.</Empty>;
  }
  if (loading) {
    return <Empty>Checking…</Empty>;
  }

  const problems = collectProblems({ yaml, yamlError: yamlErr, capabilities, runLines });
  const errors = problems.filter(p => p.severity === "error").length;

  if (problems.length === 0) {
    return (
      <Empty>
        No problems found in <code style={monoStyle}>pipeline.yaml</code>
        {capabilities === null && " (capability check skipped — plugin scan unavailable)"}
        {runLines.length === 0 && ". Nothing has been run yet."}
      </Empty>
    );
  }

  return (
    <div style={wrapStyle}>
      <p style={headerStyle}>
        {errors > 0 && <span style={{ color: "var(--danger)" }}>{errors} error{errors === 1 ? "" : "s"}</span>}
        {errors > 0 && problems.length > errors && " · "}
        {problems.length > errors && (
          <span style={{ color: "var(--warning, #d9a441)" }}>
            {problems.length - errors} warning{problems.length - errors === 1 ? "" : "s"}
          </span>
        )}
      </p>
      <ul style={listStyle}>
        {problems.map((p, i) => (
          <ProblemRow key={`${p.origin}-${i}`} problem={p} />
        ))}
      </ul>
    </div>
  );
}

function ProblemRow({ problem }: { problem: Problem }) {
  const isError = problem.severity === "error";
  return (
    <li style={rowStyle}>
      <span
        style={{
          ...badgeStyle,
          color:      isError ? "var(--danger)" : "var(--warning, #d9a441)",
          background: isError ? "rgba(244, 33, 46, 0.10)" : "rgba(217, 164, 65, 0.10)",
        }}
      >
        {isError ? "error" : "warn"}
      </span>
      <span style={messageStyle}>{problem.message}</span>
      <span style={originStyle}>{problem.origin}</span>
    </li>
  );
}

function Empty({ children }: { children: React.ReactNode }) {
  return <div style={emptyStyle}>{children}</div>;
}

const wrapStyle: CSSProperties = {
  height: "100%",
  overflowY: "auto",
  padding: "var(--space-3) var(--space-4)",
};

const headerStyle: CSSProperties = {
  margin: "0 0 var(--space-3)",
  fontSize: 11,
  fontFamily: "var(--font-mono)",
  letterSpacing: 0.4,
  textTransform: "uppercase",
};

const listStyle: CSSProperties = {
  margin: 0,
  padding: 0,
  listStyle: "none",
  display: "flex",
  flexDirection: "column",
  gap: "var(--space-2)",
};

const rowStyle: CSSProperties = {
  display: "grid",
  gridTemplateColumns: "auto 1fr auto",
  gap: "var(--space-3)",
  alignItems: "baseline",
  padding: "var(--space-2)",
  background: "var(--bg-elevated)",
  borderRadius: "var(--radius-sm)",
  fontSize: 12,
};

const badgeStyle: CSSProperties = {
  fontFamily: "var(--font-mono)",
  fontSize: 10,
  letterSpacing: 0.5,
  textTransform: "uppercase",
  padding: "2px 6px",
  borderRadius: "var(--radius-sm)",
};

const messageStyle: CSSProperties = {
  color: "var(--fg-primary)",
  lineHeight: 1.5,
};

const originStyle: CSSProperties = {
  color: "var(--fg-tertiary)",
  fontFamily: "var(--font-mono)",
  fontSize: 11,
  whiteSpace: "nowrap",
};

const monoStyle: CSSProperties = {
  fontFamily: "var(--font-mono)",
};

const emptyStyle: CSSProperties = {
  height: "100%",
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  textAlign: "center",
  padding: "var(--space-4)",
  color: "var(--fg-tertiary)",
  fontSize: 13,
  lineHeight: 1.6,
};

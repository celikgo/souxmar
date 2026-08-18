// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — inspector panel.
// Sprint 13 push 3 — first real FFI call: when
// `pipeline_introspection` is on, the panel parses the loaded
// project's pipeline.yaml through libsouxmar-c-bridge and renders
// the stage list.
//
// The stage list used to come from a `CANTILEVER_PIPELINE_YAML`
// constant embedded in this file — on *both* the FFI and the fallback
// path — so the panel printed the open project's path above a stage
// list belonging to a different project. It now reads the project's own
// pipeline.yaml off disk (read_geometry_bytes, the same path-restricted
// command the YAML editor uses) and hands that to whichever parser is
// available. Stage status comes from the last run's transcript, and is
// "not run" when there hasn't been one, rather than a permanent
// "pending".

import { useEffect, useState } from "react";
import {
  invokeCommand,
  type BridgeFeatureSet,
  type PipelineSummary,
} from "../tauri/bridge";
import { parseStages, type StageStatus } from "./diagnostics";

interface Props {
  projectId: string;
  features:  BridgeFeatureSet;
  onOpenProject: (id: string) => void;
  /** Per-stage status from the last run, keyed by stage id. */
  stageStatus?: Record<string, StageStatus>;
  /** Bump to re-read pipeline.yaml (after a save or a run). */
  reloadToken?: number;
}

export function Inspector({
  projectId,
  features,
  onOpenProject,
  stageStatus = {},
  reloadToken = 0,
}: Props) {
  const [summary, setSummary] = useState<PipelineSummary | null>(null);
  const [summaryErr, setSummaryErr] = useState<string | null>(null);
  const [yaml, setYaml] = useState<string | null>(null);
  const [yamlErr, setYamlErr] = useState<string | null>(null);

  const openSample = async () => {
    try {
      const path = await invokeCommand<string>("open_sample_project", {
        which: "cantilever-beam",
      });
      onOpenProject(path);
    } catch (err) {
      // Surfaces clearly in the inspector — better than a silent
      // failure. Sprint 12+ swaps this for a toast-notification
      // pattern shared across panels.
      // eslint-disable-next-line no-console
      console.error("[inspector] open_sample_project failed", err);
    }
  };

  // Read the open project's pipeline.yaml. Everything the panel renders
  // below is derived from this text.
  useEffect(() => {
    if (!projectId) {
      setYaml(null);
      setYamlErr(null);
      return;
    }
    let cancelled = false;
    (async () => {
      try {
        const bytes = await invokeCommand<number[]>("read_geometry_bytes", {
          projectPath: projectId,
          relPath:     "pipeline.yaml",
        });
        if (cancelled) return;
        setYaml(new TextDecoder("utf-8", { fatal: false }).decode(new Uint8Array(bytes)));
        setYamlErr(null);
      } catch (err) {
        if (cancelled) return;
        setYaml(null);
        setYamlErr(String(err));
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [projectId, reloadToken]);

  useEffect(() => {
    if (!projectId || !features.pipeline_introspection || yaml === null) {
      setSummary(null);
      setSummaryErr(null);
      return;
    }
    let cancelled = false;
    (async () => {
      try {
        const r = await invokeCommand<PipelineSummary>("pipeline_summary", {
          projectId,
          pipelineYaml: yaml,
        });
        if (!cancelled) {
          setSummary(r);
          setSummaryErr(null);
        }
      } catch (err) {
        if (!cancelled) {
          setSummary(null);
          setSummaryErr(String(err));
        }
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [projectId, features.pipeline_introspection, yaml]);

  return (
    <div style={containerStyle}>
      <h2 style={headingStyle}>Inspector</h2>
      {!projectId ? (
        <div style={{ color: "var(--fg-secondary)" }}>
          <p style={{ margin: 0 }}>No project loaded.</p>
          <button onClick={openSample} style={openBtnStyle}>
            Open the cantilever sample
          </button>
        </div>
      ) : (
        <div>
          <p style={{ margin: 0, fontSize: 12, color: "var(--fg-secondary)" }}>
            Project path
          </p>
          <code style={pathStyle}>{projectId}</code>

          {features.pipeline_introspection && summaryErr && (
            <p style={{ marginTop: "var(--space-4)", color: "var(--fg-tertiary)", fontSize: 12 }}>
              Pipeline introspection failed: {summaryErr}
            </p>
          )}

          {yamlErr ? (
            <p style={{ marginTop: "var(--space-4)", color: "var(--fg-tertiary)", fontSize: 12 }}>
              No readable pipeline.yaml in this project — {yamlErr}
            </p>
          ) : yaml === null ? (
            <p style={{ marginTop: "var(--space-4)", color: "var(--fg-tertiary)", fontSize: 12 }}>
              Reading pipeline.yaml…
            </p>
          ) : (() => {
            // Both paths now parse *this project's* YAML: the FFI summary
            // when the bridge is wired, the local shape-parser otherwise.
            const stages = features.pipeline_introspection && summary
              ? summary.stages.map(s => ({ id: s.id, plugin: s.plugin, status: s.status }))
              : parseStages(yaml).map(s => ({
                  id:     s.id,
                  plugin: s.plugin || "(no plugin)",
                  // The engine is the only thing that can say a stage ran.
                  status: stageStatus[s.id] ?? "not run",
                }));
            return (
              <div style={{ marginTop: "var(--space-4)" }}>
                <p style={{ margin: 0, fontSize: 12, color: "var(--fg-secondary)" }}>
                  Pipeline stages ({stages.length})
                  {!features.pipeline_introspection && (
                    <span style={{ marginLeft: "var(--space-2)", color: "var(--fg-tertiary)" }}>
                      — parsed locally; FFI introspection off in this build
                    </span>
                  )}
                </p>
                {stages.length === 0 ? (
                  <p style={{ marginTop: "var(--space-2)", fontSize: 12, color: "var(--fg-tertiary)" }}>
                    No stages defined in pipeline.yaml.
                  </p>
                ) : (
                  <ul style={stageListStyle}>
                    {/* Keyed by position, not id: the panel now renders
                        whatever the user wrote, and a pipeline with two
                        stages of the same id is exactly the case the
                        Problems tab flags. */}
                    {stages.map((s, i) => (
                      <li key={`${i}-${s.id}`} style={stageItemStyle}>
                        <code style={stageIdStyle}>{s.id}</code>
                        <span style={stagePluginStyle}>{s.plugin}</span>
                        <span style={{
                          ...stageStatusStyle,
                          color: s.status === "failed"  ? "var(--danger)"
                               : s.status === "ok"      ? "var(--success, var(--accent-default))"
                               : "var(--fg-tertiary)",
                        }}>
                          {s.status}
                        </span>
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            );
          })()}
        </div>
      )}
    </div>
  );
}

const containerStyle: React.CSSProperties = {
  padding: "var(--space-4)",
};

const headingStyle: React.CSSProperties = {
  margin: 0,
  marginBottom: "var(--space-3)",
  fontSize: 14,
  fontWeight: 600,
  textTransform: "uppercase",
  letterSpacing: 0.5,
  color: "var(--fg-secondary)",
};

const openBtnStyle: React.CSSProperties = {
  marginTop: "var(--space-3)",
  padding: "var(--space-2) var(--space-3)",
  background: "var(--accent-soft)",
  border: "1px solid var(--accent-default)",
  borderRadius: "var(--radius-md)",
  color: "var(--accent-default)",
  fontSize: 13,
  cursor: "pointer",
};

const pathStyle: React.CSSProperties = {
  display: "block",
  marginTop: "var(--space-2)",
  fontSize: 12,
  color: "var(--fg-primary)",
  fontFamily: "var(--font-mono)",
  background: "var(--bg-elevated)",
  padding: "var(--space-2)",
  borderRadius: "var(--radius-sm)",
  wordBreak: "break-all",
};

const stageListStyle: React.CSSProperties = {
  marginTop: "var(--space-2)",
  paddingLeft: 0,
  listStyle: "none",
  display: "flex",
  flexDirection: "column",
  gap: "var(--space-2)",
};

const stageItemStyle: React.CSSProperties = {
  display: "grid",
  gridTemplateColumns: "1fr 2fr auto",
  gap: "var(--space-2)",
  alignItems: "baseline",
  padding: "var(--space-2)",
  background: "var(--bg-elevated)",
  borderRadius: "var(--radius-sm)",
  fontSize: 12,
};

const stageIdStyle: React.CSSProperties = {
  color: "var(--fg-primary)",
  fontFamily: "var(--font-mono)",
};

const stagePluginStyle: React.CSSProperties = {
  color: "var(--fg-secondary)",
  fontFamily: "var(--font-mono)",
  overflow: "hidden",
  textOverflow: "ellipsis",
  whiteSpace: "nowrap",
};

const stageStatusStyle: React.CSSProperties = {
  color: "var(--fg-tertiary)",
  fontSize: 11,
  textTransform: "uppercase",
  letterSpacing: 0.5,
};

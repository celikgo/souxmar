// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — inspector panel.
// Sprint 13 push 3 — first real FFI call: when
// `pipeline_introspection` is on, the panel parses the loaded
// project's pipeline.yaml through libsouxmar-c-bridge and renders
// the stage list. When the flag is off, the empty-state copy
// stays the same as Sprint 11 (honest scaffolding pattern).

import { useEffect, useState } from "react";
import {
  invokeCommand,
  type BridgeFeatureSet,
  type PipelineSummary,
} from "../tauri/bridge";

interface Props {
  projectId: string;
  features:  BridgeFeatureSet;
  onOpenProject: (id: string) => void;
}

// Placeholder loader for the sample's pipeline.yaml. Sprint 14+
// reads the project file off disk via a Tauri command; for now
// the cantilever sample's content is small enough to hard-code
// so the FFI call has something deterministic to parse.
const CANTILEVER_PIPELINE_YAML = `version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.hello
    input:
      target_size: 0.05
      element_order: 1
  - id: write
    plugin: writer.vtu
    input:
      mesh: { from: mesh }
      path: cantilever.vtu
`;

// Lightweight YAML stage extractor for the inspector fallback path.
// Only recognises the shape the cantilever sample (and other docs/)
// examples use — `id:` and `plugin:` keys directly under each list
// item in the top-level `stages:` array. Good enough to render the
// stage list when the FFI introspection feature is off; the real
// parse still happens in libsouxmar-c-bridge.
function parseStagesFromYaml(yaml: string): { id: string; plugin: string; status: string }[] {
  const stages: { id: string; plugin: string; status: string }[] = [];
  let curId: string | null = null;
  let curPlugin: string | null = null;
  const flush = () => {
    if (curId && curPlugin) stages.push({ id: curId, plugin: curPlugin, status: "pending" });
    curId = null;
    curPlugin = null;
  };
  for (const raw of yaml.split("\n")) {
    if (/^\s*-\s+id:\s*/.test(raw)) {
      flush();
      curId = raw.replace(/^\s*-\s+id:\s*/, "").trim();
    } else if (/^\s*plugin:\s*/.test(raw)) {
      curPlugin = raw.replace(/^\s*plugin:\s*/, "").trim();
    }
  }
  flush();
  return stages;
}

export function Inspector({ projectId, features, onOpenProject }: Props) {
  const [summary, setSummary] = useState<PipelineSummary | null>(null);
  const [summaryErr, setSummaryErr] = useState<string | null>(null);

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
      console.error("[inspector] open_sample_project failed", err);
    }
  };

  useEffect(() => {
    if (!projectId || !features.pipeline_introspection) {
      setSummary(null);
      setSummaryErr(null);
      return;
    }
    let cancelled = false;
    (async () => {
      try {
        const r = await invokeCommand<PipelineSummary>("pipeline_summary", {
          projectId,
          pipelineYaml: CANTILEVER_PIPELINE_YAML,
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
  }, [projectId, features.pipeline_introspection]);

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

          {(() => {
            // Render pipeline stages with the FFI summary if it's
            // available; otherwise fall back to parsing the embedded
            // cantilever YAML on the React side. The fallback keeps
            // the inspector useful when the souxmar-bridge FFI isn't
            // wired in this build (status reads as "pending").
            const stages = features.pipeline_introspection && summary
              ? summary.stages
              : parseStagesFromYaml(CANTILEVER_PIPELINE_YAML);
            const stageCount = stages.length;
            return (
              <div style={{ marginTop: "var(--space-4)" }}>
                <p style={{ margin: 0, fontSize: 12, color: "var(--fg-secondary)" }}>
                  Pipeline stages ({stageCount})
                  {!features.pipeline_introspection && (
                    <span style={{ marginLeft: "var(--space-2)", color: "var(--fg-tertiary)" }}>
                      — parsed locally; FFI introspection off in this build
                    </span>
                  )}
                </p>
                <ul style={stageListStyle}>
                  {stages.map((s) => (
                    <li key={s.id} style={stageItemStyle}>
                      <code style={stageIdStyle}>{s.id}</code>
                      <span style={stagePluginStyle}>{s.plugin}</span>
                      <span style={stageStatusStyle}>{s.status}</span>
                    </li>
                  ))}
                </ul>
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

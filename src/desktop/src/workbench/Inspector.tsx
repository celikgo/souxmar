// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — inspector panel.
//
// Shows the in-process pipeline graph + stage results + selected
// field metadata. Today it's a placeholder + "open a project"
// affordance; the real surfaces depend on the FFI bridge (Sprint
// 12+) that streams the pipeline runner's state from libsouxmar-
// pipeline into the React side.

import { invokeCommand, type BridgeFeatureSet } from "../tauri/bridge";

interface Props {
  projectId: string;
  features:  BridgeFeatureSet;
  onOpenProject: (id: string) => void;
}

export function Inspector({ projectId, features, onOpenProject }: Props) {
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
          {features.pipeline_introspection ? (
            <p style={{ marginTop: "var(--space-4)", color: "var(--fg-secondary)", fontSize: 12 }}>
              pipeline_introspection flag is on; stage-graph rendering
              not yet in this build.
            </p>
          ) : (
            <p style={{ marginTop: "var(--space-4)", color: "var(--fg-tertiary)", fontSize: 12 }}>
              Pipeline introspection arrives once the souxmar-bridge
              FFI is wired (pipeline_introspection flag off in this
              build).
            </p>
          )}
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

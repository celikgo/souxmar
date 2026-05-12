// SPDX-License-Identifier: Apache-2.0
//
// Bottom status bar — the always-on row beneath the dock panel.
// Reads the project id, the bridge-real-vs-mock flag, and (later)
// the active pipeline state. The clock on the right is just a small
// "this app is alive" indicator while real telemetry is wired up.

import { useEffect, useState } from "react";
import type { CSSProperties } from "react";
import type { BridgeFeatureSet } from "../tauri/bridge";

interface Props {
  projectId: string;
  features: BridgeFeatureSet;
}

export function StatusBar({ projectId, features }: Props) {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = window.setInterval(() => setNow(new Date()), 30_000);
    return () => window.clearInterval(t);
  }, []);

  const projectName = projectId.split("/").pop() || "no project";
  // viewport_renderer is the load-bearing real-FFI flag (see ADR-0016
  // and the post-v1.0 sprint notes). When it's off we're running on
  // the mock data path; when it flips on the Rust crate has wired in.
  const bridgeReal = features.viewport_renderer;
  const bridgeMode = bridgeReal ? "ffi" : "mock";
  const dotColour = bridgeReal ? "var(--success)" : "var(--warning)";

  return (
    <footer style={barStyle}>
      <div style={clusterStyle}>
        <span style={dotStyle(dotColour)} aria-hidden />
        <span style={mutedStyle}>bridge: {bridgeMode}</span>
      </div>
      <div style={clusterStyle}>
        <span style={mutedStyle}>project:</span>
        <span style={valueStyle}>{projectName}</span>
      </div>
      <div style={spacerStyle} />
      <div style={clusterStyle}>
        <span style={mutedStyle}>UTF-8</span>
        <span style={mutedStyle}>LF</span>
        <span style={mutedStyle}>
          {now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}
        </span>
      </div>
    </footer>
  );
}

const barStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: "var(--space-4)",
  height: 22,
  padding: "0 var(--space-3)",
  background: "var(--bg-panel)",
  borderTop: "1px solid var(--border-subtle)",
  fontSize: 11,
  color: "var(--fg-secondary)",
  userSelect: "none",
};

const clusterStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: 6,
};

const spacerStyle: CSSProperties = { flex: 1 };

const mutedStyle: CSSProperties = { color: "var(--fg-tertiary)" };
const valueStyle: CSSProperties = {
  color: "var(--fg-primary)",
  fontFamily: "var(--font-mono)",
  fontSize: 10,
};

const dotStyle = (colour: string): CSSProperties => ({
  width: 8,
  height: 8,
  borderRadius: 999,
  background: colour,
  display: "inline-block",
});

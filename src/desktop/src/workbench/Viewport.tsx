// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — viewport placeholder.
//
// The real viewport (Three.js + WebGL2 fallback / WebGPU when
// present + VTK.js for native VTU reading) lands in Sprint 12+
// once souxmar-bridge can stream Mesh handles + Field arrays from
// libsouxmar-core via FFI + shared mmap regions. This component
// renders the "no project loaded yet" state today.

import type { BridgeFeatureSet } from "../tauri/bridge";

interface Props {
  projectId: string;
  features:  BridgeFeatureSet;
}

export function Viewport({ projectId, features }: Props) {
  if (!projectId) {
    return (
      <div style={emptyStyle}>
        <p style={{ margin: 0, color: "var(--fg-secondary)" }}>
          No project loaded
        </p>
        <p style={{ marginTop: "var(--space-2)", color: "var(--fg-tertiary)", fontSize: 12 }}>
          Open one from the chat ("open the cantilever sample") or
          drag a <code>pipeline.yaml</code> here.
        </p>
      </div>
    );
  }
  if (!features.viewport_renderer) {
    return (
      <div style={emptyStyle}>
        <p style={{ margin: 0, color: "var(--fg-primary)" }}>
          Loaded: <code>{projectId}</code>
        </p>
        <p style={{ marginTop: "var(--space-2)", color: "var(--fg-tertiary)", fontSize: 12 }}>
          The viewport renderer is still being wired (souxmar-bridge
          FFI; viewport_renderer flag off in this build). The chat
          agent can already read this project's pipeline.
        </p>
      </div>
    );
  }
  // viewport_renderer is true — but the Three.js canvas component
  // hasn't shipped in this push. A future push mounts it here.
  return (
    <div style={emptyStyle}>
      <p style={{ margin: 0, color: "var(--fg-primary)" }}>
        Viewport renderer flag is on; canvas component not yet
        in this build.
      </p>
    </div>
  );
}

const emptyStyle: React.CSSProperties = {
  height: "100%",
  display: "flex",
  flexDirection: "column",
  alignItems: "center",
  justifyContent: "center",
  textAlign: "center",
  padding: "var(--space-6)",
};

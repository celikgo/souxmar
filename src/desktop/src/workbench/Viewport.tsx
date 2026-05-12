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
    // 3D renderer not in this build — show an SVG sketch of the
    // cantilever case (fixed wall at the left, distributed load
    // arrows on the top, deflected shape underneath) so the user
    // has something concrete to look at while the Three.js / VTK.js
    // pipeline catches up. The sketch is purely illustrative; the
    // real solver output ships when the renderer flag flips on.
    const projectName = projectId.split("/").filter(Boolean).pop() ?? projectId;
    return (
      <div style={previewStyle}>
        <header style={previewHeaderStyle}>
          <p style={{ margin: 0, color: "var(--fg-primary)", fontWeight: 600 }}>
            {projectName}
          </p>
          <p style={{ margin: 0, marginTop: 2, color: "var(--fg-tertiary)", fontSize: 11, fontFamily: "var(--font-mono)" }}>
            {projectId}
          </p>
        </header>
        <div style={svgWrapStyle}>
          <svg viewBox="0 0 360 200" style={{ width: "100%", maxWidth: 520, height: "auto" }}>
            {/* Wall hatching on the left (fixed support). */}
            <defs>
              <pattern id="hatch" patternUnits="userSpaceOnUse" width="8" height="8" patternTransform="rotate(45)">
                <line x1="0" y1="0" x2="0" y2="8" stroke="var(--fg-tertiary)" strokeWidth="1" />
              </pattern>
            </defs>
            <rect x="20" y="30" width="20" height="140" fill="url(#hatch)" stroke="var(--fg-secondary)" strokeWidth="1" />
            {/* Undeformed beam. */}
            <rect x="40" y="90" width="280" height="20" fill="var(--accent-soft)" stroke="var(--accent-default)" strokeWidth="1.5" />
            {/* Deflected shape (cubic-ish curve under tip load). */}
            <path
              d="M40 110 Q180 110 320 150"
              fill="none"
              stroke="var(--accent-default)"
              strokeWidth="2"
              strokeDasharray="4 4"
              opacity="0.7"
            />
            {/* Tip load arrow. */}
            <line x1="320" y1="55" x2="320" y2="88" stroke="var(--fg-primary)" strokeWidth="2" />
            <polygon points="316,82 324,82 320,92" fill="var(--fg-primary)" />
            <text x="328" y="70" fill="var(--fg-secondary)" fontSize="12" fontFamily="var(--font-mono)">F</text>
            {/* Tip label. */}
            <text x="320" y="170" fill="var(--fg-tertiary)" fontSize="11" textAnchor="middle">tip deflection (illustrative)</text>
          </svg>
        </div>
        <footer style={previewFooterStyle}>
          <p style={{ margin: 0, color: "var(--fg-tertiary)", fontSize: 12 }}>
            Illustrative sketch — the Three.js / VTK.js viewport renderer
            isn't shipped in this build (<code>viewport_renderer</code>{" "}
            flag off). The pipeline stages on the left and the chat on the
            right still work against this project.
          </p>
        </footer>
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

const previewStyle: React.CSSProperties = {
  height: "100%",
  display: "flex",
  flexDirection: "column",
  padding: "var(--space-6)",
  gap: "var(--space-4)",
};

const previewHeaderStyle: React.CSSProperties = {
  flex: "0 0 auto",
};

const svgWrapStyle: React.CSSProperties = {
  flex: 1,
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  minHeight: 0,
};

const previewFooterStyle: React.CSSProperties = {
  flex: "0 0 auto",
  borderTop: "1px solid var(--border-subtle)",
  paddingTop: "var(--space-3)",
};

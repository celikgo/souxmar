// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — viewport placeholder.
//
// The real viewport (Three.js + WebGL2 fallback / WebGPU when
// present + VTK.js for native VTU reading) lands in Sprint 12+
// once souxmar-bridge can stream Mesh handles + Field arrays from
// libsouxmar-core via FFI + shared mmap regions. This component
// renders the "no project loaded yet" state today.

interface Props {
  projectId: string;
}

export function Viewport({ projectId }: Props) {
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
  // Project loaded — but the actual renderer isn't wired yet.
  // Surfacing what's missing honestly is better than rendering a
  // fake mesh that lies about what souxmar can do.
  return (
    <div style={emptyStyle}>
      <p style={{ margin: 0, color: "var(--fg-primary)" }}>
        Loaded: <code>{projectId}</code>
      </p>
      <p style={{ marginTop: "var(--space-2)", color: "var(--fg-tertiary)", fontSize: 12 }}>
        The viewport renderer is still being wired (Sprint 12+).
        The chat agent can already read this project's pipeline.
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

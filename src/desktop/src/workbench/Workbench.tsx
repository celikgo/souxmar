// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — workbench shell.
//
// The post-onboarding home of the desktop app. Three panels at this
// stage:
//
//   ┌─────────────────────┬──────────────┐
//   │                     │              │
//   │      Viewport       │     Chat     │
//   │   (Three.js, TBD)   │ (BYOK agent) │
//   │                     │              │
//   ├─────────────────────┤              │
//   │      Inspector      │              │
//   │  (pipeline / state) │              │
//   └─────────────────────┴──────────────┘
//
// The viewport is a placeholder. Three.js + VTK.js integration lands
// in a Sprint 12+ push once the FFI bridge (`souxmar-bridge` Rust
// crate) is ready to stream Mesh + Field handles from
// libsouxmar-core to the WebGL side. The inspector reads from the
// in-process pipeline state (also via FFI; same Sprint 12+
// dependency). The chat panel is the most wired-up surface today —
// it talks to the Provider abstraction (Sprint 10 push 9) through
// the Tauri bridge.

import { useEffect, useState } from "react";
import { Chat } from "../chat/Chat";
import { Viewport } from "./Viewport";
import { Inspector } from "./Inspector";
import { useBridgeFeatures } from "../store/features";
import { useAppStore } from "../store/app";
import { invokeCommand } from "../tauri/bridge";

export function Workbench() {
  // If onboarding's sample step copied a project, start there instead
  // of the empty-viewport state. The store value is "" when the user
  // skipped the sample step — falls through to the existing empty UI.
  const initialProjectPath = useAppStore(s => s.initialProjectPath);
  const [projectId, setProjectId] = useState<string>(initialProjectPath);

  // If the workbench mounts with no projectId (e.g. onboarding was
  // completed in a prior session before the sample path was wired
  // through), opportunistically open the cantilever sample so the
  // user lands on a loaded workbench. The Tauri command is
  // idempotent — it overwrites the dest with the latest example.
  useEffect(() => {
    if (projectId) return;
    let cancelled = false;
    invokeCommand<string>("open_sample_project", { which: "cantilever-beam" })
      .then(p => { if (!cancelled && p) setProjectId(p); })
      .catch(() => { /* leave the empty state in place */ });
    return () => { cancelled = true; };
  }, [projectId]);
  // Sprint 12 push 2 — query the FFI bridge once on mount. Each
  // panel branches its rendering on the matching flag (per
  // ADR-0016) instead of inventing its own scaffolding-vs-real
  // toggle.
  const features = useBridgeFeatures();

  return (
    <div className="workbench" style={shellStyle}>
      <div style={mainStyle}>
        <div style={topLeftStyle}>
          <Viewport projectId={projectId} features={features} />
        </div>
        <div style={bottomLeftStyle}>
          <Inspector projectId={projectId} features={features}
                     onOpenProject={setProjectId} />
        </div>
      </div>
      <div style={rightStyle}>
        <Chat projectId={projectId} features={features} />
      </div>
    </div>
  );
}

const shellStyle: React.CSSProperties = {
  display: "grid",
  gridTemplateColumns: "1fr 400px",
  height: "100%",
  background: "var(--bg-canvas)",
  color: "var(--fg-primary)",
};

const mainStyle: React.CSSProperties = {
  display: "grid",
  gridTemplateRows: "1fr 280px",
  borderRight: "1px solid var(--border-subtle)",
};

const topLeftStyle: React.CSSProperties = {
  background: "var(--bg-canvas)",
  borderBottom: "1px solid var(--border-subtle)",
  overflow: "hidden",
  position: "relative",
};

const bottomLeftStyle: React.CSSProperties = {
  background: "var(--bg-panel)",
  overflow: "auto",
};

const rightStyle: React.CSSProperties = {
  background: "var(--bg-panel)",
  display: "flex",
  flexDirection: "column",
};

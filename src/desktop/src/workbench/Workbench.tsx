// SPDX-License-Identifier: Apache-2.0
//
// IDE-style workbench shell.
//
//   ┌──────────────────────────────────────────────────────────┐
//   │ TitleBar:   souxmar · cantilever-beam   [P][T][C] [▶ Run]│
//   ├────────┬────────────────────────────────┬────────────────┤
//   │        │                                │                │
//   │        │            Viewport            │                │
//   │ Project│         (placeholder)          │   AI Chat      │
//   │  Tree  │                                │                │
//   │        ├────────────────────────────────┤                │
//   │        │  Terminal | Inspector | …      │                │
//   │        │  bottom dock (toggle ⌘`)       │                │
//   ├────────┴────────────────────────────────┴────────────────┤
//   │ StatusBar: bridge:mock · project · UTF-8 · LF · 17:42     │
//   └──────────────────────────────────────────────────────────┘
//
// Each side / bottom panel is collapsible from the title-bar toggles
// (or a future ⌘1 / ⌘` / ⌘J shortcut). State lives in
// store/layout.ts; the original chat / viewport / inspector
// components are unchanged — they're slotted into the new shell.

import { useCallback, useEffect, useState } from "react";
import type { CSSProperties } from "react";
import { Chat } from "../chat/Chat";
import { Viewport } from "./Viewport";
import { ProjectTree } from "./ProjectTree";
import { Terminal } from "./Terminal";
import { TitleBar } from "./TitleBar";
import { StatusBar } from "./StatusBar";
import { Welcome } from "./Welcome";
import { NewProjectDialog } from "./dialogs/NewProjectDialog";
import { OpenProjectDialog } from "./dialogs/OpenProjectDialog";
import { ImportModelDialog } from "./dialogs/ImportModelDialog";
import { useBridgeFeatures } from "../store/features";
import { useAppStore } from "../store/app";
import { useLayoutStore } from "../store/layout";
import { invokeCommand } from "../tauri/bridge";

type DialogKind = "new" | "open" | "import" | null;

export function Workbench() {
  const initialProjectPath = useAppStore(s => s.initialProjectPath);
  const [projectId, setProjectId] = useState<string>(initialProjectPath);
  const [dialog, setDialog] = useState<DialogKind>(null);

  const features = useBridgeFeatures();
  const { leftOpen, rightOpen, bottomOpen, toggleLeft, toggleBottom, toggleRight } =
    useLayoutStore();

  // Synthetic terminal log — until a real PTY tauri command exists,
  // pressing the Run button pushes one line per "stage" so the
  // placeholder UI is alive when the user clicks around.
  const [log, setLog] = useState<string[]>([]);
  const handleRun = useCallback(() => {
    const ts = new Date().toLocaleTimeString();
    setLog(prev => [
      ...prev,
      `[${ts}] souxmar run examples/cantilever-beam/pipeline.yaml`,
      `[${ts}]   resolving plugins …`,
      `[${ts}]   stage mesh    [mesher.tetra.hello]   ok`,
      `[${ts}]   stage solve   [solver.elasticity.linear]   ok`,
      `[${ts}]   stage write   [writer.vtu]   ok`,
      `[${ts}] pipeline ok (3 stages)`,
    ]);
    if (!useLayoutStore.getState().bottomOpen) {
      toggleBottom();
    }
  }, [toggleBottom]);

  const handleOpenSample = useCallback(() => {
    invokeCommand<string>("open_sample_project", { which: "cantilever-beam" })
      .then(p => {
        if (p) setProjectId(p);
      })
      .catch(err => {
        const ts = new Date().toLocaleTimeString();
        setLog(prev => [...prev, `[${ts}] open_sample_project failed: ${String(err)}`]);
      });
  }, []);

  // Keyboard shortcuts: ⌘1 (left), ⌘J (right), ⌘` (bottom).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const mod = e.metaKey || e.ctrlKey;
      if (!mod) return;
      if (e.key === "1") {
        e.preventDefault();
        toggleLeft();
      } else if (e.key === "j" || e.key === "J") {
        e.preventDefault();
        toggleRight();
      } else if (e.key === "`") {
        e.preventDefault();
        toggleBottom();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [toggleLeft, toggleRight, toggleBottom]);

  // Grid template: a column per visible side panel, "auto auto" for
  // title + status bars, and a row split for the editor / bottom dock.
  // The middle area always renders; left, right, and bottom drop out
  // of the layout when their respective stores say "closed".
  const gridStyle: CSSProperties = {
    display: "grid",
    gridTemplateRows: "36px 1fr 22px",
    gridTemplateColumns: [
      leftOpen ? "240px" : "0px",
      "1fr",
      rightOpen ? "400px" : "0px",
    ].join(" "),
    height: "100%",
    background: "var(--bg-canvas)",
    color: "var(--fg-primary)",
    overflow: "hidden",
  };

  return (
    <div className="workbench" style={gridStyle}>
      {/* row 1 — full-width title bar */}
      <div style={titleRowStyle}>
        <TitleBar
          projectId={projectId}
          onRun={handleRun}
          onNewProject={() => setDialog("new")}
          onOpenProject={() => setDialog("open")}
          onImportModel={() => setDialog("import")}
        />
      </div>

      {/* row 2 — left, middle (viewport + bottom), right */}
      <div style={{ ...sideStyle, display: leftOpen ? "block" : "none" }}>
        <ProjectTree projectId={projectId} />
      </div>

      <div style={middleStyle}>
        <div style={viewportWrapStyle}>
          {projectId ? (
            <Viewport projectId={projectId} features={features} />
          ) : (
            <Welcome
              onNewProject={() => setDialog("new")}
              onOpenProject={() => setDialog("open")}
              onOpenSample={handleOpenSample}
            />
          )}
        </div>
        {bottomOpen && (
          <div style={bottomWrapStyle}>
            <Terminal
              projectId={projectId}
              features={features}
              onOpenProject={setProjectId}
              log={log}
            />
          </div>
        )}
      </div>

      <div style={{ ...sideStyle, display: rightOpen ? "block" : "none" }}>
        <Chat projectId={projectId} features={features} />
      </div>

      {/* row 3 — full-width status bar */}
      <div style={statusRowStyle}>
        <StatusBar projectId={projectId} features={features} />
      </div>

      {dialog === "new" && (
        <NewProjectDialog
          onClose={() => setDialog(null)}
          onCreated={p => {
            setProjectId(p);
            setDialog(null);
            const ts = new Date().toLocaleTimeString();
            setLog(prev => [...prev, `[${ts}] created project ${p}`]);
          }}
        />
      )}
      {dialog === "open" && (
        <OpenProjectDialog
          onClose={() => setDialog(null)}
          onOpened={p => {
            setProjectId(p);
            setDialog(null);
          }}
        />
      )}
      {dialog === "import" && projectId && (
        <ImportModelDialog
          projectPath={projectId}
          onClose={() => setDialog(null)}
          onImported={result => {
            setDialog(null);
            const ts = new Date().toLocaleTimeString();
            setLog(prev => {
              const next = [...prev, `[${ts}] imported ${result.dst_path}`];
              if (result.pipeline_change) {
                next.push(`[${ts}]   pipeline.yaml: ${result.pipeline_change}`);
              }
              return next;
            });
          }}
        />
      )}
    </div>
  );
}

const titleRowStyle: CSSProperties = {
  gridColumn: "1 / -1",
  gridRow: "1",
};

const sideStyle: CSSProperties = {
  gridRow: "2",
  overflow: "hidden",
};

const middleStyle: CSSProperties = {
  gridRow: "2",
  display: "flex",
  flexDirection: "column",
  minWidth: 0,
  overflow: "hidden",
};

const viewportWrapStyle: CSSProperties = {
  flex: 1,
  position: "relative",
  background: "var(--bg-canvas)",
  borderRight: "1px solid var(--border-subtle)",
  borderLeft: "1px solid var(--border-subtle)",
  overflow: "hidden",
};

const bottomWrapStyle: CSSProperties = {
  height: 240,
  borderRight: "1px solid var(--border-subtle)",
  borderLeft: "1px solid var(--border-subtle)",
  flexShrink: 0,
};

const statusRowStyle: CSSProperties = {
  gridColumn: "1 / -1",
  gridRow: "3",
};

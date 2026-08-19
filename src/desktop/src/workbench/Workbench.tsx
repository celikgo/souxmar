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

import { useCallback, useEffect, useMemo, useState } from "react";
import type { CSSProperties } from "react";
import { Chat } from "../chat/Chat";
import { Viewport } from "./Viewport";
import { ModelViewer } from "./ModelViewer";
import { MarkdownViewer, isMarkdownPath } from "./MarkdownViewer";
import { YamlViewer, isYamlPath } from "./YamlViewer";
import { ResultsViewer, isResultsPath } from "./ResultsViewer";
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
import { invokeCommand, type FileEntry, type LoadSpec, type RunOutcome } from "../tauri/bridge";
import type { Overlay } from "./ModelViewer";
import { viewableExtensions } from "./geometryLoaders";
import { parseStageStatuses } from "./diagnostics";

// Sourced from the loaders module so this stays a single source of truth.
const VIEWABLE_EXTS = viewableExtensions().map(ext => "." + ext);

type DialogKind = "new" | "open" | "import" | null;

export function Workbench() {
  const initialProjectPath = useAppStore(s => s.initialProjectPath);
  const [projectId, setProjectId] = useState<string>(initialProjectPath);
  const [dialog, setDialog] = useState<DialogKind>(null);

  const features = useBridgeFeatures();
  const [activeModel, setActiveModel] = useState<string | null>(null);
  // Bumped after anything writes into the project on disk (currently a
  // pipeline run), so the sidebar picks up new outputs without the user
  // hitting the reload button.
  const [treeEpoch, setTreeEpoch] = useState(0);
  const [loads, setLoads] = useState<LoadSpec[]>([]);
  const overlays: Overlay[] = loads.map(l =>
    l.kind === "force"
      ? { kind: "force" as const, face: l.face, vector: l.vector ?? [0, 0, 0], magnitude: 1 }
      : { kind: "fixed" as const, face: l.face }
  );

  // Auto-discover the first viewable geometry file in the project on load
  // and whenever the project root changes. The user can later switch via
  // the project tree (handled by ProjectTree's onSelectFile callback).
  useEffect(() => {
    if (!projectId) {
      setActiveModel(null);
      return;
    }
    let cancelled = false;
    invokeCommand<FileEntry>("list_project_files", { projectPath: projectId })
      .then(root => {
        if (cancelled) return;
        const found = findFirstViewable(root, projectId);
        setActiveModel(found);
      })
      .catch(() => {
        if (!cancelled) setActiveModel(null);
      });
    return () => {
      cancelled = true;
    };
  }, [projectId]);
  const { leftOpen, rightOpen, bottomOpen, toggleLeft, toggleBottom, toggleRight } =
    useLayoutStore();

  // Terminal log. Every line below comes from the `souxmar run` child
  // process dispatched by the run_pipeline command — the log is a
  // transcript, never a simulation.
  const [log, setLog] = useState<string[]>([]);
  const [running, setRunning] = useState(false);
  // Raw output of the most recent run, kept separate from the timestamped
  // display log so the Problems panel and the Inspector read the engine's
  // own text rather than something this component reformatted.
  const [runLines, setRunLines] = useState<string[]>([]);
  const stageStatus = useMemo(() => parseStageStatuses(runLines), [runLines]);

  const handleRun = useCallback(async () => {
    if (running) return;
    if (!useLayoutStore.getState().bottomOpen) {
      toggleBottom();
    }
    const stamp = () => `[${new Date().toLocaleTimeString()}]`;

    if (!projectId) {
      setLog(prev => [...prev, `${stamp()} no project open — open or create one first`]);
      return;
    }

    // Run whichever pipeline YAML the user has open, so editing a second
    // pipeline in the same project and pressing Run does what it looks
    // like it does. Falls back to the project's pipeline.yaml.
    const target = activeModel && isYamlPath(activeModel) ? activeModel : "pipeline.yaml";

    setRunning(true);
    setLog(prev => [...prev, `${stamp()} souxmar run ${target}`]);
    try {
      const outcome = await invokeCommand<RunOutcome>("run_pipeline", {
        projectPath: projectId,
        relPath:     target,
      });
      const ts = stamp();
      setRunLines(outcome.lines);
      setLog(prev => [
        ...prev,
        ...outcome.lines.map(line => `${ts}   ${line}`),
        `${ts} ${outcome.ok
          ? `pipeline ok (exit ${outcome.exit_code})`
          : `pipeline failed (exit ${outcome.exit_code})`}`,
      ]);
      // A run may have written results — and, when it failed, may have
      // written nothing. Either way re-read the tree and re-run the
      // Problems checks against what is now on disk.
      setTreeEpoch(e => e + 1);
    } catch (err) {
      setRunLines([]);
      setLog(prev => [...prev, `${stamp()} run failed: ${String(err)}`]);
    } finally {
      setRunning(false);
    }
  }, [projectId, activeModel, running, toggleBottom]);

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
          onRun={() => void handleRun()}
          running={running}
          onNewProject={() => setDialog("new")}
          onOpenProject={() => setDialog("open")}
          onImportModel={() => setDialog("import")}
        />
      </div>

      {/* row 2 — left, middle (viewport + bottom), right */}
      <div style={{ ...sideStyle, display: leftOpen ? "block" : "none" }}>
        <ProjectTree
          projectId={projectId}
          reloadToken={treeEpoch}
          onSelectFile={rel => {
            // Swap viewer when the user clicks a renderable or markdown
            // file leaf. Unsupported extensions are ignored — the
            // previous viewer stays mounted.
            const lower = rel.toLowerCase();
            const viewable =
              VIEWABLE_EXTS.some(ext => lower.endsWith(ext)) ||
              isMarkdownPath(rel) ||
              isYamlPath(rel) ||
              isResultsPath(rel);
            if (viewable) setActiveModel(rel);
          }}
        />
      </div>

      <div style={middleStyle}>
        <div style={viewportWrapStyle}>
          {!projectId ? (
            <Welcome
              onNewProject={() => setDialog("new")}
              onOpenProject={() => setDialog("open")}
              onOpenSample={handleOpenSample}
            />
          ) : activeModel ? (
            isMarkdownPath(activeModel) ? (
              <MarkdownViewer projectPath={projectId} relPath={activeModel} />
            ) : isResultsPath(activeModel) ? (
              <ResultsViewer projectPath={projectId} relPath={activeModel} />
            ) : isYamlPath(activeModel) ? (
              <YamlViewer
                projectPath={projectId}
                relPath={activeModel}
                onOpenResult={setActiveModel}
                onSaved={() => setTreeEpoch(e => e + 1)}
              />
            ) : (
              <ModelViewer
                projectPath={projectId}
                relPath={activeModel}
                overlays={overlays}
                onSimplified={newRel => {
                  setActiveModel(newRel);
                  const ts = new Date().toLocaleTimeString();
                  setLog(prev => [...prev, `[${ts}] simplified → ${newRel}`]);
                }}
              />
            )
          ) : (
            <Viewport projectId={projectId} features={features} />
          )}
        </div>
        {bottomOpen && (
          <div style={bottomWrapStyle}>
            <Terminal
              projectId={projectId}
              features={features}
              onOpenProject={setProjectId}
              log={log}
              hasModel={Boolean(activeModel)}
              loads={loads}
              setLoads={setLoads}
              onLog={line => {
                const ts = new Date().toLocaleTimeString();
                setLog(prev => [...prev, `[${ts}] ${line}`]);
              }}
              runLines={runLines}
              stageStatus={stageStatus}
              reloadToken={treeEpoch}
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
            // If the imported file is renderable, drop it straight into the
            // viewer — saves the user a project-tree click after import.
            const lower = result.rel_path.toLowerCase();
            if (VIEWABLE_EXTS.some(ext => lower.endsWith(ext))) {
              setActiveModel(result.rel_path);
            }
          }}
        />
      )}
    </div>
  );
}

function findFirstViewable(node: FileEntry, projectRoot: string): string | null {
  // Prefer a renderable 3D model; fall back to README.md, then any
  // pipeline YAML, so a freshly-opened project without geometry still
  // shows *something*.
  return (
    findMatching(node, projectRoot, n => VIEWABLE_EXTS.some(ext => n.endsWith(ext))) ??
    findMatching(node, projectRoot, isMarkdownPath) ??
    findMatching(node, projectRoot, isYamlPath)
  );
}

function findMatching(
  node:        FileEntry,
  projectRoot: string,
  match:       (lowerName: string) => boolean,
): string | null {
  if (!node.is_dir) {
    if (match(node.name.toLowerCase())) {
      const prefix = projectRoot.endsWith("/") ? projectRoot : projectRoot + "/";
      return node.path.startsWith(prefix) ? node.path.slice(prefix.length) : node.path;
    }
    return null;
  }
  for (const c of node.children ?? []) {
    const hit = findMatching(c, projectRoot, match);
    if (hit) return hit;
  }
  return null;
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

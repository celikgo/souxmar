// SPDX-License-Identifier: Apache-2.0
//
// Results panel — sits below the BCs panel in the pipeline editor.
// Lists output files declared by `writer.*` stages in the open YAML
// buffer (sniffed from `path:` keys in their input blocks). Each row
// reports the file's existence + size + mtime on disk and exposes an
// "Open" button that switches the workbench's active file to the
// result so the ResultsViewer renders it.
//
// This is buffer-only on the read side (we just parse the YAML the
// user has open) and Tauri-only on the disk side (project-rel paths
// pass through the existing list_project_files command — no new
// bridge surface needed).

import { useEffect, useMemo, useState } from "react";
import type { CSSProperties } from "react";
import { invokeCommand, type FileEntry } from "../tauri/bridge";

interface Props {
  projectPath: string;
  currentText: string;
  onOpen:      (relPath: string) => void;
}

interface ResultFile {
  declaredPath: string;        // relative path as written in pipeline.yaml
  writerKind:   string;        // e.g. "writer.vtu"
  existsAs:     string | null; // project-rel path if found on disk, else null
}

export function ResultsPanel({ projectPath, currentText, onOpen }: Props) {
  const [collapsed, setCollapsed] = useState(false);
  const [files, setFiles] = useState<Record<string, string>>({});

  const declared = useMemo(() => parseResultDeclarations(currentText), [currentText]);

  // Refresh the project tree → map of (basename → project-rel path) so
  // we can resolve a writer's path:-relative-to-cwd against where the
  // file actually lives in the project. souxmar emits to $PWD by
  // default, so when the project is the cwd, the result lands at the
  // top of the project root. Re-running the effect whenever the
  // buffer changes keeps the panel in sync if the user just hit Run.
  useEffect(() => {
    let cancelled = false;
    invokeCommand<FileEntry>("list_project_files", { projectPath })
      .then(root => {
        if (cancelled) return;
        const map: Record<string, string> = {};
        collectFiles(root, projectPath, map);
        setFiles(map);
      })
      .catch(() => {
        if (!cancelled) setFiles({});
      });
    return () => { cancelled = true; };
  }, [projectPath, currentText]);

  const rows: ResultFile[] = declared.map(d => ({
    declaredPath: d.path,
    writerKind:   d.kind,
    existsAs:     files[basename(d.path)] ?? null,
  }));

  return (
    <section style={wrapStyle} aria-label="Results">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Results</span>
          <span style={countStyle}>
            {rows.length === 0
              ? "no writer stages declared"
              : `${rows.filter(r => r.existsAs).length}/${rows.length} produced`}
          </span>
        </button>
      </header>

      {!collapsed && (
        <div style={bodyStyle}>
          {rows.length === 0 ? (
            <div style={mutedStyle}>
              No <code style={codeStyle}>writer.*</code> stages with a
              <code style={codeStyle}> path:</code> in the pipeline. Add a
              writer stage to produce results.
            </div>
          ) : (
            <div style={rowsStyle}>
              {rows.map((r, i) => {
                const ready = r.existsAs !== null;
                return (
                  <div key={i} style={rowStyle}>
                    <span style={{
                      ...badgeStyle,
                      background: ready ? "rgba(103, 217, 132, 0.18)" : "rgba(255, 212, 59, 0.18)",
                      color:      ready ? "#67d984"                  : "#ffd43b",
                    }}>
                      {ready ? "produced" : "pending"}
                    </span>
                    <span style={writerStyle}>{r.writerKind}</span>
                    <span style={pathStyle}>
                      {r.declaredPath}
                      {ready && r.existsAs !== r.declaredPath && (
                        <span style={resolvedStyle}> → {r.existsAs}</span>
                      )}
                    </span>
                    <button
                      type="button"
                      onClick={() => r.existsAs && onOpen(r.existsAs)}
                      disabled={!ready}
                      style={{
                        ...openBtnStyle,
                        opacity: ready ? 1 : 0.4,
                        cursor:  ready ? "pointer" : "not-allowed",
                      }}
                      title={ready ? "Open in results viewer" : "Run the pipeline to produce this file"}
                    >
                      Open
                    </button>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      )}
    </section>
  );
}

// ---------------------------------------------------------------------------
// Parse writer stages from the YAML buffer.

function parseResultDeclarations(yaml: string): Array<{ kind: string; path: string }> {
  // Walk stage-by-stage. A stage is a `- id: …` block; within each, look
  // for a `plugin: writer.*` line followed (within the same stage) by a
  // `path: <something>` line in the input block.
  const lines = yaml.split("\n");
  const out: Array<{ kind: string; path: string }> = [];
  let i = 0;
  while (i < lines.length) {
    const idLine = lines[i];
    if (!/^\s*-\s*id\s*:/.test(idLine)) { i++; continue; }
    // Find the extent of this stage: until the next `- id:` at the same
    // (or shallower) indent.
    const stageIndent = leadingWs(idLine);
    let end = lines.length;
    for (let j = i + 1; j < lines.length; j++) {
      const ln = lines[j];
      if (/^\s*-\s*id\s*:/.test(ln) && leadingWs(ln).length <= stageIndent.length) {
        end = j;
        break;
      }
    }
    // Search [i, end) for the plugin/path pair.
    let kind: string | null = null;
    let path: string | null = null;
    for (let j = i; j < end; j++) {
      const km = /^\s*plugin\s*:\s*['"]?(writer\.[\w.\-]+)['"]?/.exec(lines[j]);
      if (km) kind = km[1];
      const pm = /^\s*path\s*:\s*['"]?([^'"#\n]+?)['"]?\s*(#.*)?$/.exec(lines[j]);
      if (pm) path = pm[1].trim();
    }
    if (kind && path) out.push({ kind, path });
    i = end;
  }
  return out;
}

function leadingWs(s: string): string {
  const m = /^(\s*)/.exec(s);
  return m ? m[1] : "";
}

// ---------------------------------------------------------------------------
// Project-tree walk → map basename → { path, size, mtime }. Used to
// resolve a writer's declared filename against what's actually on disk.

function collectFiles(
  node:        FileEntry,
  projectRoot: string,
  out:         Record<string, string>,
) {
  if (!node.is_dir) {
    const prefix = projectRoot.endsWith("/") ? projectRoot : projectRoot + "/";
    const rel = node.path.startsWith(prefix) ? node.path.slice(prefix.length) : node.path;
    out[node.name] = rel;
    return;
  }
  for (const c of node.children ?? []) {
    collectFiles(c, projectRoot, out);
  }
}

function basename(p: string): string {
  const i = p.lastIndexOf("/");
  return i < 0 ? p : p.slice(i + 1);
}

// ---------------------------------------------------------------------------
// Styles — dim theme.

const wrapStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  background:     "var(--bg-panel, rgba(255,255,255,0.02))",
  borderBottom:   "1px solid var(--border-subtle)",
  flexShrink:     0,
};

const headerStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "center",
  height:         28,
  borderBottom:   "1px solid var(--border-subtle)",
};

const titleButtonStyle: CSSProperties = {
  display:        "inline-flex",
  alignItems:     "center",
  gap:            8,
  background:     "transparent",
  border:         "none",
  color:          "var(--fg-tertiary)",
  fontSize:       11,
  cursor:         "pointer",
  padding:        "0 var(--space-3)",
  height:         "100%",
  width:          "100%",
  textAlign:      "left",
};

const chevronStyle: CSSProperties = {
  fontSize:       9,
  color:          "var(--fg-tertiary)",
  width:          12,
};

const titleTextStyle: CSSProperties = {
  textTransform:  "uppercase",
  letterSpacing:  0.6,
  fontWeight:     600,
  color:          "var(--fg-secondary)",
};

const countStyle: CSSProperties = {
  opacity:        0.8,
  fontSize:       11,
};

const codeStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       11,
  color:          "var(--accent-default, #1d9bf0)",
};

const bodyStyle: CSSProperties = {
  padding:        "8px var(--space-3, 12px)",
  maxHeight:      160,
  overflow:       "auto",
};

const mutedStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  fontSize:       11,
};

const rowsStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  gap:            4,
};

const rowStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "center",
  gap:            8,
  padding:        "4px 6px",
  fontSize:       11,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  background:     "var(--bg-elevated, rgba(255,255,255,0.03))",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm, 4px)",
};

const badgeStyle: CSSProperties = {
  display:        "inline-block",
  padding:        "1px 6px",
  borderRadius:   "var(--radius-sm)",
  fontSize:       10,
  fontWeight:     600,
  textTransform:  "uppercase",
  letterSpacing:  0.4,
  whiteSpace:     "nowrap",
};

const writerStyle: CSSProperties = {
  color:          "var(--accent-default, #1d9bf0)",
  whiteSpace:     "nowrap",
};

const pathStyle: CSSProperties = {
  color:          "var(--fg-primary)",
  flex:           1,
  minWidth:       0,
  whiteSpace:     "nowrap",
  overflow:       "hidden",
  textOverflow:   "ellipsis",
};

const resolvedStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  marginLeft:     4,
};

const openBtnStyle: CSSProperties = {
  padding:        "3px 10px",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  background:     "var(--accent-default, #1d9bf0)",
  color:          "#fff",
  fontSize:       11,
  fontWeight:     500,
};

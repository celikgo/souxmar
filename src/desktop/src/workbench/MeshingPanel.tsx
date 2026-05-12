// SPDX-License-Identifier: Apache-2.0
//
// Mesher picker — chip strip that sits above the Solvers panel when a
// pipeline.yaml is open. Mirrors SolversPanel's surface: discovers
// mesher capabilities via the `list_mesher_capabilities` Tauri
// command (project-local → SOUXMAR_PLUGINS_PATH → in-tree
// examples/plugins, deduped in that order).
//
// Clicking a chip swaps the first `plugin: mesher.*` line in the open
// YAML buffer. Mutation is buffer-only — ⌘S writes it to disk.
// Mesher swaps preserve the upstream/downstream dependency wiring
// (mesh: { from: <mesh-id> }) because we touch only the plugin line.

import { useEffect, useState, useMemo } from "react";
import type { CSSProperties } from "react";
import { invokeCommand, type SolverCapability } from "../tauri/bridge";
import { replaceMesherPlugin } from "./YamlViewer";

interface Props {
  projectPath: string;
  currentText: string;
  onChange:    (nextText: string) => void;
}

export function MeshingPanel({ projectPath, currentText, onChange }: Props) {
  const [caps, setCaps] = useState<SolverCapability[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [collapsed, setCollapsed] = useState(false);

  useEffect(() => {
    let cancelled = false;
    setCaps(null);
    setError(null);
    invokeCommand<SolverCapability[]>("list_mesher_capabilities", { projectPath })
      .then(list => {
        if (!cancelled) setCaps(list);
      })
      .catch(err => {
        if (!cancelled) setError(String(err));
      });
    return () => {
      cancelled = true;
    };
  }, [projectPath]);

  // Detect the currently-selected mesher capability from the buffer.
  const activeCap = useMemo(() => {
    const m = /^\s*plugin:\s*['"]?(mesher\.[\w.\-]+)['"]?/m.exec(currentText);
    return m ? m[1] : null;
  }, [currentText]);

  // Group capabilities by element-shape prefix (mesher.<shape>.*) for
  // a readable layout — `tetra`, `hex`, `quad`, …
  const grouped = useMemo(() => groupByShape(caps ?? []), [caps]);

  const handlePick = (cap: SolverCapability) => {
    onChange(replaceMesherPlugin(currentText, cap.capability));
  };

  return (
    <section style={wrapStyle} aria-label="Mesher picker">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Mesher</span>
          {caps && (
            <span style={countStyle}>
              {caps.length} available
              {activeCap && <> · active: <code style={codeStyle}>{activeCap}</code></>}
            </span>
          )}
        </button>
      </header>

      {!collapsed && (
        <div style={bodyStyle}>
          {error ? (
            <div style={errorStyle}>{error}</div>
          ) : caps === null ? (
            <div style={mutedStyle}>Discovering…</div>
          ) : caps.length === 0 ? (
            <div style={mutedStyle}>
              No mesher capabilities found in <code style={codeStyle}>plugins/</code>,
              <code style={codeStyle}>SOUXMAR_PLUGINS_PATH</code>, or in-tree examples.
            </div>
          ) : (
            <div style={groupsStyle}>
              {grouped.map(g => (
                <div key={g.shape} style={groupStyle}>
                  <div style={groupTitleStyle}>{g.shape}</div>
                  <div style={chipsStyle}>
                    {g.items.map(c => {
                      const active = c.capability === activeCap;
                      return (
                        <button
                          key={c.capability}
                          type="button"
                          onClick={() => handlePick(c)}
                          style={{
                            ...chipStyle,
                            ...(active ? chipActiveStyle : null),
                          }}
                          title={`${c.plugin_name} (${c.plugin_id})\n${c.plugin_dir}`}
                        >
                          <span style={chipCapStyle}>
                            {c.capability.replace(/^mesher\.[^.]+\./, "")}
                          </span>
                          <span style={chipPluginStyle}>{c.plugin_name}</span>
                          {c.in_tree && <span style={chipBadgeStyle}>in-tree</span>}
                        </button>
                      );
                    })}
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </section>
  );
}

interface Group {
  shape: string;
  items: SolverCapability[];
}

function groupByShape(caps: SolverCapability[]): Group[] {
  const map = new Map<string, SolverCapability[]>();
  for (const c of caps) {
    // capability: mesher.tetra.hello / mesher.tetra.gmsh / ...
    const parts = c.capability.split(".");
    const shape = parts.length >= 2 ? parts[1] : "other";
    const arr = map.get(shape) ?? [];
    arr.push(c);
    map.set(shape, arr);
  }
  return Array.from(map.entries())
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([shape, items]) => ({ shape, items }));
}

// ---------------------------------------------------------------------------
// Styles — clone of SolversPanel's, kept side-by-side intentionally so
// the two panels stay visually identical without an over-fitted shared
// stylesheet. If they drift, that's a signal to factor out a Picker
// primitive; not before.

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
  maxHeight:      180,
  overflow:       "auto",
};

const groupsStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  gap:            8,
};

const groupStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "flex-start",
  gap:            10,
};

const groupTitleStyle: CSSProperties = {
  flexShrink:     0,
  width:          84,
  fontSize:       10,
  textTransform:  "uppercase",
  letterSpacing:  0.6,
  color:          "var(--fg-tertiary)",
  paddingTop:     5,
};

const chipsStyle: CSSProperties = {
  display:        "flex",
  flexWrap:       "wrap",
  gap:            6,
  flex:           1,
  minWidth:       0,
};

const chipStyle: CSSProperties = {
  display:        "inline-flex",
  alignItems:     "center",
  gap:            6,
  padding:        "3px 9px",
  borderRadius:   "var(--radius-md, 6px)",
  border:         "1px solid var(--border-subtle)",
  background:     "var(--bg-elevated, rgba(255,255,255,0.04))",
  color:          "var(--fg-primary)",
  fontSize:       11,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  cursor:         "pointer",
  whiteSpace:     "nowrap",
};

const chipActiveStyle: CSSProperties = {
  background:     "var(--accent-default, #1d9bf0)",
  borderColor:    "var(--accent-default, #1d9bf0)",
  color:          "#fff",
};

const chipCapStyle: CSSProperties = {
  fontWeight:     500,
};

const chipPluginStyle: CSSProperties = {
  fontSize:       10,
  opacity:        0.7,
  fontFamily:     "var(--font-sans, system-ui, sans-serif)",
};

const chipBadgeStyle: CSSProperties = {
  fontSize:       9,
  padding:        "1px 5px",
  borderRadius:   "var(--radius-sm, 3px)",
  background:     "rgba(255,255,255,0.08)",
  color:          "var(--fg-tertiary)",
  textTransform:  "uppercase",
  letterSpacing:  0.5,
};

const mutedStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  fontSize:       11,
};

const errorStyle: CSSProperties = {
  color:          "var(--danger, #f5365c)",
  fontSize:       11,
  whiteSpace:     "pre-wrap",
};

// SPDX-License-Identifier: Apache-2.0
//
// Solver picker — chip strip that sits above the YAML editor when a
// pipeline.yaml is open. Discovers solver capabilities by scanning
// souxmar-plugin.toml files via the `list_solver_capabilities` Tauri
// command (project-local plugins → SOUXMAR_PLUGINS_PATH → in-tree
// examples/plugins, deduplicated in that order).
//
// Clicking a chip swaps the first `kind: solver.*` line in the open
// YAML buffer. The mutation is buffer-only — the user still has to
// press Save (or ⌘S) to write it to disk.

import { useEffect, useState, useMemo } from "react";
import type { CSSProperties } from "react";
import { invokeCommand, type SolverCapability } from "../tauri/bridge";
import { replaceSolverKind } from "./YamlViewer";

interface Props {
  projectPath: string;
  /** The current editor buffer (may include unsaved edits). */
  currentText: string;
  /** Mutate the editor buffer with the new YAML. */
  onChange:    (nextText: string) => void;
}

export function SolversPanel({ projectPath, currentText, onChange }: Props) {
  const [caps, setCaps] = useState<SolverCapability[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [collapsed, setCollapsed] = useState(false);

  useEffect(() => {
    let cancelled = false;
    setCaps(null);
    setError(null);
    invokeCommand<SolverCapability[]>("list_solver_capabilities", { projectPath })
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

  // Detect the currently-selected capability from the YAML buffer.
  const activeCap = useMemo(() => {
    const m = /^\s*kind:\s*['"]?(solver\.[\w.\-]+)['"]?/m.exec(currentText);
    return m ? m[1] : null;
  }, [currentText]);

  // Group capabilities by physics prefix (solver.<physics>.*) for a
  // readable panel — collapses CalculiX's many variants into one row.
  const grouped = useMemo(() => groupByPhysics(caps ?? []), [caps]);

  const handlePick = (cap: SolverCapability) => {
    onChange(replaceSolverKind(currentText, cap.capability));
  };

  return (
    <section style={wrapStyle} aria-label="Solver picker">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Solvers</span>
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
              No solver capabilities found in <code style={codeStyle}>plugins/</code>,
              <code style={codeStyle}>SOUXMAR_PLUGINS_PATH</code>, or in-tree examples.
            </div>
          ) : (
            <div style={groupsStyle}>
              {grouped.map(g => (
                <div key={g.physics} style={groupStyle}>
                  <div style={groupTitleStyle}>{g.physics}</div>
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
                            {c.capability.replace(/^solver\.[^.]+\./, "")}
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

// ---------------------------------------------------------------------------
// Grouping — splits solver.<physics>.<variant> into rows by physics.

interface Group {
  physics: string;
  items:   SolverCapability[];
}

function groupByPhysics(caps: SolverCapability[]): Group[] {
  const map = new Map<string, SolverCapability[]>();
  for (const c of caps) {
    // capability: solver.elasticity.linear / solver.thermal.steady / ...
    const parts = c.capability.split(".");
    const physics = parts.length >= 2 ? parts[1] : "other";
    const arr = map.get(physics) ?? [];
    arr.push(c);
    map.set(physics, arr);
  }
  return Array.from(map.entries())
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([physics, items]) => ({ physics, items }));
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

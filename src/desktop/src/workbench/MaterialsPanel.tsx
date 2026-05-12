// SPDX-License-Identifier: Apache-2.0
//
// Materials picker — chip strip that sits above the YAML editor when
// a pipeline.yaml is open. Mirrors SolversPanel's surface but the
// catalog is currently *curated in the frontend* because souxmar has
// no material.* plugin capability yet (Sprint 35's materials library
// will replace this with a list_material_capabilities scan).
//
// Clicking a material rewrites the property keys
// (youngs_modulus / poisson_ratio / density / thermal_conductivity /
// specific_heat / thermal_expansion / yield_strength) inside the
// *first solver stage's* `input:` block of the open YAML buffer.
// Existing keys are replaced in place; missing keys are appended
// under the same block (preserving the discovered indent). The
// mutation is buffer-only; the user still has to Save.

import { useMemo, useState } from "react";
import type { CSSProperties } from "react";

interface Props {
  /** Current editor buffer (may include unsaved edits). */
  currentText: string;
  /** Mutate the editor buffer. */
  onChange:    (nextText: string) => void;
}

export function MaterialsPanel({ currentText, onChange }: Props) {
  const [collapsed, setCollapsed] = useState(false);

  // Detect which (if any) catalog material the buffer currently
  // matches. Match heuristic: at least 3 of the material's published
  // properties appear in the buffer with the same scalar value.
  const activeId = useMemo(() => findActiveMaterial(currentText), [currentText]);

  // Materials can only be assigned to a solver stage's `input:` block.
  // Without one, chip clicks have nowhere to land — gate the panel.
  const hasSolverStage = useMemo(
    () => /^\s*plugin\s*:\s*['"]?solver\./m.test(currentText),
    [currentText],
  );

  const grouped = useMemo(() => groupByFamily(CATALOG), []);

  const handlePick = (m: Material) => {
    if (!hasSolverStage) return;
    onChange(applyMaterial(currentText, m));
  };

  return (
    <section style={wrapStyle} aria-label="Materials picker">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Materials</span>
          <span style={countStyle}>
            {CATALOG.length} in catalog
            {activeId && <> · active: <code style={codeStyle}>{activeId}</code></>}
            {!hasSolverStage && (
              <span style={hintStyle}>needs a solver stage — pick one above</span>
            )}
            <span style={badgeStyle}>curated · plugin coming v1.5</span>
          </span>
        </button>
      </header>

      {!collapsed && (
        <div style={bodyStyle}>
          <div style={groupsStyle}>
            {grouped.map(g => (
              <div key={g.family} style={groupStyle}>
                <div style={groupTitleStyle}>{g.family}</div>
                <div style={chipsStyle}>
                  {g.items.map(m => {
                    const active = m.id === activeId;
                    return (
                      <button
                        key={m.id}
                        type="button"
                        onClick={() => handlePick(m)}
                        disabled={!hasSolverStage}
                        style={{
                          ...chipStyle,
                          ...(active ? chipActiveStyle : null),
                          ...(!hasSolverStage ? chipDisabledStyle : null),
                        }}
                        title={
                          hasSolverStage
                            ? describeMaterial(m)
                            : "Pick a solver above to enable material assignment"
                        }
                      >
                        <span style={chipNameStyle}>{m.name}</span>
                        <span style={chipMetaStyle}>
                          E={fmt(m.youngs_modulus, "Pa")} · ρ={fmt(m.density, "kg/m³")}
                        </span>
                      </button>
                    );
                  })}
                </div>
              </div>
            ))}
          </div>
        </div>
      )}
    </section>
  );
}

// ---------------------------------------------------------------------------
// Catalog. Values from public sources (ASM Handbook, NIST, MMPDS public
// data); included for editorial reference, not certification. SI units
// throughout: Pa for moduli/stress, kg/m³ for density, 1/K for
// expansion, W/(m·K) for conductivity, J/(kg·K) for specific heat.

interface Material {
  id:                    string;   // catalog id, used in the active-detection display
  name:                  string;   // human label on the chip
  family:                string;   // grouping (aluminum / steel / titanium / copper / polymer)
  youngs_modulus:        number;   // Pa
  poisson_ratio:         number;   // dimensionless
  density:               number;   // kg/m³
  yield_strength:        number;   // Pa
  thermal_conductivity:  number;   // W/(m·K)
  specific_heat:         number;   // J/(kg·K)
  thermal_expansion:     number;   // 1/K
}

const CATALOG: Material[] = [
  {
    id:                   "aluminum-6061-t6",
    name:                 "Aluminum 6061-T6",
    family:               "Aluminum",
    youngs_modulus:       69e9,
    poisson_ratio:        0.33,
    density:              2700,
    yield_strength:       276e6,
    thermal_conductivity: 167,
    specific_heat:        896,
    thermal_expansion:    23.6e-6,
  },
  {
    id:                   "aluminum-7075-t6",
    name:                 "Aluminum 7075-T6",
    family:               "Aluminum",
    youngs_modulus:       71.7e9,
    poisson_ratio:        0.33,
    density:              2810,
    yield_strength:       503e6,
    thermal_conductivity: 130,
    specific_heat:        960,
    thermal_expansion:    23.4e-6,
  },
  {
    id:                   "steel-aisi-4340",
    name:                 "Steel AISI 4340",
    family:               "Steel",
    youngs_modulus:       205e9,
    poisson_ratio:        0.29,
    density:              7850,
    yield_strength:       470e6,
    thermal_conductivity: 44.5,
    specific_heat:        475,
    thermal_expansion:    12.3e-6,
  },
  {
    id:                   "steel-304ss",
    name:                 "Stainless Steel 304",
    family:               "Steel",
    youngs_modulus:       193e9,
    poisson_ratio:        0.29,
    density:              8000,
    yield_strength:       215e6,
    thermal_conductivity: 16.2,
    specific_heat:        500,
    thermal_expansion:    17.3e-6,
  },
  {
    id:                   "titanium-ti6al4v",
    name:                 "Titanium Ti-6Al-4V",
    family:               "Titanium",
    youngs_modulus:       113.8e9,
    poisson_ratio:        0.342,
    density:              4430,
    yield_strength:       880e6,
    thermal_conductivity: 6.7,
    specific_heat:        526.3,
    thermal_expansion:    8.6e-6,
  },
  {
    id:                   "copper-c11000",
    name:                 "Copper C11000 (ETP)",
    family:               "Copper",
    youngs_modulus:       115e9,
    poisson_ratio:        0.34,
    density:              8940,
    yield_strength:       69e6,
    thermal_conductivity: 391,
    specific_heat:        385,
    thermal_expansion:    17e-6,
  },
];

// ---------------------------------------------------------------------------
// Grouping + small helpers.

interface Group {
  family: string;
  items:  Material[];
}

function groupByFamily(list: Material[]): Group[] {
  const map = new Map<string, Material[]>();
  for (const m of list) {
    const arr = map.get(m.family) ?? [];
    arr.push(m);
    map.set(m.family, arr);
  }
  return Array.from(map.entries())
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([family, items]) => ({ family, items }));
}

function describeMaterial(m: Material): string {
  return [
    `${m.name}`,
    `E       = ${m.youngs_modulus} Pa`,
    `ν       = ${m.poisson_ratio}`,
    `ρ       = ${m.density} kg/m³`,
    `σ_yield = ${m.yield_strength} Pa`,
    `k       = ${m.thermal_conductivity} W/(m·K)`,
    `c_p     = ${m.specific_heat} J/(kg·K)`,
    `α       = ${m.thermal_expansion} 1/K`,
  ].join("\n");
}

function fmt(v: number, unit: string): string {
  return `${v.toExponential(2)} ${unit}`;
}

// ---------------------------------------------------------------------------
// YAML mutation. Find the first solver stage's `input:` block and
// rewrite material property keys; append the ones that don't exist.

const PROPERTY_KEYS = [
  "youngs_modulus",
  "poisson_ratio",
  "density",
  "yield_strength",
  "thermal_conductivity",
  "specific_heat",
  "thermal_expansion",
] as const;

function applyMaterial(yaml: string, m: Material): string {
  const lines = yaml.split("\n");

  // 1) Locate the first solver stage's `input:` line.
  const inputIdx = findSolverInputLine(lines);
  if (inputIdx < 0) {
    // No solver-with-input found — append a hint comment, don't touch
    // structure. The user sees what they need to add.
    return yaml + (yaml.endsWith("\n") ? "" : "\n") +
      `# TODO: no solver stage with an input: block found; cannot apply ${m.name}.\n`;
  }

  // 2) Discover the child indent of that input: block. Walk forward
  //    until the first non-blank, non-comment line; record its indent.
  const inputIndent = leadingWhitespace(lines[inputIdx]);
  let childIndent: string | null = null;
  for (let j = inputIdx + 1; j < lines.length; j++) {
    const ln = lines[j];
    if (ln.trim() === "" || ln.trim().startsWith("#")) continue;
    const ind = leadingWhitespace(ln);
    if (ind.length <= inputIndent.length) break;
    childIndent = ind;
    break;
  }
  // No child yet — use input's indent + 2 spaces.
  if (childIndent === null) {
    childIndent = inputIndent + "  ";
  }

  // 3) Determine the [start, end) of the block (lines that share the
  //    childIndent or deeper; stops when we see a sibling of `input:`
  //    or the parent of `input:`).
  let blockEnd = lines.length;
  for (let j = inputIdx + 1; j < lines.length; j++) {
    const ln = lines[j];
    if (ln.trim() === "") continue;
    if (ln.trim().startsWith("#")) continue;
    const ind = leadingWhitespace(ln);
    if (ind.length <= inputIndent.length) {
      blockEnd = j;
      break;
    }
  }

  // 4) For each material key, either replace existing value in [start, blockEnd)
  //    or queue for append.
  const props = materialAsRecord(m);
  const present = new Set<string>();
  for (let j = inputIdx + 1; j < blockEnd; j++) {
    const ln = lines[j];
    const keyMatch = /^(\s*)([A-Za-z_][\w\-]*)(\s*:\s*)([^#\n]*?)(\s*(#.*)?)$/.exec(ln);
    if (!keyMatch) continue;
    const key = keyMatch[2];
    if (key in props) {
      const indent = keyMatch[1];
      const sep    = keyMatch[3];
      const tail   = keyMatch[5] ?? "";
      lines[j] = `${indent}${key}${sep}${formatYamlNumber(props[key])}${tail}`;
      present.add(key);
    }
  }

  // 5) Append the missing keys just before blockEnd.
  const toAppend: string[] = [];
  toAppend.push(`${childIndent}# material: ${m.name}  (applied by Materials panel)`);
  for (const k of PROPERTY_KEYS) {
    if (present.has(k)) continue;
    toAppend.push(`${childIndent}${k}: ${formatYamlNumber(props[k])}`);
  }
  lines.splice(blockEnd, 0, ...toAppend);

  return lines.join("\n");
}

function findSolverInputLine(lines: string[]): number {
  // Walk to the first `plugin: solver.*` line, then forward to its
  // sibling `input:` (same indent or deeper, within the same stage).
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*)plugin:\s*['"]?solver\.[\w.\-]+['"]?\s*(#.*)?$/.exec(lines[i]);
    if (!m) continue;
    const pluginIndent = m[1];
    // Search forward for a line with indent >= pluginIndent that starts with `input:`.
    for (let j = i + 1; j < lines.length; j++) {
      const ln = lines[j];
      if (ln.trim() === "") continue;
      const ind = leadingWhitespace(ln);
      // If we step out (less indent than the plugin line), the stage ended.
      if (ind.length < pluginIndent.length) break;
      // A sibling `- id:` would start a new stage — bail.
      if (/^\s*-\s+id\s*:/.test(ln) && ind.length <= pluginIndent.length) break;
      if (/^\s*input\s*:/.test(ln)) return j;
    }
  }
  return -1;
}

function leadingWhitespace(s: string): string {
  const m = /^(\s*)/.exec(s);
  return m ? m[1] : "";
}

function materialAsRecord(m: Material): Record<string, number> {
  return {
    youngs_modulus:       m.youngs_modulus,
    poisson_ratio:        m.poisson_ratio,
    density:              m.density,
    yield_strength:       m.yield_strength,
    thermal_conductivity: m.thermal_conductivity,
    specific_heat:        m.specific_heat,
    thermal_expansion:    m.thermal_expansion,
  };
}

function formatYamlNumber(v: number): string {
  // Prefer plain decimals for small integers (density 7850, k 167).
  // Fall back to exponential for very small/large (1.23e-5, 210e9).
  if (Number.isInteger(v) && Math.abs(v) < 1e7) return String(v);
  if (Math.abs(v) >= 1e4 || (v !== 0 && Math.abs(v) < 1e-3)) {
    // Drop trailing zeros from the mantissa for compactness.
    return v.toExponential().replace(/e\+?/, "e");
  }
  return String(v);
}

function findActiveMaterial(yaml: string): string | null {
  // Pull all `\s+key: value` pairs from the YAML; compare against
  // each catalog material. Match when at least 3 properties agree
  // (to within 0.5% relative tolerance).
  const found = new Map<string, number>();
  for (const line of yaml.split("\n")) {
    const m = /^\s+([A-Za-z_][\w\-]*)\s*:\s*([-+0-9.eE]+)\s*(#.*)?$/.exec(line);
    if (!m) continue;
    if (!(PROPERTY_KEYS as readonly string[]).includes(m[1])) continue;
    const v = Number(m[2]);
    if (Number.isFinite(v)) found.set(m[1], v);
  }
  if (found.size < 3) return null;

  for (const mat of CATALOG) {
    let hits = 0;
    for (const k of PROPERTY_KEYS) {
      const got = found.get(k);
      if (got === undefined) continue;
      const ref = mat[k];
      const tol = Math.abs(ref) * 0.005 + 1e-12;
      if (Math.abs(got - ref) <= tol) hits += 1;
    }
    if (hits >= 3) return mat.id;
  }
  return null;
}

// ---------------------------------------------------------------------------
// Styles — dim theme. Same grammar as SolversPanel.

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
  display:        "inline-flex",
  alignItems:     "center",
  gap:            10,
  opacity:        0.8,
  fontSize:       11,
};

const badgeStyle: CSSProperties = {
  fontSize:       9,
  padding:        "1px 6px",
  borderRadius:   "var(--radius-sm, 3px)",
  background:     "rgba(255,255,255,0.06)",
  color:          "var(--fg-tertiary)",
  textTransform:  "uppercase",
  letterSpacing:  0.5,
};

const codeStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       11,
  color:          "var(--accent-default, #1d9bf0)",
};

const bodyStyle: CSSProperties = {
  padding:        "8px var(--space-3, 12px)",
  maxHeight:      200,
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
  flexDirection:  "column",
  alignItems:     "flex-start",
  padding:        "4px 9px",
  borderRadius:   "var(--radius-md, 6px)",
  border:         "1px solid var(--border-subtle)",
  background:     "var(--bg-elevated, rgba(255,255,255,0.04))",
  color:          "var(--fg-primary)",
  fontSize:       11,
  cursor:         "pointer",
  whiteSpace:     "nowrap",
  minWidth:       0,
};

const chipActiveStyle: CSSProperties = {
  background:     "var(--accent-default, #1d9bf0)",
  borderColor:    "var(--accent-default, #1d9bf0)",
  color:          "#fff",
};

const chipDisabledStyle: CSSProperties = {
  opacity:        0.4,
  cursor:         "not-allowed",
};

const hintStyle: CSSProperties = {
  color:          "var(--warning, #ffd43b)",
  fontStyle:      "italic",
};

const chipNameStyle: CSSProperties = {
  fontWeight:     500,
};

const chipMetaStyle: CSSProperties = {
  fontSize:       10,
  opacity:        0.65,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

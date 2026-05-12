// SPDX-License-Identifier: Apache-2.0
//
// Boundary-conditions editor that sits above the YAML editor when a
// pipeline.yaml is open. Mirrors the Solvers / Materials panels: it
// parses the current state directly out of the YAML buffer and writes
// back into the buffer on each edit, so ⌘S persists everything
// together. The legacy bottom-dock LoadsPanel keeps its own surface
// for the model-viewer mode (force + fixed bbox-face overlays) and
// is unaffected by this panel.
//
// BC vocabulary covered:
//   Mechanical: force, pressure, fixed, displacement, gravity
//   Thermal:    thermal.temperature, thermal.flux, thermal.convection
//
// gravity is intentionally face-less — it's a global body force.
// Everything else binds to one of the six named bounding-box faces.
//
// On-disk shape — flow-style mappings under `loads:` so each row is
// one line and the diff is easy to read:
//
//   loads:
//     - {face: '+x', kind: force, vector: [0, -1000, 0]}
//     - {face: '-x', kind: fixed}
//     - {face: '+y', kind: pressure, magnitude: 1.0e5}
//     - {kind: gravity, vector: [0, -9.81, 0]}
//     - {face: '+z', kind: thermal.convection, h: 25, t_ambient: 293.15}

import { useMemo, useState } from "react";
import type { CSSProperties } from "react";

type Face = "+x" | "-x" | "+y" | "-y" | "+z" | "-z";
const FACES: Face[] = ["+x", "-x", "+y", "-y", "+z", "-z"];

type BC =
  | { face: Face; kind: "force";                vector:    [number, number, number] }
  | { face: Face; kind: "pressure";             magnitude: number }
  | { face: Face; kind: "fixed" }
  | { face: Face; kind: "displacement";         vector:    [number, number, number] }
  | { kind: "gravity";                          vector:    [number, number, number] }
  | { face: Face; kind: "thermal.temperature";  value:     number }
  | { face: Face; kind: "thermal.flux";         value:     number }
  | { face: Face; kind: "thermal.convection";   h:         number; t_ambient: number };

type BCKind = BC["kind"];

interface KindMeta {
  kind:        BCKind;
  label:       string;
  family:      "mechanical" | "thermal";
  hasFace:     boolean;
  description: string;
}

const KINDS: KindMeta[] = [
  { kind: "force",                label: "Force",         family: "mechanical", hasFace: true,  description: "Distributed force on a face (N)." },
  { kind: "pressure",             label: "Pressure",      family: "mechanical", hasFace: true,  description: "Normal pressure on a face (Pa, positive into solid)." },
  { kind: "fixed",                label: "Fixed",         family: "mechanical", hasFace: true,  description: "Zero displacement on a face." },
  { kind: "displacement",         label: "Displacement",  family: "mechanical", hasFace: true,  description: "Prescribed displacement vector on a face (m)." },
  { kind: "gravity",              label: "Gravity",       family: "mechanical", hasFace: false, description: "Global body force (m/s²); no face binding." },
  { kind: "thermal.temperature",  label: "Temperature",   family: "thermal",    hasFace: true,  description: "Fixed temperature on a face (K)." },
  { kind: "thermal.flux",         label: "Heat flux",     family: "thermal",    hasFace: true,  description: "Heat flux into a face (W/m²)." },
  { kind: "thermal.convection",   label: "Convection",    family: "thermal",    hasFace: true,  description: "Convective film coefficient h + ambient T on a face." },
];

interface Props {
  /** Current editor buffer (may include unsaved edits). */
  currentText: string;
  /** Mutate the editor buffer. */
  onChange:    (nextText: string) => void;
}

export function BoundaryConditionsPanel({ currentText, onChange }: Props) {
  const [collapsed, setCollapsed] = useState(false);

  // The parser is deliberately permissive — malformed flow-mapping
  // rows are silently dropped so the panel never blocks the user on
  // a YAML they typed by hand. If we later need user-visible parse
  // errors, surface them via a parser callback rather than a
  // setState-during-render hack.
  const bcs = useMemo(() => parseLoadsFromYaml(currentText), [currentText]);

  // BCs land in a solver stage's input block; without one, edits have
  // nowhere to go. Gate the + Add menu and surface a hint.
  const hasSolverStage = useMemo(
    () => /^\s*plugin\s*:\s*['"]?solver\./m.test(currentText),
    [currentText],
  );

  const setBCs = (next: BC[]) => onChange(writeLoadsToYaml(currentText, next));

  const addBC = (kind: BCKind) => {
    if (!hasSolverStage) return;
    const fresh = defaultFor(kind);
    setBCs([...bcs, fresh]);
  };

  const updateBC = (i: number, patch: Partial<BC>) => {
    const next = bcs.map((b, idx) =>
      idx === i ? ({ ...b, ...patch } as BC) : b,
    );
    setBCs(next);
  };

  const removeBC = (i: number) => setBCs(bcs.filter((_, idx) => idx !== i));

  return (
    <section style={wrapStyle} aria-label="Boundary conditions">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Boundary conditions</span>
          <span style={countStyle}>
            {bcs.length} active
            {!hasSolverStage && (
              <span style={hintStyle}>needs a solver stage — pick one above</span>
            )}
          </span>
        </button>
        {!collapsed && <AddMenu onAdd={addBC} disabled={!hasSolverStage} />}
      </header>

      {!collapsed && (
        <div style={bodyStyle}>
          {bcs.length === 0 ? (
            <div style={emptyStyle}>
              No BCs yet. Use <strong>+ Add</strong> to set a force, pressure,
              fixity, displacement, gravity, or thermal BC.
            </div>
          ) : (
            <div style={rowsStyle}>
              {bcs.map((b, i) => (
                <Row
                  key={i}
                  bc={b}
                  onChange={p => updateBC(i, p as Partial<BC>)}
                  onRemove={() => removeBC(i)}
                />
              ))}
            </div>
          )}
        </div>
      )}
    </section>
  );
}

// ---------------------------------------------------------------------------
// Add-BC dropdown.

function AddMenu({ onAdd, disabled }: { onAdd: (kind: BCKind) => void; disabled?: boolean }) {
  const [open, setOpen] = useState(false);
  return (
    <div style={{ position: "relative", marginRight: 8 }}>
      <button
        type="button"
        onClick={() => { if (!disabled) setOpen(o => !o); }}
        disabled={disabled}
        style={{ ...addBtnStyle, opacity: disabled ? 0.4 : 1, cursor: disabled ? "not-allowed" : "pointer" }}
        aria-haspopup="menu"
        aria-expanded={open}
        title={disabled ? "Pick a solver above to enable BCs" : undefined}
      >
        + Add
      </button>
      {open && (
        <>
          <div
            style={menuBackdropStyle}
            onClick={() => setOpen(false)}
            aria-hidden="true"
          />
          <div role="menu" style={menuStyle}>
            <div style={menuGroupLabelStyle}>Mechanical</div>
            {KINDS.filter(k => k.family === "mechanical").map(k => (
              <button
                key={k.kind}
                type="button"
                role="menuitem"
                onClick={() => { onAdd(k.kind); setOpen(false); }}
                style={menuItemStyle}
                title={k.description}
              >
                <span style={menuItemLabelStyle}>{k.label}</span>
                <span style={menuItemKindStyle}>{k.kind}</span>
              </button>
            ))}
            <div style={menuGroupLabelStyle}>Thermal</div>
            {KINDS.filter(k => k.family === "thermal").map(k => (
              <button
                key={k.kind}
                type="button"
                role="menuitem"
                onClick={() => { onAdd(k.kind); setOpen(false); }}
                style={menuItemStyle}
                title={k.description}
              >
                <span style={menuItemLabelStyle}>{k.label}</span>
                <span style={menuItemKindStyle}>{k.kind}</span>
              </button>
            ))}
          </div>
        </>
      )}
    </div>
  );
}

// ---------------------------------------------------------------------------
// Row UI — varies by BC kind.

function Row({
  bc,
  onChange,
  onRemove,
}: {
  bc:       BC;
  onChange: (patch: Partial<BC>) => void;
  onRemove: () => void;
}) {
  const meta = KINDS.find(k => k.kind === bc.kind)!;
  return (
    <div style={rowStyle}>
      <span style={{
        ...badgeStyle,
        background: bc.kind.startsWith("thermal.")
          ? "rgba(245, 158, 90, 0.18)"
          : bc.kind === "fixed"
            ? "rgba(110, 203, 255, 0.18)"
            : "rgba(248, 81, 73, 0.18)",
        color: bc.kind.startsWith("thermal.")
          ? "#f5a368"
          : bc.kind === "fixed"
            ? "#6ecbff"
            : "#ff8a85",
      }}>
        {meta.label}
      </span>

      {"face" in bc && (
        <>
          <label style={labelStyle}>face</label>
          <select
            value={bc.face}
            onChange={e => onChange({ face: e.target.value as Face })}
            style={selectStyle}
          >
            {FACES.map(f => <option key={f} value={f}>{f}</option>)}
          </select>
        </>
      )}

      {bc.kind === "force" && (
        <VecInput
          label="F (N)"
          value={bc.vector}
          onChange={v => onChange({ vector: v })}
        />
      )}

      {bc.kind === "displacement" && (
        <VecInput
          label="u (m)"
          value={bc.vector}
          onChange={v => onChange({ vector: v })}
        />
      )}

      {bc.kind === "gravity" && (
        <VecInput
          label="g (m/s²)"
          value={bc.vector}
          onChange={v => onChange({ vector: v })}
        />
      )}

      {bc.kind === "pressure" && (
        <Scalar
          label="P (Pa)"
          value={bc.magnitude}
          onChange={v => onChange({ magnitude: v })}
        />
      )}

      {bc.kind === "thermal.temperature" && (
        <Scalar
          label="T (K)"
          value={bc.value}
          onChange={v => onChange({ value: v })}
        />
      )}

      {bc.kind === "thermal.flux" && (
        <Scalar
          label="q (W/m²)"
          value={bc.value}
          onChange={v => onChange({ value: v })}
        />
      )}

      {bc.kind === "thermal.convection" && (
        <>
          <Scalar
            label="h (W/m²K)"
            value={bc.h}
            onChange={v => onChange({ h: v })}
          />
          <Scalar
            label="T∞ (K)"
            value={bc.t_ambient}
            onChange={v => onChange({ t_ambient: v })}
          />
        </>
      )}

      <button
        type="button"
        onClick={onRemove}
        style={removeBtnStyle}
        title="Remove"
        aria-label="Remove"
      >
        ×
      </button>
    </div>
  );
}

function VecInput({
  label,
  value,
  onChange,
}: {
  label:    string;
  value:    [number, number, number];
  onChange: (v: [number, number, number]) => void;
}) {
  return (
    <>
      <label style={labelStyle}>{label}</label>
      {value.map((v, axis) => (
        <input
          key={axis}
          type="number"
          value={v}
          onChange={e => {
            const next = [...value] as [number, number, number];
            next[axis] = Number(e.target.value) || 0;
            onChange(next);
          }}
          style={numInputStyle}
        />
      ))}
    </>
  );
}

function Scalar({
  label,
  value,
  onChange,
}: {
  label:    string;
  value:    number;
  onChange: (v: number) => void;
}) {
  return (
    <>
      <label style={labelStyle}>{label}</label>
      <input
        type="number"
        value={value}
        onChange={e => onChange(Number(e.target.value) || 0)}
        style={{ ...numInputStyle, width: 84 }}
      />
    </>
  );
}

// ---------------------------------------------------------------------------
// Default field values when a new BC is added.

function defaultFor(kind: BCKind): BC {
  switch (kind) {
    case "force":               return { face: "+y", kind, vector: [0, -1000, 0] };
    case "pressure":            return { face: "+y", kind, magnitude: 1.0e5 };
    case "fixed":               return { face: "-x", kind };
    case "displacement":        return { face: "+x", kind, vector: [0, 0, 0] };
    case "gravity":             return { kind, vector: [0, -9.81, 0] };
    case "thermal.temperature": return { face: "+x", kind, value: 293.15 };
    case "thermal.flux":        return { face: "+x", kind, value: 0 };
    case "thermal.convection":  return { face: "+x", kind, h: 10, t_ambient: 293.15 };
  }
}

// ---------------------------------------------------------------------------
// YAML <-> BC[] parse + emit.

function parseLoadsFromYaml(yaml: string): BC[] {
  const lines = yaml.split("\n");
  const startIdx = findLoadsLine(lines);
  if (startIdx < 0) return [];

  const baseIndent = leadingWs(lines[startIdx]);
  const out: BC[] = [];
  for (let i = startIdx + 1; i < lines.length; i++) {
    const ln = lines[i];
    if (ln.trim() === "") continue;
    if (ln.trim().startsWith("#")) continue;
    const ind = leadingWs(ln);
    if (ind.length <= baseIndent.length) break;
    const m = /^\s*-\s*\{(.+)\}\s*(#.*)?$/.exec(ln);
    if (!m) continue;
    const fields = parseFlowMap(m[1]);
    const bc = fieldsToBC(fields);
    if (bc) out.push(bc);
  }
  return out;
}

function writeLoadsToYaml(yaml: string, bcs: BC[]): string {
  const lines = yaml.split("\n");
  const loadsIdx = findLoadsLine(lines);

  if (loadsIdx >= 0) {
    // Replace existing block.
    const baseIndent = leadingWs(lines[loadsIdx]);
    let blockEnd = lines.length;
    for (let i = loadsIdx + 1; i < lines.length; i++) {
      const ln = lines[i];
      if (ln.trim() === "") continue;
      if (ln.trim().startsWith("#")) continue;
      const ind = leadingWs(ln);
      if (ind.length <= baseIndent.length) {
        blockEnd = i;
        break;
      }
    }
    const childIndent = discoverChildIndent(lines, loadsIdx, baseIndent);
    const block = [
      `${baseIndent}loads:`,
      ...(bcs.length === 0
        ? [`${childIndent}# (no BCs)`]
        : bcs.map(b => `${childIndent}- ${flowMap(b)}`)),
    ];
    return [
      ...lines.slice(0, loadsIdx),
      ...block,
      ...lines.slice(blockEnd),
    ].join("\n");
  }

  // No existing block. Insert under the first solver stage's input:
  // sub-block. If there is no input: yet, append one.
  const solverInputIdx = findSolverInputLine(lines);
  if (solverInputIdx < 0) {
    if (bcs.length === 0) return yaml;
    // No solver stage at all — give up gracefully with a TODO marker.
    return yaml + (yaml.endsWith("\n") ? "" : "\n") +
      `# TODO: no solver stage found; cannot place ${bcs.length} BC(s) automatically.\n`;
  }
  const inputIndent = leadingWs(lines[solverInputIdx]);
  const childIndent = inputIndent + "  ";
  // Append at the END of the input block so we don't reshuffle existing children.
  let blockEnd = lines.length;
  for (let i = solverInputIdx + 1; i < lines.length; i++) {
    const ln = lines[i];
    if (ln.trim() === "") continue;
    if (ln.trim().startsWith("#")) continue;
    const ind = leadingWs(ln);
    if (ind.length <= inputIndent.length) {
      blockEnd = i;
      break;
    }
  }
  const block = [
    `${childIndent}loads:`,
    ...(bcs.length === 0
      ? [`${childIndent}  # (no BCs)`]
      : bcs.map(b => `${childIndent}  - ${flowMap(b)}`)),
  ];
  return [
    ...lines.slice(0, blockEnd),
    ...block,
    ...lines.slice(blockEnd),
  ].join("\n");
}

function findLoadsLine(lines: string[]): number {
  for (let i = 0; i < lines.length; i++) {
    if (/^\s+loads\s*:\s*(#.*)?$/.test(lines[i])) return i;
  }
  return -1;
}

function findSolverInputLine(lines: string[]): number {
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*)plugin\s*:\s*['"]?solver\.[\w.\-]+['"]?/.exec(lines[i]);
    if (!m) continue;
    const indent = m[1];
    for (let j = i + 1; j < lines.length; j++) {
      const ln = lines[j];
      if (ln.trim() === "") continue;
      const ind = leadingWs(ln);
      if (ind.length < indent.length) break;
      if (/^\s*-\s+id\s*:/.test(ln) && ind.length <= indent.length) break;
      if (/^\s*input\s*:/.test(ln)) return j;
    }
  }
  return -1;
}

function discoverChildIndent(
  lines:      string[],
  parentIdx:  number,
  parentInd:  string,
): string {
  for (let j = parentIdx + 1; j < lines.length; j++) {
    const ln = lines[j];
    if (ln.trim() === "") continue;
    if (ln.trim().startsWith("#")) continue;
    const ind = leadingWs(ln);
    if (ind.length <= parentInd.length) break;
    return ind;
  }
  return parentInd + "  ";
}

function leadingWs(s: string): string {
  const m = /^(\s*)/.exec(s);
  return m ? m[1] : "";
}

/** Parse the inside of a flow mapping (no outer braces): `face: '+x', kind: force, vector: [0, -1000, 0]`. */
function parseFlowMap(inner: string): Record<string, string> {
  const tokens: string[] = [];
  let depth = 0;
  let buf = "";
  for (const ch of inner) {
    if (ch === "[" || ch === "{") depth++;
    else if (ch === "]" || ch === "}") depth--;
    if (ch === "," && depth === 0) {
      tokens.push(buf);
      buf = "";
    } else {
      buf += ch;
    }
  }
  if (buf.trim()) tokens.push(buf);

  const out: Record<string, string> = {};
  for (const tok of tokens) {
    const colon = tok.indexOf(":");
    if (colon < 0) continue;
    const k = tok.slice(0, colon).trim();
    const v = tok.slice(colon + 1).trim();
    out[k] = v;
  }
  return out;
}

function fieldsToBC(f: Record<string, string>): BC | null {
  const kind = unquote(f.kind ?? "") as BCKind;
  const face = unquote(f.face ?? "") as Face;
  const hasFace = FACES.includes(face);

  switch (kind) {
    case "force":
    case "displacement":
      if (!hasFace) return null;
      return { face, kind, vector: parseVec3(f.vector) } as BC;
    case "fixed":
      if (!hasFace) return null;
      return { face, kind };
    case "pressure":
      if (!hasFace) return null;
      return { face, kind, magnitude: numOr(f.magnitude, 0) };
    case "gravity":
      return { kind, vector: parseVec3(f.vector) };
    case "thermal.temperature":
    case "thermal.flux":
      if (!hasFace) return null;
      return { face, kind, value: numOr(f.value, 0) } as BC;
    case "thermal.convection":
      if (!hasFace) return null;
      return {
        face,
        kind,
        h:         numOr(f.h,         0),
        t_ambient: numOr(f.t_ambient, 293.15),
      };
    default:
      return null;
  }
}

function unquote(s: string): string {
  const t = s.trim();
  if ((t.startsWith("'") && t.endsWith("'")) || (t.startsWith('"') && t.endsWith('"'))) {
    return t.slice(1, -1);
  }
  return t;
}

function numOr(s: string | undefined, fallback: number): number {
  if (s === undefined) return fallback;
  const n = Number(unquote(s));
  return Number.isFinite(n) ? n : fallback;
}

function parseVec3(s: string | undefined): [number, number, number] {
  if (!s) return [0, 0, 0];
  const m = /^\s*\[(.*)\]\s*$/.exec(s);
  if (!m) return [0, 0, 0];
  const parts = m[1].split(",").map(x => Number(x.trim()));
  return [
    Number.isFinite(parts[0]) ? parts[0] : 0,
    Number.isFinite(parts[1]) ? parts[1] : 0,
    Number.isFinite(parts[2]) ? parts[2] : 0,
  ];
}

function flowMap(b: BC): string {
  const parts: string[] = [];
  if ("face" in b) parts.push(`face: '${b.face}'`);
  parts.push(`kind: ${b.kind}`);

  switch (b.kind) {
    case "force":
    case "displacement":
    case "gravity":
      parts.push(`vector: [${fmtNum(b.vector[0])}, ${fmtNum(b.vector[1])}, ${fmtNum(b.vector[2])}]`);
      break;
    case "pressure":
      parts.push(`magnitude: ${fmtNum(b.magnitude)}`);
      break;
    case "fixed":
      // No extra fields.
      break;
    case "thermal.temperature":
    case "thermal.flux":
      parts.push(`value: ${fmtNum(b.value)}`);
      break;
    case "thermal.convection":
      parts.push(`h: ${fmtNum(b.h)}`);
      parts.push(`t_ambient: ${fmtNum(b.t_ambient)}`);
      break;
  }
  return `{${parts.join(", ")}}`;
}

function fmtNum(v: number): string {
  if (Number.isInteger(v) && Math.abs(v) < 1e7) return String(v);
  return String(v);
}

// ---------------------------------------------------------------------------
// Styles — dim theme, matches Solvers / Materials.

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
  justifyContent: "space-between",
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
  flex:           1,
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
  display:        "inline-flex",
  alignItems:     "center",
  gap:            10,
};

const hintStyle: CSSProperties = {
  color:          "var(--warning, #ffd43b)",
  fontStyle:      "italic",
};

const bodyStyle: CSSProperties = {
  padding:        "8px var(--space-3, 12px)",
  maxHeight:      180,
  overflow:       "auto",
};

const emptyStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  fontSize:       11,
  padding:        "4px 0",
};

const rowsStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  gap:            4,
};

const rowStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "center",
  gap:            6,
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

const labelStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  textTransform:  "uppercase",
  fontSize:       9,
  letterSpacing:  0.4,
  whiteSpace:     "nowrap",
};

const selectStyle: CSSProperties = {
  padding:        "2px 4px",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  fontSize:       11,
  fontFamily:     "inherit",
};

const numInputStyle: CSSProperties = {
  width:          64,
  padding:        "2px 4px",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  fontSize:       11,
  fontFamily:     "inherit",
};

const removeBtnStyle: CSSProperties = {
  marginLeft:     "auto",
  width:          20,
  height:         20,
  border:         "none",
  borderRadius:   "var(--radius-sm)",
  background:     "transparent",
  color:          "var(--fg-tertiary)",
  fontSize:       14,
  cursor:         "pointer",
  lineHeight:     1,
};

const addBtnStyle: CSSProperties = {
  padding:        "3px 10px",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  background:     "var(--accent-default, #1d9bf0)",
  color:          "#fff",
  fontSize:       11,
  fontWeight:     500,
  cursor:         "pointer",
};

const menuBackdropStyle: CSSProperties = {
  position:       "fixed",
  inset:          0,
  zIndex:         50,
};

const menuStyle: CSSProperties = {
  position:       "absolute",
  top:            "100%",
  right:          0,
  marginTop:      4,
  minWidth:       200,
  zIndex:         51,
  background:     "var(--bg-panel)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-md, 6px)",
  boxShadow:      "0 8px 24px rgba(0,0,0,0.4)",
  padding:        "4px 0",
  display:        "flex",
  flexDirection:  "column",
};

const menuGroupLabelStyle: CSSProperties = {
  padding:        "4px var(--space-3, 12px)",
  fontSize:       9,
  textTransform:  "uppercase",
  letterSpacing:  0.6,
  color:          "var(--fg-tertiary)",
};

const menuItemStyle: CSSProperties = {
  display:        "flex",
  justifyContent: "space-between",
  alignItems:     "center",
  gap:            12,
  padding:        "5px var(--space-3, 12px)",
  background:     "transparent",
  border:         "none",
  color:          "var(--fg-primary)",
  fontSize:       12,
  textAlign:      "left",
  cursor:         "pointer",
};

const menuItemLabelStyle: CSSProperties = {
  fontWeight:     500,
};

const menuItemKindStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       10,
  color:          "var(--fg-tertiary)",
};

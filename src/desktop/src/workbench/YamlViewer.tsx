// SPDX-License-Identifier: Apache-2.0
//
// In-workbench YAML editor with token highlighting. Reads the file
// via `read_geometry_bytes`, lets the user edit it inline, and saves
// back through `write_text_file` (both Tauri commands are path-
// restricted to the project root). The highlighter is a transparent
// overlay behind a textarea, so the editing UX is plain-old textarea
// with caret/selection/IME handled by the platform, while the
// rendered pane carries the syntax colors.
//
// When the open file is pipeline.yaml (or any pipeline*.yaml), the
// pipeline-editor panels render above the editor so the user can drive
// the document with clicks — read+write through the same buffer so
// changes go through Save like any other edit. Mesher / Solvers /
// Materials / BCs edit the existing stages; Manufacturing and Marine
// append whole new ones via `insertPipelineStages` below.

import { useEffect, useMemo, useRef, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import { invokeCommand } from "../tauri/bridge";
import { MeshingPanel } from "./MeshingPanel";
import { SolversPanel } from "./SolversPanel";
import { MaterialsPanel } from "./MaterialsPanel";
import { BoundaryConditionsPanel } from "./BoundaryConditionsPanel";
import { ManufacturingPanel } from "./ManufacturingPanel";
import { MarinePanel } from "./MarinePanel";
import { ResultsPanel } from "./ResultsPanel";

interface Props {
  projectPath:    string;
  /** Path of the YAML file relative to the project root. */
  relPath:        string;
  /** Called when the user clicks a result file's Open button in the
   *  Results panel; Workbench swaps the viewer to that file. */
  onOpenResult?:  (relPath: string) => void;
}

export function YamlViewer({ projectPath, relPath, onOpenResult }: Props) {
  const [originalText, setOriginalText] = useState<string | null>(null);
  const [text, setText] = useState<string>("");
  const [error, setError] = useState<string | null>(null);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [savedAt, setSavedAt] = useState<number | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const preRef = useRef<HTMLPreElement>(null);

  useEffect(() => {
    let cancelled = false;
    setOriginalText(null);
    setText("");
    setError(null);
    setSaveError(null);
    setSavedAt(null);
    invokeCommand<number[]>("read_geometry_bytes", { projectPath, relPath })
      .then(bytes => {
        if (cancelled) return;
        try {
          const decoded = new TextDecoder("utf-8", { fatal: false })
            .decode(new Uint8Array(bytes));
          setOriginalText(decoded);
          setText(decoded);
        } catch (e) {
          setError(`decode: ${String(e)}`);
        }
      })
      .catch(err => {
        if (!cancelled) setError(String(err));
      });
    return () => {
      cancelled = true;
    };
  }, [projectPath, relPath]);

  const rows = useMemo(() => highlightYaml(text), [text]);
  const dirty = originalText !== null && text !== originalText;
  const isPipeline = /(?:^|\/)pipeline.*\.ya?ml$/i.test(relPath);

  const handleSave = async () => {
    if (!dirty || saving) return;
    setSaving(true);
    setSaveError(null);
    try {
      await invokeCommand<void>("write_text_file", {
        projectPath,
        relPath,
        content: text,
      });
      setOriginalText(text);
      setSavedAt(Date.now());
    } catch (e) {
      setSaveError(String(e));
    } finally {
      setSaving(false);
    }
  };

  // Cmd/Ctrl+S → save.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && (e.key === "s" || e.key === "S")) {
        e.preventDefault();
        void handleSave();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
    // handleSave closes over current text/dirty; effect intentionally
    // re-binds per change so the latest closure is used.
  });

  // Keep the highlighter overlay scrolled in sync with the textarea.
  const handleScroll = () => {
    if (!preRef.current || !textareaRef.current) return;
    preRef.current.scrollTop = textareaRef.current.scrollTop;
    preRef.current.scrollLeft = textareaRef.current.scrollLeft;
  };

  return (
    <div style={wrapStyle}>
      <header style={headerStyle}>
        <span style={pathStyle}>
          {relPath}
          {dirty && <span style={dirtyDotStyle} title="Unsaved changes">●</span>}
        </span>
        <div style={headerRightStyle}>
          {text && <span style={metaStyle}>{text.split("\n").length} lines</span>}
          {saveError && <span style={saveErrorStyle} title={saveError}>save failed</span>}
          {savedAt && !dirty && !saveError && (
            <span style={savedStyle}>saved</span>
          )}
          <button
            type="button"
            onClick={() => void handleSave()}
            disabled={!dirty || saving}
            style={{
              ...saveButtonStyle,
              opacity: !dirty || saving ? 0.5 : 1,
              cursor: !dirty || saving ? "default" : "pointer",
            }}
            title="Save (⌘S)"
          >
            {saving ? "Saving…" : "Save"}
          </button>
        </div>
      </header>

      {isPipeline && originalText !== null && (
        <>
          <MeshingPanel
            projectPath={projectPath}
            currentText={text}
            onChange={(next) => setText(next)}
          />
          <SolversPanel
            projectPath={projectPath}
            currentText={text}
            onChange={(next) => setText(next)}
          />
          <MaterialsPanel
            currentText={text}
            onChange={(next) => setText(next)}
          />
          <BoundaryConditionsPanel
            currentText={text}
            onChange={(next) => setText(next)}
          />
          {/* Manufacturing / Marine come after the BC editor: they append
              whole stages rather than editing the current solver stage,
              so they read as "extend the pipeline", not "configure it". */}
          <ManufacturingPanel
            currentText={text}
            onChange={(next) => setText(next)}
          />
          <MarinePanel
            currentText={text}
            onChange={(next) => setText(next)}
          />
          <ResultsPanel
            projectPath={projectPath}
            currentText={text}
            onOpen={(rel) => onOpenResult?.(rel)}
          />
        </>
      )}

      <div style={bodyStyle}>
        {error ? (
          <pre style={errorStyle}>{error}</pre>
        ) : originalText === null ? (
          <div style={mutedStyle}>Loading…</div>
        ) : (
          <div style={editorWrapStyle}>
            <pre ref={preRef} style={highlightStyle} aria-hidden="true">
              <code>
                {rows.map((tokens, idx) => (
                  <div key={idx} style={lineStyle}>
                    <span style={gutterStyle}>{idx + 1}</span>
                    <span style={lineContentStyle}>
                      {tokens.length === 0 ? " " : tokens}
                    </span>
                  </div>
                ))}
                {/* Trailing newline → ghost row so the textarea's
                    visible content matches the overlay exactly. */}
                <div style={lineStyle}>
                  <span style={gutterStyle}>{" "}</span>
                  <span style={lineContentStyle}>{" "}</span>
                </div>
              </code>
            </pre>
            <textarea
              ref={textareaRef}
              value={text}
              spellCheck={false}
              autoComplete="off"
              autoCorrect="off"
              autoCapitalize="off"
              onChange={e => setText(e.target.value)}
              onScroll={handleScroll}
              style={textareaStyle}
            />
          </div>
        )}
      </div>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Highlighter — operates per line, emits a flat array of styled spans.

function highlightYaml(src: string): ReactNode[][] {
  const lines = src.replace(/\r\n?/g, "\n").split("\n");
  return lines.map(highlightLine);
}

function highlightLine(line: string): ReactNode[] {
  if (/^(---|\.\.\.)\s*$/.test(line)) {
    return [span(line, "doc", 0)];
  }

  const out: ReactNode[] = [];
  let i = 0;
  let key = 0;
  const nextKey = () => key++;

  const indentMatch = /^(\s+)/.exec(line);
  if (indentMatch) {
    out.push(span(indentMatch[1], "plain", nextKey()));
    i = indentMatch[1].length;
  }

  if (line[i] === "-" && (line[i + 1] === " " || line.length === i + 1)) {
    out.push(span("-", "marker", nextKey()));
    i += 1;
    if (line[i] === " ") {
      out.push(span(" ", "plain", nextKey()));
      i += 1;
    }
  }

  if (line[i] === "#") {
    out.push(span(line.slice(i), "comment", nextKey()));
    return out;
  }

  const rest = line.slice(i);
  const keyMatch = /^("(?:\\.|[^"\\])*"|'(?:''|[^'])*'|[^:#\n]+?)(\s*:)(\s|$)/.exec(rest);
  if (keyMatch) {
    out.push(span(keyMatch[1], "key", nextKey()));
    out.push(span(keyMatch[2], "punct", nextKey()));
    i += keyMatch[1].length + keyMatch[2].length;
    if (keyMatch[3] === " ") {
      out.push(span(" ", "plain", nextKey()));
      i += 1;
    }
  }

  while (i < line.length) {
    const ch = line[i];

    if (ch === "#" && (i === 0 || /\s/.test(line[i - 1]))) {
      out.push(span(line.slice(i), "comment", nextKey()));
      return out;
    }

    if (ch === " " || ch === "\t") {
      let j = i;
      while (j < line.length && (line[j] === " " || line[j] === "\t")) j++;
      out.push(span(line.slice(i, j), "plain", nextKey()));
      i = j;
      continue;
    }

    if (ch === '"') {
      const m = /^"(?:\\.|[^"\\])*"/.exec(line.slice(i));
      if (m) {
        out.push(span(m[0], "string", nextKey()));
        i += m[0].length;
        continue;
      }
    }

    if (ch === "'") {
      const m = /^'(?:''|[^'])*'/.exec(line.slice(i));
      if (m) {
        out.push(span(m[0], "string", nextKey()));
        i += m[0].length;
        continue;
      }
    }

    if ("{}[],".includes(ch)) {
      out.push(span(ch, "punct", nextKey()));
      i += 1;
      continue;
    }
    if ((ch === "|" || ch === ">") && (i === line.length - 1 || /[-+\d\s]/.test(line[i + 1]))) {
      out.push(span(line.slice(i), "marker", nextKey()));
      return out;
    }

    if (ch === "&" || ch === "*" || ch === "!") {
      const m = /^[&*!][\w.\-/:]+/.exec(line.slice(i));
      if (m) {
        out.push(span(m[0], "anchor", nextKey()));
        i += m[0].length;
        continue;
      }
    }

    let j = i;
    while (
      j < line.length &&
      !/[\s,{}\[\]]/.test(line[j]) &&
      !(line[j] === "#" && j > 0 && /\s/.test(line[j - 1]))
    ) {
      j++;
    }
    const word = line.slice(i, j);
    out.push(span(word, classifyScalar(word), nextKey()));
    i = j;
  }

  return out;
}

function classifyScalar(word: string): TokenKind {
  if (/^(true|false|yes|no|on|off|null|~)$/i.test(word)) return "keyword";
  if (/^-?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$/.test(word)) return "number";
  if (/^0x[0-9a-fA-F]+$/.test(word)) return "number";
  if (/^(\.nan|\.inf|-?\.inf)$/i.test(word)) return "number";
  return "scalar";
}

type TokenKind =
  | "plain"
  | "key"
  | "string"
  | "scalar"
  | "number"
  | "keyword"
  | "comment"
  | "punct"
  | "marker"
  | "anchor"
  | "doc";

function span(text: string, kind: TokenKind, key: number): ReactNode {
  return (
    <span key={key} style={tokenStyles[kind]}>
      {text}
    </span>
  );
}

// ---------------------------------------------------------------------------
// Styles — dim theme, tokens from src/desktop/ui/tokens.css.

const wrapStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  height:         "100%",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  overflow:       "hidden",
};

const headerStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "center",
  justifyContent: "space-between",
  height:         32,
  padding:        "0 var(--space-3)",
  borderBottom:   "1px solid var(--border-subtle)",
  background:     "var(--bg-panel)",
  fontSize:       11,
  color:          "var(--fg-tertiary)",
  flexShrink:     0,
  gap:            12,
};

const pathStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  whiteSpace:     "nowrap",
  overflow:       "hidden",
  textOverflow:   "ellipsis",
  display:        "flex",
  alignItems:     "center",
  gap:            6,
  minWidth:       0,
};

const dirtyDotStyle: CSSProperties = {
  color:    "var(--accent-default, #1d9bf0)",
  fontSize: 10,
};

const headerRightStyle: CSSProperties = {
  display:    "flex",
  alignItems: "center",
  gap:        10,
  flexShrink: 0,
};

const metaStyle: CSSProperties = {
  whiteSpace: "nowrap",
};

const savedStyle: CSSProperties = {
  color:      "var(--success, #67d984)",
  whiteSpace: "nowrap",
};

const saveErrorStyle: CSSProperties = {
  color:      "var(--danger, #f5365c)",
  whiteSpace: "nowrap",
  cursor:     "help",
};

const saveButtonStyle: CSSProperties = {
  fontSize:       11,
  padding:        "3px 10px",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm, 4px)",
  background:     "var(--accent-default, #1d9bf0)",
  color:          "#fff",
  fontWeight:     500,
};

const bodyStyle: CSSProperties = {
  flex:           1,
  overflow:       "hidden",
  position:       "relative",
};

const editorWrapStyle: CSSProperties = {
  position:       "relative",
  height:         "100%",
  width:          "100%",
};

// Shared pre/textarea metrics — fonts and line heights MUST match so
// the overlay aligns with the caret. Padding lives only on the
// container so both layers scroll over identical content geometry.

const sharedCodeStyle: CSSProperties = {
  margin:         0,
  padding:        "8px 0",
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       12.5,
  lineHeight:     "1.55em",
  background:     "transparent",
  whiteSpace:     "pre",
  tabSize:        2,
  letterSpacing:  0,
};

const highlightStyle: CSSProperties = {
  ...sharedCodeStyle,
  position:       "absolute",
  inset:          0,
  overflow:       "auto",
  pointerEvents:  "none",
};

const textareaStyle: CSSProperties = {
  ...sharedCodeStyle,
  position:       "absolute",
  inset:          0,
  width:          "100%",
  height:         "100%",
  border:         "none",
  outline:        "none",
  resize:         "none",
  paddingLeft:    44 + 10, // gutterStyle width + paddingRight
  color:          "transparent",
  caretColor:     "var(--fg-primary)",
  background:     "transparent",
  // Keep selection visible against the dim canvas.
  // (Tauri's webview honors ::selection from a stylesheet; the inline
  //  variant here is the safest fallback for an isolated component.)
};

const lineStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "flex-start",
};

const gutterStyle: CSSProperties = {
  width:          44,
  paddingRight:   10,
  textAlign:      "right",
  color:          "var(--fg-tertiary)",
  userSelect:     "none",
  opacity:        0.55,
  flexShrink:     0,
};

const lineContentStyle: CSSProperties = {
  flex:           1,
  minWidth:       0,
};

const mutedStyle: CSSProperties = {
  padding:        "var(--space-3, 10px)",
  color:          "var(--fg-tertiary)",
  fontSize:       12,
};

const errorStyle: CSSProperties = {
  padding:        "var(--space-3, 10px)",
  color:          "var(--danger, #f5365c)",
  fontSize:       12,
  whiteSpace:     "pre-wrap",
};

const tokenStyles: Record<TokenKind, CSSProperties> = {
  plain:    { color: "var(--fg-primary)" },
  key:      { color: "var(--accent-default, #1d9bf0)", fontWeight: 500 },
  string:   { color: "#a3e08a" },
  scalar:   { color: "var(--fg-primary)" },
  number:   { color: "#f0a060" },
  keyword:  { color: "#c594ff", fontWeight: 500 },
  comment:  { color: "var(--fg-tertiary)", fontStyle: "italic" },
  punct:    { color: "var(--fg-tertiary)" },
  marker:   { color: "#f0a060" },
  anchor:   { color: "#f0a060", fontStyle: "italic" },
  doc:      { color: "var(--fg-tertiary)", fontStyle: "italic" },
};

// ---------------------------------------------------------------------------
// Extension membership — exported so Workbench can dispatch viewers.

const YAML_EXTS = new Set(["yaml", "yml"]);

export function isYamlPath(relPath: string): boolean {
  const lower = relPath.toLowerCase();
  const dot = lower.lastIndexOf(".");
  if (dot < 0) return false;
  return YAML_EXTS.has(lower.slice(dot + 1));
}

// Exported for SolversPanel's swap-the-plugin-line logic. The souxmar
// pipeline schema names the capability field `plugin:` (see
// examples/*/pipeline.yaml). Keep this in sync with the schema; don't
// confuse with the toml `[plugin]` section in souxmar-plugin.toml.
//
// If no existing solver stage is found, insert a new `- id: solve`
// stage in the `stages:` block — before the first writer stage when
// possible, else at the end. That keeps the DAG dependency order
// natural (mesh → solve → write) without asking the user to edit YAML
// by hand. The new stage carries an `input: { mesh: { from: mesh } }`
// block so Materials/BC panels have a place to write into.
export function replaceSolverPlugin(yaml: string, newCapability: string): string {
  const lines = yaml.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*plugin:\s*)(['"]?)solver\.[\w.\-]+(['"]?)(\s*(#.*)?)$/.exec(lines[i]);
    if (m) {
      const quote = m[2] || "";
      const tail  = m[4] || "";
      lines[i] = `${m[1]}${quote}${newCapability}${quote}${tail}`;
      return lines.join("\n");
    }
  }
  return insertSolverStage(lines, newCapability).join("\n");
}

// Find the upstream-mesh stage id (best-effort) and the index of the
// first writer stage (so we can insert before it). Append a new stage
// using the conventional 2-space stage indent + 4-space child indent.
function insertSolverStage(lines: string[], newCapability: string): string[] {
  // Discover the mesh stage id. Heuristic: first stage whose plugin
  // starts with `mesher.`. Fall back to "mesh" — the convention used by
  // every in-tree example.
  let meshId = "mesh";
  for (let i = 0; i < lines.length; i++) {
    const m = /^\s*-\s*id\s*:\s*['"]?([\w\-]+)['"]?\s*(#.*)?$/.exec(lines[i]);
    if (!m) continue;
    // Look ahead for this stage's plugin: line.
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      const pm = /^\s*plugin\s*:\s*['"]?(mesher\.[\w.\-]+)['"]?/.exec(lines[j]);
      if (pm) { meshId = m[1]; break; }
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
  }

  // Locate the insertion point: index of the first writer stage's
  // `- id:` header line, else end-of-file.
  let writerIdx = -1;
  for (let i = 0; i < lines.length; i++) {
    if (!/^\s*-\s*id\s*:/.test(lines[i])) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      if (/^\s*plugin\s*:\s*['"]?writer\.[\w.\-]+/.test(lines[j])) {
        writerIdx = i;
        break;
      }
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
    if (writerIdx >= 0) break;
  }

  // Discover the existing stage indent by scanning the first `- id:` we
  // see; fall back to two spaces, matching every in-tree example.
  let stageIndent = "  ";
  for (const ln of lines) {
    const m = /^(\s*)-\s*id\s*:/.exec(ln);
    if (m) { stageIndent = m[1]; break; }
  }
  const childIndent = stageIndent + "  ";
  const grandchildIndent = childIndent + "  ";

  const stageBlock = [
    "",
    `${stageIndent}- id: solve`,
    `${childIndent}plugin: ${newCapability}`,
    `${childIndent}input:`,
    `${grandchildIndent}mesh: { from: ${meshId} }`,
  ];

  if (writerIdx >= 0) {
    return [
      ...lines.slice(0, writerIdx),
      ...stageBlock,
      "",
      ...lines.slice(writerIdx),
    ];
  }
  return [
    ...lines,
    ...stageBlock,
  ];
}

// Swap the first `plugin: mesher.*` line in the buffer. Mesher stages
// pre-exist in every in-tree example (the mesher is the first thing a
// pipeline does), so we don't bother inserting a stage when one is
// missing — that case only arises if the user has stripped the mesh
// stage manually.
export function replaceMesherPlugin(yaml: string, newCapability: string): string {
  const lines = yaml.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*plugin:\s*)(['"]?)mesher\.[\w.\-]+(['"]?)(\s*(#.*)?)$/.exec(lines[i]);
    if (m) {
      const quote = m[2] || "";
      const tail  = m[4] || "";
      lines[i] = `${m[1]}${quote}${newCapability}${quote}${tail}`;
      return lines.join("\n");
    }
  }
  return yaml + (yaml.endsWith("\n") ? "" : "\n") +
    `# TODO: no mesher.* stage found; add e.g. "plugin: ${newCapability}"\n`;
}

// ---------------------------------------------------------------------------
// Whole-stage insertion — shared by ManufacturingPanel and MarinePanel.
//
// The pickers above swap a single `plugin:` line. The manufacturing and
// marine panels instead append complete stages (a solver plus the
// postproc that consumes its field, a writer, …), so they need three
// things those pickers only do piecemeal:
//
//   1. resolve the upstream mesh-producing stage id from the document,
//   2. resolve the most recent field-producing stage id, for the
//      `field: { from: … }` wiring that postproc.* hard-requires and
//      writer.* optionally accepts,
//   3. splice one or more `- id:` blocks into the document without
//      disturbing DAG order — analysis stages land before the first
//      writer stage, writer stages land at the end.
//
// Input values are emitted verbatim, so the caller owns every unit and
// format decision (the manufacturing capability contract fixes both).
// Two substitution tokens are recognised inside a value:
//
//   $MESH      → the resolved upstream mesh stage id
//   $STAGE<n>  → the final (collision-resolved) id of the n-th spec in
//                this same batch, so a postproc can reference the
//                solver inserted alongside it.

export interface StageInput {
  /** Contract input key, e.g. `laser_power`. */
  key:   string;
  /** Literal YAML: `200`, `[0, 0, 1]`, `'316L'`, `{ from: $MESH }`. */
  value: string;
}

export interface StageSpec {
  /** Preferred stage id; deduped against ids already in the buffer. */
  id:     string;
  /** Capability id, e.g. `solver.am.thermal.lpbf`. */
  plugin: string;
  /** Ordered children of the stage's `input:` block. */
  input:  StageInput[];
  /** Optional trailing `#` comment on the `- id:` line. */
  note?:  string;
}

/** The stage id that produces the mesh: last `mesher.*`, else last
 *  `reader.*` (readers can emit a mesh — `reader.lattice` does), else
 *  null when the document has no mesh-producing stage at all. */
export function findMeshStageId(yaml: string): string | null {
  return findMeshStageIdIn(yaml.split("\n"));
}

/** The stage id of the last field-producing stage (`solver.*` or
 *  `postproc.*`), or null. Used for `field: { from: … }` wiring. */
export function findFieldStageId(yaml: string): string | null {
  return findLastStageIdMatching(yaml.split("\n"), /^\s*plugin\s*:\s*['"]?(?:solver|postproc)\./);
}

/** True when the document has a stage that can satisfy `mesh: { from: … }`. */
export function hasMeshStage(yaml: string): boolean {
  return findMeshStageId(yaml) !== null;
}

export function insertPipelineStages(yaml: string, specs: StageSpec[]): string {
  if (specs.length === 0) return yaml;
  const lines  = yaml.split("\n");
  const meshId = findMeshStageIdIn(lines);
  if (meshId === null) {
    // Same graceful degradation as the Materials panel: leave the
    // structure alone and tell the user what is missing.
    return yaml + (yaml.endsWith("\n") ? "" : "\n") +
      "# TODO: no mesh-producing stage (mesher.* / reader.*) found; cannot wire " +
      `${specs.map(s => s.plugin).join(", ")}.\n`;
  }

  // Resolve every id up front so a `$STAGE<n>` reference is stable even
  // when an earlier spec's preferred id collided.
  const taken = collectStageIds(lines);
  const ids   = specs.map(s => {
    const id = uniqueStageId(s.id, taken);
    taken.add(id);
    return id;
  });

  const stageIndent      = discoverStageIndent(lines);
  const childIndent      = stageIndent + "  ";
  const grandchildIndent = childIndent + "  ";

  const resolve = (value: string): string =>
    value
      .replace(/\$MESH\b/g, meshId)
      .replace(/\$STAGE(\d+)\b/g, (whole, n: string) => ids[Number(n)] ?? whole);

  // Analysis stages keep the mesh → solve → write reading order;
  // writers go last so the DAG stays topologically obvious.
  const analysis: string[] = [];
  const outputs:  string[] = [];
  specs.forEach((spec, i) => {
    const block = [
      `${stageIndent}- id: ${ids[i]}${spec.note ? `  # ${spec.note}` : ""}`,
      `${childIndent}plugin: ${spec.plugin}`,
    ];
    if (spec.input.length > 0) {
      block.push(`${childIndent}input:`);
      for (const { key, value } of spec.input) {
        block.push(`${grandchildIndent}${key}: ${resolve(value)}`);
      }
    }
    const sink = spec.plugin.startsWith("writer.") ? outputs : analysis;
    sink.push("", ...block);
  });

  const writerIdx = findFirstWriterStageIdx(lines);
  if (writerIdx < 0) {
    return [...lines, ...analysis, ...outputs].join("\n");
  }
  return [
    ...lines.slice(0, writerIdx),
    ...analysis,
    ...(analysis.length > 0 ? [""] : []),
    ...lines.slice(writerIdx),
    ...outputs,
  ].join("\n");
}

function findMeshStageIdIn(lines: string[]): string | null {
  return (
    findLastStageIdMatching(lines, /^\s*plugin\s*:\s*['"]?mesher\./) ??
    findLastStageIdMatching(lines, /^\s*plugin\s*:\s*['"]?reader\./)
  );
}

// Walk every `- id:` header, look ahead a few lines for that stage's
// `plugin:` line, and keep the LAST match — the same last-wins rule
// insertSolverStage uses, so both paths agree on the upstream id.
function findLastStageIdMatching(lines: string[], pluginRe: RegExp): string | null {
  let found: string | null = null;
  for (let i = 0; i < lines.length; i++) {
    const m = /^\s*-\s*id\s*:\s*['"]?([\w\-]+)['"]?\s*(#.*)?$/.exec(lines[i]);
    if (!m) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      if (pluginRe.test(lines[j])) { found = m[1]; break; }
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
  }
  return found;
}

function findFirstWriterStageIdx(lines: string[]): number {
  for (let i = 0; i < lines.length; i++) {
    if (!/^\s*-\s*id\s*:/.test(lines[i])) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      if (/^\s*plugin\s*:\s*['"]?writer\.[\w.\-]+/.test(lines[j])) return i;
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
  }
  return -1;
}

function collectStageIds(lines: string[]): Set<string> {
  const out = new Set<string>();
  for (const ln of lines) {
    const m = /^\s*-\s*id\s*:\s*['"]?([\w\-]+)['"]?\s*(#.*)?$/.exec(ln);
    if (m) out.add(m[1]);
  }
  return out;
}

function uniqueStageId(preferred: string, taken: Set<string>): string {
  if (!taken.has(preferred)) return preferred;
  for (let n = 2; n < 1000; n++) {
    const candidate = `${preferred}-${n}`;
    if (!taken.has(candidate)) return candidate;
  }
  return `${preferred}-x`;
}

function discoverStageIndent(lines: string[]): string {
  for (const ln of lines) {
    const m = /^(\s*)-\s*id\s*:/.exec(ln);
    if (m) return m[1];
  }
  return "  ";
}

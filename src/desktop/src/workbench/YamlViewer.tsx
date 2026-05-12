// SPDX-License-Identifier: Apache-2.0
//
// Lightweight, dependency-free YAML viewer for the workbench. Reads
// the file off disk via the existing `read_geometry_bytes` Tauri
// command, applies a token-level highlighter, and renders into a
// monospace pane with line numbers.
//
// Why no `js-yaml` or `prismjs`: we are not parsing YAML — we are
// *displaying* it. Pipeline files are short (tens to low hundreds of
// lines), the regex-based highlighter below covers the constructs
// souxmar pipelines actually use (keys, scalars, comments, anchors,
// flow markers, document separators), and pulling in either dep
// adds bytes the desktop bundle does not need.
//
// HTML escaping is unconditional. The highlighter never emits raw
// HTML; each token's text passes through React as a child node.

import { useEffect, useMemo, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import { invokeCommand } from "../tauri/bridge";

interface Props {
  projectPath: string;
  /** Path of the YAML file relative to the project root. */
  relPath:     string;
}

export function YamlViewer({ projectPath, relPath }: Props) {
  const [text, setText] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setText(null);
    setError(null);
    invokeCommand<number[]>("read_geometry_bytes", {
      projectPath,
      relPath,
    })
      .then(bytes => {
        if (cancelled) return;
        try {
          const decoded = new TextDecoder("utf-8", { fatal: false })
            .decode(new Uint8Array(bytes));
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

  const rows = useMemo(
    () => (text === null ? null : highlightYaml(text)),
    [text],
  );

  return (
    <div style={wrapStyle}>
      <header style={headerStyle}>
        <span style={pathStyle}>{relPath}</span>
        {text !== null && (
          <span style={metaStyle}>{text.split("\n").length} lines</span>
        )}
      </header>
      <div style={bodyStyle}>
        {error ? (
          <pre style={errorStyle}>{error}</pre>
        ) : rows === null ? (
          <div style={mutedStyle}>Loading…</div>
        ) : (
          <pre style={preStyle}>
            <code>
              {rows.map((tokens, idx) => (
                <div key={idx} style={lineStyle}>
                  <span style={gutterStyle}>{idx + 1}</span>
                  <span style={lineContentStyle}>
                    {tokens.length === 0 ? " " : tokens}
                  </span>
                </div>
              ))}
            </code>
          </pre>
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
  // Document separators / end markers.
  if (/^(---|\.\.\.)\s*$/.test(line)) {
    return [span(line, "doc", 0)];
  }

  const out: ReactNode[] = [];
  let i = 0;
  let key = 0;
  const nextKey = () => key++;

  // Leading indent — preserved verbatim.
  const indentMatch = /^(\s+)/.exec(line);
  if (indentMatch) {
    out.push(span(indentMatch[1], "plain", nextKey()));
    i = indentMatch[1].length;
  }

  // Block-list marker `- ` (dash followed by space or EOL).
  if (line[i] === "-" && (line[i + 1] === " " || line.length === i + 1)) {
    out.push(span("-", "marker", nextKey()));
    i += 1;
    if (line[i] === " ") {
      out.push(span(" ", "plain", nextKey()));
      i += 1;
    }
  }

  // Whole-line comment (after any leading indent / dash).
  if (line[i] === "#") {
    out.push(span(line.slice(i), "comment", nextKey()));
    return out;
  }

  // Try key: value at the current position. A YAML key here is
  // anything up to an unquoted colon followed by space or EOL.
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

  // Value / tail of the line — tokenise spaces, comments, scalars.
  while (i < line.length) {
    const ch = line[i];

    // Inline comment.
    if (ch === "#" && (i === 0 || /\s/.test(line[i - 1]))) {
      out.push(span(line.slice(i), "comment", nextKey()));
      return out;
    }

    // Whitespace run.
    if (ch === " " || ch === "\t") {
      let j = i;
      while (j < line.length && (line[j] === " " || line[j] === "\t")) j++;
      out.push(span(line.slice(i, j), "plain", nextKey()));
      i = j;
      continue;
    }

    // Double-quoted string.
    if (ch === '"') {
      const m = /^"(?:\\.|[^"\\])*"/.exec(line.slice(i));
      if (m) {
        out.push(span(m[0], "string", nextKey()));
        i += m[0].length;
        continue;
      }
    }

    // Single-quoted string.
    if (ch === "'") {
      const m = /^'(?:''|[^'])*'/.exec(line.slice(i));
      if (m) {
        out.push(span(m[0], "string", nextKey()));
        i += m[0].length;
        continue;
      }
    }

    // Flow markers — { } [ ] , and the pipe / gt block-scalar markers.
    if ("{}[],".includes(ch)) {
      out.push(span(ch, "punct", nextKey()));
      i += 1;
      continue;
    }
    if ((ch === "|" || ch === ">") && (i === line.length - 1 || /[-+\d\s]/.test(line[i + 1]))) {
      out.push(span(line.slice(i), "marker", nextKey()));
      return out;
    }

    // Anchor / alias / tag.
    if (ch === "&" || ch === "*" || ch === "!") {
      const m = /^[&*!][\w.\-/:]+/.exec(line.slice(i));
      if (m) {
        out.push(span(m[0], "anchor", nextKey()));
        i += m[0].length;
        continue;
      }
    }

    // Scalar — read until next space / flow marker / comment marker.
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
  height:         28,
  padding:        "0 var(--space-3)",
  borderBottom:   "1px solid var(--border-subtle)",
  background:     "var(--bg-panel)",
  fontSize:       11,
  color:          "var(--fg-tertiary)",
  flexShrink:     0,
};

const pathStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  whiteSpace:     "nowrap",
  overflow:       "hidden",
  textOverflow:   "ellipsis",
};

const metaStyle: CSSProperties = {
  flexShrink:     0,
  marginLeft:     12,
};

const bodyStyle: CSSProperties = {
  flex:           1,
  overflow:       "auto",
};

const preStyle: CSSProperties = {
  margin:         0,
  padding:        "8px 0",
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       12.5,
  lineHeight:     1.55,
  background:     "transparent",
  whiteSpace:     "pre",
  tabSize:        2,
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
  string:   { color: "#a3e08a" },          // soft green — matches the dim palette's success-ish hue
  scalar:   { color: "var(--fg-primary)" },
  number:   { color: "#f0a060" },          // amber for numerics
  keyword:  { color: "#c594ff", fontWeight: 500 }, // mauve for true/false/null
  comment:  { color: "var(--fg-tertiary)", fontStyle: "italic" },
  punct:    { color: "var(--fg-tertiary)" },
  marker:   { color: "#f0a060" },          // dashes, block-scalar markers — share the amber
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

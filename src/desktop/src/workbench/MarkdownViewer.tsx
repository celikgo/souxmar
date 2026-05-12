// SPDX-License-Identifier: Apache-2.0
//
// Lightweight, dependency-free Markdown viewer for the workbench. Reads
// the file off disk via the `read_geometry_bytes` Tauri command (which
// is path-restricted to the project root — extension-agnostic), parses
// a useful subset of CommonMark inline + block syntax, and renders to
// dim-theme React nodes.
//
// Why no `marked` / `react-markdown`: the desktop bundle is already
// large (three.js, occt-import-js); pulling in another parser for a
// preview-only feature is not worth the bytes. The subset handled here
// covers READMEs, tutorial drafts, RFCs, and the docs/ markdown the
// engineer hits while clicking around — fenced code, headings, lists,
// emphasis, links, blockquotes, hr, and inline code. HTML is escaped
// unconditionally; we do not render raw HTML on purpose.

import { useEffect, useMemo, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import { invokeCommand } from "../tauri/bridge";

interface Props {
  projectPath: string;
  /** Path of the markdown file relative to the project root. */
  relPath:     string;
}

export function MarkdownViewer({ projectPath, relPath }: Props) {
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

  const blocks = useMemo(
    () => (text === null ? null : renderMarkdown(text)),
    [text],
  );

  return (
    <div style={wrapStyle}>
      <header style={headerStyle}>
        <span style={pathStyle}>{relPath}</span>
      </header>
      <article style={articleStyle}>
        {error ? (
          <pre style={errorStyle}>{error}</pre>
        ) : blocks === null ? (
          <div style={mutedStyle}>Loading…</div>
        ) : (
          blocks
        )}
      </article>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Markdown subset → React. Block-level scan first; inline pass per block.

function renderMarkdown(src: string): ReactNode[] {
  // Normalise line endings; we never want CRLF in our token stream.
  const lines = src.replace(/\r\n?/g, "\n").split("\n");
  const out: ReactNode[] = [];
  let i = 0;
  let key = 0;
  const nextKey = () => `b${key++}`;

  while (i < lines.length) {
    const line = lines[i];

    // Fenced code block — ```lang ... ```
    const fence = /^```(\w*)\s*$/.exec(line);
    if (fence) {
      const lang = fence[1];
      const body: string[] = [];
      i += 1;
      while (i < lines.length && !/^```\s*$/.test(lines[i])) {
        body.push(lines[i]);
        i += 1;
      }
      // Skip the closing fence if we hit one (otherwise EOF — still render).
      if (i < lines.length) i += 1;
      out.push(
        <pre key={nextKey()} style={codeBlockStyle} data-lang={lang}>
          <code>{body.join("\n")}</code>
        </pre>,
      );
      continue;
    }

    // Horizontal rule.
    if (/^\s*(-\s*){3,}\s*$|^\s*(\*\s*){3,}\s*$|^\s*(_\s*){3,}\s*$/.test(line)) {
      out.push(<hr key={nextKey()} style={hrStyle} />);
      i += 1;
      continue;
    }

    // ATX heading — # through ######.
    const heading = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line);
    if (heading) {
      const level = heading[1].length;
      const content = renderInline(heading[2]);
      out.push(headingNode(level, nextKey(), content));
      i += 1;
      continue;
    }

    // Blockquote — one or more consecutive `>` lines.
    if (/^\s*>\s?/.test(line)) {
      const block: string[] = [];
      while (i < lines.length && /^\s*>\s?/.test(lines[i])) {
        block.push(lines[i].replace(/^\s*>\s?/, ""));
        i += 1;
      }
      out.push(
        <blockquote key={nextKey()} style={blockquoteStyle}>
          {renderMarkdown(block.join("\n"))}
        </blockquote>,
      );
      continue;
    }

    // Unordered list — `-`, `*`, or `+` markers.
    if (/^\s*[-*+]\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\s*[-*+]\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\s*[-*+]\s+/, ""));
        i += 1;
      }
      out.push(
        <ul key={nextKey()} style={listStyle}>
          {items.map((it, idx) => (
            <li key={idx} style={listItemStyle}>
              {renderInline(it)}
            </li>
          ))}
        </ul>,
      );
      continue;
    }

    // Ordered list — `1.`, `2.` etc.
    if (/^\s*\d+\.\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\s*\d+\.\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\s*\d+\.\s+/, ""));
        i += 1;
      }
      out.push(
        <ol key={nextKey()} style={listStyle}>
          {items.map((it, idx) => (
            <li key={idx} style={listItemStyle}>
              {renderInline(it)}
            </li>
          ))}
        </ol>,
      );
      continue;
    }

    // Blank line — skip.
    if (line.trim() === "") {
      i += 1;
      continue;
    }

    // Paragraph — accumulate until blank or block-starter.
    const para: string[] = [line];
    i += 1;
    while (i < lines.length && !isBlockStart(lines[i])) {
      para.push(lines[i]);
      i += 1;
    }
    out.push(
      <p key={nextKey()} style={paragraphStyle}>
        {renderInline(para.join(" "))}
      </p>,
    );
  }

  return out;
}

function isBlockStart(line: string): boolean {
  if (line.trim() === "") return true;
  if (/^```/.test(line)) return true;
  if (/^#{1,6}\s+/.test(line)) return true;
  if (/^\s*>\s?/.test(line)) return true;
  if (/^\s*[-*+]\s+/.test(line)) return true;
  if (/^\s*\d+\.\s+/.test(line)) return true;
  if (/^\s*(-\s*){3,}\s*$|^\s*(\*\s*){3,}\s*$|^\s*(_\s*){3,}\s*$/.test(line)) return true;
  return false;
}

function headingNode(level: number, key: string, content: ReactNode): ReactNode {
  const style = headingStyles[level - 1];
  switch (level) {
    case 1: return <h1 key={key} style={style}>{content}</h1>;
    case 2: return <h2 key={key} style={style}>{content}</h2>;
    case 3: return <h3 key={key} style={style}>{content}</h3>;
    case 4: return <h4 key={key} style={style}>{content}</h4>;
    case 5: return <h5 key={key} style={style}>{content}</h5>;
    default: return <h6 key={key} style={style}>{content}</h6>;
  }
}

// ---------------------------------------------------------------------------
// Inline pass. Order matters: inline code first (it suppresses everything
// else inside the span), then images, then links, then strong/em.

function renderInline(src: string): ReactNode[] {
  // Tokenise into a flat array of strings + already-rendered React nodes.
  let tokens: Array<string | ReactNode> = [src];
  let key = 0;
  const nextKey = () => `i${key++}`;

  // Inline code — `code`. Backticks suppress all other inline.
  tokens = applyRegex(tokens, /`([^`]+)`/g, m => (
    <code key={nextKey()} style={inlineCodeStyle}>{m[1]}</code>
  ));

  // Images — ![alt](url). Treat as a link to keep the surface tiny.
  tokens = applyRegex(tokens, /!\[([^\]]*)\]\(([^)]+)\)/g, m => (
    <a key={nextKey()} href={m[2]} style={linkStyle}>{m[1] || m[2]}</a>
  ));

  // Links — [text](url).
  tokens = applyRegex(tokens, /\[([^\]]+)\]\(([^)]+)\)/g, m => (
    <a
      key={nextKey()}
      href={m[2]}
      target="_blank"
      rel="noreferrer"
      style={linkStyle}
    >
      {m[1]}
    </a>
  ));

  // Bold — **text** or __text__.
  tokens = applyRegex(tokens, /\*\*([^*]+)\*\*|__([^_]+)__/g, m => (
    <strong key={nextKey()}>{m[1] ?? m[2]}</strong>
  ));

  // Italic — *text* or _text_ (but not the middle of words for `_`).
  tokens = applyRegex(tokens, /\*([^*\n]+)\*|(?:^|[\s({\[])_([^_\n]+)_(?=[\s.,;:!?)}\]]|$)/g, m => (
    <em key={nextKey()}>{m[1] ?? m[2]}</em>
  ));

  return tokens;
}

function applyRegex(
  tokens: Array<string | ReactNode>,
  re: RegExp,
  make: (m: RegExpExecArray) => ReactNode,
): Array<string | ReactNode> {
  const out: Array<string | ReactNode> = [];
  for (const tok of tokens) {
    if (typeof tok !== "string") {
      out.push(tok);
      continue;
    }
    let last = 0;
    re.lastIndex = 0;
    let m: RegExpExecArray | null;
    while ((m = re.exec(tok)) !== null) {
      if (m.index > last) out.push(tok.slice(last, m.index));
      out.push(make(m));
      last = m.index + m[0].length;
      // Guard against zero-width matches.
      if (m[0].length === 0) re.lastIndex += 1;
    }
    if (last < tok.length) out.push(tok.slice(last));
  }
  return out;
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

const articleStyle: CSSProperties = {
  flex:           1,
  overflow:       "auto",
  padding:        "var(--space-5, 16px) var(--space-6, 24px)",
  maxWidth:       "100%",
  lineHeight:     1.55,
  fontSize:       14,
};

const mutedStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  fontSize:       12,
};

const errorStyle: CSSProperties = {
  color:          "var(--danger, #f5365c)",
  fontSize:       12,
  whiteSpace:     "pre-wrap",
};

const paragraphStyle: CSSProperties = {
  margin:         "0 0 var(--space-3, 10px) 0",
};

const headingBase: CSSProperties = {
  margin:         "var(--space-5, 18px) 0 var(--space-2, 8px) 0",
  fontWeight:     600,
  color:          "var(--fg-primary)",
  lineHeight:     1.25,
};

const headingStyles: CSSProperties[] = [
  { ...headingBase, fontSize: 24, borderBottom: "1px solid var(--border-subtle)", paddingBottom: 6 },
  { ...headingBase, fontSize: 20, borderBottom: "1px solid var(--border-subtle)", paddingBottom: 4 },
  { ...headingBase, fontSize: 17 },
  { ...headingBase, fontSize: 15 },
  { ...headingBase, fontSize: 14, textTransform: "uppercase", letterSpacing: 0.5, color: "var(--fg-secondary)" },
  { ...headingBase, fontSize: 13, color: "var(--fg-tertiary)" },
];

const linkStyle: CSSProperties = {
  color:          "var(--accent-default, #1d9bf0)",
  textDecoration: "none",
};

const inlineCodeStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  background:     "var(--bg-elevated, rgba(255,255,255,0.05))",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm, 4px)",
  padding:        "1px 4px",
  fontSize:       "0.92em",
};

const codeBlockStyle: CSSProperties = {
  margin:         "0 0 var(--space-3, 10px) 0",
  padding:        "var(--space-3, 10px) var(--space-4, 12px)",
  background:     "var(--bg-elevated, rgba(255,255,255,0.04))",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-md, 6px)",
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       12.5,
  lineHeight:     1.5,
  overflowX:      "auto",
  whiteSpace:     "pre",
};

const blockquoteStyle: CSSProperties = {
  margin:         "0 0 var(--space-3, 10px) 0",
  padding:        "2px var(--space-4, 12px)",
  borderLeft:     "3px solid var(--accent-default, #1d9bf0)",
  color:          "var(--fg-secondary)",
  background:     "var(--bg-panel, rgba(255,255,255,0.02))",
};

const listStyle: CSSProperties = {
  margin:         "0 0 var(--space-3, 10px) 0",
  paddingLeft:    "var(--space-6, 22px)",
};

const listItemStyle: CSSProperties = {
  margin:         "0 0 4px 0",
};

const hrStyle: CSSProperties = {
  border:         "none",
  borderTop:      "1px solid var(--border-subtle)",
  margin:         "var(--space-4, 14px) 0",
};

// ---------------------------------------------------------------------------
// Extension membership — exported so Workbench can dispatch viewers.

const MARKDOWN_EXTS = new Set(["md", "markdown", "mkd", "mdown"]);

export function isMarkdownPath(relPath: string): boolean {
  const lower = relPath.toLowerCase();
  const dot = lower.lastIndexOf(".");
  if (dot < 0) return false;
  return MARKDOWN_EXTS.has(lower.slice(dot + 1));
}

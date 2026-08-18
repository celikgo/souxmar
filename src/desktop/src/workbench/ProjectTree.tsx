// SPDX-License-Identifier: Apache-2.0
//
// Left-sidebar project tree. Walks the project directory through the
// `list_project_files` Tauri command. If the command is unavailable
// (vite preview, no project open) the tree falls back to a static
// demo showing the canonical project layout.

import { useCallback, useEffect, useState } from "react";
import type { CSSProperties } from "react";
import { invokeCommand, type FileEntry } from "../tauri/bridge";
import { IconChevron, IconFile, IconFolder, IconRefresh } from "./icons";

interface Node {
  id:        string;
  name:      string;
  kind:      "file" | "folder";
  children?: Node[];
}

function toNode(entry: FileEntry): Node {
  return {
    id: entry.path,
    name: entry.name,
    kind: entry.is_dir ? "folder" : "file",
    children: entry.is_dir ? (entry.children ?? []).map(toNode) : undefined,
  };
}

interface Props {
  projectId:     string;
  /** Called with the path *relative to the project root* when the user
   *  clicks a file leaf. Workbench uses this to dispatch viewers. */
  onSelectFile?: (relPath: string) => void;
  /** Bump to force a re-read of the tree — e.g. after a pipeline run
   *  writes new files into `outputs/`. */
  reloadToken?:  number;
}

export function ProjectTree({ projectId, onSelectFile, reloadToken = 0 }: Props) {
  const [tree, setTree] = useState<Node | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [selected, setSelected] = useState<string>("");

  const reload = useCallback(async () => {
    if (!projectId) {
      setTree(null);
      setError(null);
      return;
    }
    try {
      const root = await invokeCommand<FileEntry>("list_project_files", {
        projectPath: projectId,
      });
      setTree(toNode(root));
      setError(null);
    } catch (e) {
      // No fallback tree. This used to render a fabricated five-file
      // listing (pipeline.yaml, README.md, plugins/, outputs/) that did
      // not exist on disk and whose rows were inert when clicked — the
      // sidebar's default state on every launch, since no project is
      // open until the user picks one. An empty sidebar that says why
      // is worth more than a populated one that is wrong.
      setTree(null);
      setError(String(e));
    }
  }, [projectId]);

  useEffect(() => {
    void reload();
  }, [reload, reloadToken]);

  return (
    <aside style={panelStyle}>
      <header style={headerStyle}>
        <span style={titleStyle}>Project</span>
        <button
          type="button"
          onClick={() => void reload()}
          title="Reload tree"
          style={reloadButtonStyle}
          aria-label="Reload tree"
        >
          <IconRefresh />
        </button>
      </header>
      {!projectId ? (
        <p style={emptyStyle}>
          No project open. Use <strong style={{ color: "var(--fg-secondary)" }}>New</strong>,{" "}
          <strong style={{ color: "var(--fg-secondary)" }}>Open</strong> or{" "}
          <strong style={{ color: "var(--fg-secondary)" }}>Open sample</strong> to load one.
        </p>
      ) : error ? (
        <p style={emptyStyle} title={error}>
          Couldn't read this project directory.
          <span style={errorDetailStyle}>{error}</span>
        </p>
      ) : !tree ? (
        <p style={emptyStyle}>Reading project…</p>
      ) : (
        <div style={treeStyle} role="tree">
          <TreeNode
            node={tree}
            depth={0}
            selected={selected}
            onSelect={n => {
              setSelected(n.id);
              if (n.kind !== "file" || !onSelectFile) return;
              // Tree ids are absolute paths from the bridge; strip the
              // project prefix so the viewer gets a project-rel path.
              const prefix = projectId.endsWith("/") ? projectId : projectId + "/";
              const rel = n.id.startsWith(prefix) ? n.id.slice(prefix.length) : n.id;
              onSelectFile(rel);
            }}
            forceOpen
          />
        </div>
      )}
    </aside>
  );
}

interface NodeProps {
  node: Node;
  depth: number;
  selected: string;
  onSelect: (node: Node) => void;
  forceOpen?: boolean;
}

function TreeNode({ node, depth, selected, onSelect, forceOpen }: NodeProps) {
  const [open, setOpen] = useState(depth < 1);
  const isOpen = forceOpen || open;
  const isFolder = node.kind === "folder";
  const isSelected = node.id === selected;

  return (
    <div role="treeitem" aria-expanded={isFolder ? isOpen : undefined}>
      <div
        role="button"
        tabIndex={0}
        onClick={() => {
          onSelect(node);
          if (isFolder && !forceOpen) setOpen(o => !o);
        }}
        onKeyDown={e => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            onSelect(node);
            if (isFolder && !forceOpen) setOpen(o => !o);
          }
        }}
        style={{
          ...rowStyle,
          paddingLeft: 6 + depth * 12,
          background: isSelected ? "var(--accent-soft)" : "transparent",
          color: isSelected ? "var(--accent-default)" : "var(--fg-primary)",
        }}
      >
        {isFolder ? (
          <IconChevron dir={isOpen ? "down" : "right"} style={chevronStyle} />
        ) : (
          <span style={chevronStyle} />
        )}
        {isFolder ? <IconFolder /> : <IconFile />}
        <span style={labelStyle}>{node.name}</span>
      </div>
      {isFolder && isOpen && node.children?.map(child => (
        <TreeNode
          key={child.id}
          node={child}
          depth={depth + 1}
          selected={selected}
          onSelect={onSelect}
        />
      ))}
    </div>
  );
}

const panelStyle: CSSProperties = {
  display: "flex",
  flexDirection: "column",
  height: "100%",
  background: "var(--bg-panel)",
  borderRight: "1px solid var(--border-subtle)",
  fontSize: 12,
  overflow: "hidden",
};

const headerStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  height: 28,
  padding: "0 var(--space-3)",
  borderBottom: "1px solid var(--border-subtle)",
  textTransform: "uppercase",
  letterSpacing: 0.6,
  fontSize: 10,
  color: "var(--fg-tertiary)",
};

const titleStyle: CSSProperties = {
  fontWeight: 600,
};

const reloadButtonStyle: CSSProperties = {
  display: "inline-flex",
  alignItems: "center",
  justifyContent: "center",
  width: 22,
  height: 22,
  border: "1px solid transparent",
  borderRadius: "var(--radius-sm)",
  background: "transparent",
  color: "var(--fg-tertiary)",
  cursor: "pointer",
};

const emptyStyle: CSSProperties = {
  margin: 0,
  padding: "var(--space-3)",
  fontSize: 12,
  lineHeight: 1.5,
  color: "var(--fg-tertiary)",
};

const errorDetailStyle: CSSProperties = {
  display: "block",
  marginTop: "var(--space-2)",
  fontFamily: "var(--font-mono)",
  fontSize: 10,
  wordBreak: "break-word",
};

const treeStyle: CSSProperties = {
  flex: 1,
  overflowY: "auto",
  padding: "var(--space-1) 0",
};

const rowStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: 4,
  height: 22,
  paddingRight: 6,
  cursor: "pointer",
  outline: "none",
};

const chevronStyle: CSSProperties = {
  width: 12,
  height: 12,
  color: "var(--fg-tertiary)",
  flexShrink: 0,
};

const labelStyle: CSSProperties = {
  whiteSpace: "nowrap",
  overflow: "hidden",
  textOverflow: "ellipsis",
  minWidth: 0,
  marginLeft: 2,
};

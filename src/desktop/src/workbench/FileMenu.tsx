// SPDX-License-Identifier: Apache-2.0
//
// Title-bar "File" dropdown. Surfaces the three project-lifecycle
// commands the workbench supports today: New, Open, Import. The
// menu is its own component so the TitleBar stays focused on layout.

import { useEffect, useRef, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import { IconFolderOpen, IconImport, IconPlus } from "./icons";

interface Props {
  hasProject:    boolean;
  onNewProject:  () => void;
  onOpenProject: () => void;
  onImportModel: () => void;
}

export function FileMenu({ hasProject, onNewProject, onOpenProject, onImportModel }: Props) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    function onDocClick(e: MouseEvent) {
      if (ref.current && !ref.current.contains(e.target as Node)) {
        setOpen(false);
      }
    }
    function onKey(e: KeyboardEvent) {
      if (e.key === "Escape") setOpen(false);
    }
    document.addEventListener("mousedown", onDocClick);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("mousedown", onDocClick);
      document.removeEventListener("keydown", onKey);
    };
  }, [open]);

  function pick(fn: () => void) {
    return () => {
      setOpen(false);
      fn();
    };
  }

  return (
    <div ref={ref} style={{ position: "relative" }}>
      <button
        type="button"
        onClick={() => setOpen(o => !o)}
        aria-expanded={open}
        style={triggerStyle}
        title="File menu"
      >
        File
      </button>
      {open && (
        <div style={menuStyle} role="menu">
          <MenuItem onClick={pick(onNewProject)} icon={<IconPlus />}>
            New project…
          </MenuItem>
          <MenuItem onClick={pick(onOpenProject)} icon={<IconFolderOpen />}>
            Open project…
          </MenuItem>
          <div style={dividerStyle} />
          <MenuItem
            onClick={pick(onImportModel)}
            icon={<IconImport />}
            disabled={!hasProject}
            hint={hasProject ? undefined : "Open a project first"}
          >
            Import model…
          </MenuItem>
        </div>
      )}
    </div>
  );
}

function MenuItem(props: {
  onClick:   () => void;
  icon:      ReactNode;
  children:  ReactNode;
  disabled?: boolean;
  hint?:     string;
}) {
  const { onClick, icon, children, disabled, hint } = props;
  return (
    <button
      type="button"
      role="menuitem"
      onClick={disabled ? undefined : onClick}
      disabled={disabled}
      title={hint}
      style={{
        ...menuItemStyle,
        color: disabled ? "var(--fg-tertiary)" : "var(--fg-primary)",
        cursor: disabled ? "not-allowed" : "pointer",
      }}
    >
      <span style={menuIconStyle}>{icon}</span>
      <span>{children}</span>
    </button>
  );
}

const triggerStyle: CSSProperties = {
  display: "inline-flex",
  alignItems: "center",
  height: 26,
  padding: "0 10px",
  border: "1px solid transparent",
  borderRadius: "var(--radius-sm)",
  background: "transparent",
  color: "var(--fg-primary)",
  cursor: "pointer",
  fontSize: 12,
  fontWeight: 500,
};

const menuStyle: CSSProperties = {
  position: "absolute",
  top: "100%",
  left: 0,
  marginTop: 4,
  minWidth: 200,
  background: "var(--bg-elevated)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-md)",
  boxShadow: "0 4px 16px rgba(0,0,0,0.4)",
  padding: 4,
  zIndex: 50,
};

const menuItemStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: 8,
  width: "100%",
  padding: "6px 10px",
  border: "none",
  borderRadius: "var(--radius-sm)",
  background: "transparent",
  fontSize: 12,
  textAlign: "left",
  font: "inherit",
};

const menuIconStyle: CSSProperties = {
  display: "inline-flex",
  width: 16,
  height: 16,
  alignItems: "center",
  justifyContent: "center",
  color: "var(--fg-secondary)",
};

const dividerStyle: CSSProperties = {
  height: 1,
  margin: "4px 6px",
  background: "var(--border-subtle)",
};

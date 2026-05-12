// SPDX-License-Identifier: Apache-2.0
//
// IDE-style title bar.
// Layout: [app icon] [project name] · · · [panel toggles] [actions]

import type { CSSProperties, ReactNode } from "react";
import { useLayoutStore } from "../store/layout";
import { FileMenu } from "./FileMenu";
import {
  IconChat,
  IconProjectTree,
  IconRun,
  IconSettings,
  IconTerminal,
} from "./icons";

interface Props {
  projectId:      string;
  onRun?:         () => void;
  onNewProject:   () => void;
  onOpenProject:  () => void;
  onImportModel:  () => void;
}

export function TitleBar({
  projectId,
  onRun,
  onNewProject,
  onOpenProject,
  onImportModel,
}: Props) {
  const { leftOpen, rightOpen, bottomOpen, toggleLeft, toggleRight, toggleBottom } =
    useLayoutStore();
  const projectName = projectId ? projectId.split("/").pop() || projectId : "no project";

  return (
    <header style={barStyle}>
      <div style={leftClusterStyle}>
        <span style={appBadgeStyle}>souxmar</span>
        <span style={separatorStyle}>·</span>
        <div style={menuClusterStyle}>
          <FileMenu
            hasProject={Boolean(projectId)}
            onNewProject={onNewProject}
            onOpenProject={onOpenProject}
            onImportModel={onImportModel}
          />
        </div>
        <span style={separatorStyle}>·</span>
        <span style={projectStyle} title={projectId}>
          {projectName}
        </span>
      </div>

      <div style={rightClusterStyle}>
        <ToggleButton
          active={leftOpen}
          onClick={toggleLeft}
          label="Project"
          shortcut="⌘1"
        >
          <IconProjectTree />
        </ToggleButton>
        <ToggleButton
          active={bottomOpen}
          onClick={toggleBottom}
          label="Terminal"
          shortcut="⌘`"
        >
          <IconTerminal />
        </ToggleButton>
        <ToggleButton
          active={rightOpen}
          onClick={toggleRight}
          label="AI Chat"
          shortcut="⌘J"
        >
          <IconChat />
        </ToggleButton>

        <span style={dividerStyle} />

        <button
          type="button"
          style={runButtonStyle}
          title="Run pipeline"
          onClick={onRun}
        >
          <IconRun />
          <span style={{ fontWeight: 500 }}>Run</span>
        </button>
        <button type="button" style={iconButtonStyle} title="Settings">
          <IconSettings />
        </button>
      </div>
    </header>
  );
}

function ToggleButton(props: {
  active: boolean;
  onClick: () => void;
  label: string;
  shortcut?: string;
  children: ReactNode;
}) {
  const { active, onClick, label, shortcut, children } = props;
  return (
    <button
      type="button"
      onClick={onClick}
      title={shortcut ? `${label} (${shortcut})` : label}
      aria-pressed={active}
      style={{
        ...iconButtonStyle,
        background: active ? "var(--accent-soft)" : "transparent",
        color: active ? "var(--accent-default)" : "var(--fg-secondary)",
      }}
    >
      {children}
    </button>
  );
}

const barStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  height: 36,
  padding: "0 var(--space-3)",
  background: "var(--bg-panel)",
  borderBottom: "1px solid var(--border-subtle)",
  fontSize: 12,
  userSelect: "none",
  // Tauri-specific CSS prop — make the bar a window-drag region.
  // Not in CSS Properties typings; cast through `as` to bypass.
  ...({ WebkitAppRegion: "drag" } as CSSProperties),
};

const leftClusterStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: "var(--space-2)",
  minWidth: 0,
};

const menuClusterStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  // Menu items must intercept clicks — opt out of the drag region.
  ...({ WebkitAppRegion: "no-drag" } as CSSProperties),
};

const rightClusterStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: 2,
  // Buttons must intercept clicks — opt out of the drag region.
  ...({ WebkitAppRegion: "no-drag" } as CSSProperties),
};

const appBadgeStyle: CSSProperties = {
  fontWeight: 600,
  color: "var(--fg-primary)",
  letterSpacing: 0.2,
};

const separatorStyle: CSSProperties = {
  color: "var(--fg-tertiary)",
};

const projectStyle: CSSProperties = {
  color: "var(--fg-secondary)",
  fontFamily: "var(--font-mono)",
  fontSize: 11,
  whiteSpace: "nowrap",
  overflow: "hidden",
  textOverflow: "ellipsis",
  maxWidth: 480,
};

const iconButtonStyle: CSSProperties = {
  display: "inline-flex",
  alignItems: "center",
  justifyContent: "center",
  gap: 4,
  height: 26,
  minWidth: 26,
  padding: "0 6px",
  border: "1px solid transparent",
  borderRadius: "var(--radius-sm)",
  background: "transparent",
  color: "var(--fg-secondary)",
  cursor: "pointer",
  font: "inherit",
};

const dividerStyle: CSSProperties = {
  width: 1,
  height: 16,
  background: "var(--border-subtle)",
  margin: "0 6px",
};

const runButtonStyle: CSSProperties = {
  ...iconButtonStyle,
  color: "var(--success)",
  padding: "0 10px 0 6px",
};

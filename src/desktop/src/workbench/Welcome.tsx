// SPDX-License-Identifier: Apache-2.0
//
// Empty-state Welcome panel — shown in the viewport area when there
// is no project loaded. Surfaces the three project-lifecycle actions
// as big cards so the user doesn't have to discover the File menu.

import type { CSSProperties, ReactNode } from "react";
import { IconFolderOpen, IconImport, IconPlus } from "./icons";

interface Props {
  onNewProject:    () => void;
  onOpenProject:   () => void;
  onOpenSample:    () => void;
  onImportModel?:  () => void;
}

export function Welcome({ onNewProject, onOpenProject, onOpenSample, onImportModel }: Props) {
  return (
    <div style={wrapStyle}>
      <div style={inner}>
        <h1 style={titleStyle}>Welcome to souxmar</h1>
        <p style={subtitleStyle}>
          An open-source CAD + FEM + visualization workbench. Get started by creating a
          new project, opening one from disk, or loading a bundled sample.
        </p>

        <div style={cardGridStyle}>
          <Card
            icon={<IconPlus size={22} />}
            title="New project"
            body="Scaffold a project directory with a starter pipeline.yaml and a geometry/ folder."
            onClick={onNewProject}
          />
          <Card
            icon={<IconFolderOpen size={22} />}
            title="Open project"
            body="Point at an existing project directory. It must contain a pipeline.yaml."
            onClick={onOpenProject}
          />
          <Card
            icon={<IconImport size={22} />}
            title="Open sample"
            body="Load the bundled cantilever-beam example to see the workbench in action."
            onClick={onOpenSample}
          />
        </div>

        {onImportModel && (
          <p style={hintStyle}>
            Already have a project open? Use <strong>File → Import model…</strong> to add
            an STL / STEP / OBJ file to the project's <code>geometry/</code> folder.
          </p>
        )}
      </div>
    </div>
  );
}

function Card(props: { icon: ReactNode; title: string; body: string; onClick: () => void }) {
  const { icon, title, body, onClick } = props;
  return (
    <button type="button" onClick={onClick} style={cardStyle}>
      <span style={cardIconStyle}>{icon}</span>
      <span style={cardTitleStyle}>{title}</span>
      <span style={cardBodyStyle}>{body}</span>
    </button>
  );
}

const wrapStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  height: "100%",
  padding: "var(--space-4)",
  background: "var(--bg-canvas)",
  overflow: "auto",
};

const inner: CSSProperties = {
  maxWidth: 720,
  width: "100%",
  textAlign: "center",
};

const titleStyle: CSSProperties = {
  margin: 0,
  fontSize: 22,
  fontWeight: 600,
  color: "var(--fg-primary)",
  letterSpacing: 0.2,
};

const subtitleStyle: CSSProperties = {
  marginTop: 10,
  marginBottom: 28,
  color: "var(--fg-secondary)",
  fontSize: 13,
  lineHeight: 1.55,
};

const cardGridStyle: CSSProperties = {
  display: "grid",
  gridTemplateColumns: "repeat(auto-fit, minmax(180px, 1fr))",
  gap: 12,
  textAlign: "left",
};

const cardStyle: CSSProperties = {
  display: "flex",
  flexDirection: "column",
  alignItems: "flex-start",
  gap: 8,
  padding: "16px",
  background: "var(--bg-panel)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-md)",
  color: "var(--fg-primary)",
  cursor: "pointer",
  font: "inherit",
  textAlign: "left",
};

const cardIconStyle: CSSProperties = {
  display: "inline-flex",
  width: 32,
  height: 32,
  alignItems: "center",
  justifyContent: "center",
  borderRadius: "var(--radius-sm)",
  background: "var(--accent-soft)",
  color: "var(--accent-default)",
};

const cardTitleStyle: CSSProperties = {
  fontWeight: 600,
  fontSize: 13,
};

const cardBodyStyle: CSSProperties = {
  fontSize: 12,
  color: "var(--fg-secondary)",
  lineHeight: 1.5,
};

const hintStyle: CSSProperties = {
  marginTop: 24,
  fontSize: 12,
  color: "var(--fg-tertiary)",
};

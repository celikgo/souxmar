// SPDX-License-Identifier: Apache-2.0
//
// Modal that imports an existing CAD / mesh file into the project's
// `geometry/` directory. The HTML5 file input gives a path on Tauri
// (via the OS native picker) — we forward that to the
// `import_model` Tauri command which copies it server-side so the
// project is self-contained.

import { useState } from "react";
import {
  Modal,
  dialogErrorStyle,
  dialogFieldStyle,
  dialogInputStyle,
  dialogLabelStyle,
  primaryButtonStyle,
  secondaryButtonStyle,
} from "./Modal";
import { invokeCommand, type ImportResult } from "../../tauri/bridge";

const SUPPORTED_EXTS = ["stl", "obj", "step", "stp", "iges", "igs", "blend"];

interface Props {
  projectPath: string;
  onClose:     () => void;
  onImported:  (result: ImportResult) => void;
}

export function ImportModelDialog({ projectPath, onClose, onImported }: Props) {
  const [sourcePath, setSourcePath] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function onSubmit() {
    setBusy(true);
    setError(null);
    try {
      const result = await invokeCommand<ImportResult>("import_model", {
        projectPath,
        sourceFile: sourcePath,
      });
      onImported(result);
    } catch (e) {
      setError(String(e));
      setBusy(false);
    }
  }

  async function onBrowse() {
    setError(null);
    try {
      const picked = await invokeCommand<string | null>("pick_file", {
        startDir:   "",
        extensions: SUPPORTED_EXTS,
      });
      if (picked) setSourcePath(picked);
    } catch (e) {
      setError(String(e));
    }
  }

  return (
    <Modal
      title="Import model"
      onClose={onClose}
      footer={
        <>
          <button type="button" onClick={onClose} style={secondaryButtonStyle} disabled={busy}>
            Cancel
          </button>
          <button
            type="button"
            onClick={onSubmit}
            style={primaryButtonStyle}
            disabled={busy || sourcePath.trim().length === 0}
          >
            {busy ? "Importing…" : "Import"}
          </button>
        </>
      }
    >
      <p style={{ marginTop: 0, color: "var(--fg-secondary)", fontSize: 12 }}>
        Copies a CAD / mesh file into <code>{projectPath || "the project"}/geometry/</code>.
        Reader plugins shipped with souxmar today: <code>.stl</code>, <code>.obj</code>,{" "}
        <code>.step</code> / <code>.stp</code>, <code>.iges</code> / <code>.igs</code>, and{" "}
        <code>.blend</code>.
      </p>
      <div style={dialogFieldStyle}>
        <label style={dialogLabelStyle}>Source file</label>
        <div style={{ display: "flex", gap: 6 }}>
          <input
            value={sourcePath}
            onChange={e => setSourcePath(e.target.value)}
            style={{ ...dialogInputStyle, flex: 1 }}
            placeholder="/Users/you/cad/part.step"
          />
          <button type="button" onClick={onBrowse} style={secondaryButtonStyle}>
            Browse…
          </button>
        </div>
      </div>
      {error && <div style={dialogErrorStyle}>{error}</div>}
    </Modal>
  );
}

// SPDX-License-Identifier: Apache-2.0
//
// Modal that imports an existing CAD / mesh file into the project's
// `geometry/` directory. The HTML5 file input gives a path on Tauri
// (via the OS native picker) — we forward that to the
// `import_model` Tauri command which copies it server-side so the
// project is self-contained.

import { useRef, useState } from "react";
import {
  Modal,
  dialogErrorStyle,
  dialogFieldStyle,
  dialogInputStyle,
  dialogLabelStyle,
  primaryButtonStyle,
  secondaryButtonStyle,
} from "./Modal";
import { invokeCommand } from "../../tauri/bridge";

interface Props {
  projectPath: string;
  onClose:     () => void;
  onImported:  (destPath: string) => void;
}

export function ImportModelDialog({ projectPath, onClose, onImported }: Props) {
  const [sourcePath, setSourcePath] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const fileRef = useRef<HTMLInputElement>(null);

  async function onSubmit() {
    setBusy(true);
    setError(null);
    try {
      const dst = await invokeCommand<string>("import_model", {
        projectPath,
        sourceFile: sourcePath,
      });
      onImported(dst);
    } catch (e) {
      setError(String(e));
      setBusy(false);
    }
  }

  function onPickFile(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    // In Tauri, File.path is available; in a plain browser it isn't.
    // We fall back to file.name (the user can edit the field to a
    // full path manually).
    const fileWithPath = file as File & { path?: string };
    setSourcePath(fileWithPath.path ?? file.name);
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
        <input
          value={sourcePath}
          onChange={e => setSourcePath(e.target.value)}
          style={dialogInputStyle}
          placeholder="/Users/you/cad/part.step"
        />
        <div style={{ marginTop: 6 }}>
          <button
            type="button"
            onClick={() => fileRef.current?.click()}
            style={secondaryButtonStyle}
          >
            Browse…
          </button>
          <input
            ref={fileRef}
            type="file"
            style={{ display: "none" }}
            onChange={onPickFile}
            accept=".stl,.obj,.step,.stp,.iges,.igs,.blend"
          />
        </div>
      </div>
      {error && <div style={dialogErrorStyle}>{error}</div>}
    </Modal>
  );
}

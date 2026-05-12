// SPDX-License-Identifier: Apache-2.0
//
// Modal that collects a project name + parent directory and invokes
// the `create_project` Tauri command. On success the caller receives
// the absolute path of the newly created project root.

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
import { invokeCommand } from "../../tauri/bridge";

interface Props {
  onClose:   () => void;
  onCreated: (path: string) => void;
}

export function NewProjectDialog({ onClose, onCreated }: Props) {
  const [name, setName] = useState("my-project");
  const [parent, setParent] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function onSubmit() {
    setBusy(true);
    setError(null);
    try {
      const path = await invokeCommand<string>("create_project", {
        name,
        parentDir: parent,
      });
      onCreated(path);
    } catch (e) {
      setError(String(e));
      setBusy(false);
    }
  }

  return (
    <Modal
      title="New project"
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
            disabled={busy || name.trim().length === 0}
          >
            {busy ? "Creating…" : "Create"}
          </button>
        </>
      }
    >
      <p style={{ marginTop: 0, color: "var(--fg-secondary)", fontSize: 12 }}>
        Creates a new project directory with a starter <code>pipeline.yaml</code>,{" "}
        <code>README.md</code>, and a <code>geometry/</code> folder.
      </p>
      <div style={dialogFieldStyle}>
        <label style={dialogLabelStyle}>Name</label>
        <input
          autoFocus
          value={name}
          onChange={e => setName(e.target.value)}
          style={dialogInputStyle}
          placeholder="my-project"
        />
      </div>
      <div style={dialogFieldStyle}>
        <label style={dialogLabelStyle}>Parent directory (optional)</label>
        <div style={{ display: "flex", gap: 6 }}>
          <input
            value={parent}
            onChange={e => setParent(e.target.value)}
            style={{ ...dialogInputStyle, flex: 1 }}
            placeholder="~/souxmar-projects"
          />
          <button
            type="button"
            onClick={async () => {
              setError(null);
              try {
                const picked = await invokeCommand<string | null>("pick_directory", {
                  startDir: parent,
                });
                if (picked) setParent(picked);
              } catch (e) {
                setError(String(e));
              }
            }}
            style={secondaryButtonStyle}
          >
            Browse…
          </button>
        </div>
        <p style={{ marginTop: 4, marginBottom: 0, color: "var(--fg-tertiary)", fontSize: 11 }}>
          Leave blank to use <code>~/souxmar-projects</code>.
        </p>
      </div>
      {error && <div style={dialogErrorStyle}>{error}</div>}
    </Modal>
  );
}

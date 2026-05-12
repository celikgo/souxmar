// SPDX-License-Identifier: Apache-2.0
//
// Modal that takes a path to an existing project directory and
// invokes `open_project`, which validates that the directory contains
// a `pipeline.yaml` before returning the canonical absolute path.

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
  onClose:  () => void;
  onOpened: (path: string) => void;
}

export function OpenProjectDialog({ onClose, onOpened }: Props) {
  const [path, setPath] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function onSubmit() {
    setBusy(true);
    setError(null);
    try {
      const canonical = await invokeCommand<string>("open_project", { path });
      onOpened(canonical);
    } catch (e) {
      setError(String(e));
      setBusy(false);
    }
  }

  return (
    <Modal
      title="Open project"
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
            disabled={busy || path.trim().length === 0}
          >
            {busy ? "Opening…" : "Open"}
          </button>
        </>
      }
    >
      <p style={{ marginTop: 0, color: "var(--fg-secondary)", fontSize: 12 }}>
        Paste the absolute path of a project directory. It must contain a{" "}
        <code>pipeline.yaml</code>.
      </p>
      <div style={dialogFieldStyle}>
        <label style={dialogLabelStyle}>Project path</label>
        <input
          autoFocus
          value={path}
          onChange={e => setPath(e.target.value)}
          style={dialogInputStyle}
          placeholder="/Users/you/souxmar-projects/cantilever-beam"
          onKeyDown={e => {
            if (e.key === "Enter" && path.trim().length > 0) onSubmit();
          }}
        />
      </div>
      {error && <div style={dialogErrorStyle}>{error}</div>}
    </Modal>
  );
}

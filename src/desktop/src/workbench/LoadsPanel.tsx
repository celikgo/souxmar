// SPDX-License-Identifier: Apache-2.0
//
// Boundary-condition / load editor. Lives as a tab inside the bottom dock.
// The user adds rows (force or fixed BC) keyed to the six named bbox faces
// of the active model; the rows feed both the ModelViewer (as arrow / disc
// overlays) and the `apply_loads_to_pipeline` Tauri command (which writes
// them under the first solver.* stage in pipeline.yaml).

import { useState } from "react";
import type { CSSProperties } from "react";
import { invokeCommand, type BboxFace, type LoadSpec } from "../tauri/bridge";

const FACES: BboxFace[] = ["+x", "-x", "+y", "-y", "+z", "-z"];

interface Props {
  projectId: string;
  hasModel:  boolean;
  loads:     LoadSpec[];
  setLoads:  (next: LoadSpec[]) => void;
  onLog:     (line: string) => void;
}

export function LoadsPanel({ projectId, hasModel, loads, setLoads, onLog }: Props) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  function addLoad(kind: "force" | "fixed") {
    const next: LoadSpec[] = [
      ...loads,
      kind === "force"
        ? { face: "+y", kind: "force", vector: [0, -1000, 0] }
        : { face: "-x", kind: "fixed" },
    ];
    setLoads(next);
  }

  function removeLoad(i: number) {
    setLoads(loads.filter((_, idx) => idx !== i));
  }

  function updateLoad(i: number, patch: Partial<LoadSpec>) {
    setLoads(loads.map((l, idx) => (idx === i ? ({ ...l, ...patch } as LoadSpec) : l)));
  }

  async function apply() {
    setBusy(true);
    setError(null);
    try {
      const summary = await invokeCommand<string>("apply_loads_to_pipeline", {
        projectPath: projectId,
        loads,
      });
      onLog(`pipeline.yaml: ${summary}`);
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  }

  if (!projectId) {
    return <div style={emptyStyle}>Open a project to define loads.</div>;
  }
  if (!hasModel) {
    return (
      <div style={emptyStyle}>
        Import a model first. Loads are placed on the model's bounding-box faces.
      </div>
    );
  }

  return (
    <div style={panelStyle}>
      <header style={headerStyle}>
        <div style={{ display: "flex", gap: 6 }}>
          <button type="button" style={addBtnStyle} onClick={() => addLoad("force")}>
            + Force
          </button>
          <button type="button" style={addBtnStyle} onClick={() => addLoad("fixed")}>
            + Fixed BC
          </button>
        </div>
        <button
          type="button"
          style={applyBtnStyle}
          onClick={apply}
          disabled={busy || loads.length === 0}
          title="Write loads under the solver.* stage in pipeline.yaml"
        >
          {busy ? "Applying…" : "Apply to pipeline"}
        </button>
      </header>

      {error && <div style={errorStyle}>{error}</div>}

      {loads.length === 0 ? (
        <div style={emptyStyle}>
          No loads yet. Click <strong>+ Force</strong> or <strong>+ Fixed BC</strong> to add one.
        </div>
      ) : (
        <div style={listStyle}>
          {loads.map((l, i) => (
            <LoadRow
              key={i}
              load={l}
              onChange={patch => updateLoad(i, patch)}
              onRemove={() => removeLoad(i)}
            />
          ))}
        </div>
      )}
    </div>
  );
}

function LoadRow(props: {
  load:     LoadSpec;
  onChange: (patch: Partial<LoadSpec>) => void;
  onRemove: () => void;
}) {
  const { load, onChange, onRemove } = props;
  return (
    <div style={rowStyle}>
      <span style={{
        ...badgeStyle,
        background: load.kind === "force" ? "rgba(248, 81, 73, 0.18)" : "rgba(110, 203, 255, 0.18)",
        color:      load.kind === "force" ? "#ff8a85"                : "#6ecbff",
      }}>
        {load.kind}
      </span>
      <label style={labelStyle}>face</label>
      <select
        value={load.face}
        onChange={e => onChange({ face: e.target.value as BboxFace })}
        style={selectStyle}
      >
        {FACES.map(f => (
          <option key={f} value={f}>{f}</option>
        ))}
      </select>
      {load.kind === "force" && (
        <>
          <label style={labelStyle}>F (N)</label>
          {(load.vector ?? [0, 0, 0]).map((v, axis) => (
            <input
              key={axis}
              type="number"
              value={v}
              onChange={e => {
                const next = [...(load.vector ?? [0, 0, 0])] as [number, number, number];
                next[axis] = Number(e.target.value) || 0;
                onChange({ vector: next });
              }}
              style={numInputStyle}
            />
          ))}
        </>
      )}
      <button type="button" onClick={onRemove} style={removeBtnStyle} title="Remove">
        ×
      </button>
    </div>
  );
}

const panelStyle: CSSProperties = {
  display: "flex",
  flexDirection: "column",
  height: "100%",
  background: "var(--bg-panel)",
};

const headerStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  padding: "8px 12px",
  borderBottom: "1px solid var(--border-subtle)",
};

const addBtnStyle: CSSProperties = {
  padding: "4px 10px",
  background: "transparent",
  color: "var(--fg-secondary)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  fontSize: 11,
  cursor: "pointer",
};

const applyBtnStyle: CSSProperties = {
  padding: "4px 12px",
  background: "var(--accent-default)",
  color: "white",
  border: "none",
  borderRadius: "var(--radius-sm)",
  fontSize: 11,
  fontWeight: 500,
  cursor: "pointer",
};

const listStyle: CSSProperties = {
  flex: 1,
  overflow: "auto",
  padding: "8px 0",
};

const rowStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  gap: 8,
  padding: "4px 12px",
  fontSize: 11,
  fontFamily: "var(--font-mono)",
  borderBottom: "1px solid var(--border-subtle)",
};

const badgeStyle: CSSProperties = {
  display: "inline-block",
  padding: "1px 6px",
  borderRadius: "var(--radius-sm)",
  fontSize: 10,
  fontWeight: 600,
  textTransform: "uppercase",
  letterSpacing: 0.4,
};

const labelStyle: CSSProperties = {
  color: "var(--fg-tertiary)",
  textTransform: "uppercase",
  fontSize: 9,
  letterSpacing: 0.4,
};

const selectStyle: CSSProperties = {
  padding: "2px 4px",
  background: "var(--bg-canvas)",
  color: "var(--fg-primary)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  fontSize: 11,
  fontFamily: "var(--font-mono)",
};

const numInputStyle: CSSProperties = {
  width: 64,
  padding: "2px 4px",
  background: "var(--bg-canvas)",
  color: "var(--fg-primary)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  fontSize: 11,
  fontFamily: "var(--font-mono)",
};

const removeBtnStyle: CSSProperties = {
  marginLeft: "auto",
  width: 20,
  height: 20,
  border: "none",
  borderRadius: "var(--radius-sm)",
  background: "transparent",
  color: "var(--fg-tertiary)",
  fontSize: 16,
  cursor: "pointer",
};

const emptyStyle: CSSProperties = {
  padding: 16,
  color: "var(--fg-tertiary)",
  fontSize: 12,
  textAlign: "center",
};

const errorStyle: CSSProperties = {
  margin: "8px 12px 0",
  padding: "6px 8px",
  background: "rgba(244, 33, 46, 0.12)",
  border: "1px solid rgba(244, 33, 46, 0.4)",
  borderRadius: "var(--radius-sm)",
  color: "#f4212e",
  fontSize: 11,
};

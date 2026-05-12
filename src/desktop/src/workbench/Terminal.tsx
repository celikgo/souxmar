// SPDX-License-Identifier: Apache-2.0
//
// Bottom dock panel with tab strip. Today the panel hosts three tabs:
//
//   - Terminal:  a read-only log of pipeline runs + plugin output.
//                A future push wires this to a real PTY via a new
//                Tauri command. For now it tails an in-memory log
//                buffer; the `Run` button in the title bar pushes a
//                synthetic line so the placeholder UI is alive.
//   - Inspector: re-uses the existing pipeline / state inspector
//                that used to live in the bottom-left corner.
//   - Problems:  pipeline / solver / agent warnings + errors. Empty
//                until the FFI bridge surfaces them.

import { useEffect, useRef } from "react";
import type { CSSProperties, ReactNode } from "react";
import { Inspector } from "./Inspector";
import type { BridgeFeatureSet } from "../tauri/bridge";
import { useLayoutStore, type BottomTab } from "../store/layout";
import { IconClose } from "./icons";

interface Props {
  projectId: string;
  features: BridgeFeatureSet;
  onOpenProject: (path: string) => void;
  log: string[];
}

const TABS: { id: BottomTab; label: string }[] = [
  { id: "terminal", label: "Terminal" },
  { id: "inspector", label: "Inspector" },
  { id: "problems", label: "Problems" },
];

export function Terminal({ projectId, features, onOpenProject, log }: Props) {
  const { bottomTab, setBottomTab, toggleBottom } = useLayoutStore();

  return (
    <section style={panelStyle}>
      <header style={tabBarStyle}>
        <div style={tabsStyle}>
          {TABS.map(tab => {
            const active = tab.id === bottomTab;
            return (
              <button
                key={tab.id}
                type="button"
                onClick={() => setBottomTab(tab.id)}
                aria-pressed={active}
                style={{
                  ...tabStyle,
                  color: active ? "var(--fg-primary)" : "var(--fg-secondary)",
                  borderBottomColor: active ? "var(--accent-default)" : "transparent",
                }}
              >
                {tab.label}
              </button>
            );
          })}
        </div>
        <button
          type="button"
          onClick={toggleBottom}
          title="Hide panel (⌘`)"
          style={closeButtonStyle}
        >
          <IconClose />
        </button>
      </header>

      <div style={bodyStyle}>
        {bottomTab === "terminal" && <TerminalLog log={log} />}
        {bottomTab === "inspector" && (
          <div style={inspectorWrapStyle}>
            <Inspector
              projectId={projectId}
              features={features}
              onOpenProject={onOpenProject}
            />
          </div>
        )}
        {bottomTab === "problems" && <EmptyState>No problems detected.</EmptyState>}
      </div>
    </section>
  );
}

function TerminalLog({ log }: { log: string[] }) {
  const endRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: "smooth", block: "end" });
  }, [log.length]);
  if (log.length === 0) {
    return (
      <EmptyState>
        <span>No output yet — press </span>
        <kbd style={kbdStyle}>Run</kbd>
        <span> in the title bar to dispatch the project's pipeline.</span>
      </EmptyState>
    );
  }
  return (
    <pre style={logStyle}>
      {log.map((line, i) => (
        <div key={i} style={logLineStyle}>
          <span style={logGutterStyle}>{String(i + 1).padStart(3, " ")}</span>
          <span>{line}</span>
        </div>
      ))}
      <div ref={endRef} />
    </pre>
  );
}

function EmptyState({ children }: { children: ReactNode }) {
  return <div style={emptyStyle}>{children}</div>;
}

const panelStyle: CSSProperties = {
  display: "flex",
  flexDirection: "column",
  height: "100%",
  background: "var(--bg-panel)",
  borderTop: "1px solid var(--border-subtle)",
  overflow: "hidden",
};

const tabBarStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  height: 30,
  borderBottom: "1px solid var(--border-subtle)",
  paddingRight: 6,
};

const tabsStyle: CSSProperties = {
  display: "flex",
  alignItems: "stretch",
  height: "100%",
};

const tabStyle: CSSProperties = {
  height: "100%",
  padding: "0 var(--space-3)",
  border: "none",
  borderBottom: "2px solid transparent",
  background: "transparent",
  fontSize: 11,
  letterSpacing: 0.2,
  fontWeight: 500,
  cursor: "pointer",
};

const closeButtonStyle: CSSProperties = {
  display: "inline-flex",
  alignItems: "center",
  justifyContent: "center",
  height: 22,
  width: 22,
  border: "1px solid transparent",
  borderRadius: "var(--radius-sm)",
  background: "transparent",
  color: "var(--fg-tertiary)",
  cursor: "pointer",
};

const bodyStyle: CSSProperties = {
  flex: 1,
  overflow: "auto",
};

const inspectorWrapStyle: CSSProperties = {
  height: "100%",
  overflow: "auto",
};

const emptyStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  height: "100%",
  padding: "var(--space-4)",
  color: "var(--fg-tertiary)",
  fontSize: 12,
  gap: 4,
  textAlign: "center",
};

const kbdStyle: CSSProperties = {
  display: "inline-block",
  padding: "1px 5px",
  fontFamily: "var(--font-mono)",
  fontSize: 10,
  color: "var(--fg-secondary)",
  background: "var(--bg-elevated)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
};

const logStyle: CSSProperties = {
  margin: 0,
  padding: "var(--space-2) 0",
  fontFamily: "var(--font-mono)",
  fontSize: 12,
  lineHeight: 1.5,
  color: "var(--fg-primary)",
  whiteSpace: "pre",
};

const logLineStyle: CSSProperties = {
  display: "grid",
  gridTemplateColumns: "40px 1fr",
  gap: 8,
  padding: "0 var(--space-3)",
};

const logGutterStyle: CSSProperties = {
  color: "var(--fg-tertiary)",
  textAlign: "right",
};

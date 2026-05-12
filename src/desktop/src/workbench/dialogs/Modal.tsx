// SPDX-License-Identifier: Apache-2.0
//
// Lightweight modal primitive used by the project / import dialogs.
// Renders a centred card over a dimmed backdrop; the backdrop and the
// Escape key both call `onClose`.

import { useEffect } from "react";
import type { CSSProperties, ReactNode } from "react";

interface Props {
  title:    string;
  onClose:  () => void;
  children: ReactNode;
  footer?:  ReactNode;
}

export function Modal({ title, onClose, children, footer }: Props) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") onClose();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  return (
    <div style={backdropStyle} onMouseDown={onClose}>
      <div style={cardStyle} onMouseDown={e => e.stopPropagation()}>
        <header style={headerStyle}>
          <span style={{ fontWeight: 600 }}>{title}</span>
          <button type="button" onClick={onClose} style={closeStyle} title="Close">
            ×
          </button>
        </header>
        <div style={bodyStyle}>{children}</div>
        {footer && <footer style={footerStyle}>{footer}</footer>}
      </div>
    </div>
  );
}

const backdropStyle: CSSProperties = {
  position: "fixed",
  inset: 0,
  background: "rgba(0,0,0,0.45)",
  display: "flex",
  alignItems: "center",
  justifyContent: "center",
  zIndex: 1000,
};

const cardStyle: CSSProperties = {
  width: 460,
  maxWidth: "calc(100vw - 32px)",
  background: "var(--bg-panel)",
  color: "var(--fg-primary)",
  borderRadius: "var(--radius-md)",
  border: "1px solid var(--border-subtle)",
  boxShadow: "0 8px 32px rgba(0,0,0,0.5)",
  display: "flex",
  flexDirection: "column",
};

const headerStyle: CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  padding: "10px 14px",
  borderBottom: "1px solid var(--border-subtle)",
  fontSize: 13,
};

const closeStyle: CSSProperties = {
  border: "none",
  background: "transparent",
  color: "var(--fg-tertiary)",
  fontSize: 18,
  lineHeight: 1,
  cursor: "pointer",
  padding: "0 4px",
};

const bodyStyle: CSSProperties = {
  padding: "14px",
  fontSize: 13,
};

const footerStyle: CSSProperties = {
  display: "flex",
  justifyContent: "flex-end",
  gap: 8,
  padding: "10px 14px",
  borderTop: "1px solid var(--border-subtle)",
};

export const dialogInputStyle: CSSProperties = {
  width: "100%",
  padding: "6px 8px",
  background: "var(--bg-canvas)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  color: "var(--fg-primary)",
  fontSize: 12,
  fontFamily: "var(--font-mono)",
  boxSizing: "border-box",
};

export const dialogLabelStyle: CSSProperties = {
  display: "block",
  fontSize: 11,
  textTransform: "uppercase",
  letterSpacing: 0.4,
  color: "var(--fg-tertiary)",
  marginBottom: 4,
};

export const dialogFieldStyle: CSSProperties = {
  marginBottom: 12,
};

export const dialogErrorStyle: CSSProperties = {
  marginTop: 8,
  padding: "6px 8px",
  background: "rgba(244, 33, 46, 0.1)",
  border: "1px solid rgba(244, 33, 46, 0.4)",
  borderRadius: "var(--radius-sm)",
  color: "var(--error, #f4212e)",
  fontSize: 12,
};

export const primaryButtonStyle: CSSProperties = {
  padding: "6px 14px",
  background: "var(--accent-default)",
  color: "white",
  border: "none",
  borderRadius: "var(--radius-sm)",
  fontSize: 12,
  fontWeight: 500,
  cursor: "pointer",
};

export const secondaryButtonStyle: CSSProperties = {
  padding: "6px 14px",
  background: "transparent",
  color: "var(--fg-secondary)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  fontSize: 12,
  cursor: "pointer",
};

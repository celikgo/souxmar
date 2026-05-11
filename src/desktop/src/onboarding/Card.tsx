// SPDX-License-Identifier: Apache-2.0
//
// Reusable card + button primitives. Wraps Radix Slot only where the
// design system calls for it (Sprint 11+ extracts these into a
// proper `src/desktop/src/ui/` component library).

import type { ReactNode, ButtonHTMLAttributes } from "react";

export function Card({ children }: { children: ReactNode }) {
  return (
    <div
      style={{
        background: "var(--bg-panel)",
        border: "1px solid var(--border-subtle)",
        borderRadius: "var(--radius-lg)",
        padding: "var(--space-6)",
        maxWidth: 560,
        width: "100%",
      }}
    >
      {children}
    </div>
  );
}

export function CardActions({ children }: { children: ReactNode }) {
  return (
    <div
      style={{
        marginTop: "var(--space-5)",
        display: "flex",
        gap: "var(--space-2)",
        justifyContent: "flex-end",
      }}
    >
      {children}
    </div>
  );
}

type BtnProps = ButtonHTMLAttributes<HTMLButtonElement>;

const baseBtn: React.CSSProperties = {
  fontFamily: "inherit",
  fontSize: 14,
  fontWeight: 500,
  padding: "8px 16px",
  borderRadius: "var(--radius-md)",
  border: "1px solid transparent",
  cursor: "pointer",
  transition: "background-color var(--duration-fast) var(--ease-out)",
};

export function PrimaryButton(props: BtnProps) {
  return (
    <button
      {...props}
      style={{
        ...baseBtn,
        background: "var(--accent-default)",
        color: "var(--fg-on-accent)",
      }}
    />
  );
}

export function SecondaryButton(props: BtnProps) {
  return (
    <button
      {...props}
      style={{
        ...baseBtn,
        background: "transparent",
        color: "var(--fg-primary)",
        border: "1px solid var(--border-strong)",
      }}
    />
  );
}

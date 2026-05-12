// SPDX-License-Identifier: Apache-2.0
//
// Inline SVG icons used across the IDE chrome (title bar, side panels,
// status bar). Each icon is a 16x16 stroke-based glyph that picks up
// the current text colour so it matches token-themed surfaces.

import type { CSSProperties } from "react";

type Props = { size?: number; style?: CSSProperties };

const base = (size: number): CSSProperties => ({
  width: size,
  height: size,
  flexShrink: 0,
  verticalAlign: "middle",
});

export const IconProjectTree = ({ size = 16, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <rect x="2" y="3" width="12" height="10" rx="1.5" />
    <path d="M2 6h12" />
    <path d="M6 6v7" />
  </svg>
);

export const IconTerminal = ({ size = 16, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <rect x="2" y="3" width="12" height="10" rx="1.5" />
    <path d="M5 7l2 1.5L5 10" />
    <path d="M9 10h3" />
  </svg>
);

export const IconChat = ({ size = 16, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M3 4.5a1.5 1.5 0 0 1 1.5-1.5h7A1.5 1.5 0 0 1 13 4.5v5A1.5 1.5 0 0 1
             11.5 11H7l-3 2.5V11H4.5A1.5 1.5 0 0 1 3 9.5z" />
  </svg>
);

export const IconRun = ({ size = 16, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="currentColor"
       style={{ ...base(size), ...style }}>
    <path d="M5 3.2c0-.6.7-.95 1.2-.62l7.3 4.8a.74.74 0 0 1 0 1.24l-7.3 4.8c-.5.33-1.2-.02-1.2-.62z" />
  </svg>
);

export const IconSettings = ({ size = 16, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <circle cx="8" cy="8" r="2" />
    <path d="M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1" />
  </svg>
);

export const IconChevron = ({ size = 12, style, dir = "right" }: Props & { dir?: "right" | "down" }) => (
  <svg viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{
         ...base(size),
         transform: dir === "down" ? "rotate(90deg)" : undefined,
         transition: "transform var(--duration-fast, 150ms) ease",
         ...style,
       }}>
    <path d="M4.5 3l3 3-3 3" />
  </svg>
);

export const IconFile = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M9 1.5H4A1.5 1.5 0 0 0 2.5 3v10A1.5 1.5 0 0 0 4 14.5h8A1.5 1.5 0 0 0 13.5 13V6L9 1.5z" />
    <path d="M9 1.5V6h4.5" />
  </svg>
);

export const IconFolder = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M2 4a1.5 1.5 0 0 1 1.5-1.5h2.4l1.6 1.5h5A1.5 1.5 0 0 1 14 5.5v7A1.5 1.5 0 0 1 12.5 14h-9A1.5 1.5 0 0 1 2 12.5z" />
  </svg>
);

export const IconClose = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M4 4l8 8M12 4l-8 8" />
  </svg>
);

export const IconPlus = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M8 3v10M3 8h10" />
  </svg>
);

export const IconFolderOpen = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M2 4a1.5 1.5 0 0 1 1.5-1.5h2.4l1.6 1.5h5A1.5 1.5 0 0 1 14 5.5v.5H2z" />
    <path d="M2 6.5h12.5L13 12.7a1.5 1.5 0 0 1-1.5 1.3h-8A1.5 1.5 0 0 1 2 12.5z" />
  </svg>
);

export const IconImport = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M8 2v7M5 7l3 3 3-3" />
    <path d="M3 12.5h10" />
  </svg>
);

export const IconRefresh = ({ size = 14, style }: Props) => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5"
       strokeLinecap="round" strokeLinejoin="round"
       style={{ ...base(size), ...style }}>
    <path d="M13 4.5A5.5 5.5 0 1 0 14 8" />
    <path d="M13 2v3h-3" />
  </svg>
);

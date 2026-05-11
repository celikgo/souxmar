// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — Radix Progress wrapper, tokens-only colours.

import * as RProgress from "@radix-ui/react-progress";

export function Progress({ current, total }: { current: number; total: number }) {
  const pct = ((current + 1) / total) * 100;
  return (
    <RProgress.Root
      value={pct}
      style={{
        position: "relative",
        overflow: "hidden",
        background: "var(--bg-elevated)",
        borderRadius: "var(--radius-sm)",
        width: "100%",
        height: 4,
      }}
    >
      <RProgress.Indicator
        style={{
          backgroundColor: "var(--accent-default)",
          width: "100%",
          height: "100%",
          transform: `translateX(-${100 - pct}%)`,
          transition: "transform var(--duration-fast) var(--ease-out)",
        }}
      />
    </RProgress.Root>
  );
}

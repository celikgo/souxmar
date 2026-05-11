// SPDX-License-Identifier: Apache-2.0

import { Card, CardActions, PrimaryButton } from "./Card";

export function Welcome({ onNext }: { onNext: () => void }) {
  return (
    <Card>
      <h1 style={{ margin: 0, fontSize: 28, fontWeight: 600 }}>
        Welcome to souxmar
      </h1>
      <p style={{ color: "var(--fg-secondary)", marginTop: "var(--space-3)" }}>
        souxmar is a CAD + FEM + CFD pipeline with an agentic AI chat at
        its centre. The next three steps configure the things you'll
        want before your first analysis: an AI provider key (optional —
        you can run locally without one) and a sample project to look
        at while you find your bearings.
      </p>
      <ul
        style={{
          margin: "var(--space-4) 0 0 0",
          paddingLeft: "var(--space-4)",
          color: "var(--fg-secondary)",
          lineHeight: 1.7,
        }}
      >
        <li>BYOK is the default — your provider key never leaves your machine.</li>
        <li>The desktop app, CLI, and Python API are peers; whatever the agent does, you can also do.</li>
        <li>Plugins are open ABI. Anything in <code>~/.local/share/souxmar/plugins</code> loads.</li>
      </ul>
      <CardActions>
        <PrimaryButton onClick={onNext}>Get started</PrimaryButton>
      </CardActions>
    </Card>
  );
}

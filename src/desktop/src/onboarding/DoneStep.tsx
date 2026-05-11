// SPDX-License-Identifier: Apache-2.0

import { Card, CardActions, PrimaryButton } from "./Card";
import type { BYOKChoice } from "./BYOKStep";

export function DoneStep({
  byok, onFinish,
}: {
  byok: BYOKChoice | null;
  onFinish: () => void;
}) {
  return (
    <Card>
      <h1 style={{ margin: 0, fontSize: 24, fontWeight: 600 }}>You're set up</h1>
      <p style={{ color: "var(--fg-secondary)", marginTop: "var(--space-3)" }}>
        {byok
          ? `AI provider: ${byok.provider}${byok.validated ? " (connection OK)" : ""}.`
          : "No AI provider configured. The chat tab will prompt you to set one up the first time you open it."}
      </p>
      <p style={{ color: "var(--fg-secondary)", marginTop: "var(--space-3)" }}>
        From here you can:
      </p>
      <ul style={{ color: "var(--fg-secondary)", lineHeight: 1.7, paddingLeft: "var(--space-4)" }}>
        <li>Open a project from the workbench shell.</li>
        <li>Ask the agent to run a stage from the chat tab.</li>
        <li>Drop plugins under <code>~/.local/share/souxmar/plugins</code> and reload.</li>
      </ul>
      <CardActions>
        <PrimaryButton onClick={onFinish}>Open the workbench</PrimaryButton>
      </CardActions>
    </Card>
  );
}

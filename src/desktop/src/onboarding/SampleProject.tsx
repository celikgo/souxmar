// SPDX-License-Identifier: Apache-2.0

import { useState } from "react";
import { Card, CardActions, PrimaryButton, SecondaryButton } from "./Card";
import { invokeCommand } from "../tauri/bridge";

export function SampleProject({
  onNext, onSkip,
}: {
  onNext: () => void;
  onSkip: () => void;
}) {
  const [busy, setBusy]   = useState(false);
  const [err, setErr]     = useState("");

  const copyAndOpen = async () => {
    setBusy(true);
    setErr("");
    try {
      // Tauri side copies examples/cantilever-beam/ to
      // ~/souxmar-projects/cantilever and returns the destination path.
      // We don't bother surfacing the path here — the main shell will
      // show it after onComplete.
      await invokeCommand("open_sample_project", { which: "cantilever-beam" });
      onNext();
    } catch (e) {
      setErr(String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Card>
      <h1 style={{ margin: 0, fontSize: 24, fontWeight: 600 }}>
        Try a sample project
      </h1>
      <p style={{ color: "var(--fg-secondary)", marginTop: "var(--space-3)" }}>
        The cantilever beam example walks through the full pipeline:
        OBJ geometry → tetrahedral mesh → linear-elastic solve →
        post-processing → VTU export. Open it now and the agent's first
        message will be "what would you like to do with this case?"
      </p>
      <p style={{ color: "var(--fg-tertiary)", marginTop: "var(--space-3)", fontSize: 12 }}>
        Copied to <code>~/souxmar-projects/cantilever</code>. The
        originals stay read-only in the install.
      </p>
      {err && (
        <p style={{ color: "var(--danger)", marginTop: "var(--space-3)" }}>
          {err}
        </p>
      )}
      <CardActions>
        <SecondaryButton onClick={onSkip} disabled={busy}>
          Skip — I'll start blank
        </SecondaryButton>
        <PrimaryButton onClick={copyAndOpen} disabled={busy}>
          {busy ? "Copying…" : "Open the sample"}
        </PrimaryButton>
      </CardActions>
    </Card>
  );
}

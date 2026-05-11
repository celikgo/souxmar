// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — top-level desktop-app shell. The shell asks the
// Tauri side whether the user has completed onboarding (a single
// boolean persisted in OS-appropriate user settings). If not, the
// Onboarding wizard takes over the whole window until the user
// finishes or skips. The main workbench shell (viewport + chat +
// inspector) is not in this scaffolding — that lands incrementally
// across Sprint 11+.

import { useEffect, useState } from "react";
import { Onboarding } from "./onboarding/Onboarding";
import { useAppStore } from "./store/app";
import { invokeCommand } from "./tauri/bridge";

export function App() {
  const onboardingDone = useAppStore(s => s.onboardingDone);
  const setOnboardingDone = useAppStore(s => s.setOnboardingDone);
  const [bootstrapped, setBootstrapped] = useState(false);

  useEffect(() => {
    // Ask the Tauri side whether onboarding has been completed before.
    // The bridge returns false the first time the user launches; that
    // boolean is persisted to ~/Library/Application Support/souxmar/
    // settings.json (or the platform equivalent) on completion.
    invokeCommand<boolean>("onboarding_status")
      .then(done => setOnboardingDone(done))
      .catch(() => setOnboardingDone(false))
      .finally(() => setBootstrapped(true));
  }, [setOnboardingDone]);

  if (!bootstrapped) {
    return (
      <main className="splash">
        <p style={{ color: "var(--fg-secondary)" }}>Starting souxmar…</p>
      </main>
    );
  }

  if (!onboardingDone) {
    return <Onboarding onComplete={() => setOnboardingDone(true)} />;
  }

  return (
    <main
      style={{
        padding: "var(--space-6)",
        color: "var(--fg-primary)",
      }}
    >
      <h1 style={{ margin: 0, fontSize: 24, fontWeight: 600 }}>
        Welcome back
      </h1>
      <p style={{ color: "var(--fg-secondary)" }}>
        The main workbench is still landing. For now, this shell exists
        so the onboarding flow has somewhere to deliver you to.
      </p>
    </main>
  );
}

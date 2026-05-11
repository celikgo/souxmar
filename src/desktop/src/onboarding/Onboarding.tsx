// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — first-run onboarding wizard.
//
// Four steps:
//   1. Welcome      — what souxmar is, why dim theme, one-line scope.
//   2. BYOK         — paste an Anthropic / OpenAI / Ollama key; skip-
//                     able (the user can configure later under
//                     Settings → AI providers).
//   3. SampleProject — copies examples/cantilever-beam/ to
//                     ~/souxmar-projects/cantilever and opens it.
//                     Skippable for users who already know what
//                     they're doing.
//   4. Done         — handoff to the main shell.
//
// Each step is a small focused component. The wizard owns the
// step-index state + the gathered choices; the Tauri commands are
// invoked at the points the design calls for, never speculatively.

import { useState } from "react";
import { invokeCommand } from "../tauri/bridge";
import { Welcome } from "./Welcome";
import { BYOKStep, type BYOKChoice } from "./BYOKStep";
import { SampleProject } from "./SampleProject";
import { DoneStep } from "./DoneStep";
import { Progress } from "./Progress";

interface Props {
  onComplete: () => void;
}

type Step = "welcome" | "byok" | "sample" | "done";
const STEP_ORDER: Step[] = ["welcome", "byok", "sample", "done"];

export function Onboarding({ onComplete }: Props) {
  const [step, setStep] = useState<Step>("welcome");
  const [byok, setByok] = useState<BYOKChoice | null>(null);

  const advance = () => {
    const i = STEP_ORDER.indexOf(step);
    setStep(STEP_ORDER[Math.min(i + 1, STEP_ORDER.length - 1)]);
  };

  const finish = async () => {
    // Persist completion + any gathered choices. We DON'T persist
    // the BYOK key here — that already happened during the BYOK step
    // (so a user who quits during the sample-project copy still has
    // their key saved, not lost).
    try {
      await invokeCommand("onboarding_complete");
    } catch {
      // Best-effort. If Tauri isn't running (vite preview / unit
      // test), we still let the user proceed; the next-launch path
      // will show the wizard again.
    }
    onComplete();
  };

  return (
    <div
      style={{
        height: "100%",
        display: "flex",
        flexDirection: "column",
        background: "var(--bg-canvas)",
      }}
    >
      <header
        style={{
          padding: "var(--space-4) var(--space-6)",
          borderBottom: "1px solid var(--border-subtle)",
        }}
      >
        <Progress current={STEP_ORDER.indexOf(step)} total={STEP_ORDER.length} />
      </header>

      <main
        style={{
          flex: 1,
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          padding: "var(--space-6)",
        }}
      >
        {step === "welcome"  && <Welcome      onNext={advance} />}
        {step === "byok"     && <BYOKStep     onNext={(c) => { setByok(c); advance(); }} onSkip={advance} />}
        {step === "sample"   && <SampleProject onNext={advance} onSkip={advance} />}
        {step === "done"     && <DoneStep    byok={byok} onFinish={finish} />}
      </main>
    </div>
  );
}

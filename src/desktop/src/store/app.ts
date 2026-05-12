// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — minimal Zustand store. One slice today
// (onboarding-completion); more slices land as the workbench grows.

import { create } from "zustand";

interface AppState {
  onboardingDone: boolean;
  setOnboardingDone: (done: boolean) => void;
  // Project the user opened during onboarding (sample step). The
  // workbench reads this on mount so the user lands on a loaded
  // sample instead of the empty-viewport state.
  initialProjectPath: string;
  setInitialProjectPath: (path: string) => void;
}

export const useAppStore = create<AppState>(set => ({
  onboardingDone: false,
  setOnboardingDone: done => set({ onboardingDone: done }),
  initialProjectPath: "",
  setInitialProjectPath: path => set({ initialProjectPath: path }),
}));

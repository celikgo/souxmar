// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — minimal Zustand store. One slice today
// (onboarding-completion); more slices land as the workbench grows.

import { create } from "zustand";

interface AppState {
  onboardingDone: boolean;
  setOnboardingDone: (done: boolean) => void;
}

export const useAppStore = create<AppState>(set => ({
  onboardingDone: false,
  setOnboardingDone: done => set({ onboardingDone: done }),
}));

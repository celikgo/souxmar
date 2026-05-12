// SPDX-License-Identifier: Apache-2.0
//
// Workbench layout state — which side / bottom panels are open and
// which bottom tab is active. Persists nothing yet; panel-open booleans
// reset on app restart. A future push wires this to the same
// per-window settings the onboarding boolean uses.

import { create } from "zustand";

export type BottomTab = "terminal" | "inspector" | "problems";

interface LayoutState {
  leftOpen: boolean;
  rightOpen: boolean;
  bottomOpen: boolean;
  bottomTab: BottomTab;

  toggleLeft: () => void;
  toggleRight: () => void;
  toggleBottom: () => void;
  setBottomTab: (tab: BottomTab) => void;
}

export const useLayoutStore = create<LayoutState>(set => ({
  leftOpen: true,
  rightOpen: true,
  bottomOpen: true,
  bottomTab: "terminal",
  toggleLeft: () => set(s => ({ leftOpen: !s.leftOpen })),
  toggleRight: () => set(s => ({ rightOpen: !s.rightOpen })),
  toggleBottom: () => set(s => ({ bottomOpen: !s.bottomOpen })),
  setBottomTab: tab => set({ bottomTab: tab }),
}));

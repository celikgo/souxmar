// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — React entry point. The desktop app is intentionally
// thin at this layer: mount the App, let it decide whether the
// onboarding wizard or the main shell is shown.

import React from "react";
import ReactDOM from "react-dom/client";
import { App } from "./App";

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);

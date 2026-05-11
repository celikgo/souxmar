// SPDX-License-Identifier: Apache-2.0
//
// Sprint 12 push 2 — bridge-features hook. Queries
// bridge_feature_set once on mount; panels use the returned flags
// to render real vs. scaffolding content.

import { useEffect, useState } from "react";
import {
  BridgeFeatureSet,
  fallbackFeatureSet,
  invokeCommand,
} from "../tauri/bridge";

export function useBridgeFeatures(): BridgeFeatureSet {
  const [features, setFeatures] = useState<BridgeFeatureSet>(fallbackFeatureSet);

  useEffect(() => {
    invokeCommand<BridgeFeatureSet>("bridge_feature_set")
      .then(setFeatures)
      .catch(() => {
        // Tauri runtime absent (vite preview / Playwright).
        // Keep the fallback so the workbench renders honestly
        // scaffolded.
      });
  }, []);

  return features;
}

/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

import { _lazyForTestMocking } from "chrome://global/content/ml/MLEngine.worker.mjs";

globalThis.InferenceSession.isAvailable = () => {
  throw new Error("Native ORT availability probe failed");
};

// browser_ml_best_onnx_fallback.js drives an engine build against this stub to
// observe the resolved best-onnx backend, so getBackend must succeed. It is
// never called by browser_ml_native_ort_availability.js, which only probes.
_lazyForTestMocking.getBackend = async () => ({ run: () => ({}) });

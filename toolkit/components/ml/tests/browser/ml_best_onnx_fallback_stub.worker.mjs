/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

import { _lazyForTestMocking } from "chrome://global/content/ml/MLEngine.worker.mjs";

// Force the availability probe to report the native runtime as present so the
// best-onnx resolution attempts onnx-native and exercises the engine-creation
// fallback below. Throw if the interface is missing to surface a broken
// assumption rather than silently testing the probe-false path instead.
if (typeof globalThis.InferenceSession?.isAvailable !== "function") {
  throw new Error("InferenceSession.isAvailable is not defined");
}
globalThis.InferenceSession.isAvailable = () => true;

_lazyForTestMocking.getBackend = async function (
  _mlEngineWorker,
  _wasm,
  options
) {
  if (options.backend === "onnx-native") {
    throw new Error("onnxruntime shared library could not be loaded");
  }

  return { run: () => ({}) };
};

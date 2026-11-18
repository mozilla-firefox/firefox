/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

import { _lazyForTestMocking } from "chrome://global/content/ml/MLEngine.worker.mjs";

/**
 * @typedef {object} EchoOutcome
 * @property {boolean} received
 * @property {number} byteLength
 * @property {?string} sha256
 * @property {?string} etag
 * @property {?string} error
 * @property {object} metrics - MLTelemetry reads metrics off every run result
 *   and logs a console.error when it is missing.
 */

/** @type {any} */
const lazy = {};
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    createFileUrl: "chrome://global/content/ml/Utils.sys.mjs",
  },
  { global: "current" }
);

/**
 * Fetches the model file through the real getModelFile plumbing and reports
 * what arrived back through run(), so a test can assert the bytes survived the
 * trip from the parent process into this worker rather than just that a fetch
 * was attempted.
 *
 * @param {any} mlEngineWorker - The MLEngineWorker driving this backend.
 * @param {any} _wasm - Unused; no inference runs here.
 * @param {any} options - The resolved pipeline options.
 * @returns {Promise<{ run: () => EchoOutcome }>}
 */
async function echoBackend(mlEngineWorker, _wasm, options = {}) {
  const url = lazy.createFileUrl({
    model: options.modelId,
    revision: options.modelRevision,
    file: options.modelFile,
    urlTemplate: options.modelHubUrlTemplate,
    rootUrl: options.modelHubRootUrl,
  });

  /** @type {EchoOutcome} */
  const outcome = {
    received: false,
    byteLength: 0,
    sha256: null,
    etag: null,
    error: null,
    metrics: {},
  };

  try {
    const [, headers, blob] = (await mlEngineWorker.getModelFile({ url })).ok;
    const bytes = new Uint8Array(await blob.arrayBuffer());
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));

    outcome.received = true;
    outcome.byteLength = bytes.byteLength;
    outcome.sha256 = Array.from(digest)
      .map(byte => byte.toString(16).padStart(2, "0"))
      .join("");
    outcome.etag = headers?.ETag ?? null;
  } catch (error) {
    outcome.error = error?.message ?? String(error);
  }

  return { run: () => outcome };
}

/** @type {any} */ (_lazyForTestMocking).getBackend = echoBackend;

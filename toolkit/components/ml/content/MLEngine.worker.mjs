/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

const lazy = {};

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    PromiseWorker: "resource://gre/modules/workers/PromiseWorker.mjs",
    getBackend: "chrome://global/content/ml/backends/Pipeline.mjs",
    OPFS: "chrome://global/content/ml/OPFS.sys.mjs",
    generateUUID: "chrome://global/content/ml/Utils.sys.mjs",
  },
  { global: "current" }
);

/**
 * The actual MLEngine lives here in a worker.
 */
export class MLEngineWorker {
  #pipeline;
  #sessionId;

  constructor() {
    // Connect the provider to the worker.
    this.#connectToPromiseWorker();
  }

  /**
   * Implements the `match` function from the Cache API for Transformers.js custom cache.
   *
   * See https://developer.mozilla.org/en-US/docs/Web/API/Cache
   *
   * Attempts to match and retrieve a model file based on a provided key.
   * Fetches a model file by delegating the call to the worker's main thread.
   * Then wraps the fetched model file into a response object compatible with Transformers.js expectations.
   *
   * @param {string} key The unique identifier for the model to fetch.
   * @returns {Promise<Response|null>} A promise that resolves with a Response object containing the model file or null if not found.
   */
  async match(key) {
    // if the key starts with NO_LOCAL, we return null immediately to tell transformers.js
    // we don't server local files, and it will do a second call with the full URL
    if (key.startsWith("NO_LOCAL")) {
      return null;
    }
    let res = await this.getModelFile({ url: key });
    if (res.fail) {
      return null;
    }

    // Transformers.js expects a response object, so we wrap the array buffer
    return lazy.OPFS.toResponse(res.ok[2], res.ok[1]);
  }

  async getModelFile(args) {
    let result = await self.callMainThread("getModelFile", [
      { sessionId: this.#sessionId, ...args },
    ]);
    return result;
  }

  async notifyModelDownloadComplete() {
    return self.callMainThread("notifyModelDownloadComplete", [
      this.#sessionId,
    ]);
  }

  /**
   * Placeholder for the `put` method from the Cache API for Transformers.js custom cache.
   *
   * @throws {Error} Always thrown to indicate the method is not implemented.
   */
  put() {
    throw new Error("Method not implemented.");
  }

  /**
   * Returns true if the native ONNX runtime is available, otherwise false.
   *
   * @returns {boolean}
   */
  isNativeOnnxRuntimeAvailable() {
    return globalThis.InferenceSession?.isAvailable() ?? false;
  }

  /**
   * @param {ArrayBuffer} wasm
   * @param {object} options received as an object, converted to a PipelineOptions instance
   */
  async initializeEngine(wasm, options) {
    this.#sessionId = lazy.generateUUID();
    this.#pipeline = await lazy
      .getBackend(this, wasm, options)
      .finally(async () => {
        // Notifying here means the backend doesn't need to notify. But the backend could notify
        // so that we receive completion as soon as possible. Otherwise, we receive download completion
        // once pipeline is fully initialized.
        await this.notifyModelDownloadComplete();
      });
  }
  /**
   * Run the worker.
   *
   * @param {string} request
   * @param {string} requestId - The identifier used to internally track this request.
   * @param {object} engineRunOptions - Additional run options for the engine.
   * @param {boolean} engineRunOptions.enableInferenceProgress - Whether to enable inference progress.
   */
  async run(request, requestId, engineRunOptions = {}) {
    if (request === "throw") {
      throw new Error(
        'Received the message "throw", so intentionally throwing an error.'
      );
    }

    return await this.#pipeline.run(
      request,
      requestId,
      engineRunOptions.enableInferenceProgress
        ? data => self.callMainThread("onInferenceProgress", [data])
        : null
    );
  }

  /**
   * End a run early, resolving once the backend is ready for the next one.
   * Backends that cannot do this leave the run to finish on its own.
   *
   * @param {string} requestId - The identifier of the run to cancel.
   */
  async cancel(requestId) {
    await this.#pipeline?.cancel?.(requestId);
  }

  /**
   * Glue code to connect the `MLEngineWorker` to the PromiseWorker interface.
   */
  #connectToPromiseWorker() {
    const worker = new lazy.PromiseWorker.AbstractWorker();
    worker.dispatch = (method, args = []) => {
      if (!this[method]) {
        throw new Error("Method does not exist: " + method);
      }
      return this[method](...args);
    };
    worker.close = () => self.close();
    worker.postMessage = (message, ...transfers) => {
      self.postMessage(message, ...transfers);
    };

    self.callMainThread = worker.callMainThread.bind(worker);
    self.addEventListener("message", msg => worker.handleMessage(msg));
    self.addEventListener("unhandledrejection", function (error) {
      const reason =
        error?.reason?.fail ??
        error?.reason ??
        new Error("MLEngine.worker.mjs had an unhandled error.");

      if (reason) {
        // The PromiseWorker message passing doesn't properly expose the call stack of
        // errors which makes it really hard to debug code. Log the error here to
        // ensure that nice call stacks are preserved.
        console.error("MLEngine.worker.mjs had an unhandled error.", reason);
      }

      throw reason;
    });
  }
}

new MLEngineWorker();

export { lazy as _lazyForTestMocking };

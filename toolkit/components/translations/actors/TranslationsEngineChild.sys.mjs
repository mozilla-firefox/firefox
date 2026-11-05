/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.translations.logLevel",
    logModule: "Translations",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  getEngineStateForTests:
    "chrome://global/content/translations/translations-engine.sys.mjs",
  handleActorMessage:
    "chrome://global/content/translations/translations-engine.sys.mjs",
  startEngineIdleTimeoutForTests:
    "chrome://global/content/translations/translations-engine.sys.mjs",
});

/**
 * @typedef {import("../translations").LanguagePair} LanguagePair
 * @typedef {import("../translations").TranslationsEnginePayload} TranslationsEnginePayload
 */

/**
 * Decompresses a blob of zstd-compressed data.
 *
 * The Translations models are hosted and served from Remote Settings in zstd format.
 * They need to be decompressed before they can be loaded into the WASM memory.
 *
 * @param {Blob} blob
 * @param {boolean} isMocked - Whether the TranslationsEngine payload is mocked for testing.
 * @returns {ArrayBuffer}
 */
async function decompressFromZstdBlob(blob, isMocked) {
  if (isMocked) {
    return new ArrayBuffer();
  }

  const decompressionStream = new DecompressionStream("zstd");
  const buffer = await new Response(
    blob.stream().pipeThrough(decompressionStream)
  ).arrayBuffer();

  return buffer;
}

/**
 * The engine child is responsible for exposing privileged code to the un-privileged
 * space the engine runs in.
 */
export class TranslationsEngineChild extends JSProcessActorChild {
  /**
   * The resolve function for the Promise returned by the
   * "TranslationsEngine:ForceShutdown" message.
   *
   * @type {null | () => {}}
   */
  #resolveForceShutdown = null;

  #isDestroyed = false;

  // eslint-disable-next-line consistent-return
  async receiveMessage({ name, data }) {
    if (this.#isDestroyed) {
      return undefined;
    }

    switch (name) {
      case "TranslationsEngine:StartTranslation": {
        const { languagePair, innerWindowId, port } = data;
        const message = {
          type: "StartTranslation",
          languagePair,
          innerWindowId,
          port,
        };
        lazy.handleActorMessage(message);
        break;
      }
      case "TranslationsEngine:DiscardTranslations": {
        const { innerWindowId } = data;
        lazy.handleActorMessage({
          type: "DiscardTranslations",
          innerWindowId,
        });
        break;
      }
      case "TranslationsEngine:ForceShutdown": {
        lazy.handleActorMessage({
          type: "ForceShutdown",
        });
        return new Promise(resolve => {
          this.#resolveForceShutdown = resolve;
        });
      }
      case "TranslationsEngine:GetEngineState": {
        return lazy.getEngineStateForTests();
      }
      case "TranslationsEngine:StartEngineIdleTimeoutForTests": {
        const { languagePair, timeoutMs, requestId } = data;
        return lazy.startEngineIdleTimeoutForTests(
          languagePair,
          timeoutMs,
          requestId
        );
      }
      default: {
        console.error("Unknown message received", name);
      }
    }
  }

  /**
   * @param {object} options
   * @param {number?} options.startTime
   * @param {string?} options.type
   * @param {string} options.message
   * @param {number} options.innerWindowId
   */
  TE_addProfilerMarker({ startTime, type, message, innerWindowId }) {
    ChromeUtils.addProfilerMarker(
      type ? `TranslationsEngine ${type}` : "TranslationsEngine",
      { startTime, innerWindowId },
      message
    );
  }

  /**
   * Pass the message from content that the engines were shut down.
   */
  TE_resolveForceShutdown() {
    this.#resolveForceShutdown();
    this.#resolveForceShutdown = null;
  }

  /**
   * @returns {string}
   */
  TE_getLogLevel() {
    return Services.prefs.getCharPref("browser.translations.logLevel");
  }

  /**
   * Log messages if "browser.translations.logLevel" is set to "All".
   *
   * @param {...any} args
   */
  TE_log(...args) {
    lazy.console.log(...args);
  }

  /**
   * Report an error to the console.
   *
   * @param {...any} args
   */
  TE_logError(...args) {
    lazy.console.error(...args);
  }

  /**
   * Reports translation engine performance data to telemetry.
   *
   * @param {object} data
   * @param {string} data.sourceLanguage - The BCP-47 language tag of the source text.
   * @param {string} data.targetLanguage - The BCP-47 language tag of the target text.
   * @param {number} data.totalInferenceSeconds - Total total seconds spent in active translation inference.
   * @param {number} data.totalTranslatedWords - Total total count of words that were translated.
   * @param {number} data.totalCompletedRequests - Total total count of completed translation requests.
   */
  TE_reportEnginePerformance({
    sourceLanguage,
    targetLanguage,
    totalInferenceSeconds,
    totalTranslatedWords,
    totalCompletedRequests,
  }) {
    if (this.#isDestroyed) {
      return;
    }

    this.sendAsyncMessage("TranslationsEngine:ReportEnginePerformance", {
      sourceLanguage,
      targetLanguage,
      totalInferenceSeconds,
      totalTranslatedWords,
      totalCompletedRequests,
    });
  }

  /**
   * @param {LanguagePair} languagePair
   *
   * @returns {Promise<TranslationsEnginePayload> | undefined}
   */
  async TE_requestEnginePayload(languagePair) {
    if (this.#isDestroyed) {
      return undefined;
    }

    /** @type {TranslationsEnginePayload} */
    const payload = await this.sendQuery(
      "TranslationsEngine:RequestEnginePayload",
      {
        languagePair,
      }
    );

    const wasmPromise = decompressFromZstdBlob(
      payload.bergamotWasmBlob,
      payload.isMocked
    ).then(buffer => {
      payload.bergamotWasmArrayBuffer = buffer;
      payload.bergamotWasmBlob = null;
    });

    payload.translationModelPayloads = await Promise.all(
      payload.translationModelPayloads.map(async modelPayload => {
        const entries = Object.values(modelPayload.languageModelFiles);

        await Promise.all(
          entries.map(async entry => {
            entry.buffer = await decompressFromZstdBlob(
              entry.blob,
              payload.isMocked
            );
            entry.blob = null;
          })
        );

        return modelPayload;
      })
    );

    await wasmPromise;

    return payload;
  }

  /**
   * @param {number} innerWindowId
   * @param {"ready" | "error"} status
   */
  TE_reportEngineStatus(innerWindowId, status) {
    if (this.#isDestroyed) {
      return;
    }

    this.sendAsyncMessage("TranslationsEngine:ReportEngineStatus", {
      innerWindowId,
      status,
    });
  }

  /**
   * Reports that an engine's test-only idle timer expired.
   *
   * @param {number} requestId
   */
  TE_reportEngineIdleTimeoutForTests(requestId) {
    if (!Cu.isInAutomation || this.#isDestroyed) {
      return;
    }

    this.sendAsyncMessage("TranslationsEngine:EngineIdleTimeoutForTests", {
      requestId,
    });
  }

  /**
   * No engines are still alive, signal that the process can be destroyed.
   */
  TE_destroyEngineProcess() {
    if (this.#isDestroyed) {
      return;
    }

    this.sendAsyncMessage("TranslationsEngine:DestroyEngineProcess");
  }

  didDestroy() {
    this.#isDestroyed = true;
  }
}

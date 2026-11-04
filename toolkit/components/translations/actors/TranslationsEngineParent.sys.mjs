/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  TranslationsTelemetry:
    "chrome://global/content/translations/TranslationsTelemetry.sys.mjs",
});

/**
 * @typedef {import("../translations").LanguagePair} LanguagePair
 * @typedef {import("../translations").TranslationsEnginePayload} TranslationsEnginePayload
 */

/**
 * The translations engine is in its own content process. This actor handles the
 * marshalling of the data such as the engine payload and port passing.
 */
export class TranslationsEngineParent extends JSProcessActorParent {
  /** @type {Map<number, PromiseWithResolvers<void>>} */
  #engineIdleTimeoutWaitersForTests = new Map();

  /** @type {number} */
  #nextEngineIdleTimeoutRequestIdForTests = 0;

  /**
   * Keep track of the live actors by InnerWindowID.
   *
   * @type {Map<InnerWindowID, TranslationsParent | AboutTranslationsParent>}
   */
  #translationsParents = new Map();

  /**
   * Set by EngineProcess when creating the TranslationsEngineParent.
   * Keeps the "inference" process alive until it is cleared.
   *
   * NOTE: Invalidating this keepAlive does not guarantee that the process will
   * exit, and this actor may be re-used if it does not (e.g. because the
   * inference process was kept alive by MLEngine).
   *
   * @type {nsIContentParentKeepAlive | null}
   */
  processKeepAlive = null;

  async receiveMessage({ name, data }) {
    if (this.#isDestroyed) {
      return undefined;
    }

    switch (name) {
      case "TranslationsEngine:RequestEnginePayload": {
        const { languagePair } = data;

        /** @type {Promise<TranslationsEnginePayload>} */
        const payloadPromise =
          lazy.TranslationsParent.getTranslationsEnginePayload(languagePair);

        payloadPromise.catch(error => {
          lazy.TranslationsParent.telemetry().onError(String(error));
        });

        return payloadPromise;
      }
      case "TranslationsEngine:ReportEnginePerformance": {
        const {
          sourceLanguage,
          targetLanguage,
          totalInferenceSeconds,
          totalTranslatedWords,
          totalCompletedRequests,
        } = data;
        lazy.TranslationsTelemetry.onReportEnginePerformance({
          sourceLanguage,
          targetLanguage,
          totalInferenceSeconds,
          totalTranslatedWords,
          totalCompletedRequests,
        });
        return undefined;
      }
      case "TranslationsEngine:ReportEngineStatus": {
        const { innerWindowId, status } = data;
        const translationsParent = this.#translationsParents.get(innerWindowId);

        // about:translations will not have a TranslationsParent associated with
        // this call.
        if (translationsParent) {
          switch (status) {
            case "ready":
              translationsParent.languageState.isEngineReady = true;
              break;
            case "error":
              translationsParent.languageState.error = "engine-load-failure";
              break;
            default:
              throw new Error("Unknown engine status: " + status);
          }
        }
        return undefined;
      }
      case "TranslationsEngine:DestroyEngineProcess": {
        if (this.processKeepAlive) {
          ChromeUtils.addProfilerMarker(
            "EngineProcess",
            {},
            `Dropping TranslationsEngine "inference" process keep-alive`
          );
          this.processKeepAlive.invalidateKeepAlive();
          this.processKeepAlive = null;
        }
        return undefined;
      }
      case "TranslationsEngine:EngineIdleTimeoutForTests": {
        const waiter = this.#engineIdleTimeoutWaitersForTests.get(
          data.requestId
        );
        waiter?.resolve();
        return undefined;
      }
      default: {
        return undefined;
      }
    }
  }

  /**
   * @param {LanguagePair} languagePair
   * @param {MessagePort} port
   * @param {TranslationsParent} [translationsParent]
   */
  startTranslation(languagePair, port, translationsParent) {
    const innerWindowId = translationsParent?.innerWindowId;
    if (translationsParent) {
      this.#translationsParents.set(innerWindowId, translationsParent);
    }
    if (this.#isDestroyed) {
      throw new Error("The translation engine process was already destroyed.");
    }
    const transferables = [port];
    this.sendAsyncMessage(
      "TranslationsEngine:StartTranslation",
      {
        languagePair,
        innerWindowId,
        port,
      },
      transferables
    );
  }

  /**
   * Remove all the translations that are currently queued, and remove
   * the communication port.
   *
   * @param {number} innerWindowId
   */
  discardTranslations(innerWindowId) {
    this.#translationsParents.delete(innerWindowId);
    if (this.#isDestroyed) {
      return;
    }

    this.sendAsyncMessage("TranslationsEngine:DiscardTranslations", {
      innerWindowId,
    });
  }

  /**
   * Returns observable translations engine state for tests.
   *
   * @returns {Promise<{activeEngineCount: number, activePortCount: number}>}
   */
  getEngineStateForTests() {
    return this.sendQuery("TranslationsEngine:GetEngineState");
  }

  /**
   * Starts and waits for a cached engine's idle timer in an automated test.
   *
   * @param {LanguagePair} languagePair
   * @param {number} timeoutMs
   * @returns {Promise<void>}
   */
  async waitForEngineIdleTimeoutForTests(languagePair, timeoutMs) {
    if (!Cu.isInAutomation || this.#isDestroyed) {
      throw new Error("The translations engine idle timer is unavailable.");
    }

    const requestId = ++this.#nextEngineIdleTimeoutRequestIdForTests;
    const waiter = Promise.withResolvers();
    this.#engineIdleTimeoutWaitersForTests.set(requestId, waiter);

    try {
      await Promise.all([
        this.sendQuery("TranslationsEngine:StartEngineIdleTimeoutForTests", {
          languagePair,
          timeoutMs,
          requestId,
        }),
        waiter.promise,
      ]);
    } finally {
      this.#engineIdleTimeoutWaitersForTests.delete(requestId);
    }
  }

  /**
   * Manually shuts down the engines.
   *
   * After the engine has shut down, notify each associated TranslationsParent
   * so its TranslationsDocument can discard stale ports and request a fresh
   * engine when more translations are scheduled.
   *
   * @returns {Promise<void>}
   */
  async forceShutdown() {
    try {
      return await this.sendQuery("TranslationsEngine:ForceShutdown");
    } finally {
      await Promise.allSettled(
        [...this.#translationsParents.values()].map(translationsParent =>
          translationsParent.notifyEngineTerminated()
        )
      );
      this.#translationsParents.clear();
    }
  }

  #isDestroyed = false;

  didDestroy() {
    this.#isDestroyed = true;

    // Disabling Translations via AI Controls can destroy this actor without
    // stopping the shared inference process. We should release this actor's
    // keep-alive immediately so that the process does not remain until GC runs.
    this.processKeepAlive?.invalidateKeepAlive();
    this.processKeepAlive = null;

    for (const waiter of this.#engineIdleTimeoutWaitersForTests.values()) {
      waiter.reject(
        new Error("The translations engine process was destroyed.")
      );
    }
    this.#engineIdleTimeoutWaitersForTests.clear();
  }
}

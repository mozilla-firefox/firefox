/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { createEngine } from "chrome://global/content/ml/EngineProcess.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  FunComputerAccounts: "resource:///modules/FunComputerAccounts.sys.mjs",
});

const APIKEY_PREF = "browser.smartwindow.apiKey";
const ENDPOINT_PREF = "browser.smartwindow.endpoint";
const CUSTOM_ENDPOINT_PREF = "browser.smartwindow.customEndpoint";
const CUSTOM_MODEL_CHOICE_ID = "0";
const DEFAULT_ENGINE_ID = "smart-openai";

/**
 * The default endpoint used for preset models
 */
const MOZILLA_MLPA_ENDPOINT =
  "https://mlpa-prod-prod-mozilla.freetls.fastly.net/v1";
const DEFAULT_ENDPOINT = "https://funsearchapp.netlify.app/v1";

/**
 * Transport for AI Window LLM calls against an OpenAI-style backend.
 */
export class openAIEngine {
  /**
   * Exposing createEngine for testing purposes.
   */
  static _createEngine = createEngine;

  /**
   * Main feature name. Retained on the instance so _recreateEngine() can
   * rebuild after 401 retry without the caller re-supplying it.
   *
   * @type {string | null}
   */
  feature = null;

  /**
   * Resolved model name for LLM inference. Retained on the instance for
   * _recreateEngine() (same reason as `feature`).
   *
   * @type {string | null}
   */
  model = null;

  /**
   * Engine ID used for creating the engine instance
   *
   * @type {string | null}
   */
  #engineId = null;

  /**
   * Service type used for creating the engine instance
   *
   * @type {string | null}
   */
  #serviceType = null;

  /**
   * Purpose used for creating the engine instance
   *
   * @type {string | null}
   */
  #purpose = null;

  /**
   * Flow ID for correlating frontend and backend telemetry.
   *
   * @type {string | null}
   */
  #flowId = null;

  /**
   * Base URL for this engine instance. Resolved during build time from
   * the selected model choice.
   *
   * @type {string | null}
   */
  #baseURL = null;

  /**
   * Resolved API key for this engine instance.
   *
   * @type {string | null}
   */
  #apiKey = null;

  /**
   * Checks whether a custom endpoint is configured via pref.
   *
   * @returns {boolean} True if the endpoint pref has a user-set value.
   */
  static hasCustomEndpoint() {
    return Services.prefs.prefHasUserValue(CUSTOM_ENDPOINT_PREF);
  }

  /**
   * Whether the current engine instance uses a custom model endpoint.
   *
   * @returns {boolean}
   */
  get isCustomEndpoint() {
    return this.#baseURL !== null && this.#baseURL !== MOZILLA_MLPA_ENDPOINT;
  }

  /**
   * Returns the endpoint and API key for a model choice.
   *
   * @param {string} [modelChoiceId] - Selected model choice id
   * @returns {{baseURL: string, apiKey: string}}
   * @throws {Error} If the custom model choice is selected but not configured.
   */
  static resolveEndpointConfig(modelChoiceId) {
    if (modelChoiceId === CUSTOM_MODEL_CHOICE_ID) {
      const baseURL = Services.prefs.getStringPref(CUSTOM_ENDPOINT_PREF, "");
      if (!baseURL) {
        throw new Error("Custom model choice selected but not configured");
      }
      return {
        baseURL,
        apiKey: Services.prefs.getStringPref(APIKEY_PREF, ""),
      };
    }
    return { baseURL: openAIEngine.endpoint, apiKey: openAIEngine.apiKey || "funxplorer" };
  }

  /**
   * Builds an openAIEngine instance.
   *
   * @param {object} options
   * @param {string} options.model
   * @param {string} options.serviceType
   * @param {string} options.purpose
   * @param {string|null} [options.flowId]
   * @param {string} options.feature
   * @param {string} [options.baseURL] - Endpoint base URL
   * @param {string} [options.apiKey] - API key for the endpoint
   * @returns {Promise<openAIEngine>}
   */
  static async build({
    model,
    serviceType,
    purpose,
    flowId,
    feature,
    baseURL = openAIEngine.endpoint,
    apiKey = "",
  }) {
    const engine = new openAIEngine();
    const engineId = `${DEFAULT_ENGINE_ID}-${feature}-${model}`;
    engine.#engineId = engineId;
    engine.feature = feature;
    engine.model = model;
    engine.#serviceType = serviceType;
    engine.#purpose = purpose;
    engine.#flowId = flowId;
    engine.#baseURL = baseURL;
    engine.#apiKey = apiKey;
    engine.engineInstance = await openAIEngine.#createOpenAIEngine(
      engineId,
      serviceType,
      purpose,
      model,
      flowId,
      feature,
      baseURL,
      apiKey
    );
    return engine;
  }

  /**
   * Retrieves the FunComputer account token used for Smart Window API calls.
   *
   * @returns {Promise<string|null>}
   */
  static async getFxAccountToken() {
    try {
      const session = await lazy.FunComputerAccounts.getSession();
      return session?.access_token || null;
    } catch (error) {
      console.warn("Error obtaining FunComputer token:", error);
      return null;
    }
  }

  /**
   * MLPA returns 429 for several sub-conditions (budget, QPS, upstream limit);
   * callers should back off the same way regardless of the sub-code.
   *
   * @param {Error} error
   * @returns {boolean}
   */
  static is429Error(error) {
    if (!error) {
      return false;
    }
    return error.status === 429 || !!error.message?.includes("429 status code");
  }

  /**
   * Checks if an error is transient and worth retrying: rate limits (429),
   * timeouts (408), conflicts (409), server errors (>= 500), and network-layer
   * failures. Deterministic errors (400/401/403/404/422) are NOT retryable.
   *
   * @param {Error} error  The error to check
   * @returns {boolean}    True if the error is transient and worth retrying
   */
  static isRetryableError(error) {
    if (!error) {
      return false;
    }
    if (this.is429Error(error)) {
      return true;
    }
    const isRetryableStatus = status =>
      status === 408 || status === 409 || (status >= 500 && status <= 599);
    if (typeof error.status === "number" && isRetryableStatus(error.status)) {
      return true;
    }
    // Some backends only encode the status in the message text, e.g.
    // "... 503 status code ...".
    const messageStatus = error.message?.match(/(\d{3}) status code/)?.[1];
    if (messageStatus && isRetryableStatus(Number(messageStatus))) {
      return true;
    }
    // Network-layer failures expose no HTTP status; in Gecko they surface as
    // NS_ERROR_* names or generic network/timeout messages.
    const text = `${error.name ?? ""} ${error.message ?? ""}`;
    return /NS_ERROR_(NET_|CONNECTION|PROXY)|NetworkError|connection (refused|reset)|timed? ?out/i.test(
      text
    );
  }

  /**
   * Creates an OpenAI engine instance
   *
   * @param {string} engineId     The identifier for the engine instance
   * @param {string} serviceType  The type of message to be sent ("ai", "memories", "s2s")
   * @param {string} purpose      The purpose of the request, used for telemetry tracking
   * @param {string | null} modelId  The resolved model ID (already contains fallback logic)
   * @param {string | null} flowId   Flow ID for correlating frontend and backend telemetry
   * @param {string | null} featureId - Feature name passed to PipelineOptions
   * @param {string} baseURL - The endpoint base URL for this engine instance
   * @param {string} apiKey - The API key for this engine instance
   * @returns {Promise<object>} - The configured engine instance
   */
  static async #createOpenAIEngine(
    engineId,
    serviceType,
    purpose,
    modelId = null,
    flowId = null,
    featureId = null,
    baseURL,
    apiKey
  ) {
    const extraHeadersPref = Services.prefs.getStringPref(
      "browser.smartwindow.extraHeaders",
      "{}"
    );
    let extraHeaders = {};
    try {
      extraHeaders = JSON.parse(extraHeadersPref);
    } catch (e) {
      console.error("Failed to parse extra headers from prefs:", e);
      Services.prefs.clearUserPref("browser.smartwindow.extraHeaders");
    }

    try {
      const engineInstance = await openAIEngine._createEngine({
        apiKey,
        backend: "openai",
        baseURL,
        engineId,
        featureId,
        flowId,
        modelId,
        modelRevision: "main",
        taskName: "text-generation",
        serviceType,
        purpose,
        extraHeaders,
      });
      return engineInstance;
    } catch (error) {
      console.error("Failed to create OpenAI engine:", error);
      throw error;
    }
  }

  /**
   * Wrapper around engine.run to send message to the LLM
   * Will eventually use `usage` from the LiteLLM API response for token telemetry
   *
   * @param {Map<string, any>} content  OpenAI formatted messages to be sent to the LLM
   * @returns {object}                  LLM response
   */
  async run(content) {
    return await this._runWithAuth(content);
  }

  /**
   * Helper method to handle 401 authentication errors and retry with new token.
   *
   * @param {Map<string, any>} content  OpenAI formatted messages to be sent to the LLM
   * @returns {object}                  LLM response
   */
  async _runWithAuth(content) {
    try {
      return await this.engineInstance.run(content);
    } catch (ex) {
      // Skip the token retry flow when using a custom endpoint,
      // as the retry logic only applies to FxAccounts tokens.
      if (!this._is401Error(ex) || this.isCustomEndpoint) {
        throw ex;
      }

      console.warn(
        "LLM request returned a 401 - retrying with a fresh FunComputer token"
      );

      await this._recreateEngine();

      const newToken = await openAIEngine.getFxAccountToken();
      const updatedContent = { ...content, fxAccountToken: newToken };

      return await this.engineInstance.run(updatedContent);
    }
  }

  /**
   * Recreates the engine instance with current configuration.
   *
   * @returns {Promise<void>}
   * @private
   */
  async _recreateEngine() {
    if (!this.#engineId || !this.#serviceType) {
      console.warn("Cannot recreate engine: missing engineId or serviceType");
      return;
    }

    this.engineInstance = await openAIEngine.#createOpenAIEngine(
      this.#engineId,
      this.#serviceType,
      this.#purpose,
      this.model,
      this.#flowId,
      this.feature,
      this.#baseURL,
      this.#apiKey
    );
  }

  /**
   * Checks if an error is a 401 authentication error.
   *
   * @param {Error} error  The error to check
   * @returns {boolean}    True if the error is a 401 error
   * @private
   */
  _is401Error(error) {
    if (!error) {
      return false;
    }

    return error.status === 401 || error.message?.includes("401 status code");
  }

  /**
   * Helper async generator to handle 401 authentication errors and retry with new token for streaming requests.
   *
   * @param {Map<string, any>} options  OpenAI formatted messages with streaming and tooling options to be sent to the LLM
   * @yields {object}                   LLM streaming response chunks
   */
  async *_runWithGeneratorAuth(options) {
    // AbortSignal cannot be cloned via postMessage (structured clone algorithm).
    const { signal, ...engineOptions } = options;
    try {
      const generator = this.engineInstance.runWithGenerator(engineOptions);
      for await (const chunk of generator) {
        if (signal?.aborted) {
          return;
        }
        yield chunk;
      }
    } catch (ex) {
      // Skip the token retry flow when using a custom endpoint,
      // as the retry logic only applies to FxAccounts tokens.
      if (!this._is401Error(ex) || this.isCustomEndpoint) {
        throw ex;
      }

      console.warn(
        "LLM streaming request returned a 401 - retrying with a fresh FunComputer token"
      );

      await this._recreateEngine();

      const newToken = await openAIEngine.getFxAccountToken();
      const updatedOptions = { ...engineOptions, fxAccountToken: newToken };

      const generator = this.engineInstance.runWithGenerator(updatedOptions);
      for await (const chunk of generator) {
        if (signal?.aborted) {
          return;
        }
        yield chunk;
      }
    }
  }

  /**
   * Wrapper around engine.runWithGenerator to send message to the LLM
   * Will eventually use `usage` from the LiteLLM API response for token telemetry
   *
   * @param {Map<string, any>} options  OpenAI formatted messages with streaming and tooling options to be sent to the LLM
   * @returns {AsyncGenerator}          LLM streaming response
   */
  runWithGenerator(options) {
    return this._runWithGeneratorAuth(options);
  }
}

XPCOMUtils.defineLazyPreferenceGetter(
  openAIEngine,
  "endpoint",
  ENDPOINT_PREF,
  DEFAULT_ENDPOINT
);

XPCOMUtils.defineLazyPreferenceGetter(openAIEngine, "apiKey", APIKEY_PREF, "");

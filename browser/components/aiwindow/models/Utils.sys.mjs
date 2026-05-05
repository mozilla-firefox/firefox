/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * This module defines utility functions and classes needed for invoking LLMs such as:
 * - Creating and running OpenAI engine instances
 * - Rendering prompts from files
 */

import { createEngine } from "chrome://global/content/ml/EngineProcess.sys.mjs";
import { getFxAccountsSingleton } from "resource://gre/modules/FxAccounts.sys.mjs";
import {
  OAUTH_CLIENT_ID,
  SCOPE_PROFILE_UID,
  SCOPE_SMART_WINDOW,
} from "resource://gre/modules/FxAccountsCommon.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
});

const APIKEY_PREF = "browser.smartwindow.apiKey";
const MODEL_PREF = "browser.smartwindow.model";
const ENDPOINT_PREF = "browser.smartwindow.endpoint";
const MODEL_CHOICE_PREF = "browser.smartwindow.firstrun.modelChoice";
const GENERIC_MODEL_NAME = "generic";

/**
 * Default engine ID used for all AI Window features
 */
export const DEFAULT_ENGINE_ID = "smart-openai";

/**
 * Observer for model preference changes.
 * Invalidates the Remote Settings client cache when user changes their model preference.
 */
const modelPrefObserver = {
  observe(_subject, topic, data) {
    if (topic === "nsPref:changed" && data === MODEL_PREF) {
      console.warn(
        "Model preference changed, invalidating Remote Settings cache"
      );
      openAIEngine._remoteClient = null;
    }
  },
};
Services.prefs.addObserver(MODEL_PREF, modelPrefObserver);

/**
 * Feature identifiers for AI Window model, configurations and prompts.
 * These are used to look up model configs, prompts, and inference parameters
 * from Remote Settings or local defaults.
 */
export const MODEL_FEATURES = Object.freeze({
  CHAT: "chat",
  TITLE_GENERATION: "title-generation",
  CONVERSATION_STARTERS_SIDEBAR_SYSTEM: "conversation-starters-sidebar-system",
  CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER:
    "conversation-suggestions-sidebar-starter",
  CONVERSATION_SUGGESTIONS_FOLLOWUP: "conversation-suggestions-followup",
  CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS:
    "conversation-suggestions-assistant-limitations",
  CONVERSATION_SUGGESTIONS_MEMORIES: "conversation-suggestions-memories",
  // memories generation features
  MEMORIES_INITIAL_GENERATION_SYSTEM: "memories-initial-generation-system",
  MEMORIES_INITIAL_GENERATION_USER: "memories-initial-generation-user",
  MEMORIES_DEDUPLICATION_SYSTEM: "memories-deduplication-system",
  MEMORIES_DEDUPLICATION_USER: "memories-deduplication-user",
  MEMORIES_SENSITIVITY_FILTER_SYSTEM: "memories-sensitivity-filter-system",
  MEMORIES_SENSITIVITY_FILTER_USER: "memories-sensitivity-filter-user",
  // memories usage features
  MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM:
    "memories-message-classification-system",
  MEMORIES_MESSAGE_CLASSIFICATION_USER: "memories-message-classification-user",
  // real time context
  REAL_TIME_CONTEXT_DATE: "real-time-context-date",
  REAL_TIME_CONTEXT_TAB: "real-time-context-tab",
  REAL_TIME_CONTEXT_MENTIONS: "real-time-context-mentions",
  MEMORIES_RELEVANT_CONTEXT: "memories-relevant-context",
  DISABLE_TABLE_INSTRUCTIONS: "disable-table-instructions",
  ENABLE_TABLE_INSTRUCTIONS: "enable-table-instructions",
});

/**
 * Default model IDs for each feature.
 * These are Mozilla's recommended models, used when user hasn't configured
 * custom settings or when remote setting retrieval fails.
 */
export const DEFAULT_MODEL = Object.freeze({
  [MODEL_FEATURES.CHAT]: "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.TITLE_GENERATION]: "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER]:
    "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP]:
    "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS]:
    "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_INSIGHTS]:
    "qwen3-235b-a22b-instruct-2507-maas",
  // memories generation flow
  [MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM]: "gemini-2.5-flash-lite",
  [MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_USER]: "gemini-2.5-flash-lite",
  [MODEL_FEATURES.MEMORIES_DEDUPLICATION_SYSTEM]: "gemini-2.5-flash-lite",
  [MODEL_FEATURES.MEMORIES_DEDUPLICATION_USER]: "gemini-2.5-flash-lite",
  [MODEL_FEATURES.MEMORIES_SENSITIVITY_FILTER_SYSTEM]: "gemini-2.5-flash-lite",
  [MODEL_FEATURES.MEMORIES_SENSITIVITY_FILTER_USER]: "gemini-2.5-flash-lite",
  // memories usage flow
  [MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM]:
    "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_USER]:
    "qwen3-235b-a22b-instruct-2507-maas",
  [MODEL_FEATURES.MEMORIES_RELEVANT_CONTEXT]:
    "qwen3-235b-a22b-instruct-2507-maas",
});

/**
 * Service types for different AI Window features
 */
export const SERVICE_TYPES = Object.freeze({
  AI: "ai",
  MEMORIES: "memories",
});

/**
 * Purposes for different AI Window features, used to track usage and performance in telemetry
 */
export const PURPOSES = Object.freeze({
  CHAT: "chat",
  TITLE_GENERATION: "title-generation",
  CONVERSATION_STARTERS_SIDEBAR: "convo-starters-sidebar",
  MEMORY_GENERATION: "memory-generation",
});

/**
 * Default purposes for different AI Window features, used to track usage and performance in telemetry
 * (purposes are now defined in remote-settings)
 */
export const DEFAULT_PURPOSE = "default";
export const FEATURE_PURPOSES = Object.freeze({
  DEFAULT_PURPOSE: PURPOSES.CHAT,
  [MODEL_FEATURES.CHAT]: PURPOSES.CHAT,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER]:
    PURPOSES.CONVERSATION_STARTERS_SIDEBAR,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP]:
    PURPOSES.CONVERSATION_STARTERS_SIDEBAR,
  [MODEL_FEATURES.TITLE_GENERATION]: PURPOSES.TITLE_GENERATION,
  [MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM]:
    PURPOSES.MEMORY_GENERATION,
  [MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM]:
    PURPOSES.MEMORY_GENERATION,
});

/**
 * Major version compatibility requirements for each feature.
 * When incrementing a feature's major version:
 * - Update this constant
 * - Ensure Remote Settings has configs for the new major version
 * - Old clients will continue using old major version
 */
export const FEATURE_MAJOR_VERSIONS = Object.freeze({
  [MODEL_FEATURES.CHAT]: 4,
  [MODEL_FEATURES.TITLE_GENERATION]: 1,
  [MODEL_FEATURES.CONVERSATION_STARTERS_SIDEBAR_SYSTEM]: 1,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER]: 2,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP]: 1,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS]: 1,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_INSIGHTS]: 1,
  // memories generation feature versions
  [MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM]: 1,
  [MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_USER]: 1,
  [MODEL_FEATURES.MEMORIES_DEDUPLICATION_SYSTEM]: 1,
  [MODEL_FEATURES.MEMORIES_DEDUPLICATION_USER]: 1,
  [MODEL_FEATURES.MEMORIES_SENSITIVITY_FILTER_SYSTEM]: 1,
  [MODEL_FEATURES.MEMORIES_SENSITIVITY_FILTER_USER]: 1,
  // memories usage feature versions
  [MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM]: 1,
  [MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_USER]: 1,
  [MODEL_FEATURES.MEMORIES_RELEVANT_CONTEXT]: 2,
});

/**
 * Remote Settings configuration record structure
 *
 * @typedef {object} RemoteSettingsConfig
 * @property {string} feature - Feature identifier
 * @property {string} model - Model identifier for LLM inference
 * @property {string} prompts - Prompt template content
 * @property {string} version - Version string in "v{major}.{minor}" format
 * @property {boolean} [is_default] - Whether this is the default config for the feature
 * @property {object} [parameters] - Optional inference parameters (e.g., temperature)
 * @property {string[]} [additional_components] - Optional list of dependent feature configs
 */

/**
 * @typedef {object} RemoteSettingsClient
 * @property {() => Promise<object[]>} get - Function to get records from remote settings
 */

/**
 * Parses a version string in the format "{major}.{minor}".
 *
 * @param {string} versionString - Version string to parse (e.g., "1.2")
 * @returns {object|null} Parsed version with major and minor numbers, or null if invalid
 */
export function parseVersion(versionString) {
  const match = /^v?(\d+)\.(\d+)$/.exec(versionString || "");
  if (!match) {
    return null;
  }
  return {
    major: Number(match[1]),
    minor: Number(match[2]),
    original: versionString,
  };
}

/**
 * Verifies that the RS record is matches the current Fx build
 *
 * @param {string} recordVersion {majorVersion}.{minorVersion}
 * @param {string} comparisonVersion major version supported by this build
 * @returns {boolean} whether or not major version in recordVersion matches comparisonVersion
 */
export function checkMajorVersion(recordVersion, comparisonVersion) {
  const parsed = parseVersion(recordVersion);
  return parsed && parsed.major == comparisonVersion;
}

/**
 * Selects the main configuration for a feature based on version and model preferences.
 *
 * Remote Settings maintains only the latest minor version for each (feature, model, major_version) combination.
 *
 * Selection logic:
 * 1. Filter to configs matching the required major version
 * 2. If user has model preference, find that model's config
 * 3. Otherwise, find the default config (is_default: true)
 *
 * @param {Array} featureConfigs - All configs for the feature from Remote Settings
 * @param {object} options - Selection options
 * @param {number} options.majorVersion - Required major version for the feature
 * @param {string} options.userModel - User's preferred model (empty string if none)
 * @param {string} options.modelChoiceId
 * @param {string} options.feature
 * @returns {object|null} Selected config or null if no match
 */
function selectMainConfig(
  featureConfigs,
  { majorVersion, userModel, modelChoiceId, feature }
) {
  // Filter to configs matching the required major version
  const sameMajor = featureConfigs.filter(config =>
    checkMajorVersion(config.version, majorVersion)
  );

  if (sameMajor.length === 0) {
    console.warn(`Missing featureConfigs for major version ${majorVersion}`);
    return null;
  }

  // We only allow customization of main assistant model unless user is
  //  using custom endpoint (which is handled by _applyCustomEndpointModel)
  if (feature === MODEL_FEATURES.CHAT) {
    // If user specified a model preference, find that model's config
    if (userModel) {
      const userModelConfig = sameMajor.find(
        config => config.model === userModel
      );
      if (userModelConfig) {
        return userModelConfig;
      }
      // User's model not found in this major version - fall through to defaults
      console.warn(
        `User model "${userModel}" not found for major version ${majorVersion} for feature '${feature}', using modelChoice ${modelChoiceId}`
      );
    }

    // If user specified a model preference, find that model's config
    if (modelChoiceId) {
      const userModelConfig = sameMajor.find(
        config => config.model_choice_id == modelChoiceId
      );
      if (userModelConfig) {
        return userModelConfig;
      }
      // User's model not found in this major version - fall through to defaults
      console.warn(
        `User model choice "${modelChoiceId}" not found for major version ${majorVersion} for feature '${feature}', using default`
      );
    }
  }

  // No user model pref OR user's model not found: use default
  const defaultConfig = sameMajor.find(config => config.is_default === true);
  if (defaultConfig) {
    return defaultConfig;
  }

  // No default found - this shouldn't happen with proper Remote Settings data
  console.warn(`No default config found for major version ${majorVersion}`);
  return null;
}

/**
 * openAIEngine class
 *
 * Contains methods to create engine instances and estimate token usage.
 */
export class openAIEngine {
  /**
   * Exposing createEngine for testing purposes.
   */
  static _createEngine = createEngine;

  /**
   *  The Remote Settings collection name for AI window prompt configurations
   */
  static RS_AI_WINDOW_COLLECTION = "ai-window-prompts";

  /**
   * Cached Remote Settings client
   * Cache is invalidated when user changes MODEL_PREF pref via modelPrefObserver
   *
   * @type {RemoteSettingsClient | null}
   */
  static _remoteClient = null;

  /**
   * Configuration map: { featureName: configObject }
   *
   * @type {object | null}
   */
  #configs = null;

  /**
   * Main feature name
   *
   * @type {string | null}
   */
  feature = null;

  /**
   * Resolved model name for LLM inference
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
   * Feature name passed to PipelineOptions as featureId for telemetry.
   *
   * @type {string | null}
   */
  #feature = null;

  /**
   * Flow ID for correlating frontend and backend telemetry.
   *
   * @type {string | null}
   */
  #flowId = null;

  /**
   * Gets the Remote Settings client for AI window configurations.
   *
   * @returns {RemoteSettingsClient}
   */
  static getRemoteClient() {
    if (openAIEngine._remoteClient) {
      return openAIEngine._remoteClient;
    }

    const client = lazy.RemoteSettings(openAIEngine.RS_AI_WINDOW_COLLECTION, {
      bucketName: "main",
    });

    openAIEngine._remoteClient = client;
    return client;
  }

  /**
   * Overrides the model when using a custom endpoint.
   * Only called after Remote Settings config has been loaded.
   *
   * @private
   */
  _applyCustomEndpointModel() {
    const userModel = Services.prefs.getStringPref(MODEL_PREF, "");
    if (userModel) {
      console.warn(
        `Using custom model "${userModel}" for feature: ${this.feature}`
      );
      this.model = userModel;
    }
  }

  /**
   * Overrides the model config with generic config
   *
   * @param {Array} featureConfigs - All configs for the feature from Remote Settings
   * @param {number} majorVersion - Required major version for the feature
   *
   * @private
   */
  _loadGenericChatPrompt(featureConfigs, majorVersion) {
    console.warn(`Custom endpoint detected. Using generic chat prompt`);
    this.#configs[MODEL_FEATURES.CHAT] = selectMainConfig(featureConfigs, {
      majorVersion,
      userModel: GENERIC_MODEL_NAME,
      modelChoiceId: "",
      feature: MODEL_FEATURES.CHAT,
    });
  }

  /**
   * Applies configuration from Remote Settings with version-aware selection.
   *
   * @param {string} feature - The feature identifier
   * @param {Array} allRecords - All Remote Settings records
   * @param {Array} featureConfigs - Remote Settings configs for this feature
   * @param {number} majorVersion - Required major version
   * @private
   */
  _applyRemoteSettingsConfig(
    feature,
    allRecords,
    featureConfigs,
    majorVersion
  ) {
    if (!featureConfigs.length) {
      const msg = `No Remote Settings records found for feature: ${feature}`;
      console.error(msg);
      throw new Error(msg);
    }

    const userModel = Services.prefs.getStringPref(MODEL_PREF, "");
    const hasCustomModel = Services.prefs.prefHasUserValue(MODEL_PREF);
    const modelChoiceId = Services.prefs.getStringPref(MODEL_CHOICE_PREF, "");

    const mainConfig = selectMainConfig(featureConfigs, {
      majorVersion,
      userModel: hasCustomModel ? userModel : "",
      modelChoiceId,
      feature,
    });

    if (!mainConfig) {
      const msg = `No matching model config found for feature: ${feature} with major version ${majorVersion};`;
      console.error(msg);
      throw new Error(msg);
    }

    this.feature = feature;
    this.model = mainConfig.model;

    // Parse JSON string fields if needed
    if (typeof mainConfig.additional_components === "string") {
      try {
        mainConfig.additional_components = JSON.parse(
          mainConfig.additional_components
        );
      } catch (e) {
        // Fallback: parse malformed array string like "[item1, item2, item3]"
        const match = /^\[([^\]]*)\]$/.exec(
          mainConfig.additional_components.trim()
        );
        if (match) {
          mainConfig.additional_components = match[1]
            .split(",")
            .map(s => s.trim())
            .filter(s => !!s.length);
        } else {
          console.warn(
            `Failed to parse additional_components for ${feature}, setting to empty array`
          );
          mainConfig.additional_components = [];
        }
      }
    }
    if (typeof mainConfig.parameters === "string") {
      try {
        mainConfig.parameters = JSON.parse(mainConfig.parameters);
      } catch (e) {
        console.warn(`Failed to parse parameters for ${feature}:`, e);
        mainConfig.parameters = {};
      }
    }

    // Build configsMap for looking up additional_components
    const configsMap = new Map(allRecords.map(r => [r.feature, r]));

    // Build configs map: { featureName: configObject }
    this.#configs = {};
    this.#configs[feature] = mainConfig;

    // Add additional_components if exists
    // This field lists what other remote settings configs are needed
    // as dependency to the current feature.
    if (mainConfig.additional_components) {
      for (const componentFeature of mainConfig.additional_components) {
        const componentConfig = configsMap.get(componentFeature);
        if (componentConfig) {
          this.#configs[componentFeature] = componentConfig;
        } else {
          console.warn(
            `Additional component "${componentFeature}" not found in Remote Settings`
          );
        }
      }
    }
  }

  /**
   * Loads configuration from Remote Settings with version-aware selection.
   *
   * Selection logic:
   * 1. Filter configs by feature and major version compatibility
   * 2. If user has model preference, find latest minor for that model
   * 3. Otherwise, find latest minor among default configs
   * 4. Fall back to latest minor overall if no defaults
   * 5. Fall back to local defaults if no matching major version
   * 6. If custom endpoint is set, override model with pref value
   *
   * @param {string} feature - The feature identifier from MODEL_FEATURES
   * @param {number} majorVersionOverride - Used to override hardcoded major version
   * @returns {Promise<void>}
   *   Sets this.feature to the feature name
   *   Sets this.model to the selected model ID
   *   Sets this.#configs to contain feature's and additional_components' configs
   */
  async loadConfig(feature, majorVersionOverride = null) {
    const client = openAIEngine.getRemoteClient();
    const allRecords = await client.get();

    const featureConfigs = allRecords.filter(
      record => record.feature === feature
    );

    const majorVersion =
      majorVersionOverride ?? FEATURE_MAJOR_VERSIONS[feature];

    this._applyRemoteSettingsConfig(
      feature,
      allRecords,
      featureConfigs,
      majorVersion
    );

    if (openAIEngine.hasCustomEndpoint()) {
      if (feature === MODEL_FEATURES.CHAT) {
        this._loadGenericChatPrompt(featureConfigs, majorVersion);
      }
      this._applyCustomEndpointModel();
    }
  }

  /**
   * Checks whether a custom endpoint is configured via pref.
   *
   * @returns {boolean} True if the endpoint pref has a user-set value.
   */
  static hasCustomEndpoint() {
    return Services.prefs.prefHasUserValue(ENDPOINT_PREF);
  }

  /**
   * Gets the configuration for a specific feature.
   *
   * @param {string} [feature] - The feature identifier. Defaults to the main feature.
   * @returns {object|null} The feature's configuration object
   */
  getConfig(feature) {
    const targetFeature = feature || this.feature;
    // load custom prompt pref if exists
    // custom prompts should be entered as { feature_name: prompt }
    const prefPromptRaw = Services.prefs.getStringPref(
      "browser.smartwindow.customPrompts",
      ""
    );
    let prefPrompt = null;
    if (prefPromptRaw) {
      try {
        prefPrompt = JSON.parse(prefPromptRaw);
      } catch (e) {
        console.warn(
          "browser.smartwindow.customPrompts contains invalid JSON. Expecting: { feature: prompt }",
          e
        );
      }
    }
    const prefPromptMapping = prefPrompt?.[targetFeature]
      ? { prompts: prefPrompt[targetFeature] }
      : null;

    return {
      ...this.#configs?.[targetFeature],
      ...prefPromptMapping,
    };
  }

  /**
   * Loads a prompt for the specified feature.
   * Tries Remote Settings first, then falls back to local prompts.
   *
   * @param {string} feature - The feature identifier
   * @returns {Promise<string>} The prompt content
   */
  async loadPrompt(feature) {
    const config = this.getConfig(feature);
    if (config?.prompts) {
      return config.prompts;
    }

    console.error(`Failed to load prompt for ${feature}`);
    throw new Error(`Failed to load prompt for ${feature}`);
  }

  /**
   * Builds an openAIEngine instance with configuration loaded from Remote Settings.
   *
   * @param {string} feature
   *   The feature name to use to retrieve remote settings for prompts.
   * @param {string | null} [flowId]
   *   Flow ID for correlating frontend and backend telemetry.
   * @returns {Promise<object>}
   *   Promise that will resolve to the configured engine instance.
   */
  static async build(feature, flowId = null) {
    const engine = new openAIEngine();

    await engine.loadConfig(feature);

    const config = engine.getConfig(feature);
    const engineId = `${DEFAULT_ENGINE_ID}-${feature}`;
    engine.#engineId = engineId;
    engine.#serviceType =
      config?.service_type ?? getDefaultServiceType(feature);
    engine.#purpose =
      config?.purpose ??
      FEATURE_PURPOSES[feature] ??
      FEATURE_PURPOSES[DEFAULT_PURPOSE];
    engine.#feature = feature;
    engine.#flowId = flowId;

    engine.engineInstance = await openAIEngine.#createOpenAIEngine(
      engineId,
      engine.#serviceType,
      engine.#purpose,
      engine.model,
      flowId,
      feature
    );

    return engine;
  }

  /**
   * Retrieves the Firefox account token
   *
   * @returns {Promise<string|null>}   The Firefox account token (string) or null
   */
  static async getFxAccountToken() {
    try {
      const fxAccounts = getFxAccountsSingleton();
      return await fxAccounts.getOAuthToken({
        scope: [SCOPE_SMART_WINDOW, SCOPE_PROFILE_UID],
        client_id: OAUTH_CLIENT_ID,
      });
    } catch (error) {
      console.warn("Error obtaining FxA token:", error);
      return null;
    }
  }

  /**
   * Creates an OpenAI engine instance
   *
   * @param {string} engineId     The identifier for the engine instance
   * @param {string} serviceType  The type of message to be sent ("ai", "memories", "s2s")
   * @param {string} purpose      The purpose of the request, used for telemetry tracking
   * @param {string | null} modelId  The resolved model ID (already contains fallback logic)
   * @param {string | null} flowId   Flow ID for correlating frontend and backend telemetry
   * @param {string | null} featureId  Feature name passed to PipelineOptions
   * @returns {Promise<object>}   The configured engine instance
   */
  static async #createOpenAIEngine(
    engineId,
    serviceType,
    purpose,
    modelId = null,
    flowId = null,
    featureId = null
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
        apiKey: this.hasCustomEndpoint() ? this.apiKey : "",
        backend: "openai",
        baseURL: this.endpoint,
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
      if (!this._is401Error(ex) || openAIEngine.hasCustomEndpoint()) {
        throw ex;
      }

      console.warn(
        "LLM request returned a 401 - revoking our token and retrying"
      );

      const fxAccounts = getFxAccountsSingleton();
      const oldToken = content.fxAccountToken;
      if (oldToken) {
        await fxAccounts.removeCachedOAuthToken({ token: oldToken });
      }

      await this._recreateEngine();

      const newToken = await openAIEngine.getFxAccountToken();
      const updatedContent = { ...content, fxAccountToken: newToken };

      try {
        return await this.engineInstance.run(updatedContent);
      } catch (retryEx) {
        if (!this._is401Error(retryEx)) {
          throw retryEx;
        }

        console.warn(
          "Retry LLM request still returned a 401 - revoking our token and failing"
        );

        if (newToken) {
          await fxAccounts.removeCachedOAuthToken({ token: newToken });
        }

        throw retryEx;
      }
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
      this.#feature
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
    // Extract signal before passing options to engineInstance — AbortSignal
    // cannot be cloned via postMessage (structured clone algorithm).
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
      if (!this._is401Error(ex) || openAIEngine.hasCustomEndpoint()) {
        throw ex;
      }

      console.warn(
        "LLM streaming request returned a 401 - revoking our token and retrying"
      );

      const fxAccounts = getFxAccountsSingleton();
      const oldToken = options.fxAccountToken;
      if (oldToken) {
        await fxAccounts.removeCachedOAuthToken({ token: oldToken });
      }

      await this._recreateEngine();

      const newToken = await openAIEngine.getFxAccountToken();
      const updatedOptions = { ...engineOptions, fxAccountToken: newToken };

      try {
        const generator = this.engineInstance.runWithGenerator(updatedOptions);
        for await (const chunk of generator) {
          if (signal?.aborted) {
            return;
          }
          yield chunk;
        }
      } catch (retryEx) {
        if (!this._is401Error(retryEx)) {
          throw retryEx;
        }

        console.warn(
          "Retry LLM streaming request still returned a 401 - revoking our token and failing"
        );

        if (newToken) {
          await fxAccounts.removeCachedOAuthToken({ token: newToken });
        }

        throw retryEx;
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
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(openAIEngine, "apiKey", APIKEY_PREF, "");

/**
 * Resolves chat model metadata for a given choice ID from Remote Settings.
 *
 * @param {string} choiceId - Model choice ID (e.g., "1", "2", "3")
 * @param {number} [maxMajorVersion] - Maximum major version to include
 * @returns {Promise<{model: string, ownerName: string}|null>}
 *   Returns null if choice ID not found in Remote Settings
 */
export async function resolveChatModelChoice(
  choiceId,
  maxMajorVersion = FEATURE_MAJOR_VERSIONS[MODEL_FEATURES.CHAT]
) {
  if (choiceId === "0") {
    // Custom model - no RS lookup needed
    return {
      model: "custom-model",
      ownerName: "",
    };
  }

  try {
    const client = openAIEngine.getRemoteClient();
    const allRecords = await client.get();

    const record = selectMainConfig(
      allRecords.filter(r => r.feature === MODEL_FEATURES.CHAT),
      {
        majorVersion: maxMajorVersion,
        feature: MODEL_FEATURES.CHAT,
        modelChoiceId: choiceId,
      }
    );
    if (!record) {
      return null;
    }

    return {
      model: record.model,
      ownerName: record.owner_name ?? "",
    };
  } catch (error) {
    console.warn(
      "Failed to resolve chat model choice from Remote Settings:",
      error
    );
    return null;
  }
}

/**
 * Renders a prompt from a string, replacing placeholders with provided strings.
 *
 * @param {string} rawPromptContent               The raw prompt as a string
 * @param {Map<string, string>} stringsToReplace  A map of placeholder strings to their replacements
 * @returns {Promise<string>}                     The rendered prompt
 */
export function renderPrompt(rawPromptContent, stringsToReplace = {}) {
  let finalPromptContent = rawPromptContent;

  for (const [orig, repl] of Object.entries(stringsToReplace)) {
    const regex = new RegExp(`{${orig}}`, "g");
    finalPromptContent = finalPromptContent.replace(regex, () => repl);
  }

  return finalPromptContent;
}

function getDefaultServiceType(feature) {
  if (feature.startsWith("memories")) {
    return SERVICE_TYPES.MEMORIES;
  }
  return SERVICE_TYPES.AI;
}

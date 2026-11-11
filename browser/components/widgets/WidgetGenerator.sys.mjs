/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  DECLINE_KINDS,
  TOOLS,
} from "moz-src:///browser/components/widgets/Tools.sys.mjs";
import { Workspace } from "moz-src:///browser/components/widgets/Workspace.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  Conversation:
    "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs",
  GeneratedWidgetsFeature:
    "moz-src:///browser/components/widgets/GeneratedWidgetsFeature.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SERVICE_TYPES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  openAIEngine:
    "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs",
  parseVersion: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  console.createInstance({
    logModule: "WidgetGenerator",
    maxLogLevel: "Warn",
    maxLogLevelPref: "browser.widgets.generated.loglevel",
  })
);

const ENDPOINT_PREF = "browser.widgets.generated.endpoint";
const API_KEY_PREF = "browser.widgets.generated.apiKey";
const MODEL_PREF = "browser.widgets.generated.model";
const MAX_TURNS_PREF = "browser.widgets.generated.maxTurns";
const LOCAL_PROMPT_PREF = "browser.widgets.generated.localPrompt";

const COLLECTION = "widget-generation";
const LOCAL_PROMPT_URL = "chrome://browser/content/widgets/prompt.md";

/**
 * The record version this build reads. Bump it when the tools or file checks
 * change.
 */
export const PROMPT_MAJOR_VERSION = 2;

/**
 * The codes of a WidgetGeneratorError.
 */
export const GENERATION_ERRORS = Object.freeze({
  /** AI Controls block custom widgets. */
  BLOCKED: "blocked",
  /** No account is signed in. */
  AUTH: "auth",
  /** The model can't be reached. */
  SERVICE: "service",
  /** The model's files don't make a widget. */
  GENERATION: "generation",
  /** The model declined a request that needs more than a widget can do. */
  UNSUPPORTED: DECLINE_KINDS.UNSUPPORTED,
  /** The model declined a request that doesn't describe a widget. */
  UNCLEAR: DECLINE_KINDS.UNCLEAR,
  /** The model declined a request under the acceptable use rules. */
  POLICY: DECLINE_KINDS.POLICY,
});

/**
 * An error generating a widget.
 */
export class WidgetGeneratorError extends Error {
  name = "WidgetGeneratorError";

  /**
   * @param {string} message
   * @param {string} code - one of GENERATION_ERRORS
   * @param {Error} [cause] - the error that led to this one
   */
  constructor(message, code, cause) {
    super(message, { cause });
    this.code = code;
  }
}

/**
 * Turns a prompt into a widget's files.
 */
class _WidgetGenerator {
  /**
   * @param {object} options
   * @param {string} options.prompt - what the user asks for
   * @param {AbortSignal} [options.signal] - aborting it ends generation at the
   *   next step
   * @returns {Promise<Map<string, string>>} the files the model wrote, by path
   */
  async generate({ prompt, signal }) {
    signal?.throwIfAborted();
    this.#throwIfBlocked();
    let endpoint = Services.prefs.getStringPref(ENDPOINT_PREF, "");
    let connection = {};
    let token = null;
    if (endpoint) {
      // A custom connection set from prefs
      connection = {
        baseURL: endpoint,
        apiKey: Services.prefs.getStringPref(API_KEY_PREF, ""),
      };
    } else {
      // The default openAIEngine connection using an account token.
      token = await lazy.AuthProvider.getToken();
      if (!token) {
        throw new WidgetGeneratorError(
          "No account is signed in",
          GENERATION_ERRORS.AUTH
        );
      }
    }
    signal?.throwIfAborted();

    let config = await this.#loadConfig();
    signal?.throwIfAborted();
    let engine = await this.#buildEngine({
      ...connection,
      model: Services.prefs.getStringPref(MODEL_PREF, "") || config.model,
    });
    signal?.throwIfAborted();
    this.#throwIfBlocked();
    let conversation = this.#startConversation({
      ...config,
      engine,
      prompt,
    });
    let files = await this.#runConversation(conversation, { token, signal });
    if (!files.size) {
      throw new WidgetGeneratorError(
        "The model wrote no files",
        GENERATION_ERRORS.GENERATION
      );
    }
    return files;
  }

  #throwIfBlocked() {
    if (lazy.GeneratedWidgetsFeature.isBlocked) {
      throw new WidgetGeneratorError(
        "Custom widgets are blocked in AI Controls",
        GENERATION_ERRORS.BLOCKED
      );
    }
  }

  /**
   * Reads the latest generator record for PROMPT_MAJOR_VERSION. Without one,
   * the local prompt is used and the server picks the model and parameters.
   * LOCAL_PROMPT_PREF uses the local prompt with any record.
   *
   * @returns {Promise<{systemPrompt: string, model: ?string, parameters: ?object}>}
   */
  async #loadConfig() {
    let record = await this.#loadRecord();
    let systemPrompt =
      record && !Services.prefs.getBoolPref(LOCAL_PROMPT_PREF, false)
        ? record.prompt
        : await this.#loadLocalPrompt();
    return {
      systemPrompt,
      model: record?.model,
      parameters: record?.parameters,
    };
  }

  /**
   * Reads the latest generator record for PROMPT_MAJOR_VERSION.
   *
   * @returns {Promise<?object>} the record, or null when none can be read
   */
  async #loadRecord() {
    let records;
    try {
      records = await lazy.RemoteSettings(COLLECTION).get();
    } catch (error) {
      lazy.logger.warn("The generator records can't be read", error);
      return null;
    }
    let record = records
      .map(entry => ({ entry, version: lazy.parseVersion(entry.version) }))
      .filter(({ version }) => version?.major === PROMPT_MAJOR_VERSION)
      .sort((a, b) => b.version.minor - a.version.minor)[0]?.entry;
    if (!record) {
      lazy.logger.warn("No generator record found");
    }
    return record ?? null;
  }

  /**
   * Reads the prompt packaged with Firefox.
   *
   * @returns {Promise<string>}
   */
  async #loadLocalPrompt() {
    let response = await fetch(LOCAL_PROMPT_URL);
    return response.text();
  }

  /**
   * Builds the engine for the model.
   *
   * @param {object} options
   * @param {string} options.model
   * @param {string} [options.baseURL]
   * @param {string} [options.apiKey]
   */
  async #buildEngine({ model, baseURL, apiKey }) {
    try {
      return await lazy.openAIEngine.build({
        model,
        baseURL,
        apiKey,
        feature: lazy.MODEL_FEATURES.WIDGET_GENERATION,
        serviceType: lazy.SERVICE_TYPES.WIDGET_GENERATION,
      });
    } catch (error) {
      throw new WidgetGeneratorError(
        "The engine setup failed",
        GENERATION_ERRORS.SERVICE,
        error
      );
    }
  }

  /**
   * Starts a conversation with the system prompt as the system message.
   *
   * @param {object} options
   * @param {openAIEngine} options.engine
   * @param {string} options.systemPrompt
   * @param {object} options.parameters
   * @param {string} options.prompt - what the user asks for
   */
  #startConversation({ engine, systemPrompt, parameters, prompt }) {
    let conversation = new lazy.Conversation({
      feature: lazy.MODEL_FEATURES.WIDGET_GENERATION,
      engine,
      parameters,
    });
    conversation.setSystemMessage(systemPrompt);
    conversation.addUserMessage(prompt);
    return conversation;
  }

  /**
   * Sends the conversation to the model for its next reply.
   *
   * @param {object} conversation - the conversation holding the request
   * @param {?string} token - null when the endpoint pref is set
   * @returns {Promise<{finalOutput: string, toolCalls: object[]}>} the reply
   */
  async #runTurn(conversation, token) {
    try {
      return await conversation.run({ token, tools: TOOLS });
    } catch (error) {
      throw new WidgetGeneratorError(
        "The model request failed",
        GENERATION_ERRORS.SERVICE,
        error
      );
    }
  }

  /**
   * Runs the conversation until a reply calls no tools or the model
   * declines the request.
   *
   * @param {object} conversation - the conversation holding the request
   * @param {object} options
   * @param {?string} options.token - null when the endpoint pref is set
   * @param {AbortSignal} [options.signal] - checked after each reply
   * @returns {Promise<Map<string, string>>} the files the model wrote, by path
   */
  async #runConversation(conversation, { token, signal }) {
    let workspace = new Workspace();
    let lastError;
    let maxTurns = Services.prefs.getIntPref(MAX_TURNS_PREF, 30);
    for (let turn = 0; turn < maxTurns; turn++) {
      let response = await this.#runTurn(conversation, token);
      signal?.throwIfAborted();
      this.#throwIfBlocked();
      let text = response?.finalOutput?.trim();
      if (text) {
        lazy.logger.debug(text);
      }
      let calls = response?.toolCalls ?? [];
      if (!calls.length) {
        if (!workspace.changed && lastError) {
          throw new WidgetGeneratorError(
            lastError,
            GENERATION_ERRORS.GENERATION
          );
        }
        return workspace.files;
      }
      conversation.addAssistantMessage({}, { tool_calls: calls });
      for (let call of calls) {
        let name = call.function?.name;
        let { reply, error, decline } = workspace.run(call);
        if (decline) {
          lazy.logger.debug("Declined", decline.kind, decline.reason);
          throw new WidgetGeneratorError(
            decline.reason || decline.kind,
            decline.kind
          );
        }
        lastError = error;
        if (error) {
          lazy.logger.debug(error, name, call.function?.arguments);
        }
        conversation.addToolMessage({
          tool_call_id: call.id,
          content: error ?? reply,
          name,
        });
      }
    }
    throw new WidgetGeneratorError(
      "The model was still calling tools after the last turn",
      GENERATION_ERRORS.GENERATION
    );
  }
}

export const WidgetGenerator = new _WidgetGenerator();

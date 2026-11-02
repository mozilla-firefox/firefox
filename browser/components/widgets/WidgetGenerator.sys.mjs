/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  WRITE_FILE,
  WRITE_FILE_TOOL_CONFIG,
} from "moz-src:///browser/components/widgets/Tools.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  Conversation:
    "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  PURPOSES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SERVICE_TYPES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  openAIEngine:
    "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs",
  parseVersion: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  console.createInstance({
    prefix: "WidgetGenerator",
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
export const PROMPT_MAJOR_VERSION = 1;

/**
 * The codes of a WidgetGeneratorError.
 */
export const GENERATION_ERRORS = Object.freeze({
  /** No account is signed in. */
  AUTH: "auth",
  /** The model can't be reached. */
  SERVICE: "service",
  /** The model's files don't make a widget. */
  GENERATION: "generation",
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
    let conversation = this.#startConversation({
      ...config,
      engine,
      prompt,
    });
    let files = await this.#writeFiles(conversation, { token, signal });

    if (!files.size) {
      throw new WidgetGeneratorError(
        "The model wrote no files",
        GENERATION_ERRORS.GENERATION
      );
    }

    return files;
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
        purpose: lazy.PURPOSES.WIDGET_GENERATION,
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
   * @param {object[]} tools - the tools the model can call
   * @returns {Promise<{finalOutput: string, toolCalls: object[]}>} the reply
   */
  async #runTurn(conversation, token, tools) {
    try {
      return await conversation.run({ token, tools });
    } catch (error) {
      throw new WidgetGeneratorError(
        "The model request failed",
        GENERATION_ERRORS.SERVICE,
        error
      );
    }
  }

  /**
   * Reads the path and content of a write_file call.
   *
   * @param {object} call - a tool call from the model's reply
   * @returns {{file_path: string, content: string}} the file to write
   */
  #parseWrite(call) {
    let name = call.function?.name;
    if (name !== WRITE_FILE) {
      throw new WidgetGeneratorError(
        `The model called an unknown tool: ${name}`,
        GENERATION_ERRORS.GENERATION
      );
    }
    let write;
    try {
      write = JSON.parse(call.function.arguments);
    } catch (error) {
      throw new WidgetGeneratorError(
        "The write_file arguments are not valid JSON",
        GENERATION_ERRORS.GENERATION,
        error
      );
    }
    if (typeof write?.file_path !== "string" || !write.file_path) {
      throw new WidgetGeneratorError(
        "A write_file call has no path",
        GENERATION_ERRORS.GENERATION
      );
    }
    if (typeof write.content !== "string") {
      throw new WidgetGeneratorError(
        `A write_file call has no content: ${write.file_path}`,
        GENERATION_ERRORS.GENERATION
      );
    }
    return write;
  }

  /**
   * Runs the conversation until a reply calls no tools, storing each file the
   * model writes.
   *
   * @param {object} conversation - the conversation holding the request
   * @param {object} options
   * @param {?string} options.token - null when the endpoint pref is set
   * @param {AbortSignal} [options.signal] - checked after each reply
   * @returns {Promise<Map<string, string>>} the files the model wrote, by path
   */
  async #writeFiles(conversation, { token, signal }) {
    let files = new Map();
    let maxTurns = Services.prefs.getIntPref(MAX_TURNS_PREF, 20);
    for (let turn = 0; turn < maxTurns; turn++) {
      let response = await this.#runTurn(conversation, token, [
        WRITE_FILE_TOOL_CONFIG,
      ]);
      signal?.throwIfAborted();
      let text = response?.finalOutput?.trim();
      if (text) {
        lazy.logger.debug(text);
      }
      let calls = response?.toolCalls ?? [];
      if (!calls.length) {
        return files;
      }
      conversation.addAssistantMessage({}, { tool_calls: calls });
      for (let call of calls) {
        let { file_path, content } = this.#parseWrite(call);
        files.set(file_path, content);
        conversation.addToolMessage({
          tool_call_id: call.id,
          content: "ok",
          name: call.function.name,
        });
      }
    }
    throw new WidgetGeneratorError(
      "The model was still writing files after the last turn",
      GENERATION_ERRORS.GENERATION
    );
  }
}

export const WidgetGenerator = new _WidgetGenerator();

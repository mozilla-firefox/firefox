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
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  GeneratedWidgetsFeature:
    "moz-src:///browser/components/widgets/GeneratedWidgetsFeature.sys.mjs",
  getLocalIsoTime:
    "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs",
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
ChromeUtils.defineLazyGetter(lazy, "ZipWriter", () =>
  Components.Constructor("@mozilla.org/zipwriter;1", "nsIZipWriter", "open")
);
ChromeUtils.defineLazyGetter(lazy, "StringInputStream", () =>
  Components.Constructor(
    "@mozilla.org/io/string-input-stream;1",
    "nsIStringInputStream",
    "setUTF8Data"
  )
);

const WIDGET_FILE = "widget.json";
const WIDGET_PAGE = "widget.html";

export const OUTPUT_DIRECTORY = "widget-generator";

/**
 * The longest prompt that GeneratedWidgetManifest in builtin_widget.json
 * accepts.
 */
const MAX_PROMPT_LENGTH = 2000;

const ENDPOINT_PREF = "browser.widgets.generated.endpoint";
const CUSTOM_ENDPOINT_PREF = "browser.widgets.generated.customEndpoint";
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
  /** No account token is available, or the service rejects it. */
  AUTH: "auth",
  /** The model can't be reached. */
  SERVICE: "service",
  /** The account reached its daily limit. */
  BUDGET: "budget",
  /** The account sent too many requests in a short time. */
  RATE_LIMIT: "rate_limit",
  /** The service has no room for more requests. */
  CAPACITY: "capacity",
  /** The network blocks requests to the service. */
  NETWORK: "network",
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
 * The MLPA error codes in the body of a failed model request.
 */
const MLPA_ERRORS = Object.freeze({
  BUDGET_EXCEEDED: 1,
  RATE_LIMIT_EXCEEDED: 2,
  MAX_USERS_REACHED: 4,
  UPSTREAM_RATE_LIMIT: 5,
  FASTLY_WAF_RATE_LIMIT: 6,
  GLOBAL_BUDGET_EXCEEDED: 10,
});

/**
 * The GENERATION_ERRORS code for each HTTP status of a failed model request,
 * either directly or by the MLPA error code in the response body.
 */
const REQUEST_ERRORS = new Map([
  [401, GENERATION_ERRORS.AUTH],
  [403, new Map([[MLPA_ERRORS.MAX_USERS_REACHED, GENERATION_ERRORS.CAPACITY]])],
  [406, GENERATION_ERRORS.NETWORK],
  [413, GENERATION_ERRORS.GENERATION],
  [
    429,
    new Map([
      [MLPA_ERRORS.BUDGET_EXCEEDED, GENERATION_ERRORS.BUDGET],
      [MLPA_ERRORS.RATE_LIMIT_EXCEEDED, GENERATION_ERRORS.RATE_LIMIT],
      [MLPA_ERRORS.UPSTREAM_RATE_LIMIT, GENERATION_ERRORS.RATE_LIMIT],
      [MLPA_ERRORS.FASTLY_WAF_RATE_LIMIT, GENERATION_ERRORS.RATE_LIMIT],
      [MLPA_ERRORS.GLOBAL_BUDGET_EXCEEDED, GENERATION_ERRORS.CAPACITY],
    ]),
  ],
  [
    500,
    new Map([[MLPA_ERRORS.GLOBAL_BUDGET_EXCEEDED, GENERATION_ERRORS.CAPACITY]]),
  ],
]);

/**
 * @param {Error} error - the error of a failed model request
 * @returns {string} the GENERATION_ERRORS code for it. A 429 without a known
 *   MLPA error code is a rate limit.
 */
function requestErrorCode(error) {
  let code = REQUEST_ERRORS.get(error?.status);
  if (code instanceof Map) {
    code = code.get(error.error);
  }
  if (!code && error?.status === 429) {
    return GENERATION_ERRORS.RATE_LIMIT;
  }
  return code ?? GENERATION_ERRORS.SERVICE;
}

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
 * Turns a prompt into a widget's zip file.
 */
class _WidgetGenerator {
  #clearing = null;

  /**
   * Generates a widget's zip file.
   *
   * @param {object} options
   * @param {string} options.prompt - what the user asks for
   * @param {AbortSignal} [options.signal] - aborting it ends generation at the
   *   next step
   * @returns {Promise<string>} the path of the new zip file
   */
  async generate({ prompt, signal }) {
    signal?.throwIfAborted();
    if (prompt.length > MAX_PROMPT_LENGTH) {
      throw new WidgetGeneratorError(
        `The prompt is longer than ${MAX_PROMPT_LENGTH} characters`,
        GENERATION_ERRORS.GENERATION
      );
    }
    this.#throwIfBlocked();
    let customEndpoint = Services.prefs.getStringPref(CUSTOM_ENDPOINT_PREF, "");
    let connection = {};
    let token = null;
    if (customEndpoint) {
      // A custom connection set from prefs
      connection = {
        baseURL: customEndpoint,
        apiKey: Services.prefs.getStringPref(API_KEY_PREF, ""),
      };
    } else {
      connection = {
        baseURL:
          Services.prefs.getStringPref(ENDPOINT_PREF, "") ||
          lazy.openAIEngine.endpoint,
      };
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
    return this.#package({ prompt, files, signal });
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
   * LOCAL_PROMPT_PREF uses the local prompt with any record. The prompt ends
   * with the local date.
   *
   * @returns {Promise<{systemPrompt: string, model: ?string, parameters: ?object}>}
   */
  async #loadConfig() {
    let record = await this.#loadRecord();
    let systemPrompt =
      record && !Services.prefs.getBoolPref(LOCAL_PROMPT_PREF, false)
        ? record.prompt
        : await this.#loadLocalPrompt();
    let today = lazy.getLocalIsoTime()?.split("T")[0];
    if (today) {
      systemPrompt += `\n\nToday is ${today}.`;
    }
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
   * @param {object} options
   * @param {?string} options.token - null when the custom endpoint pref is set
   * @param {AbortSignal} [options.signal] - aborting it ends the request
   * @returns {Promise<{finalOutput: string, toolCalls: object[]}>} the reply
   */
  async #runTurn(conversation, { token, signal }) {
    try {
      return await conversation.run({ token, tools: TOOLS, signal });
    } catch (error) {
      signal?.throwIfAborted();
      throw new WidgetGeneratorError(
        "The model request failed",
        requestErrorCode(error),
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
   * @param {?string} options.token - null when the custom endpoint pref is set
   * @param {AbortSignal} [options.signal] - aborting it ends the request in
   *   progress
   * @returns {Promise<Map<string, string>>} the files the model wrote, by path
   */
  async #runConversation(conversation, { token, signal }) {
    let workspace = new Workspace();
    let lastError;
    let maxTurns = Services.prefs.getIntPref(MAX_TURNS_PREF, 30);
    for (let turn = 0; turn < maxTurns; turn++) {
      let response = await this.#runTurn(conversation, { token, signal });
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

  /**
   * Reads the manifest the model wrote.
   *
   * @param {Map<string, string>} files - the files the model wrote, by path
   * @returns {object} the parsed manifest
   * @throws {WidgetGeneratorError} when manifest.json is missing or not valid
   *   JSON
   */
  #readManifest(files) {
    try {
      return JSON.parse(files.get("manifest.json"));
    } catch (error) {
      throw new WidgetGeneratorError(
        "The manifest is missing or not valid JSON",
        GENERATION_ERRORS.GENERATION,
        error
      );
    }
  }

  /**
   * Checks the manifest has a name and the files have a widget page.
   *
   * @param {object} manifest - the model's manifest
   * @param {Map<string, string>} files - the model's files, by path
   * @throws {WidgetGeneratorError} when the name is missing or the files have
   *   no widget.html
   */
  #validate(manifest, files) {
    let name = manifest?.name;
    if (typeof name != "string" || !name.trim()) {
      throw new WidgetGeneratorError(
        "The widget has no name",
        GENERATION_ERRORS.GENERATION
      );
    }
    if (!files.has(WIDGET_PAGE)) {
      throw new WidgetGeneratorError(
        "The widget page is missing",
        GENERATION_ERRORS.GENERATION
      );
    }
  }

  /**
   * Packages the widget's files and a widget.json with the model's name and the
   * prompt.
   *
   * @param {object} widget
   * @param {string} widget.prompt - the request that made the widget
   * @param {Map<string, string>} widget.files - the files the model wrote, by
   *   path
   * @param {AbortSignal} [widget.signal] - checked once the zip is written
   * @returns {Promise<string>} the path of the new zip file
   */
  async #package({ prompt, files, signal }) {
    let manifest = this.#readManifest(files);
    this.#validate(manifest, files);
    let packaged = new Map(files);
    packaged.delete("manifest.json");
    packaged.set(
      WIDGET_FILE,
      JSON.stringify({ name: manifest.name, prompt }, null, 2)
    );
    let directory = PathUtils.join(PathUtils.profileDir, OUTPUT_DIRECTORY);
    await this.#clearOutputDirectory(directory);
    await IOUtils.makeDirectory(directory);
    let path = await IOUtils.createUniqueFile(directory, "widget.zip");
    try {
      this.#writeZip(path, packaged);
      signal?.throwIfAborted();
      this.#throwIfBlocked();
    } catch (error) {
      await IOUtils.remove(path, { ignoreAbsent: true });
      throw error;
    }
    return path;
  }

  /**
   * Clears the output directory the first time a widget is packaged.
   *
   * @param {string} directory - the generator's output directory
   * @returns {Promise<void>}
   */
  async #clearOutputDirectory(directory) {
    this.#clearing ??= IOUtils.remove(directory, {
      recursive: true,
      ignoreAbsent: true,
    }).catch(error =>
      lazy.logger.warn("The generator directory can't be cleared", error)
    );
    await this.#clearing;
  }

  /**
   * Writes files to a new zip file.
   *
   * @param {string} path - the zip file's path
   * @param {Map<string, string>} files - the files' text, by path
   */
  #writeZip(path, files) {
    let writer = new lazy.ZipWriter(
      new lazy.FileUtils.File(path),
      lazy.FileUtils.MODE_WRONLY |
        lazy.FileUtils.MODE_CREATE |
        lazy.FileUtils.MODE_TRUNCATE
    );
    let time = Date.now() * 1000;
    try {
      for (let [name, text] of files) {
        writer.addEntryStream(
          name,
          time,
          Ci.nsIZipWriter.COMPRESSION_DEFAULT,
          new lazy.StringInputStream(text),
          false
        );
      }
    } finally {
      writer.close();
    }
  }
}

export const WidgetGenerator = new _WidgetGenerator();

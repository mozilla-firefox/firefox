/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  renderPrompt,
  checkMajorVersion,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  ChatStore:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs",
  openAIEngine:
    "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({
    prefix: "LLMTelemetry",
    maxLogLevelPref: "browser.smartwindow.telemetryLogLevel",
  });
});

export const TELEMETRY_MAJOR_VERSIONS = Object.freeze({
  wasSuccessful: 1,
  conversationCategory: 1,
});

export const TRIGGER_MAJOR_VERSIONS = Object.freeze({
  uniform_sample: 1,
  uniform_sample_at_turn: 1,
  min_turns: 1,
});

// Registry of named check strategies
export const TRIGGER_CHECK_STRATEGIES = {
  uniform_sample: (conversation, _params) =>
    conversation.currentTurnIndex?.() == 0,
  uniform_sample_at_turn: (conversation, { turn }) =>
    conversation._telemetryUniformSample === true &&
    conversation.currentTurnIndex?.() == turn,
  min_turns: (conversation, { minTurns }) =>
    (conversation.currentTurnIndex?.() ?? 0) >= minTurns,
};

// Probability that a turn-1 conversation is flagged for uniform sampling.
export const UNIFORM_SAMPLING_NAME = "uniform_sample";
const UNKNOWN = "unknown";

const RS_TELEMETRY_PROMPTS_COLLECTION = "ai-window-telemetry-prompts";
const RS_TELEMETRY_TRIGGERS_COLLECTION = "ai-window-telemetry-triggers";

// Pause before every prompt, including each conversation's first, so requests
// stay under the 20 rpm rate limit (at most 15 rpm) regardless of latency.
const PROMPT_PAUSE_MS = 4000;

// ============================================================
// Trigger
// ============================================================

/**
 * Represents a single telemetry trigger.
 *
 * A trigger is evaluated against the conversation after each agent response.
 * If it fires and passes the sampling check, the trigger is included in the
 * set passed to runTelemetry.
 */
export class Trigger {
  /**
   * @param {string} name - Unique identifier, must match the "triggers" field in RS records.
   * @param {function(object): boolean} checkFn - Returns true if the trigger should fire.
   * @param {number} samplingProbability - Fraction [0, 1] of fires that proceed to evaluation.
   * @param {string} description - Description of trigger
   */
  constructor(name, checkFn, samplingProbability = 1.0, description = "") {
    this.name = name;
    this.check = checkFn;
    this.samplingProbability = samplingProbability;
    this.description = description;
  }
}

// ============================================================
// telemetryPromptEngine
// ============================================================

/**
 * Remote Settings records in RS_TELEMETRY_COLLECTION are expected to have:
 *   - telemetry_name   {string}   Unique identifier for the evaluation prompt
 *   - triggers  {string[]} Trigger names this prompt applies to
 *   - prompts   {string}   System prompt content
 *   - model     {string}   Model ID to use for inference
 */
export class TelemetryPromptEngine {
  /** @type {object} */
  #promptRecord;

  /** @type {object} */
  #engineInstance;

  /**
   * Builds a telemetryPromptEngine for the given RS record.
   *
   * @param {object} promptRecord
   * @returns {Promise<TelemetryPromptEngine>}
   */
  static async build(promptRecord) {
    const engine = new TelemetryPromptEngine();
    engine.#promptRecord = promptRecord;

    const extraHeadersPref = Services.prefs.getStringPref(
      "browser.smartwindow.extraHeaders",
      "{}"
    );
    let extraHeaders = {};
    try {
      extraHeaders = JSON.parse(extraHeadersPref);
    } catch (e) {
      lazy.console.error(
        "Failed to parse extra headers for telemetry engine:",
        e
      );
    }

    engine.#engineInstance = await lazy.openAIEngine._createEngine({
      apiKey: "",
      backend: "openai",
      baseURL: lazy.openAIEngine.endpoint,
      featureId: "llm-telemetry",
      flowId: null,
      modelId: promptRecord.model ?? null,
      modelRevision: "main",
      taskName: "text-generation",
      serviceType: promptRecord.service_type ?? "ai",
      purpose: promptRecord.purpose ?? "chat",
      extraHeaders,
      maxRetries: 0,
    });

    return engine;
  }

  verifyResult(result) {
    const schema = this.#promptRecord.output_schema;
    const finalMapping = {};

    let metrics;
    try {
      metrics = JSON.parse(result.finalOutput);
    } catch {
      return Object.fromEntries(Object.keys(schema).map(key => [key, UNKNOWN]));
    }

    for (const key of Object.keys(schema)) {
      if (!schema[key]?.includes(metrics[key])) {
        finalMapping[key] = UNKNOWN;
      } else {
        finalMapping[key] = metrics[key];
      }
    }

    return finalMapping;
  }

  /**
   * Runs the evaluation prompt against the conversation.
   *
   * @param {object} conversation
   * @returns {Promise<object>} Raw LLM response
   */
  async run(conversation) {
    const token = await lazy.AuthProvider.getToken();
    // The LLM-as-judge model needs the raw URLs, not the chat URL tokens.
    const messages = conversation.getMessagesInChatCompletionsFormat({
      applyUrlTokens: false,
    });

    const prompt = renderPrompt(this.#promptRecord.prompt, {
      chatConversation: JSON.stringify(messages),
      fields: JSON.stringify(Object.keys(this.#promptRecord.output_schema)),
    });

    const result = await this.#engineInstance.run({
      token,
      args: [{ role: "system", content: prompt }],
    });

    lazy.console.debug("Returning: ", result);
    return this.verifyResult(result);
  }
}

// ============================================================
// TelemetryEngine
// ============================================================
/**
 * Orchestrates conversation telemetry evaluation. Loads trigger definitions
 * from Remote Settings, checks which triggers fire for a given conversation,
 * and runs LLM-based prompt evaluations for each matched trigger.
 *
 */
export class TelemetryEngine {
  _triggers = null;

  /**
   *
   * @param {object} conversation
   * @returns {Trigger[]}
   */

  async _fetchRecords(collection, fallback = []) {
    try {
      const client = lazy.RemoteSettings(collection);
      const records = await client.get();
      return records;
    } catch (e) {
      lazy.console.error("Telemetry: failed to fetch records:", e);
      return fallback;
    }
  }

  async getTriggerDefinitions() {
    if (this._triggers) {
      return;
    }

    const triggerRecords = await this._fetchRecords(
      RS_TELEMETRY_TRIGGERS_COLLECTION
    );

    this._triggers = triggerRecords
      .filter(def => TRIGGER_CHECK_STRATEGIES[def.check])
      .filter(def =>
        checkMajorVersion(def.version, TRIGGER_MAJOR_VERSIONS[def.check])
      )
      .map(
        def =>
          new Trigger(
            def.name,
            conversation =>
              TRIGGER_CHECK_STRATEGIES[def.check](
                conversation,
                def.params ?? {}
              ),
            def.sampling_probability ?? 1.0,
            def.description ?? ""
          )
      );
  }

  _getRandom() {
    return Math.random();
  }

  _pause(ms) {
    return new Promise(resolve => lazy.setTimeout(resolve, ms));
  }

  async getTriggers(conversation) {
    await this.getTriggerDefinitions();

    if (!conversation._checkedTelemetryTriggers) {
      conversation._checkedTelemetryTriggers = new Set();
    }

    const fired = [];
    for (const trigger of this._triggers) {
      if (conversation._checkedTelemetryTriggers.has(trigger.name)) {
        lazy.console.debug(
          "checking:",
          trigger.name,
          "-> skip (already checked)"
        );
        continue;
      }
      lazy.console.debug(
        "checking:",
        trigger.name,
        "-> evaluating",
        conversation._telemetryUniformSample,
        conversation.currentTurnIndex?.()
      );
      if (trigger.check(conversation)) {
        conversation._checkedTelemetryTriggers.add(trigger.name);
        if (this._getRandom() < trigger.samplingProbability) {
          lazy.console.debug("checking:", trigger.name, "-> fired");
          fired.push(trigger);
          if (trigger.name == UNIFORM_SAMPLING_NAME) {
            conversation._telemetryUniformSample = true;
            conversation._telemetryUniformProbability =
              trigger.samplingProbability;
          }
        }
      }
    }
    return fired;
  }

  /**
   * Selects the prompt records that match any of the fired triggers. Prompts
   * that apply to multiple triggers are deduplicated and selected only once,
   * taking the sampling probability of the first matching trigger.
   *
   * @param {Trigger[]} triggers
   * @returns {Promise<Array<{record: object, samplingProbability: number}>>}
   */
  async selectPrompts(triggers) {
    if (!triggers.length) {
      return [];
    }

    const allRecords = await this._fetchRecords(
      RS_TELEMETRY_PROMPTS_COLLECTION
    );
    const triggerByName = new Map(triggers.map(t => [t.name, t]));

    const seen = new Set();
    const selected = [];
    for (const record of allRecords) {
      const recordTriggers = record.triggers ?? [];
      if (
        !seen.has(record.telemetry_name) && // sometimes multiple triggers will use the same telemetry
        checkMajorVersion(
          record.version,
          TELEMETRY_MAJOR_VERSIONS[record.telemetry_name]
        )
      ) {
        const matchingTriggerName = recordTriggers.find(t =>
          triggerByName.has(t)
        );
        if (matchingTriggerName) {
          seen.add(record.telemetry_name);
          selected.push({
            record,
            samplingProbability:
              triggerByName.get(matchingTriggerName).samplingProbability,
          });
        }
      }
    }
    return selected;
  }

  /**
   * Runs LLM-based evaluations for all telemetry prompts passed by name.
   *
   * @param {string[]} promptNames
   * @param {object} conversation
   * @returns {Promise<Array<{feature: string, result: object}>>}
   */
  async runTelemetryByName(promptNames, conversation) {
    if (!promptNames.length) {
      return [];
    }

    const allRecords = await this._fetchRecords(
      RS_TELEMETRY_PROMPTS_COLLECTION
    );

    const nameSet = new Set(promptNames);
    const promptsToRun = allRecords
      .filter(record => nameSet.has(record.telemetry_name))
      .filter(record =>
        checkMajorVersion(
          record.version,
          TELEMETRY_MAJOR_VERSIONS[record.telemetry_name]
        )
      );

    return this._runPrompts(promptsToRun, conversation);
  }

  /**
   * Runs each prompt record once, without retries, pausing before each one.
   * A 429 is rethrown so the caller can stop sending requests; other errors
   * are logged and skipped.
   *
   * @param {object[]} promptsToRun
   * @param {object} conversation
   * @returns {Promise<object[]>}
   */
  async _runPrompts(promptsToRun, conversation) {
    const results = [];
    for (const record of promptsToRun) {
      await this._pause(PROMPT_PAUSE_MS);
      try {
        const engine = await TelemetryPromptEngine.build(record);
        const result = await engine.run(conversation);
        results.push({
          telemetry_name: record.telemetry_name,
          result,
          telemetry_version: record.version,
          judge_model: record.model ?? "",
        });
      } catch (error) {
        if (lazy.openAIEngine.is429Error(error)) {
          throw error;
        }
        lazy.console.error(
          `Telemetry: evaluation failed for ${record.telemetry_name}:`,
          error
        );
      }
    }

    lazy.console.debug("Returning!", results);
    return results;
  }
}

export function normalizeMetadata(metadata = {}) {
  const {
    chat_version = "",
    record_type = "",
    uniform_sampling_probability = 0,
    trigger_sampling_probability = 0,
    triggers = [],
  } = metadata;

  return {
    chat_version,
    record_type,
    uniform_sampling_probability: Math.round(
      uniform_sampling_probability * 1000
    ),
    trigger_sampling_probability: Math.round(
      trigger_sampling_probability * 1000
    ),
    triggers: Array.isArray(triggers)
      ? JSON.stringify(triggers)
      : (triggers ?? "[]"),
  };
}

export function submitTelemetryResult(
  telemetryResults,
  conversation,
  modelId,
  metadata
) {
  for (const resultObject of telemetryResults ?? []) {
    const normalized_metadata = normalizeMetadata({
      ...metadata,
      trigger_sampling_probability: resultObject?.samplingProbability ?? 0.0,
    });

    const result = resultObject?.result ?? {};
    for (const [attributeName, attributeValue] of Object.entries(result)) {
      Glean.smartWindow.llmajBasedTelemetry.record({
        ...normalized_metadata,
        chat_id: conversation.id,
        model: modelId,
        judge_model: resultObject?.judge_model ?? "",
        turn_number: conversation.currentTurnIndex(),
        telemetry_name: resultObject?.telemetry_name ?? "",
        attribute_name: attributeName,
        attribute_value: String(attributeValue ?? UNKNOWN),
        uniform_sampled: (metadata.triggers ?? []).includes(
          UNIFORM_SAMPLING_NAME
        ),
        trigger_sampled: (metadata.triggers ?? []).some(
          t => t !== UNIFORM_SAMPLING_NAME
        ),
        telemetry_version: resultObject?.telemetry_version ?? "",
      });
    }
  }
}

/**
 * Marks conversations as unprocessed (since new turns have been added) and
 * queues the prompts matched by any fired triggers for the end-of-conversation
 * run in TelemetryScheduler. No LLM requests are made here.
 *
 * @param {ChatConversation} conversation
 */
export async function runLLMaJTelemetry(conversation) {
  const turnIndex = conversation.currentTurnIndex();
  await lazy.ChatStore.markLLMTelemetryUnprocessed(conversation.id).catch(e =>
    console.error("Failed to mark telemetry unprocessed:", e)
  );

  const telemetryEngine = new TelemetryEngine();
  const triggers = await telemetryEngine.getTriggers(conversation);
  const selected = await telemetryEngine.selectPrompts(triggers);
  if (!selected.length) {
    return;
  }

  const prompts = Object.fromEntries(
    selected.map(({ record }) => [record.telemetry_name, turnIndex])
  );
  const probabilities = Object.fromEntries(
    selected.map(({ record, samplingProbability }) => [
      record.telemetry_name,
      samplingProbability,
    ])
  );
  await lazy.ChatStore.updateLLMTelemetryRecord(
    conversation.id,
    prompts,
    probabilities,
    conversation._telemetryUniformProbability,
    0,
    triggers.map(t => t.name)
  ).catch(e => console.error("Failed to update telemetry record:", e));
}

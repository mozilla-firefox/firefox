/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  openAIEngine,
  DEFAULT_ENGINE_ID,
  renderPrompt,
  checkMajorVersion,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
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
  uniform_sample: (conversation, {}) => conversation.currentTurnIndex?.() === 0,
  uniform_sample_at_turn: (conversation, { turn }) =>
    conversation._telemetryUniformSample === true &&
    conversation.currentTurnIndex?.() === turn,
  min_turns: (conversation, { minTurns }) =>
    (conversation.currentTurnIndex?.() ?? 0) >= minTurns,
};

// Probability that a turn-1 conversation is flagged for uniform sampling.
const UNIFORM_SAMPLING_NAME = "uniform_sample";
const TELEMETRY_PURPOSE = "chat";
const TELEMETRY_MODEL = "qwen3-235b-a22b-instruct-2507-maas";
const UNKNOWN = "unknown";

const RS_TELEMETRY_PROMPTS_COLLECTION = "ai-window-telemetry-prompts";
const RS_TELEMETRY_TRIGGERS_COLLECTION = "ai-window-telemetry-triggers";

// Example remote settings records
const PROMPT1 = `Read the following conversation between a user and an AI browser assistant. Your task is to evaluate the interaction as of the current point in the conversation.

### Conversation ###
{chatConversation}

Assess the conversation based on the following criteria:

- "useCase":
  - "summarization": The user asked the assistant to summarize content.
  - "page_qa": The user asked questions about the content of a specific page.
  - "tab_compare": The user compared multiple tabs or sources, including @mentioned tabs.
  - "history_search": The user asked about previously visited pages or browsing history.
  - "other": The interaction does not clearly fall into the above categories.

- "resolutionStatus":
  - "satisfied": The assistant has successfully completed the user's request to a satisfactory level.
  - "dissatisfied": The assistant failed to satisfactorily complete the request.
  - "ongoing": The user's request is still in progress or cannot yet be judged.

- "reason":
  - "completed_successfully": The assistant fully addressed the user's request with a useful response. Use only if resolutionStatus = "satisfied"
  - "incorrect_answer": The assistant provided incorrect or misleading information. Use only if resolutionStatus = "dissatisfied".
  - "user_frustration": The user expressed frustration or dissatisfaction. Use only if resolutionStatus = "dissatisfied".
  - "topic_change": The user changed topics before the original request was completed. Use only if resolutionStatus = "dissatisfied".

Instructions:
- Choose exactly one value for "useCase" and "resolutionStatus".
- Prefer "ongoing" unless there is clear evidence that the task has been fully completed or has clearly failed.
- If resolutionStatus is "ongoing", set "reason" to "none".
- Base your judgment only on the conversation provided so far.

Respond using a well-formatted JSON with the following fields:
{fields}
`;

const OUTPUT_SCHEMA1 = {
  useCase: ["summarization", "page_qa", "tab_compare", "history_search", "other"],
  resolutionStatus: [
    "satisfied",
    "dissatisfied",
    "ongoing",
  ],
  reason: ["completed_successfully", "incorrect_answer", "user_frustration", "topic_change"],
};

const PROMPT2 = `Read the following conversation between a user and an AI browser assistant. Your task is to determine the primary category of the conversation.

### Conversation ###
{chatConversation}

Classify the conversation into exactly one of the following categories:

- "Adult"
- "Arts & Entertainment"
- "Autos & Vehicles"
- "Beauty & Fitness"
- "Books & Literature"
- "Business & Industrial"
- "Computers & Electronics"
- "Finance"
- "Food & Drink"
- "Games"
- "Health"
- "Hobbies & Leisure"
- "Home & Garden"
- "Internet & Telecom"
- "Jobs & Education"
- "Law & Government"
- "News"
- "Online Communities"
- "People & Society"
- "Pets & Animals"
- "Real Estate"
- "Reference"
- "Science"
- "Sensitive Subjects"
- "Shopping"
- "Sports"
- "Travel & Transportation"

Instructions:
- Choose the single category that best represents the **primary intent** of the user.
- Focus on the user’s goal, not incidental details in the conversation.
- If multiple categories apply, select the one most central to the task.
- Use "Reference" for general knowledge queries that do not clearly fit another category.
- Use "People & Society" for general advice, relationships, or personal topics.
- Use "Sensitive Subjects" for topics involving self-harm, violence, or other sensitive issues.
- If the conversation is ambiguous, choose the closest reasonable category rather than inventing a new one.

Respond using a well-formatted JSON with the following field:
{fields}`;


const OUTPUT_SCHEMA2 = {
  category: [
    "Adult",
    "Arts & Entertainment",
    "Autos & Vehicles",
    "Beauty & Fitness",
    "Books & Literature",
    "Business & Industrial",
    "Computers & Electronics",
    "Finance",
    "Food & Drink",
    "Games",
    "Health",
    "Hobbies & Leisure",
    "Home & Garden",
    "Internet & Telecom",
    "Jobs & Education",
    "Law & Government",
    "News",
    "Online Communities",
    "People & Society",
    "Pets & Animals",
    "Real Estate",
    "Reference",
    "Science",
    "Sensitive Subjects",
    "Shopping",
    "Sports",
    "Travel & Transportation"
  ],
};


// ai-window-telemetry-prompts sample records
const ALL_RECORDS = [
  {
    id: "wasSuccessful-v1",
    version: "1.0",
    model: TELEMETRY_MODEL,
    telemetry_name: "wasSuccessful",
    triggers: [
      "uniform_sample",
      "uniform_sample_turn2",
      "uniform_sample_turn4",
    ],
    run_terminal: true,
    output_schema: OUTPUT_SCHEMA1,
    prompt: PROMPT1,
  },
  {
    id: "conversationCategory-v1",
    version: "1.0",
    model: TELEMETRY_MODEL,
    telemetry_name: "conversationCategory",
    triggers: [
      "uniform_sample",
      "uniform_sample_turn2",
      "uniform_sample_turn4",
    ],
    run_terminal: true,
    output_schema: OUTPUT_SCHEMA2,
    prompt: PROMPT2,
  },
];

// ai-window-telemetry-triggers sample records

const TRIGGER_RECORDS = [
  {
    name: "uniform_sample",
    check: "uniform_sample",
    params: {},
    sampling_probability: 1.0,
    version: "1.0",
    description: "",
  },
  {
    name: "uniform_sample_turn2",
    check: "uniform_sample_at_turn",
    params: { turn: 2 },
    sampling_probability: 1.0,
    version: "1.0",
  },
  {
    name: "uniform_sample_turn4",
    check: "uniform_sample_at_turn",
    params: { turn: 4 },
    sampling_probability: 0.5,
    version: "1.0",
  },
  {
    name: "uniform_sample_turn8",
    check: "uniform_sample_at_turn",
    params: { turn: 8 },
    sampling_probability: 0.5,
    version: "1.0",
  },
  {
    name: "long_conversation",
    check: "min_turns",
    params: { minTurns: 10 },
    sampling_probability: 1.0,
    version: "1.0",
    description:
      "long conversation flag -- samples conversations that exceed 10 turns",
  },
];
// end remote settings

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
      console.error("Failed to parse extra headers for telemetry engine:", e);
    }

    engine.#engineInstance = await openAIEngine._createEngine({
      apiKey: "",
      backend: "openai",
      baseURL: openAIEngine.endpoint,
      engineId: `${DEFAULT_ENGINE_ID}-telemetry-${promptRecord.telemetry_name}`,
      featureId: "chat",
      flowId: null,
      modelId: promptRecord.model ?? null,
      modelRevision: "main",
      taskName: "text-generation",
      serviceType: "ai",
      purpose: TELEMETRY_PURPOSE,
      extraHeaders,
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
    const fxAccountToken = await openAIEngine.getFxAccountToken();
    const messages = conversation.getMessagesInOpenAiFormat();

    const prompt = renderPrompt(this.#promptRecord.prompt, {
      chatConversation: JSON.stringify(messages),
      fields: JSON.stringify(Object.keys(this.#promptRecord.output_schema)),
    });

    const result = await this.#engineInstance.run({
      fxAccountToken,
      args: [{ role: "system", content: prompt }],
    });

    console.warn("****");
    console.warn(result);
    console.warn("***");
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

  async _fetchRecords(collection, fallback) {
    // try {
    //   const client = lazy.RemoteSettings(collection);
    //   return await client.get();
    // } catch (e) {
    //   console.error("Telemetry: failed to fetch records:", e);
    //   return fallback;
    // }
    return fallback;
  }

  async getTriggerDefinitions() {
    if (this._triggers) {
      return;
    }

    const triggerRecords = await this._fetchRecords(
      RS_TELEMETRY_TRIGGERS_COLLECTION,
      TRIGGER_RECORDS
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

  async getTriggers(conversation) {
    await this.getTriggerDefinitions();

    if (!conversation._checkedTelemetryTriggers) {
      conversation._checkedTelemetryTriggers = new Set();
    }

    const fired = [];
    for (const trigger of this._triggers) {
      if (conversation._checkedTelemetryTriggers.has(trigger.name)) {
        continue;
      }
      if (trigger.check(conversation)) {
        conversation._checkedTelemetryTriggers.add(trigger.name);
        if (this._getRandom() < trigger.samplingProbability) {
          fired.push(trigger);
          if (trigger.name == UNIFORM_SAMPLING_NAME) {
            conversation._telemetryUniformSample = true;
          }
        }
      }
    }
    return fired;
  }

  /**
   * Runs LLM-based evaluations for all prompt records that match any of the
   * fired triggers. Prompts that apply to multiple triggers are deduplicated
   * and run only once.
   *
   * @param {Trigger[]} triggers
   * @param {object} conversation
   * @returns {Promise<Array<{feature: string, result: object}>>}
   */
  async runTelemetry(triggers, conversation) {
    if (!triggers.length) {
      return [];
    }

    const allRecords = await this._fetchRecords(
      RS_TELEMETRY_PROMPTS_COLLECTION,
      ALL_RECORDS
    );
    const triggerByName = new Map(triggers.map(t => [t.name, t]));

    const seen = new Set();
    const promptsToRun = [];
    const samplingProbabilities = new Map();
    for (const record of allRecords) {
      const recordTriggers = record.triggers ?? [];
      if (
        !seen.has(record.telemetry_name) && // sometimes multiple triggers will use the same telemetry
        checkMajorVersion(
          record.version,
          TELEMETRY_MAJOR_VERSIONS[record.telemetry_name]
        )
      ) {
        const matchingTriggerName = recordTriggers.find(t => triggerByName.has(t));
        if (matchingTriggerName) {
          seen.add(record.telemetry_name);
          promptsToRun.push(record);
          samplingProbabilities.set(
            record.telemetry_name,
            triggerByName.get(matchingTriggerName).samplingProbability
          );
        }
      }
    }
    const results = await this._runPrompts(promptsToRun, conversation);
    return results.map(r => ({
      ...r,
      samplingProbability: samplingProbabilities.get(r.telemetry_name),
    }));
  }

  /**
   * Runs LLM-based evaluations for all telemetry prompts passed by name. 
   * Prompt records that have run_terminal flag set to False will not be run
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
      RS_TELEMETRY_PROMPTS_COLLECTION,
      ALL_RECORDS
    );

    const nameSet = new Set(promptNames);
    const promptsToRun = allRecords
      .filter(
        record => nameSet.has(record.telemetry_name)
      )
      .filter(
        record => checkMajorVersion(record.version, TELEMETRY_MAJOR_VERSIONS[record.telemetry_name])
      )
      .filter(
        record => record.run_terminal
      )

    return this._runPrompts(promptsToRun, conversation)
  }

  async _runPrompts(promptsToRun, conversation) {
    const results = [];
    for (const record of promptsToRun) {
      try {
        const engine = await TelemetryPromptEngine.build(record);
        const result = await engine.run(conversation);
        results.push({ telemetry_name: record.telemetry_name, result });
      } catch (e) {
        console.error(`Telemetry: evaluation failed for ${record.feature}:`, e);
      }
    }
    console.warn("Returning!", results);
    return results;
  }
}

export function submitTelemetryResult(telemetryResults, conversationId, modelId, currentTurn, telemetryType) {  
  for (const result_object of telemetryResults) {
    console.log("Result Object:", result_object);
    for (const [attributeName, attributeValue] of Object.entries(result_object.result)) {
      Glean.smartWindow.llmResponseTelemetry.record({
        chat_id: conversationId,
        model: modelId,
        turn_number: currentTurn,
        attribute_name: attributeName,
        attribute_value: String(attributeValue ?? UNKNOWN),
        telemetryType: telemetryType
      });
    }
  }
}

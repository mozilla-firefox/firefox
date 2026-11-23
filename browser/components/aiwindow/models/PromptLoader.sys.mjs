/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @import { PipelineOptions } from "chrome://global/content/ml/EngineProcess.sys.mjs"
 */

import {
  getRemoteRecords,
  selectMainConfig,
  renderPrompt,
  checkMajorVersion,
  parseVersion,
  MODEL_PREF,
  FEATURE_MAJOR_VERSIONS,
  MODEL_FEATURES,
  PURPOSES,
  SERVICE_TYPES,
  GENERIC_MODEL_NAME,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";
import {
  constructRealTimeInfoInjectionMessage,
  getLocalIsoTime,
  sanitizeUntrustedContent,
} from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";
import { openAIEngine } from "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs";
import { Conversation } from "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs";
import { SmartWindowLocation } from "moz-src:///browser/components/aiwindow/models/SmartWindowLocation.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @typedef {import("moz-src:///browser/components/aiwindow/models/Utils.sys.mjs").InferenceParams} InferenceParams
 */

const lazy = XPCOMUtils.declareLazy({
  getSmartPageName:
    "chrome://browser/content/aiwindow/modules/TrustedInternalURLs.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  aitabEnabled: { pref: "browser.smartwindow.aitab.enabled" },
  agentEnabled: { pref: "browser.smartwindow.agent.enabled" },
});

const CUSTOM_PROMPTS_PREF = "browser.smartwindow.customPrompts";
const MODEL_CHOICE_PREF = "browser.smartwindow.firstrun.modelChoice";

// Core modules that define the assistant; if either is missing
// buildChatSystemPrompt throws rather than serve a partial prompt. Others are
// optional (e.g. model-details is model-specific and has no generic record).
const REQUIRED_CHAT_MODULES = new Set(["identity", "response-rules"]);
// Chat modules that describe a gated feature, each with the check that says
// whether the feature is available. They are assembled only while it is, so
// the model is never taught about something the user cannot use: a tool that
// filterFeatureGatedTools (Chat.sys.mjs) will not give it, or a monitor
// follow-up that would do nothing when clicked. The pages gate is the same
// pref as AITAB_PREF in Tools.sys.mjs, kept local to avoid an import cycle.
// The monitor gate matches AgentUI.tryHandleCommand.
const GATED_CHAT_MODULES = new Map([
  ["pages", () => lazy.aitabEnabled],
  [
    "monitor",
    () => lazy.agentEnabled && lazy.MonitorUIUtils.isMonitorRegionSupported(),
  ],
]);
// Browser-context fragments predate its params manifest. Without one at this
// build's major, read the legacy 1.x "tab" fragment, which builds that do not
// render `{#key}...{/key}` sections also read.
const LEGACY_BROWSER_CONTEXT_VERSION = "1.0";
const LEGACY_TAB_MODULE = "tab";
const TAB_MODULE = "tab-conditional-fields";

export const DEFAULT_PURPOSE = "default";
export const FEATURE_PURPOSES = Object.freeze({
  [DEFAULT_PURPOSE]: PURPOSES.CHAT,
  [MODEL_FEATURES.CHAT]: PURPOSES.CHAT,
  [MODEL_FEATURES.SMART_FORM_FILL]: PURPOSES.SMART_FORM_FILL,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER]:
    PURPOSES.CONVERSATION_STARTERS_SIDEBAR,
  [MODEL_FEATURES.CONVERSATION_SUGGESTIONS_FOLLOWUP]:
    PURPOSES.CONVERSATION_STARTERS_SIDEBAR,
  [MODEL_FEATURES.TITLE_GENERATION]: PURPOSES.TITLE_GENERATION,
  [MODEL_FEATURES.TAB_GROUP_NAMING]: PURPOSES.TAB_GROUP_NAMING,
  [MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM]:
    PURPOSES.MEMORY_GENERATION,
  [MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM]:
    PURPOSES.MEMORY_GENERATION,
  [MODEL_FEATURES.AGENT_MONITOR]: PURPOSES.MONITOR,
  [MODEL_FEATURES.AGENT_MONITOR_SETUP]: PURPOSES.MONITOR,
  [MODEL_FEATURES.SEARCH_ANSWER_GENERATION]: PURPOSES.CHAT,
  [MODEL_FEATURES.AITAB]: PURPOSES.AITAB,
});

function getDefaultServiceType(feature) {
  if (feature.startsWith("memories")) {
    return SERVICE_TYPES.MEMORIES;
  } else if (feature.startsWith("agent")) {
    return SERVICE_TYPES.AGENT;
  } else if (feature.startsWith("conversation-suggestions")) {
    return SERVICE_TYPES.CONVERSATION_STARTERS;
  }
  return SERVICE_TYPES.AI;
}

const V2_RECORD_KINDS = new Set(["module", "skill", "params"]);

async function loadV2Records() {
  const records = await getRemoteRecords();
  return records.filter(r => V2_RECORD_KINDS.has(r.kind));
}

// Numeric value of a record's `version` field ("1.1" -> 1000001); 0 when absent.
function versionOf(record) {
  const version = parseVersion(record.version);
  // set major * 1000000 + minor so that 1.10 > 1.2 and will be fine for all foreseeable versions
  return version ? version.major * 1000000 + version.minor : 0;
}

function pickRecord(records, predicate, { model, version } = {}) {
  const requestedVersion = version == null ? null : parseVersion(version);
  if (version != null && !requestedVersion) {
    return null;
  }
  let matching = records.filter(predicate);

  if (requestedVersion) {
    matching = matching.filter(
      r => parseVersion(r.version)?.major === requestedVersion.major
    );
  } else {
    const maxVersion = Math.max(...matching.map(versionOf));
    matching = matching.filter(r => versionOf(r) === maxVersion);
  }

  if (!matching.length) {
    return null;
  }

  return (
    (model && matching.find(r => r.model === model)) ||
    matching.find(r => r.model === GENERIC_MODEL_NAME) ||
    null
  );
}

function findModule(
  records,
  { feature, module, options: { model, version = null } }
) {
  return pickRecord(
    records,
    r => r.kind === "module" && r.feature === feature && r.module === module,
    { model, version }
  );
}

function findParams(records, { feature, model, majorVersion }) {
  const major = majorVersion ?? FEATURE_MAJOR_VERSIONS[feature];
  const candidates = records.filter(
    r =>
      r.kind === "params" &&
      r.feature === feature &&
      Array.isArray(r.modules) &&
      checkMajorVersion(r.version, major)
  );
  return (
    (model && candidates.find(r => r.model === model)) ||
    candidates.find(r => r.model === GENERIC_MODEL_NAME) ||
    candidates.find(r => r.is_default === true) ||
    null
  );
}

function findSkill(records, { name, model }) {
  return pickRecord(records, r => r.kind === "skill" && r.name === name, {
    model,
  });
}

function listSkills(records, model) {
  const byName = new Map();
  for (const r of records) {
    if (r.kind !== "skill") {
      continue;
    }
    const isExactModel = model && r.model === model;
    const isGenericModel = r.model === GENERIC_MODEL_NAME;
    if (!isExactModel && !isGenericModel) {
      continue;
    }
    const current = byName.get(r.name);
    if (
      !current ||
      versionOf(r) > versionOf(current) ||
      (versionOf(r) === versionOf(current) &&
        current.model === GENERIC_MODEL_NAME &&
        isExactModel)
    ) {
      byName.set(r.name, r);
    }
  }
  return [...byName.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/**
 * Replaces each `{name}` placeholder that has a substitution with its value.
 * Placeholders are replaced in a single pass, so substituted values are
 * inserted literally and never expanded again.
 *
 * @param {string} rawContent - Template text.
 * @param {object} [substitutions] - Values keyed by placeholder name.
 * @returns {string} The rendered text.
 */
function renderTemplate(rawContent, substitutions = {}) {
  return rawContent.replace(/\{(\w+)\}/g, (placeholder, name) =>
    Object.hasOwn(substitutions, name)
      ? (substitutions[name] ?? "")
      : placeholder
  );
}

/**
 * Assemble the chat system prompt from modular v2 records. The chat params
 * record for this build's major version carries a `modules` manifest — an
 * ordered [{name, version}] list — that dictates the module set, order, and
 * the version of each module to load; Firefox keys only on the params major
 * version, everything else comes from data.
 *
 * Chat is fully v2: there is no hardcoded module order and no v1 fallback. If
 * the params manifest is absent, or it omits or can't resolve a required core
 * module (identity, response-rules), this throws (clientReason
 * "promptLoadFailure") rather than serve a partial prompt. Optional modules
 * are skipped when no record is published. The returned `version` is the
 * params version.
 *
 * @param {string} model
 * @returns {Promise<{prompt: string, version: string}>}
 */
export async function buildChatSystemPrompt(model) {
  const records = await loadV2Records();
  const params = findParams(records, {
    feature: MODEL_FEATURES.CHAT,
    model,
  });
  if (!params) {
    // No manifest means nothing to assemble from (and chat has no v1 fallback),
    // so fail loud rather than serve a partial or hardcoded prompt.
    const err = new Error("Missing chat module manifest");
    err.clientReason = "promptLoadFailure";
    throw err;
  }

  // Every required core module must be named by the manifest (an empty or
  // omitting manifest must not yield a partial prompt); the loop below also
  // throws if a named required module has no published record.
  for (const required of REQUIRED_CHAT_MODULES) {
    if (!params.modules.some(m => m.name === required)) {
      const err = new Error(`Missing required chat module: ${required}`);
      err.clientReason = "promptLoadFailure";
      throw err;
    }
  }

  // Manifest-driven: module set, order, and per-module version all come from
  // the params record. Modules are selected by the major version it names.
  const sections = [];
  for (const entry of params.modules) {
    const isAvailable = GATED_CHAT_MODULES.get(entry.name);
    if (isAvailable && !isAvailable()) {
      continue;
    }
    const record = findModule(records, {
      feature: MODEL_FEATURES.CHAT,
      module: entry.name,
      options: { model, version: entry.version },
    });
    const text = record?.prompts?.trim();
    if (text) {
      sections.push(text);
    } else if (REQUIRED_CHAT_MODULES.has(entry.name)) {
      const err = new Error(`Missing required chat module: ${entry.name}`);
      err.clientReason = "promptLoadFailure";
      throw err;
    }
  }

  const isoTimestamp = getLocalIsoTime() ?? "";
  const skillList = listSkills(records, model)
    .map(
      ({ name, description = "" }) =>
        `- <name>${name}</name><description>${description}</description>`
    )
    .join("\n");

  // The location is read and fetched only when a module renders it.
  let locationText = "Unavailable";
  if (sections.some(section => section.includes("{location}"))) {
    locationText = SmartWindowLocation.getCachedLocation() ?? locationText;
    // Refreshed here as well as when a Smart Window opens, so that a long
    // session or a feature enabled mid-session picks up the location. Not
    // awaited so that prompt assembly never waits on the network.
    SmartWindowLocation.refresh();
  }

  const prompt = renderTemplate(sections.join("\n\n\n"), {
    skill_list: skillList,
    locale: Services.locale.appLocaleAsBCP47,
    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
    location: locationText,
    isoTimestamp,
    todayDate: isoTimestamp.split("T")[0],
  });
  return { prompt, version: params.version };
}

/**
 * Look up a skill record by name and return its prompt text, or an
 * `{ error }` object the model can act on when the name is invalid or no
 * matching skill exists (mirrors the other tool handlers' result shape).
 *
 * @param {string} name
 * @param {string} model
 * @returns {Promise<string|{error: string}>}
 */
export async function getSkillPrompt(name, model) {
  // Defensive: skill name comes from the LLM. The findSkill lookup is by
  // equality so traversal isn't possible, but reject obviously malformed
  // names before the records fetch.
  const skillName = String(name || "").trim();
  if (!/^[A-Za-z0-9_-]+$/.test(skillName)) {
    return { error: `Invalid skill name: ${skillName}` };
  }
  const records = await loadV2Records();
  const record = findSkill(records, { name: skillName, model });
  if (!record?.prompts) {
    return { error: `Unknown skill: ${skillName}` };
  }
  return record.prompts;
}

/**
 * Serialize mentioned URLs: List expanded tabs from a tab group under their
 * respective group.
 *
 * @param {object[]} mentionsWithUrl - Mentions with an URL
 * @returns {string}
 */
function renderMentionedUrls(mentionsWithUrl) {
  // url is intentionally not wrapped in sanitizeUntrustedContent — it's a
  // structured value the model uses to navigate/fetch, and the spotlighting
  // tokens would corrupt it. The user-controlled label is sanitized.
  let lastGroupId = null;
  return mentionsWithUrl
    .map(({ url, label, groupId, groupLabel }) => {
      const lines = [];
      if (groupId && groupId !== lastGroupId) {
        const group = sanitizeUntrustedContent(groupLabel);
        lines.push(group ? `- Tab group: ${group}` : "- Tab group");
      }
      lastGroupId = groupId ?? null;
      // Members are indented under the group
      const indent = groupId ? "  " : "";
      lines.push(
        `${indent}- URL: ${url}`,
        `${indent}  Title: ${sanitizeUntrustedContent(label)}`
      );
      const pageSlug = lazy.getSmartPageName(URL.parse(url));
      if (pageSlug) {
        lines.push(`${indent}  Page slug: ${pageSlug}`);
      }
      return lines.join("\n");
    })
    .join("\n");
}

/**
 * Build the per-turn browser-context prompt (active tab + any @mentions).
 * Returns null when nothing is available.
 *
 * @param {string} model
 * @param {object} [opts]
 * @param {Function} [opts.getRealTimeMapping]
 * @param {object[]} [opts.contextMentions]
 * @param {object} opts.securityProperties
 * @returns {Promise<string|null>}
 */
export async function buildBrowserContextPrompt(
  model,
  {
    getRealTimeMapping = constructRealTimeInfoInjectionMessage,
    contextMentions,
    securityProperties,
  } = {}
) {
  const browserContextMapping = await getRealTimeMapping(contextMentions);
  if (!browserContextMapping) {
    return null;
  }

  const records = await loadV2Records();
  const paramsRecord = findParams(records, {
    feature: MODEL_FEATURES.BROWSER_CONTEXT,
    model,
  });
  const findFragment = fragment =>
    findModule(records, {
      feature: MODEL_FEATURES.BROWSER_CONTEXT,
      module: fragment,
      options: {
        model,
        version:
          paramsRecord?.modules.find(m => m.name === fragment)?.version ??
          LEGACY_BROWSER_CONTEXT_VERSION,
      },
    });

  const fragments = [];

  if (browserContextMapping.hasTabInfo) {
    securityProperties.setPrivateData();
    const record = findFragment(paramsRecord ? TAB_MODULE : LEGACY_TAB_MODULE);
    if (record?.prompts) {
      fragments.push(record.prompts);
    }
  } else {
    delete browserContextMapping.url;
    delete browserContextMapping.title;
    delete browserContextMapping.description;
    delete browserContextMapping.pageSlug;
  }
  delete browserContextMapping.hasTabInfo;

  if (contextMentions?.length) {
    // Tab group mentions do not have a URL
    const mentionsWithUrl = contextMentions.filter(mention => mention.url);
    if (mentionsWithUrl.length) {
      securityProperties.setPrivateData();
      browserContextMapping.contextUrls = renderMentionedUrls(mentionsWithUrl);
      const record = findFragment("mentions");
      if (record?.prompts) {
        fragments.push(record.prompts);
      }
    }
  }

  if (!fragments.length) {
    return null;
  }

  return renderPrompt(fragments.join("\n\n"), browserContextMapping) ?? null;
}

// ===========================================================================
// Feature config + engine
// ===========================================================================

/**
 * Read Remote Settings and pick the model+params config record for a feature.
 * Features with v2 kind:"params" records resolve from those records. Other
 * features read their v1 main-config record (no `kind`). V2 module/skill
 * records are never model-selection candidates.
 *
 * @param {string} feature
 * @param {object} [opts]
 * @param {number} [opts.majorVersionOverride]
 * @param {string} [opts.modelChoiceIdOverride]
 * @returns {Promise<object>}
 */
async function selectFeatureConfig(feature, opts = {}) {
  const allRecords = await getRemoteRecords();

  const hasV2Params = allRecords.some(
    r => r.feature === feature && r.kind === "params"
  );
  const featureConfigs = allRecords.filter(r =>
    hasV2Params
      ? r.feature === feature && r.kind === "params"
      : r.feature === feature && !r.kind
  );
  if (!featureConfigs.length) {
    const err = new Error(
      `No Remote Settings records found for feature: ${feature}`
    );
    err.clientReason = "remoteSettingsUnavailable";
    throw err;
  }

  const majorVersion =
    opts.majorVersionOverride ?? FEATURE_MAJOR_VERSIONS[feature];
  const userModel = Services.prefs.prefHasUserValue(MODEL_PREF)
    ? Services.prefs.getStringPref(MODEL_PREF, "")
    : "";
  const modelChoiceId =
    opts.modelChoiceIdOverride ??
    Services.prefs.getStringPref(MODEL_CHOICE_PREF, "");

  const mainConfig = selectMainConfig(featureConfigs, {
    majorVersion,
    userModel,
    modelChoiceId,
    feature,
  });

  if (!mainConfig) {
    const err = new Error(
      `No matching model config found for feature: ${feature} with major version ${majorVersion}`
    );
    err.clientReason = "modelConfigUnavailable";
    throw err;
  }

  return mainConfig;
}

// Features whose generic record means "use the model of this other feature".
const MODEL_SOURCE_FEATURES = new Map([
  [MODEL_FEATURES.AGENT_MONITOR, MODEL_FEATURES.CHAT],
  [MODEL_FEATURES.AGENT_MONITOR_SETUP, MODEL_FEATURES.AGENT_MONITOR],
  [MODEL_FEATURES.AITAB, MODEL_FEATURES.CHAT],
  [MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION, MODEL_FEATURES.CHAT],
  [MODEL_FEATURES.RESUME_ACTIVITY_CONVERSATION_STARTER, MODEL_FEATURES.CHAT],
]);

/**
 * Resolve the model a feature runs inference on. A generic record for a
 * feature in MODEL_SOURCE_FEATURES takes the model its source feature resolves
 * to.
 *
 * @param {string} feature
 * @param {object} config - The feature's selected config record.
 * @param {object} [opts]
 * @param {string} [opts.modelChoiceIdOverride]
 * @returns {Promise<string>}
 */
async function resolveInferenceModel(feature, config, opts = {}) {
  const model = config.inference_model || config.model;
  const sourceFeature = MODEL_SOURCE_FEATURES.get(feature);
  if (model !== GENERIC_MODEL_NAME || !sourceFeature) {
    return model;
  }
  const sourceConfig = await selectFeatureConfig(sourceFeature, {
    modelChoiceIdOverride: opts.modelChoiceIdOverride,
  });
  return resolveInferenceModel(sourceFeature, sourceConfig, opts);
}

/**
 * Resolve the RS record for a feature and build a fresh openAIEngine +
 * inference parameters.
 *
 * @param {string} feature
 * @param {object} [opts]
 * @param {Partial<PipelineOptions>} [opts.onDeviceOptions] - Runs on device
 *   with llama.cpp instead of the cloud endpoint.
 * @returns {Promise<{engine: openAIEngine, parameters: inferenceParams}>}
 */
export async function buildEngineForFeature(feature, opts = {}) {
  const mainConfig = await selectFeatureConfig(feature, opts);

  let parameters = mainConfig.parameters ?? {};
  if (typeof parameters === "string") {
    try {
      parameters = JSON.parse(parameters);
    } catch (_e) {
      parameters = {};
    }
  }
  const serviceType = mainConfig.service_type ?? getDefaultServiceType(feature);
  let purpose =
    mainConfig.purpose ??
    FEATURE_PURPOSES[feature] ??
    FEATURE_PURPOSES[DEFAULT_PURPOSE];
  const modelChoiceId =
    opts.modelChoiceIdOverride ??
    Services.prefs.getStringPref(MODEL_CHOICE_PREF, "");
  const { baseURL, apiKey } = openAIEngine.resolveEndpointConfig(modelChoiceId);

  const model = await resolveInferenceModel(feature, mainConfig, opts);

  const engine = await openAIEngine.build({
    model,
    modelChoiceId,
    serviceType,
    purpose,
    flowId: opts.flowId ?? null,
    feature,
    baseURL,
    apiKey,
    onDeviceOptions: opts.onDeviceOptions,
  });

  return { engine, parameters };
}

/**
 * Build a ready-to-use Conversation for a feature: resolves
 * model/parameters/serviceType/purpose from RS, builds the engine, and
 * returns a fresh Conversation wired to both.
 *
 * @param {string} feature
 * @param {object} [opts]
 * @returns {Promise<Conversation>}
 */
export async function buildConversation(feature, opts = {}) {
  const { engine, parameters } = await buildEngineForFeature(feature, opts);
  return new Conversation({ feature, engine, parameters });
}

// ===========================================================================
// Public entry point
// ===========================================================================

/**
 * Load the prompt text for a feature. CHAT is assembled from v2 modular
 * records for the given model (`opts.model`, recoverable from the engine);
 * every other feature reads its prompt from the v1 main-config record.
 * Honors the `customPrompts` pref override.
 *
 * @param {string} feature
 * @param {object} [opts]
 * @param {string} [opts.model] - Model to assemble the CHAT prompt for.
 * @param {string} [opts.modelChoiceIdOverride]
 * @returns {Promise<{prompt: string, version: string}>}
 */
export async function loadPrompt(feature, opts = {}) {
  const customPromptsRaw = Services.prefs.getStringPref(
    CUSTOM_PROMPTS_PREF,
    ""
  );
  if (customPromptsRaw) {
    try {
      const override = JSON.parse(customPromptsRaw)?.[feature];
      if (override) {
        return { prompt: override, version: "" };
      }
    } catch (_e) {
      // invalid JSON — fall through to RS
    }
  }

  if (opts.module) {
    return loadPromptV2(feature, opts);
  }

  // CHAT is fully v2: assemble from modular records for the engine's model.
  // Throws (no v1 fallback) if a required module is missing.
  if (feature === MODEL_FEATURES.CHAT) {
    return buildChatSystemPrompt(opts.model);
  }

  // Every other feature still reads its prompt from the v1 main-config record.
  const mainConfig = await selectFeatureConfig(feature, opts);
  if (!mainConfig.prompts) {
    const err = new Error(`No prompts field in record for feature: ${feature}`);
    err.clientReason = "promptLoadFailure";
    throw err;
  }
  return { prompt: mainConfig.prompts, version: mainConfig.version };
}

/**
 * Loads the prompt text for a feature from the v2 records.
 *
 * @param {string} feature - Feature identifier from MODEL_FEATURES
 * @param {object} [opts]
 * @param {string} opts.module - Module name from the feature's params manifest
 * @param {string} [opts.model] - Model to resolve for; defaults to the model pref
 * @param {number} [opts.majorVersionOverride] - Major to read instead of
 *   FEATURE_MAJOR_VERSIONS[feature] (tests, next-release dry runs)
 * @returns {Promise<{prompt: string, version: string}>} The prompt text and version
 */
export async function loadPromptV2(feature, opts = {}) {
  // load the records
  const v2Records = await loadV2Records();

  // resolve the model
  const model = opts.model ?? Services.prefs.getStringPref(MODEL_PREF, "");

  // find the params record for the feature+model at this build's major, or
  // the override (honored like the v1 path); findParams applies the default
  const paramsRecord = findParams(v2Records, {
    feature,
    model,
    majorVersion: opts.majorVersionOverride,
  });
  if (!paramsRecord) {
    const majorVersion =
      opts.majorVersionOverride ?? FEATURE_MAJOR_VERSIONS[feature];
    const err = new Error(
      `No matching v2 params record found for feature: ${feature} with model ${model} at major version ${majorVersion}`
    );
    err.clientReason = "v2ParamsUnavailable";
    throw err;
  }

  // pull the module version from the params record
  const moduleVersion =
    paramsRecord.modules?.find(m => m.name === opts.module)?.version ??
    paramsRecord.version;

  // find the module record for the feature+module+model+version by the param
  // record's model
  const moduleRecord = findModule(v2Records, {
    feature,
    module: opts.module,
    options: { model: paramsRecord.model, version: moduleVersion },
  });
  if (!moduleRecord?.prompts) {
    const err = new Error(
      `No matching v2 module record found for feature: ${feature} with module ${opts.module}`
    );
    err.clientReason = "v2ModuleUnavailable";
    throw err;
  }

  return {
    prompt: moduleRecord.prompts.trim(),
    version: moduleVersion,
  };
}

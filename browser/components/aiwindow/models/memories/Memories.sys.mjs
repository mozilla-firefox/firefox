/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/**
 * This module defines functions to generate, filter, and merge memories.
 *
 * The primary method is `runSessionMemoryPipeline`, which orchestrates the
 * pipeline over a batch of unified session bundles (see `buildSessions`):
 * 1. Generates initial memories, one LLM call per chunk of <=MAX_SESSIONS_PER_BATCH sessions
 * 2. Filters out low-quality (generic/ephemeral) AND sensitive memories (one global call)
 * 3. Returns the final memory objects plus the watermark the caller should advance to
 *
 * `runSessionMemoryPipeline` requires:
 * 1. `conversation`: a Conversation instance, reused across every LLM call (each step clears messages before setSystemMessage / addUserMessage)
 * 2. `sessions`: gate-filtered session bundles from `buildSessions`
 */

import {
  renderPrompt,
  MODEL_FEATURES,
  makeJSONSchemaBlob,
  parseAndExtractJSON,
} from "../Utils.sys.mjs";
import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { openAIEngine } from "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

import {
  CATEGORIES,
  CATEGORIES_LIST,
  INTENTS,
  INTENTS_LIST,
  MAX_MEMORY_SUMMARY_LENGTH,
  HISTORY,
  CONVERSATION,
  CONVERSATION_USER_REQUEST as USER,
  SESSION,
  MEMORY_TYPE_SHORT_TERM_MEMORY,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
  MEMORY_STRENGTH_EVIDENCE_WEIGHT,
  MEMORY_STRENGTH_EVIDENCE_CAP,
  MEMORY_STRENGTH_LIFETIME_ACCESSED_WEIGHT,
  MEMORY_STRENGTH_LIFETIME_ACCESSED_HALFLIFE,
  MEMORY_STRENGTH_MERGE_COUNT_WEIGHT,
  MEMORY_STRENGTH_MERGE_COUNT_HALFLIFE,
  MEMORY_STRENGTH_FLOOR,
  MEMORY_STRENGTH_PRECISION,
  MEMORY_STRENGTH_USER_REQUEST_MODIFIER,
  MEMORY_TYPE_TIERS,
  MEMORY_FRECENCY_MAX_DAYS,
  MEMORY_FRECENCY_DAY_HALFLIFE,
  MEMORY_DECAY_THRESHOLD,
  GENERATION_RUN_OUTCOME,
  GENERATION_RUN_ERROR_STAGE,
  GENERATION_RUN_ERROR_TYPE,
} from "./MemoriesConstants.sys.mjs";

/** @typedef {import("./MemoriesConstants.sys.mjs").MemoriesGenerationRunStats} MemoriesGenerationRunStats */
/** @typedef {import("./MemoriesConstants.sys.mjs").JourneysGenerationRunStats} JourneysGenerationRunStats */

// Pipeline input key for unified session bundles.
const SESSIONS = "sessions";

// Max number of session bundles sent to the LLM in a single generation call.
const MAX_SESSIONS_PER_BATCH = 10;

// Max number of retries on transient errors for a single batch before raising the 429 up to the caller.
const MAX_RETRIES_PER_BATCH = 3;

// Number of MS to wait before retrying a transient error
const INITIAL_MEMORY_GENERATION_BATCH_RETRY_DELAY_MS = 12000;

/**
 * Records a failed stage on a generation run's stats.
 *
 * Only the first error is kept.
 *
 * @param {MemoriesGenerationRunStats|JourneysGenerationRunStats} [stats] Stats object for telemetry
 * @param {string} stage  One of GENERATION_RUN_ERROR_STAGE
 * @param {Error} error
 */
export function setGenerationRunError(stats, stage, error) {
  if (!stats || stats.error_stage) {
    return;
  }
  stats.outcome = GENERATION_RUN_OUTCOME.ERROR;
  stats.error_stage = stage;
  if (openAIEngine.is429Error(error)) {
    stats.error_type = GENERATION_RUN_ERROR_TYPE.RATE_LIMITED;
  } else if (openAIEngine.isRetryableError(error)) {
    stats.error_type = GENERATION_RUN_ERROR_TYPE.TRANSIENT;
  } else if (
    typeof error?.status === "number" ||
    /\d{3} status code/.test(error?.message ?? "")
  ) {
    stats.error_type = GENERATION_RUN_ERROR_TYPE.SERVER_REJECTED;
  } else {
    stats.error_type = GENERATION_RUN_ERROR_TYPE.FIREFOX_INTERNAL;
  }
}

/**
 * Generates, filters, and deduplicates memories from a batch of unified session
 * bundles.
 *
 * Sessions are processed in chunks of at most {@link MAX_SESSIONS_PER_BATCH}:
 * each chunk is one generation call. Candidate memories from every chunk are
 * accumulated, then the expensive quality+sensitivity filter and the dedup pass
 * each run ONCE over the whole pool (cheap rejection first, single global
 * comparison last). Running them globally is what lets dedup catch duplicates
 * that surfaced across different chunks.
 *
 * A chunk that fails on a 429 (rate limit) is retried before aborting the whole pipeline:
 * the error is re-thrown so the caller can back off, nothing is persisted, and the run is
 * retried in full next time. A chunk that fails on any other (deterministic)
 * error loses its candidates and the watermark advances past it, since retrying
 * it would only wedge the pipeline.
 *
 * @param {Conversation} conversation           Conversation reused across the pipeline (cleared between calls)
 * @param {Array<object>} sessions              Session bundles from `buildSessions` (gate-filtered by the caller)
 * @param {object} [opts]
 * @param {number} [opts.batchSize]             Max sessions per generation call
 * @param {number} [opts.maxBatchRetries]       Max retries per batch per generation call
 * @param {number} [opts.initialMemoryGenerationRetryDelayMS] Number of MS to delay before retrying a transient error
 * @param {MemoriesGenerationRunStats} [opts.stats] Stats object to record telemetry information
 * @returns {Promise<{memories: Array<object>, processedThroughMs: number}>}
 *   `memories` is the final list of generated, filtered objects.
 *   `processedThroughMs` is the max `session_end_ms` the caller should
 *   advance its watermark to: the latest chunk that either succeeded or failed
 *   deterministically.
 * @throws Re-throws a 429 (rate limit) error from any LLM call so the caller can
 *   trigger its back-off.
 */
export async function runSessionMemoryPipeline(
  conversation,
  sessions,
  {
    batchSize = MAX_SESSIONS_PER_BATCH,
    maxBatchRetries = MAX_RETRIES_PER_BATCH,
    initialMemoryGenerationRetryDelayMS = INITIAL_MEMORY_GENERATION_BATCH_RETRY_DELAY_MS,
    stats = {},
  } = {}
) {
  const candidateMemories = [];
  let processedThroughMs = 0;
  let lastBatchError;
  stats.generation_batches = 0;
  stats.generation_batches_skipped = 0;

  // Step 1: Per-batch generation. Accumulate candidates across all batches.
  for (let i = 0; i < sessions.length; i += batchSize) {
    const batch = sessions.slice(i, i + batchSize);
    const batchMaxEndMs = batch.reduce(
      (max, session) => Math.max(max, session.session_end_ms),
      0
    );

    stats.generation_batches++;
    let batchHandled = false;
    let lastRetryableError;
    for (let attempt = 0; attempt < maxBatchRetries; attempt++) {
      try {
        const batchMemories = await generateInitialMemoriesList(conversation, {
          [SESSIONS]: batch,
        });
        candidateMemories.push(...batchMemories);
        processedThroughMs = Math.max(processedThroughMs, batchMaxEndMs);
        batchHandled = true;
        break;
      } catch (e) {
        if (openAIEngine.isRetryableError(e)) {
          // Transient error: these may be simple rate limiting or budget errors
          // In the event it's just rate limiting, retrying the current batch after 2 minutes to salvage the run
          lastRetryableError = e;
          await new Promise(r =>
            lazy.setTimeout(r, initialMemoryGenerationRetryDelayMS)
          );
          continue;
        }
        // Deterministic failure: retrying won't help, so advance past this batch
        // (its candidates are lost) instead of wedging the pipeline on it.
        processedThroughMs = Math.max(processedThroughMs, batchMaxEndMs);
        console.error(
          "runSessionMemoryPipeline: batch generation failed; skipping past it",
          e
        );
        stats.generation_batches_skipped++;
        lastBatchError = e;
        batchHandled = true;
        break;
      }
    }
    if (!batchHandled) {
      // Retries exhausted on a transient error: propagate so the caller can
      // back off and retry. Watermark is not advanced.
      setGenerationRunError(
        stats,
        GENERATION_RUN_ERROR_STAGE.GENERATION,
        lastRetryableError
      );
      throw lastRetryableError;
    }
  }

  stats.generation_candidates = candidateMemories.length;
  if (
    stats.generation_batches &&
    stats.generation_batches_skipped === stats.generation_batches
  ) {
    setGenerationRunError(
      stats,
      GENERATION_RUN_ERROR_STAGE.GENERATION,
      lastBatchError
    );
    return { memories: [], processedThroughMs };
  }

  if (!candidateMemories.length) {
    stats.outcome ??= GENERATION_RUN_OUTCOME.NO_CANDIDATES;
    return { memories: [], processedThroughMs };
  }

  // Step 2: Single global quality+sensitivity filter over the full candidate pool.
  const candidateSummaries = candidateMemories.map(
    memory => memory.memory_summary
  );
  let filteredSummaries;
  try {
    filteredSummaries = await applyQualityAndSensitivityFilter(
      conversation,
      candidateSummaries
    );
  } catch (e) {
    setGenerationRunError(stats, GENERATION_RUN_ERROR_STAGE.FILTER, e);
    throw e;
  }
  if (!filteredSummaries || !filteredSummaries.length) {
    stats.filter_dropped = candidateMemories.length;
    stats.outcome ??= GENERATION_RUN_OUTCOME.ALL_FILTERED;
    return { memories: [], processedThroughMs };
  }

  // Step 3: Map surviving summaries back to full memory objects.
  const memories = await mapFilteredMemoriesToInitialList(
    candidateMemories,
    filteredSummaries
  );
  stats.filter_dropped = candidateMemories.length - memories.length;
  return { memories, processedThroughMs };
}

/**
 * Computes the strength of a memory as a floor plus four additive terms.
 * `decay(d, h) = 0.5 ** (d / h)` is exponential decay over `d` days with
 * half-life `h`:
 *
 *   strength = FLOOR
 *            + (from a user request ? USER_REQUEST_MODIFIER : 0)
 *            + min(evidenceCount, EVIDENCE_CAP) * EVIDENCE_WEIGHT
 *            + sqrt(lifetime_accessed_count)
 *              * decay(daysSinceAccessed, LIFETIME_ACCESSED_HALFLIFE)
 *              * LIFETIME_ACCESSED_WEIGHT
 *            + merge_count
 *              * decay(daysSinceMerged, MERGE_COUNT_HALFLIFE)
 *              * MERGE_COUNT_WEIGHT
 *
 * The evidence term is capped so a lot of evidence URLs or chats cannot overwhelm final
 * strength.
 * The use count is damped by sqrt so repeated use has diminishing returns.
 * Both decay terms are measured against the current time, so strength falls
 * as a memory goes unused. Type is derived from strength by
 * {@link classifyMemoryAndCapStrength} rather than being an input here.
 *
 * @param {object} memory   Memory object
 * @returns {number}        Computed memory strength
 */
export function computeMemoryStrength(memory) {
  // If the memory was derived from a user request, add a constant to boost its strength
  const userRequestModifier = memory.sources.includes(USER)
    ? MEMORY_STRENGTH_USER_REQUEST_MODIFIER
    : 0;

  const evidenceCount = Object.values(memory.source_ids).reduce(
    (sum, sourceIds) => sum + sourceIds.length,
    0
  );

  // Both decay terms are keyed on elapsed days, falling back to created_at
  // when the memory has never been used or merged.
  const now = Date.now();
  const daysSince = timestamp => (now - timestamp) / (1000 * 60 * 60 * 24);
  const daysSinceAccessed = daysSince(
    memory.last_accessed ?? memory.created_at
  );
  const daysSinceMerged = daysSince(memory.last_merged ?? memory.created_at);

  const strength =
    MEMORY_STRENGTH_FLOOR +
    userRequestModifier +
    Math.min(evidenceCount, MEMORY_STRENGTH_EVIDENCE_CAP) *
      MEMORY_STRENGTH_EVIDENCE_WEIGHT +
    Math.sqrt(memory.lifetime_accessed_count) *
      0.5 ** (daysSinceAccessed / MEMORY_STRENGTH_LIFETIME_ACCESSED_HALFLIFE) *
      MEMORY_STRENGTH_LIFETIME_ACCESSED_WEIGHT +
    memory.merge_count *
      0.5 ** (daysSinceMerged / MEMORY_STRENGTH_MERGE_COUNT_HALFLIFE) *
      MEMORY_STRENGTH_MERGE_COUNT_WEIGHT;

  return (
    Math.round(strength * MEMORY_STRENGTH_PRECISION) / MEMORY_STRENGTH_PRECISION
  );
}

/**
 * Assigns a memory's type to the strongest tier of
 * {@link MEMORY_TYPE_TIERS} whose strength and age requirements it exceeds,
 * falling back to {@link MEMORY_TYPE_SHORT_TERM_MEMORY}.
 *
 * A memory that is strong enough for a tier but too young for it is held at
 * that tier's `minStrength`, which denies the promotion.
 *
 * Expects `memory.strength` to have been recomputed by
 * {@link computeMemoryStrength} first. Updates `memory.type` and possibly
 * `memory.strength` in place.
 *
 * @param {object} memory   Memory object
 */
export function classifyMemoryAndCapStrength(memory) {
  const daysSinceCreated =
    (Date.now() - memory.created_at) / (1000 * 60 * 60 * 24);

  for (const { type, minStrength, minAgeDays } of MEMORY_TYPE_TIERS) {
    if (daysSinceCreated < minAgeDays) {
      memory.strength = Math.min(memory.strength, minStrength);
    } else if (memory.strength > minStrength) {
      memory.type = type;
      return;
    }
  }

  memory.type = MEMORY_TYPE_SHORT_TERM_MEMORY;
}

/**
 * Computes a memory's position on the Ebbinghaus Forgetting Curve to
 * determine if it should be deleted due to decay. Compares the result
 * to {@link MEMORY_DECAY_THRESHOLD} and returns true if the memory
 * should be deleted or false if it shouldn't
 *
 * The Ebbinghouse Forgetting Curve computes retention `r` using
 * time `t` (here as days since last accessed) and strength `s` (
 * memory strength computed with {@link computeMemoryStrength}).
 *
 * r = exp(-t / s)
 *
 * @param {object} memory   Memory object
 * @returns {boolean}       Result of the decay comparison. True if the
 *                          memory should be deleted due to decay or false
 *                          if it shouldn't
 */
export function isShouldDeleteMemoryDueToDecay(memory) {
  // Compute t, time in days since the memory was last_accessed.
  // A never-used memory stores last_accessed as null, so fall back to
  // created_at rather than measuring from the epoch.
  const now = Date.now();
  const lastAccessed = memory.last_accessed ?? memory.created_at;
  const t = (now - lastAccessed) / (1000 * 60 * 60 * 24);

  // Compute r, "retention"
  const r = Math.exp(-t / memory.strength);

  // Compare to the threshold and return
  return r <= MEMORY_DECAY_THRESHOLD;
}

/**
 * Computes a memory's frecency using its 7-day rolling usage count
 *
 * @param {object} memory   Memory object
 * @returns {number}        Computed memory frecency
 */
export function computeMemoryFrecency(memory) {
  let frecency = 0;

  for (let day = 0; day < MEMORY_FRECENCY_MAX_DAYS; day++) {
    frecency +=
      memory.recent_accessed_counts[day] *
      Math.pow(0.5, day / MEMORY_FRECENCY_DAY_HALFLIFE);
  }

  return frecency;
}

/**
 * Formats a list of strings into a prompt-friendly bullet list
 *
 * @param {List<string>} list
 * @returns {string}
 */
export function formatListForPrompt(list) {
  return list.map(item => `- "${item}"`).join("\n");
}

/**
 * Utility function to cleanly get bullet-formatted category and memory lists
 *
 * @param {string} attributeName  "categories" or "intents"
 * @returns {string}              Formatted list string
 */
export function getFormattedMemoryAttributeList(attributeName) {
  if (attributeName === CATEGORIES) {
    return formatListForPrompt(CATEGORIES_LIST);
  } else if (attributeName === INTENTS) {
    return formatListForPrompt(INTENTS_LIST);
  }
  throw new Error(`Unsupported memory attribute name: ${attributeName}`);
}

/**
 * Renders a batch of unified session bundles into prompt text. Each session is
 * a time-window bundle of searches, page titles, and chat messages that
 * occurred together. Source IDs are intentionally NOT rendered: they stay
 * client-side and are never sent to the LLM.
 *
 * @param {Array<object>} sessions  Session bundles produced by `buildSessions`
 * @returns {string}                Prompt-ready text, one block per session
 */
export function renderSessionsForPrompt(sessions) {
  const blocks = [];
  sessions.forEach((session, index) => {
    const lines = [];
    const date = new Date(session.session_start_ms).toISOString().slice(0, 10);
    lines.push(`# Session ${index + 1} (${date})`);

    if (session.search_queries.length) {
      lines.push("## Web Searches");
      for (const query of session.search_queries) {
        lines.push(`- ${query}`);
      }
    }

    if (session.titles.length) {
      lines.push("## Website Titles");
      for (const title of session.titles) {
        lines.push(`- ${title}`);
      }
    }

    if (session.chats.length) {
      const chatLines = [];
      for (const message of session.chats) {
        const content =
          typeof message.content === "string" ? message.content.trim() : "";
        if (content) {
          chatLines.push(`- ${content}`);
        }
      }
      if (chatLines.length) {
        lines.push("## Chat", ...chatLines);
      }
    }

    blocks.push(lines.join("\n"));
  });
  return blocks.join("\n\n").trim();
}

/**
 * Sanitizes a single memory object from LLM output, checking required fields and normalizing score
 *
 * @param {*} memory               Raw memory object from LLM
 * @returns {Map<{
 *  category: string|null,
 *  intent: string|null,
 *  memory_summary: string|null,
 *  score: number,
 * }>|null}                         Sanitized memory or null if invalid
 */
function sanitizeMemory(memory) {
  // Shortcut to return nothing if memory is bad
  if (!memory || typeof memory !== "object") {
    return null;
  }

  // Check for maximum memory summary length
  if (
    memory.memory_summary &&
    memory.memory_summary.length > MAX_MEMORY_SUMMARY_LENGTH
  ) {
    console.warn(
      `Memory rejected: memory_summary exceeds max length of ${MAX_MEMORY_SUMMARY_LENGTH}: "${memory.memory_summary}"`
    );
    return null;
  }

  // Check that the candidate memory object has all the required string fields
  for (const field of ["category", "intent", "memory_summary", "reasoning"]) {
    if (!(field in memory) || typeof memory[field] !== "string") {
      return null;
    }
  }

  // Clamp score to [1,5]; treat missing/invalid as 1
  let score = Number.isFinite(memory.score) ? Math.round(memory.score) : 1;
  if (score < 1) {
    score = 1;
  } else if (score > 5) {
    score = 5;
  }

  const evidence = Array.isArray(memory.evidence) ? memory.evidence : [];

  return {
    category: memory.category,
    intent: memory.intent,
    memory_summary: memory.memory_summary,
    reasoning: memory.reasoning,
    score,
    source: deriveSource(evidence),
    keywords: memory.entities,
    // Retained transiently so `generateInitialMemoriesList` can attribute
    // source IDs; stripped before the memory leaves that function.
    evidence,
  };
}

/**
 * Derives a memory's source tag from the types of its supporting evidence.
 * Evidence types come from the LLM as one of "title" | "search" | "chat" |
 * "user". Browsing + any conversational evidence is cross-modal (SESSION);
 * a direct user query (USER) takes precedence over multi-turn chat.
 *
 * @param {Array<object>} evidence  Evidence items, each with a `type`
 * @returns {string}                One of HISTORY, CONVERSATION, USER, or SESSION
 */
function deriveSource(evidence) {
  if (!Array.isArray(evidence) || !evidence.length) {
    return SESSION;
  }
  const types = new Set(evidence.map(e => e?.type));
  const hasBrowse = types.has("title") || types.has("search");
  const hasUser = types.has("user");
  const hasChat = types.has("chat");
  if (hasBrowse && (hasChat || hasUser)) {
    return SESSION;
  }
  if (hasBrowse) {
    return HISTORY;
  }
  if (hasUser) {
    return USER;
  }
  if (hasChat) {
    return CONVERSATION;
  }
  return SESSION;
}

/**
 * Attributes the real source IDs behind a memory by matching its verbatim
 * evidence strings back to the sessions that produced them. IDs are never sent
 * to the LLM.
 *
 * Attribution is session-level: if any evidence string appears in a session's
 * browse content (titles/queries) or chat content, that session's history /
 * conversation source IDs are credited to the memory.
 *
 * @param {Array<object>} evidence  Evidence items with verbatim `value` strings
 * @param {Array<object>} sessions  The batch's session bundles from `buildSessions`
 * @returns {{history_source_ids: Array<string|number>, conversation_source_ids: Array<string>}}
 */
function attributeSourceIds(evidence, sessions) {
  const historyIds = new Set();
  const conversationIds = new Set();

  for (const item of evidence) {
    const value = typeof item?.value === "string" ? item.value : "";
    if (!value) {
      continue;
    }
    for (const session of sessions) {
      const inBrowse =
        session.search_queries.includes(value) ||
        session.titles.includes(value);
      const inChat = session.chats?.some(
        msg => typeof msg.content === "string" && msg.content.includes(value)
      );
      if (inBrowse) {
        session.history_source_ids.forEach(id => historyIds.add(id));
      }
      if (inChat) {
        session.conversation_source_ids.forEach(id => conversationIds.add(id));
      }
    }
  }

  return {
    history_source_ids: [...historyIds],
    conversation_source_ids: [...conversationIds],
  };
}

/**
 * Normalizes and validates parsed LLM output into a list of memories to handle LLM output variability
 *
 * @param {*} parsed                JSON-parsed LLM output
 * @returns {Array<Map<{
 *  category: string,
 *  intent: string,
 *  memory_summary: string,
 *  score: number,
 * }>>}                             List of sanitized memories
 */
function normalizeMemoryList(parsed) {
  let list = parsed;
  if (!Array.isArray(list)) {
    // If list isn't an array, check that it's an object with a nested "items" array
    if (list && Array.isArray(list.items)) {
      list = list.items;
    } else if (list && typeof list === "object") {
      // If list isn't an array, check that it's a least a single object, so check that list has memory-like keys
      const looksLikeMemory =
        "category" in list || "intent" in list || "memory_summary" in list;
      if (looksLikeMemory) {
        list = [list];
      }
    }
  }
  if (!Array.isArray(list)) {
    return [];
  }

  return list.map(sanitizeMemory).filter(Boolean);
}

/**
 * Prompts an LLM to generate an initial, unfiltered list of candidate memories from user data
 *
 * @param {Conversation} conversation  Conversation reused across the pipeline (cleared between calls)
 * @param {object} sources  User data source type to aggregrated records (i.e., {history: [domainItems, titleItems, searchItems]})
 * @returns {Promise<Array<object>>}  Promise resolving the list of generated memories
 */
export async function generateInitialMemoriesList(conversation, sources) {
  const [{ prompt: systemPrompt }, { prompt: userPromptTemplate }] =
    await Promise.all([
      lazy.loadPrompt(MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM),
      lazy.loadPrompt(MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_USER),
    ]);

  // Build sources string from the unified session bundles.
  let profileRecordsRenderedStr = "";
  if (sources.hasOwnProperty(SESSIONS)) {
    profileRecordsRenderedStr += renderSessionsForPrompt(sources[SESSIONS]);
  }

  const userPrompt = renderPrompt(userPromptTemplate, {
    categoriesList: getFormattedMemoryAttributeList(CATEGORIES),
    intentsList: getFormattedMemoryAttributeList(INTENTS),
    profileRecordsRenderedStr,
  });

  conversation.clearMessages();
  conversation.setSystemMessage(systemPrompt);
  conversation.addUserMessage(userPrompt);
  const response = await conversation.run({
    token: await AuthProvider.getToken(),
  });

  const parsed = parseAndExtractJSON(response, []);

  // Join real source IDs back from the sessions client-side, then drop the
  // transient evidence (never persisted).
  const sessions = sources[SESSIONS] ?? [];

  // Add and fill metadata fields for the new memories list
  const now = Date.now();
  return normalizeMemoryList(parsed).map(memory => {
    const m = {
      // System fields
      type: MEMORY_TYPE_SHORT_TERM_MEMORY,
      sources: [memory.source],
      source_ids: attributeSourceIds(memory.evidence, sessions),
      sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
      is_deleted: false,

      // Descriptive fields
      memory_summary: memory.memory_summary,
      reasoning: memory.reasoning,
      tags: [`category:${memory.category}`, `intent:${memory.intent}`],
      keywords: memory.keywords,
      component_summaries: [],

      // Tracker fields
      created_at: now,
      updated_at: now,
      last_accessed: null,
      recent_accessed_counts: Object.fromEntries(
        Array.from({ length: MEMORY_FRECENCY_MAX_DAYS }, (_, i) => [i, 0])
      ),
      lifetime_accessed_count: 0,
      frecency: 0,
      merge_count: 0,
    };

    // Delete the evidence attribute after it's been used to derive the source_ids
    delete memory.evidence;
    // Compute strength after we've added the necessary calculation components above
    m.strength = computeMemoryStrength(m);
    classifyMemoryAndCapStrength(m);

    return m;
  });
}

/**
 * Prompts an LLM to filter out both low-quality (generic/ephemeral) and sensitive
 * memories.
 *
 * @param {Conversation} conversation   Conversation reused across the pipeline (cleared between calls)
 * @param {Array<string>} memoriesList  List of memory summary strings to filter
 * @returns {Promise<Array<string>>}    Promise resolving the list of memory summary strings that are both high quality and non-sensitive
 */
export async function applyQualityAndSensitivityFilter(
  conversation,
  memoriesList
) {
  const { prompt: systemPrompt } = await lazy.loadPrompt(
    MODEL_FEATURES.MEMORIES_QUALITY_AND_SENSITIVITY_FILTER_SYSTEM
  );

  const { prompt: userPromptTemplate } = await lazy.loadPrompt(
    MODEL_FEATURES.MEMORIES_QUALITY_AND_SENSITIVITY_FILTER_USER
  );

  const userPrompt = renderPrompt(userPromptTemplate, {
    memoriesList: formatListForPrompt(memoriesList),
  });

  conversation.clearMessages();
  conversation.setSystemMessage(systemPrompt);
  conversation.addUserMessage(userPrompt);
  const response = await conversation.run({
    token: await AuthProvider.getToken(),
  });

  const parsed = parseAndExtractJSON(response, { kept_memories: [] });

  if (
    parsed.kept_memories === undefined ||
    !Array.isArray(parsed.kept_memories)
  ) {
    return [];
  }

  // Retain input memories and dont let the LLM reword memories
  const inputSet = new Set(memoriesList);
  return parsed.kept_memories.filter(
    item => typeof item === "string" && inputSet.has(item)
  );
}

/**
 *
 * @param {Map<string, any>} initialMemories    List of original, unfiltered memory objects
 * @param {Array<string>} filteredMemoriesList  List of deduplicated and sensitivity-filtered memory summary strings
 * @returns {Promise<Map<string, any>>}         Promise resolving the final list of memory objects
 */
export async function mapFilteredMemoriesToInitialList(
  initialMemories,
  filteredMemoriesList
) {
  return initialMemories.filter(memory =>
    filteredMemoriesList.includes(memory.memory_summary)
  );
}

/**
 * Prompts an LLM to return a list of memories grouped by theme
 * and topic that can be merged into a single memory
 *
 * @param {Conversation} conversation   Conversation object holding the openAIEngine
 * @param {Array<object>} memories      Array of memories
 * @returns {Array<object>}             Array of memory merge candidates
 */
export async function getMergeMemoryCandidates(conversation, memories) {
  // Gather memory summaries and reasoning and render the prompt
  const memoriesForPrompt = [];
  for (const memory of memories) {
    memoriesForPrompt.push(
      `- Statement: ${memory.memory_summary}\n  Reasoning: ${memory.reasoning}`
    );
  }

  // Prompt the LLM and extract output
  const [memoriesMergeSystemPrompt, memoriesMergeUserPrompt] =
    await Promise.all([
      lazy.loadPrompt(lazy.MODEL_FEATURES.MEMORIES_MERGE, {
        module: "system-instructions",
        model: conversation.engine?.model,
      }),
      lazy.loadPrompt(lazy.MODEL_FEATURES.MEMORIES_MERGE, {
        module: "user-data",
        model: conversation.engine?.model,
      }),
    ]);

  const userPrompt = renderPrompt(memoriesMergeUserPrompt.prompt, {
    memoriesList: memoriesForPrompt.join("\n"),
  });

  conversation.clearMessages();
  conversation.setSystemMessage(memoriesMergeSystemPrompt.prompt);
  conversation.addUserMessage(userPrompt);

  const response = await conversation.run({
    inferenceParams: {
      response_format: makeJSONSchemaBlob("MemoriesMergeCandidates", {
        type: "array",
        items: {
          type: "object",
          additionalProperties: false,
          required: ["component_statements", "new_reasoning", "new_statement"],
          properties: {
            component_statements: {
              type: "array",
              minItems: 2,
              items: { type: "string" },
            },
            new_reasoning: { type: "string" },
            new_statement: {
              type: "string",
              maxLength: MAX_MEMORY_SUMMARY_LENGTH,
            },
          },
        },
      }),
    },
    token: await AuthProvider.getToken(),
  });
  const mergedMemoriesOut = parseAndExtractJSON(response, []);

  return mergedMemoriesOut;
}

/**
 * Latest of a set of timestamps that may be null, or null when none are set.
 * `Math.max` coerces null to 0, which would report the epoch for a set of
 * memories that have never been used.
 *
 * @param {Array<?number>} timestamps   Timestamps in milliseconds since Unix epoch
 * @returns {?number}                   The latest timestamp, or null if there are none
 */
function latestTimestamp(timestamps) {
  const set = timestamps.filter(timestamp => timestamp != null);
  return set.length ? Math.max(...set) : null;
}

/**
 * Creates merged memories from a set of merge candidates
 *
 * @param {Array<object>} mergedMemoryCandidates  Array of memory merge candidates
 * @param {Array<object>} memories                Array of existing memories
 * @returns {{finalMergedMemories: Array<object>, componentMemoryIdsToDelete: Array<string>}}
 *   `finalMergedMemories`: new merged memory objects
 *   `componentMemoryIdsToDelete`: ids of the component memories that were merged to be deleted
 */
export function createMergedMemories(mergedMemoryCandidates, memories) {
  const finalMergedMemories = [];
  const componentMemoryIdsToDelete = new Set();

  const now = Date.now();

  for (const mergedMemory of mergedMemoryCandidates) {
    // Skip a merged memory that's empty or too long
    if (
      typeof mergedMemory.new_statement !== "string" ||
      !mergedMemory.new_statement.length ||
      mergedMemory.new_statement.length > MAX_MEMORY_SUMMARY_LENGTH
    ) {
      continue;
    }
    // Skip a merged memory with empty reasoning
    if (
      typeof mergedMemory.new_reasoning !== "string" ||
      !mergedMemory.new_reasoning.length
    ) {
      continue;
    }
    // Skip a merged memory without a list of component statements
    if (!Array.isArray(mergedMemory.component_statements)) {
      continue;
    }

    const componentMemories = memories.filter(mem =>
      mergedMemory.component_statements.includes(mem.memory_summary)
    );

    // A merged memory must have at least 2 components
    if (componentMemories.length < 2) {
      continue;
    }

    const mergedMemoryToSave = {
      // System fields
      sources: [...new Set(componentMemories.flatMap(mem => mem.sources))],
      source_ids: {
        history_source_ids: [
          ...new Set(
            componentMemories.flatMap(
              mem => mem.source_ids?.history_source_ids ?? []
            )
          ),
        ],
        conversation_source_ids: [
          ...new Set(
            componentMemories.flatMap(
              mem => mem.source_ids?.conversation_source_ids ?? []
            )
          ),
        ],
      },
      sensitivity_category: componentMemories.some(
        mem =>
          mem.sensitivity_category === MEMORY_SENSITIVITY_CATEGORY_SENSITIVE
      )
        ? MEMORY_SENSITIVITY_CATEGORY_SENSITIVE
        : MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
      is_deleted: false,

      // Descriptive fields
      memory_summary: mergedMemory.new_statement,
      reasoning: mergedMemory.new_reasoning,
      tags: [...new Set(componentMemories.flatMap(mem => mem.tags))],
      keywords: [...new Set(componentMemories.flatMap(mem => mem.keywords))],
      component_summaries: [
        ...new Map(
          [
            ...componentMemories.flatMap(mem => mem.component_summaries),
            ...componentMemories.map(mem => ({
              memory_summary: mem.memory_summary,
              reasoning: mem.reasoning,
            })),
          ].map(component => [component.memory_summary, component])
        ).values(),
      ],

      // Tracker fields
      created_at: Math.min(...componentMemories.map(mem => mem.created_at)),
      updated_at: Math.max(...componentMemories.map(mem => mem.updated_at)),
      last_accessed: latestTimestamp(
        componentMemories.map(mem => mem.last_accessed)
      ),
      recent_accessed_counts: Object.fromEntries(
        Array.from({ length: MEMORY_FRECENCY_MAX_DAYS }, (_, day) => [
          day,
          componentMemories.reduce(
            (sum, mem) => sum + (mem.recent_accessed_counts[day] ?? 0),
            0
          ),
        ])
      ),
      lifetime_accessed_count: Math.sumPrecise(
        componentMemories.map(mem => mem.lifetime_accessed_count)
      ),
      last_merged: now,
      merge_count:
        Math.sumPrecise(componentMemories.map(mem => mem.merge_count)) + 1,
    };
    mergedMemoryToSave.frecency = computeMemoryFrecency(mergedMemoryToSave);
    mergedMemoryToSave.strength = computeMemoryStrength(mergedMemoryToSave);
    classifyMemoryAndCapStrength(mergedMemoryToSave);
    finalMergedMemories.push(mergedMemoryToSave);

    // Record component memory IDs to delete
    for (const mem of componentMemories) {
      componentMemoryIdsToDelete.add(mem.id);
    }
  }

  return {
    finalMergedMemories,
    componentMemoryIdsToDelete: [...componentMemoryIdsToDelete],
  };
}

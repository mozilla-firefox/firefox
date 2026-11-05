/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  getRecentHistory,
  countRecentVisits,
} from "moz-src:///browser/components/aiwindow/models/memories/MemoriesHistorySource.sys.mjs";
import { getRecentChats } from "./MemoriesChatSource.sys.mjs";
import { buildSessions } from "./MemoriesSessions.sys.mjs";
import { runHeuristicGate } from "./MemoriesSessionGate.sys.mjs";
import {
  MODEL_FEATURES,
  renderPrompt,
  parseAndExtractJSON,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";
import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { openAIEngine } from "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs";
import {
  buildConversation,
  loadPrompt,
} from "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs";
import {
  ChatStore,
  MESSAGE_ROLE,
} from "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs";
import { SensitiveInfoDetector } from "moz-src:///browser/components/aiwindow/models/memories/SensitiveInfoDetector.sys.mjs";

import { MemoryStore } from "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs";
import {
  CATEGORIES,
  INTENTS,
  GATE_SKIP,
  HISTORY as SOURCE_HISTORY,
  CONVERSATION as SOURCE_CONVERSATION,
  CONVERSATION_USER_REQUEST as SOURCE_USER_REQUEST,
  PREF_GENERATE_MEMORIES_FROM_HISTORY,
  PREF_GENERATE_MEMORIES_FROM_CONVERSATION,
  MAX_MEMORY_SUMMARY_LENGTH,
  MEMORY_FRECENCY_MAX_DAYS,
  MEMORY_MERGE_MIN_MEMORY_COUNT,
  MAX_SESSIONS_FIRST_RUN,
  MAX_SESSIONS_DELTA_RUN,
  MEMORY_TYPE_PROFILE_FACT,
  DEFAULT_RELEVANT_MEMORIES_TOP_K,
  DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD,
} from "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs";
import {
  getFormattedMemoryAttributeList,
  runSessionMemoryPipeline,
  isShouldDeleteMemoryDueToDecay,
  computeMemoryFrecency,
  computeMemoryStrength,
  classifyMemoryAndCapStrength,
  getMergeMemoryCandidates,
  createMergedMemories,
} from "moz-src:///browser/components/aiwindow/models/memories/Memories.sys.mjs";
import { AIWindow } from "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs";
import { EveryWindow } from "resource:///modules/EveryWindow.sys.mjs";

const lazy = {};
ChromeUtils.defineLazyGetter(lazy, "console", function () {
  return console.createInstance({
    logModule: "MemoriesManager",
    maxLogLevelPref: "browser.smartwindow.memoriesLogLevel",
  });
});

const DEFAULT_HISTORY_FULL_LOOKUP_DAYS = 7;
const DEFAULT_HISTORY_FULL_MAX_RESULTS = 500;
const DEFAULT_HISTORY_DELTA_MAX_RESULTS = 500;
const DEFAULT_CHAT_FULL_MAX_RESULTS = 50;
const DEFAULT_CHAT_HALF_LIFE_DAYS_FULL_RESULTS = 7;

const LAST_SESSION_MEMORY_TS_ATTRIBUTE = "last_session_memory_ts";
const LAST_GENERATION_RUN_TS_ATTRIBUTE = "last_generation_run_ts";

const PREF_FIRSTRUN_HAS_COMPLETED = "browser.smartwindow.firstrun.hasCompleted";

// Single shared detector instance, mirroring MemoriesChatSource /
// MemoriesHistorySource usage.
const _sensitiveInfoDetector = new SensitiveInfoDetector();

/**
 * Keeps at most `maxSessions` sessions, selecting the most recent ones.
 *
 * Selection is newest-first so that a backlog contributes its most recent
 * activity rather than an arbitrary prefix. The result is returned in
 * chronological order because the pipeline batches sessions in array order and
 * advances `processedThroughMs` monotonically as batches complete, so an
 * ascending run keeps that watermark contiguous with the work actually done.
 *
 * @param {Array<object>} sessions  Gated session bundles from `buildSessions`
 * @param {number} maxSessions      Hard cap on sessions handed to the pipeline
 * @returns {Array<object>}
 *        At most `maxSessions` sessions, ascending by `session_end_ms`.
 */
function takeMostRecentSessions(sessions, maxSessions) {
  if (sessions.length <= maxSessions) {
    return sessions;
  }
  return sessions
    .slice()
    .sort((a, b) => b.session_end_ms - a.session_end_ms)
    .slice(0, maxSessions)
    .reverse();
}

/**
 * MemoriesManager class
 */
export class MemoriesManager {
  // Exposed to be stubbed for testing
  static _getRecentChats = getRecentChats;
  static _getRecentHistory = getRecentHistory;

  // Cached Conversation for the 3 serial LLM steps in one generateMemories()
  // pass. Callers MUST NOT invoke generation concurrently — clearMessages /
  // addMessage sequences on the shared instance race across acquires.
  static #generationConversationPromise = null;

  // Cached Conversation for memory usage (classification, relevance).
  // Same serial-only contract.
  static #usageConversationPromise = null;
  /**
   * Returns a Conversation wired to the memory-generation feature. Used for:
   * initial generation, sensitivity filter.
   *
   * @returns {Promise<Conversation>}
   */
  static async ensureConversationForGeneration() {
    const buildFresh = async () => {
      this.#generationConversationPromise = buildConversation(
        MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM
      );
      return this.#generationConversationPromise;
    };

    if (!this.#generationConversationPromise) {
      return await buildFresh();
    }

    let conversation;
    try {
      conversation = await this.#generationConversationPromise;
    } catch (e) {
      this.#generationConversationPromise = null;
      return await buildFresh();
    }

    if (!conversation?.isReady) {
      this.#generationConversationPromise = null;
      return await buildFresh();
    }
    return conversation;
  }

  /**
   * Returns a Conversation wired to the memory-usage feature. Used for:
   * message classification, relevant context.
   *
   * @returns {Promise<Conversation>}
   */
  static async ensureConversationForUsage() {
    const buildFresh = async () => {
      this.#usageConversationPromise = buildConversation(
        MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM
      );
      return this.#usageConversationPromise;
    };

    if (!this.#usageConversationPromise) {
      return await buildFresh();
    }

    let conversation;
    try {
      conversation = await this.#usageConversationPromise;
    } catch (e) {
      this.#usageConversationPromise = null;
      return await buildFresh();
    }

    if (!conversation?.isReady) {
      this.#usageConversationPromise = null;
      return await buildFresh();
    }
    return conversation;
  }

  /**
   * Updates or deletes memories in the MemoryStore
   *
   * Memories are deleted if 1 of these conditions is met:
   * 1. Soft deletion from natural language command: NL delete memories flags a memory for deletion, so the action can be
   *    undone in the same session if the user picked the wrong one. We permanently delete them here.
   * 2. Decay: We determine if a memory should be deleted by checking its position on the Ebbinghaus Forgetting Curve
   *    using its strength attribute and the number of days since it was last accessed. We compare the resulting value
   *    to {@link MEMORY_DECAY_THRESHOLD}.
   *
   * If a memory is not deleted, we update its attributes. Attribute updates and the
   * decay check are per-memory daily: each memory tracks its own day in `updated_at`
   * and is skipped until a full day has elapsed for it.
   */
  static async runMemoryMaintenance() {
    const now = Date.now();

    // Tracks whether the in-memory state of the MemoryStore changed while this function
    // is running to request a save at the end
    let changed = false;

    // Pull all memories, including soft deleted ones
    const allMemories = await MemoryStore.getMemories({
      includeSoftDeleted: true,
    });

    // Hard delete any soft deleted memories first
    for (const memory of allMemories.filter(mem => mem.is_deleted)) {
      await MemoryStore.hardDeleteMemory(memory.id);
      changed = true;
    }

    // Filter for the remainder that aren't soft deleted
    const liveMemories = allMemories.filter(memory => !memory.is_deleted);

    for (const memory of liveMemories) {
      const daysElapsed = Math.floor(
        (now - memory.updated_at) / (24 * 60 * 60 * 1000)
      );
      // Skip if a full day hasn't passed since last update
      if (daysElapsed < 1) {
        continue;
      }

      // Age the rolling usage window by however many whole days have elapsed
      const shifted = {};
      for (let day = 0; day < MEMORY_FRECENCY_MAX_DAYS; day++) {
        const source = day - daysElapsed;
        shifted[day] =
          source >= 0 ? (memory.recent_accessed_counts[source] ?? 0) : 0;
      }
      memory.recent_accessed_counts = shifted;

      // Update computed properties
      memory.frecency = computeMemoryFrecency(memory);
      memory.strength = computeMemoryStrength(memory);
      classifyMemoryAndCapStrength(memory);

      // Set updated_at to now, starting this memory's next day
      memory.updated_at = now;

      changed = true;

      // If a memory should be deleted due to decay, hard delete it now that its strength is updated
      if (isShouldDeleteMemoryDueToDecay(memory)) {
        await MemoryStore.hardDeleteMemory(memory.id);
      }
    }

    // If anything was changed to the in-memory MemoryStore, save it to disk
    if (changed) {
      await MemoryStore.requestSave();
    }
  }

  /**
   * Unified entry point: generates and persists memories from cross-modal
   * session bundles built from the user's recent browsing history AND chats.
   *
   *  1. Resolves which sources are enabled (history and/or conversation).
   *  2. Reads the single {@link getLastSessionMemoryTimestamp} watermark and
   *     pulls recent history rows and/or chat messages since it (delta), or a
   *     full lookup on first run. Disabled sources contribute `[]`.
   *  3. Builds unified sessions via {@link buildSessions}, drops sessions the
   *     heuristic gate marks `SKIP`, and keeps only the most recent
   *     {@link MAX_SESSIONS_FIRST_RUN} (first run) or
   *     {@link MAX_SESSIONS_DELTA_RUN} (delta) survivors, so one run cannot
   *     issue an unbounded number of LLM calls.
   *  4. Runs the batched generate -> global filter pipeline.
   *  5. Persists survivors once and advances the unified watermark to the
   *     contiguous successfully-processed point.
   *
   * @param {object} [pipelineOpts={}]
   *        Options forwarded to {@link runSessionMemoryPipeline} (e.g.
   *        `batchSize`, `maxBatchRetries`). Omitted keys fall back to the
   *        pipeline's own defaults.
   * @returns {Promise<Memory[]>}  Persisted memories (possibly empty).
   */
  static async generateMemoriesFromSessions(pipelineOpts = {}) {
    const historyEnabled =
      this.shouldEnableMemoriesFromSchedulers(SOURCE_HISTORY);
    const conversationEnabled =
      this.shouldEnableMemoriesFromSchedulers(SOURCE_CONVERSATION);

    if (!historyEnabled && !conversationEnabled) {
      return [];
    }

    const watermarkMs = await this.getLastSessionMemoryTimestamp();
    const isDelta = watermarkMs > 0;
    const deltaStartMs = this.getSessionMemoryDeltaStartMs(watermarkMs);

    let historyRows = [];
    if (historyEnabled) {
      const recentHistoryOpts = isDelta
        ? {
            sinceMicros: deltaStartMs * 1000,
            maxResults: DEFAULT_HISTORY_DELTA_MAX_RESULTS,
          }
        : {
            days: DEFAULT_HISTORY_FULL_LOOKUP_DAYS,
            maxResults: DEFAULT_HISTORY_FULL_MAX_RESULTS,
          };
      historyRows = await this._getRecentHistory(recentHistoryOpts);
    }

    let chatMessages = [];
    if (conversationEnabled) {
      chatMessages = await this._getRecentChats(
        deltaStartMs,
        DEFAULT_CHAT_FULL_MAX_RESULTS,
        DEFAULT_CHAT_HALF_LIFE_DAYS_FULL_RESULTS
      );
    }

    const sessions = buildSessions(historyRows, chatMessages);
    const gatedSessions = sessions.filter(
      session => runHeuristicGate(session).decision !== GATE_SKIP
    );

    // Cap sessions after gating.
    const maxSessions = isDelta
      ? MAX_SESSIONS_DELTA_RUN
      : MAX_SESSIONS_FIRST_RUN;
    const retainedSessions = takeMostRecentSessions(gatedSessions, maxSessions);
    if (retainedSessions.length < gatedSessions.length) {
      lazy.console.debug(
        "[generateMemoriesFromSessions] " +
          `Capped ${gatedSessions.length} sessions to the ${maxSessions} most ` +
          `recent; the older ${gatedSessions.length - retainedSessions.length} ` +
          "will not be processed."
      );
    }

    if (!retainedSessions.length) {
      // Since no retainedSessions are present due to SKIP decisions, then advance
      // the watermark past them to avoid re-pulling and re-gating the same
      // trivial sessions next run.
      const maxSessionEndMs = sessions.reduce(
        (max, session) => Math.max(max, session.session_end_ms),
        0
      );
      if (maxSessionEndMs > watermarkMs) {
        await this.setLastSessionMemoryTimestamp(maxSessionEndMs);
      }
      lazy.console.debug(
        "[generateMemoriesFromSessions] " +
          "No sessions to process after gating; skipping memory generation."
      );
      return [];
    }

    const conversation = await this.ensureConversationForGeneration();

    let result;
    try {
      result = await runSessionMemoryPipeline(
        conversation,
        retainedSessions,
        pipelineOpts
      );
    } catch (e) {
      // Pipeline failed; don't advance the watermark. Re-throw retryable errors
      // so the scheduler can back off; swallow permanent ones.
      console.error(
        "MemoriesManager.generateMemoriesFromSessions: " +
          "pipeline failed; watermark not advanced.",
        e
      );
      if (openAIEngine.isRetryableError(e)) {
        throw e;
      }
      return [];
    }

    const { persistedMemories } = await this.saveMemories(result.memories);

    if (result.processedThroughMs > 0) {
      await this.setLastSessionMemoryTimestamp(
        Math.max(watermarkMs, result.processedThroughMs)
      );
    }

    return persistedMemories;
  }

  /**
   * Merges memories by topic or theme to form stronger, more general, and more durable memories over time
   *
   * Profile facts are never merged.
   *
   * @param {object} [options]
   * @param {number} [options.minMemoryCount]
   *   Number of mergeable memories that must be exceeded before a merge pass
   *   runs. Only overridden by tests, which set it to 0 to exercise merging on
   *   small fixtures.
   */
  static async mergeMemories({
    minMemoryCount = MEMORY_MERGE_MIN_MEMORY_COUNT,
  } = {}) {
    // Pull all memories, not including soft deleted ones
    const allMemories = await MemoryStore.getMemories();

    // Filter out profile facts
    const mergeableMemories = allMemories.filter(
      memory => memory.type !== MEMORY_TYPE_PROFILE_FACT
    );

    // Return immediately if there aren't enough memories to merge
    // Too few memories and memory merging results in too much compression.
    if (mergeableMemories.length <= minMemoryCount) {
      return;
    }

    // Create merge candidate memory objects
    const conversation = await this.ensureConversationForGeneration();
    const mergeMemoryCandidates = await getMergeMemoryCandidates(
      conversation,
      mergeableMemories
    );
    const { finalMergedMemories, componentMemoryIdsToDelete } =
      createMergedMemories(mergeMemoryCandidates, mergeableMemories);

    // Save merge candidates in the MemoryStore and delete their component memories
    const mergedMemoryIds = new Set();
    for (const finalMergedMemory of finalMergedMemories) {
      const saved = await MemoryStore.addMemory(finalMergedMemory);
      mergedMemoryIds.add(saved.id);
    }

    for (const componentMemoryId of componentMemoryIdsToDelete) {
      // Guard to make sure we don't delete a merged memory if its ID happens to be the same as a candidate
      // Memory IDs are hashes computed from the memory summary, so it may be possible that a merged memory
      // has the same ID as an existing component
      if (mergedMemoryIds.has(componentMemoryId)) {
        continue;
      }
      await this.hardDeleteMemoryById(componentMemoryId);
    }
  }

  /**
   * Retrieves all stored memories.
   * This is a quick-access wrapper around MemoryStore.getMemories() with no additional processing.
   *
   * @param {object} [opts={}]
   * @param {boolean} [opts.includeSoftDeleted=false]
   *        Whether to include soft-deleted memories.
   * @returns {Promise<Array<object>>}
   *        List of memories
   */
  static async getAllMemories(opts = { includeSoftDeleted: false }) {
    return await MemoryStore.getMemories(opts);
  }

  /**
   * Retrieves memories by ID.
   * This is a quick-access wrapper around MemoryStore.getMemories() specifically requiring the memoryIds option.
   *
   * @param {Set<string>} memoryIds   Set of memory IDs
   * @returns {Promise<Array<Map>>}
   */
  static async getMemoriesByID(memoryIds) {
    return await MemoryStore.getMemories({ memoryIds });
  }

  /**
   * Records that a set of memories was used and resolves them to full memory
   * objects. Consumers should call this function instead of touching counts,
   * themselves.
   *
   * For each used memory, this:
   * - Increments lifetime_accessed_count
   * - Increments today's bucket (day 0) in recent_accessed_counts
   * - Stamps last_accessed with the current time
   *
   * @param {Iterable<string>} memoryIds        IDs of the memories that were used
   * @returns {Promise<Array<object>>}          The used memory objects
   */
  static async resolveUsedMemories(memoryIds) {
    const ids = new Set(memoryIds);
    if (!ids.size) {
      return [];
    }

    const used = (await this.getAllMemories()).filter(memory =>
      ids.has(memory.id)
    );
    if (!used.length) {
      return [];
    }

    const now = Date.now();
    for (const memory of used) {
      memory.lifetime_accessed_count += 1;
      memory.recent_accessed_counts[0] =
        (memory.recent_accessed_counts[0] ?? 0) + 1;
      memory.last_accessed = now;
    }

    await MemoryStore.requestSave();

    return used;
  }

  /**
   * Public API wrapper around MemoryStore.getMemories
   *
   * @param {Array<object>} [attributeFilters]     // Filtering options
   * @param {string} [attributeFilters.field]      // Memory field (attribute) on which to filter
   * @param {string} [attributeFilters.value]      // The value to filter against
   * @param {string} [attributeFilters.comparator] // How the filter should be applied (equal to, greater than, less than, etc.)
   * @returns {Array<object>}                      // Array of filtered memories
   */
  static async getMemoriesByAttribute(attributeFilters) {
    return await MemoryStore.getMemories({ attributeFilters });
  }

  /**
   * Returns the unified session-memory watermark (ms since Unix epoch): the
   * point through which the combined history+chat session pipeline has been
   * processed.
   *
   * On first read after migrating from the two legacy per-modality watermarks,
   * this seeds from the older of the two so the first unified run does a delta
   * pull rather than re-scanning all history.
   *
   * @returns {Promise<number>}  Milliseconds since Unix epoch (0 if never run)
   */
  static async getLastSessionMemoryTimestamp() {
    const meta = await MemoryStore.getMeta();
    if (meta.last_session_memory_ts) {
      return meta.last_session_memory_ts;
    }
    const legacy = [
      meta.last_history_memory_ts,
      meta.last_chat_memory_ts,
    ].filter(ts => typeof ts === "number" && ts > 0);
    return legacy.length ? Math.min(...legacy) : 0;
  }

  /**
   * Converts the session-memory watermark into the start of the not-yet-processed
   * range. The watermark is the last timestamp already processed. The next session
   * must be +1 milliseconds later to avoid pulling events included in the last session.
   *
   * @param {number} watermarkMs   Value from {@link getLastSessionMemoryTimestamp}
   * @returns {number}             Inclusive start for a delta read, 0 on first run
   */
  static getSessionMemoryDeltaStartMs(watermarkMs) {
    return watermarkMs > 0 ? watermarkMs + 1 : 0;
  }

  /**
   * Persists the unified session-memory watermark.
   *
   * @param {number} tsMs  Milliseconds since Unix epoch
   * @returns {Promise<void>}
   */
  static async setLastSessionMemoryTimestamp(tsMs) {
    await MemoryStore.updateMeta({ [LAST_SESSION_MEMORY_TS_ATTRIBUTE]: tsMs });
  }

  /**
   * Returns when generation last ran (ms since Unix epoch).
   *
   * Profiles written before this was persisted seed from the last session
   * memory watermark, which is never newer than the run that wrote it.
   *
   * @returns {Promise<number>}  Milliseconds since Unix epoch (0 if never run)
   */
  static async getLastGenerationRunTimestamp() {
    const meta = await MemoryStore.getMeta();
    return meta.last_generation_run_ts || meta.last_session_memory_ts || 0;
  }

  /**
   * Persists when generation last ran.
   *
   * @param {number} tsMs  Milliseconds since Unix epoch
   * @returns {Promise<void>}
   */
  static async setLastGenerationRunTimestamp(tsMs) {
    await MemoryStore.updateMeta({ [LAST_GENERATION_RUN_TS_ATTRIBUTE]: tsMs });
  }

  /**
   * Persists a list of generated memories, tagged with the given source. The
   * unified session watermark is advanced separately by the caller
   * (see {@link setLastSessionMemoryTimestamp}), so this no longer touches
   * MemoryStore.meta.
   *
   * @param {Array<object>|null|undefined} generatedMemories
   *        Array of MemoryPartial-like objects to persist.
   * @returns {Promise<{ persistedMemories: Array<object> }>}
   */
  static async saveMemories(generatedMemories) {
    const persistedMemories = [];

    if (Array.isArray(generatedMemories)) {
      for (const memoryPartial of generatedMemories) {
        const stored = await MemoryStore.addMemory(memoryPartial);
        persistedMemories.push(stored);
      }
    }

    return { persistedMemories };
  }

  /**
   * Adds a single memory based on a user request. Rejects requests with empty
   * summaries or containing personally identifiable information (PII).
   * Unlike saveMemories, this does not advance the conversation/history meta
   * timestamps, so it won't interfere with delta memory generation.
   *
   * @param {string} memorySummary
   * @returns {Promise<{ok: true,  memory: MemoryPartial, action: string} | {ok: false, reason: string}>}
   */
  static async saveRequestedMemory(memorySummary) {
    if (typeof memorySummary !== "string" || !memorySummary.trim()) {
      return { ok: false, reason: "Memory summary is empty." };
    }

    // Hard-truncate independently of the schema's maxLength: the stored summary is
    // later treated as trusted context, so bound any payload that survives.
    const summary = memorySummary.trim().slice(0, MAX_MEMORY_SUMMARY_LENGTH);

    // The chat message that triggered this tool, used as evidence and as a second
    // input to the PII detector.
    const recentUserMessages = await ChatStore.getMostRecentMessages(
      MESSAGE_ROLE.USER,
      1
    );
    const message = recentUserMessages[0]?.content?.body ?? "";

    // Structured PII/financial pattern detection.
    if (
      _sensitiveInfoDetector.containsSensitiveInfo(summary) ||
      _sensitiveInfoDetector.containsSensitiveInfo(message)
    ) {
      return {
        ok: false,
        reason: "Memory contains personally identifiable information.",
      };
    }

    let candidateMemory = {
      memory_summary: summary,
      reasoning: "User requested.",
      evidence: [{ type: "user", value: message }],
      sources: [SOURCE_USER_REQUEST],
    };

    const addedMemory = await MemoryStore.addMemory(candidateMemory);
    return { ok: true, memory: addedMemory, action: "created" };
  }

  /**
   * Enriches an existing memory with classified categories and intents.
   * Intended to be called fire-and-forget after saveRequestedMemory.
   *
   * @param {string} memoryId
   * @param {string} memorySummary
   */
  static async enrichExistingMemory(memoryId, memorySummary) {
    const { categories, intents } =
      await this.memoryClassifyMessage(memorySummary);

    const tags = [];
    if (categories[0]) {
      tags.push(`category:${categories[0]}`);
    }
    if (intents[0]) {
      tags.push(`intent:${intents[0]}`);
    }
    await MemoryStore.updateMemory(memoryId, { tags });
  }

  /**
   * Soft deletes a memory by its ID.
   * Soft deletion sets the memory's `is_deleted` flag to true. This prevents memory getter functions
   * from returning the memory when using default parameters. It does not delete the memory from storage.
   *
   * From the user's perspective, soft-deleted memories will not be used in assistant responses but will still exist in storage.
   *
   * @param {string} memoryId        ID of the memory to soft-delete
   * @returns {Promise<Memory|null>} The soft-deleted memory, or null if not found
   */
  static async softDeleteMemoryById(memoryId) {
    return await MemoryStore.softDeleteMemory(memoryId);
  }

  /**
   * Hard deletes a memory by its ID.
   * Hard deletion permenantly removes the memory from storage entirely. This method should be used
   * by UI to allow users to delete memories they no longer want stored.
   *
   * @param {string} memoryId       ID of the memory to hard-delete
   * @param {string} trigger        What was the trigger (assistant, settings, other)
   * @param {number|null} inUse     Number of memories still applied to the message after removal, or null if not triggered by assistant
   * @returns {Promise<boolean>}    True if the memory was found and deleted, false otherwise
   */
  static async hardDeleteMemoryById(memoryId, trigger, inUse) {
    return await MemoryStore.hardDeleteMemory(memoryId, trigger, inUse);
  }

  /**
   * Classifies a user message into memory categories and intents.
   *
   * @param {string} message                                                        User message to classify
   * @returns {Promise<Map<{categories: Array<string>, intents: Array<string>}>>}}  Categories and intents into which the message was classified
   */
  static async memoryClassifyMessage(message) {
    const conversation = await this.ensureConversationForUsage();
    const { prompt: systemPrompt } = await loadPrompt(
      MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_SYSTEM
    );
    const { prompt: userPromptTemplate } = await loadPrompt(
      MODEL_FEATURES.MEMORIES_MESSAGE_CLASSIFICATION_USER
    );
    const userPrompt = await renderPrompt(userPromptTemplate, {
      message,
      categories: getFormattedMemoryAttributeList(CATEGORIES),
      intents: getFormattedMemoryAttributeList(INTENTS),
    });

    conversation.clearMessages();
    conversation.setSystemMessage(systemPrompt);
    conversation.addUserMessage(userPrompt);
    const response = await conversation.run({
      token: await AuthProvider.getToken(),
    });

    const parsed = parseAndExtractJSON(response, {
      categories: [],
      intents: [],
    });

    if (!parsed.categories || !parsed.intents) {
      return { categories: [], intents: [] };
    }
    return parsed;
  }

  /**
   * Clears the embeddings cache in MemoryStore. Used for testing.
   *
   * @private
   */
  static _clearEmbeddingsCache() {
    MemoryStore._clearEmbeddingsCache();
  }

  /**
   * Public API wrapper around MemoryStore.getRelevantMemories
   *
   * @param {string} message                  User message to find relevant memories for
   * @param {number} topK                     Number of top relevant memories to return
   *                                          (default: {@link DEFAULT_RELEVANT_MEMORIES_TOP_K})
   * @param {number} similarityThreshold      Minimum similarity score (0-1) to include
   *                                          (default: {@link DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD})
   * @returns {Promise<Array<object>>}        List of relevant memories sorted by similarity
   */
  static async getRelevantMemories(
    message,
    topK = DEFAULT_RELEVANT_MEMORIES_TOP_K,
    similarityThreshold = DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD
  ) {
    return await MemoryStore.getRelevantMemories(
      message,
      topK,
      similarityThreshold
    );
  }

  /**
   * Helper returns true if memories generation from sources (either browsing history / conversation)
   * should be enabled.
   *
   * Gating logic for all schedulers:
   * - browser.smartwindow.enabled pref
   * - memories-from-source specific pref (history / conversation)
   * - ToU consent
   * - browser.smartwindow.firstrun.hasCompleted pref
   * - and whether any AIWindow is currently active
   *
   * If window APIs are not available (or throw), this falls back to false.
   *
   * @param {string} source - either SOURCE_HISTORY or SOURCE_CONVERSATION.
   * @return {boolean}
   */
  static shouldEnableMemoriesFromSchedulers(source) {
    // Pref checks
    const aiWindowEnabled = AIWindow.isAIWindowEnabled();
    let memoriesEnabled;
    if (source === SOURCE_HISTORY) {
      memoriesEnabled = Services.prefs.getBoolPref(
        PREF_GENERATE_MEMORIES_FROM_HISTORY,
        false
      );
    } else if (source === SOURCE_CONVERSATION) {
      memoriesEnabled = Services.prefs.getBoolPref(
        PREF_GENERATE_MEMORIES_FROM_CONVERSATION,
        false
      );
    } else {
      throw new TypeError(
        `Invalid source passed to shouldEnableMemoriesFromSchedulers: ${source}`
      );
    }

    const hasConsent = AuthProvider.hasToUConsent;

    const hasFirstrunCompleted = Services.prefs.getBoolPref(
      PREF_FIRSTRUN_HAS_COMPLETED,
      false
    );

    if (
      !aiWindowEnabled ||
      !memoriesEnabled ||
      !hasConsent ||
      !hasFirstrunCompleted
    ) {
      return false;
    }

    // Window/activity gate (fail closed)
    try {
      return EveryWindow.readyWindows.some(win =>
        AIWindow.isAIWindowActive(win)
      );
    } catch (e) {
      // If we cannot check window state, do NOT enable schedulers.
      return false;
    }
  }

  /**
   * Count recent history visits.
   * Thin wrapper around MemoriesHistorySource.countRecentVisits for callers/tests.
   *
   * @param {object} opts
   * @returns {Promise<number>}
   */
  static async countRecentVisits(opts = {}) {
    return await countRecentVisits(opts);
  }
}

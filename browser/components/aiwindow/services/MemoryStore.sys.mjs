/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Implementation of all the disk I/O required by the Memory store
 */

import { JSONFile } from "resource://gre/modules/JSONFile.sys.mjs";
import {
  MEMORY_FILTER_COMPARATOR,
  MEMORY_FILTER_PREDICATES,
  MEMORY_FILTER_VALIDATORS,
  AGGREGATE_FIELD_REGEX,
} from "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs";
import {
  HISTORY,
  CONVERSATION,
  SESSION,
  MEMORY_TYPES,
  MEMORY_SENSIVITITY_CATEGORIES,
  MEMORY_TYPE_SHORT_TERM_MEMORY,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_FRECENCY_MAX_DAYS,
  DEFAULT_RELEVANT_MEMORIES_TOP_K,
  DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD,
} from "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs";
import {
  computeMemoryFrecency,
  computeMemoryStrength,
} from "moz-src:///browser/components/aiwindow/models/memories/Memories.sys.mjs";
import { embeddingsGeneratorFactory } from "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs";
import { cosSim } from "chrome://global/content/ml/NLPUtils.sys.mjs";

/**
 * MemoryStore
 *
 * In-memory JSON state + persisted JSON file, modeled after SessionStore.
 *
 * File format (on disk):
 * {
 *   "memories": [ { ... } ],
 *   "meta": {
 *     "last_history_memory_ts": 0,
 *     "last_chat_memory_ts": 0,
 *     "last_session_memory_ts": 0,
 *     "last_generation_run_ts": 0,
 *   },
 *   "version": 1
 * }
 */

const MEMORY_STORE_FILE = "memories.json.lz4";
const MEMORY_STORE_VERSION = 2;

// Observer notification topic
const MEMORY_STORE_CHANGED = "memory-store-changed";

// In-memory state
let gState = {
  memories: [],
  meta: {
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
    last_session_memory_ts: 0,
    last_generation_run_ts: 0,
  },
  version: MEMORY_STORE_VERSION,
};

/**
 * Normalizes a raw source-IDs bundle to the canonical
 * `{ history_source_ids, conversation_source_ids }` shape with deduped arrays.
 * Memories created before source-ID tracking normalize to empty arrays.
 *
 * @param {object} [raw]
 * @returns {{history_source_ids: Array<string|number>, conversation_source_ids: Array<string>}}
 */
function normalizeSourceIds(raw) {
  return {
    history_source_ids: Array.isArray(raw?.history_source_ids)
      ? [...new Set(raw.history_source_ids)]
      : [],
    conversation_source_ids: Array.isArray(raw?.conversation_source_ids)
      ? [...new Set(raw.conversation_source_ids)]
      : [],
  };
}

/**
 * Unions two source-ID bundles (used when a memory is regenerated and we want
 * to accumulate, not replace, its provenance).
 *
 * @param {object} a
 * @param {object} b
 * @returns {{history_source_ids: Array<string|number>, conversation_source_ids: Array<string>}}
 */
function unionSourceIds(a, b) {
  const an = normalizeSourceIds(a);
  const bn = normalizeSourceIds(b);
  return {
    history_source_ids: [
      ...new Set([...an.history_source_ids, ...bn.history_source_ids]),
    ],
    conversation_source_ids: [
      ...new Set([
        ...an.conversation_source_ids,
        ...bn.conversation_source_ids,
      ]),
    ],
  };
}

/**
 * Whether a value is a well-formed list of component summaries.
 *
 * @param {*} v
 * @returns {boolean}
 */
function isComponentSummaries(v) {
  return (
    Array.isArray(v) &&
    v.every(
      component =>
        !!component &&
        typeof component === "object" &&
        typeof component.memory_summary === "string" &&
        typeof component.reasoning === "string"
    )
  );
}

// Whether we've finished initial load
let gInitialized = false;
let lazy = {};
let gInitPromise = null;
let gJSONFile = null;

// Where we store the file (choose something similar to sessionstore)
ChromeUtils.defineLazyGetter(lazy, "gStorePath", () => {
  const profD = Services.dirsvc.get("ProfD", Ci.nsIFile).path;
  return PathUtils.join(profD, MEMORY_STORE_FILE);
});

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    logModule: "MemoryStore",
    maxLogLevelPref: "browser.smartwindow.conversation.logLevel",
  })
);

/**
 * Migrates memories saved using the memory store v1 schema to v2
 *
 * @param {Array[object]} memories  Memories stored in the v1 schama
 * @returns {Array[object]}         Memories converted to the v2 schema
 */
export function migrateMemoryStoreVersionOneToTwo(memories) {
  return memories.map(m => {
    /** System fields */
    // Add missing type, assume short term memory
    if (!m.type) {
      m.type = MEMORY_TYPE_SHORT_TERM_MEMORY;
    }

    // Normalize source_ids
    m.source_ids = normalizeSourceIds(m.source_ids);
    // Create sources array if it's missing and move source into it
    if (!m.sources) {
      // Fill source if available, otherwise infer from source_ids
      // Fall back to "history" if source_ids don't exist
      if (m.source) {
        m.sources = [m.source];
      } else if (
        m.source_ids.history_source_ids.length &&
        m.source_ids.conversation_source_ids.length
      ) {
        m.sources = [SESSION];
      } else if (m.source_ids.conversation_source_ids.length) {
        m.sources = [CONVERSATION];
      } else {
        m.sources = [HISTORY];
      }
    }
    // Delete source
    delete m.source;
    // Add sensitivity category, assume not sensitive
    if (!m.sensitivity_category) {
      m.sensitivity_category = MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE;
    }
    // Make sure soft deletion flag exists
    if (typeof m.is_deleted !== "boolean") {
      m.is_deleted = false;
    }

    /** Descriptive fields */
    // Backfill empty reasoning if it doesn't exist
    if (!m.reasoning) {
      m.reasoning = "";
    }
    // Add tags
    if (!m.tags) {
      m.tags = [];
      // Move category and intent into tags if they exist as separate keys
      if (m.hasOwnProperty("category")) {
        if (m.category) {
          m.tags.push(`category:${m.category}`);
        }
      }
      if (m.hasOwnProperty("intent")) {
        if (m.intent) {
          m.tags.push(`intent:${m.intent}`);
        }
      }
    }
    // Delete category and intent
    delete m.category;
    delete m.intent;
    // Rename entities to keywords if it exists, otherwise add the keywords array
    if (Array.isArray(m.entities)) {
      m.keywords = m.entities;
    } else {
      m.keywords = [];
    }
    delete m.entities;
    // Add component summaries
    if (!m.component_summaries) {
      m.component_summaries = [];
    }

    /** Tracker fields */
    // Make sure updated_at exists
    if (!m.updated_at) {
      m.updated_at = Date.now();
    }
    // If created_at doesn't exist, set it to updated_at
    // By this point, we've lost the actual created_at timestamp
    if (!m.created_at) {
      m.created_at = m.updated_at;
    }
    // If last_accessed doesn't exist, set it to null (never used)
    // By this point, we don't have any evidence the memory was ever used
    if (!m.last_accessed) {
      m.last_accessed = null;
    }
    // If recent_accessed_counts doesn't exist, create it
    if (!m.recent_accessed_counts) {
      m.recent_accessed_counts = Object.fromEntries(
        Array.from({ length: MEMORY_FRECENCY_MAX_DAYS }, (_, i) => [i, 0])
      );
    }
    // Create counts and set to 0 if they don't exist
    for (const countField of ["lifetime_accessed_count", "merge_count"]) {
      if (!m[countField]) {
        m[countField] = 0;
      }
    }

    // Delete score if it exists
    // Score is not a v2 field
    delete m.score;

    // Add frecency & strength last
    m.frecency = computeMemoryFrecency(m);
    m.strength = computeMemoryStrength(m);

    return m;
  });
}

/**
 * Internal helper to load (and possibly migrate) memory data from disk.
 *
 * @returns {Promise<void>}
 */
async function loadMemories() {
  gJSONFile = new JSONFile({
    path: lazy.gStorePath,
    saveDelayMs: 1000,
    compression: "lz4",
    sanitizedBasename: "memories",
  });

  let markerData = null;
  if (Services.profiler.IsActive()) {
    let sizeLabel = "0 B";
    try {
      const stat = await IOUtils.stat(lazy.gStorePath);
      sizeLabel = `${(stat.size / 1048576).toFixed(1)} MiB`;
    } catch (_e) {}
    markerData = { startTime: ChromeUtils.now(), sizeLabel };
  }

  try {
    await gJSONFile.load();
  } catch (ex) {
    console.error("MemoryStore: failed to load state", ex);
    // If load fails, fall back to default gState.
    gJSONFile.data = gState;
    gInitialized = true;
    return;
  } finally {
    if (markerData) {
      ChromeUtils.addProfilerMarker(
        "SmartWindow",
        { startTime: markerData.startTime },
        `MemoryStore:load_db(${markerData.sizeLabel})`
      );
    }
  }

  // Normalize the loaded data into our expected shape.
  const data = gJSONFile.data;
  let isMemoriesMigrated = false;
  if (!data || typeof data !== "object") {
    gJSONFile.data = gState;
  } else {
    let memories;
    // Handle backwards compatibility by converting memories stored in older schemas to the current version
    if (data.version === MEMORY_STORE_VERSION && Array.isArray(data.memories)) {
      memories = data.memories;
    } else if (
      typeof data.version === "number" &&
      data.version === 1 &&
      Array.isArray(data.memories)
    ) {
      memories = migrateMemoryStoreVersionOneToTwo(data.memories);
      isMemoriesMigrated = true;
      data.version = 2;
    } else {
      // All memory store JSONs are saved with a version number, but if something went wrong and it wasn't, clear the store
      lazy.console.warn(
        "Could not determine MemoryStore version; clearing saved memories"
      );
      memories = [];
    }

    // Add extra v2 schema fields
    memories.map(mem => {
      if (!mem.merge_count) {
        mem.merge_count = 0;
      }
      return mem;
    });

    gState = {
      memories,
      meta: {
        last_history_memory_ts: data.meta?.last_history_memory_ts || 0,
        last_chat_memory_ts: data.meta?.last_chat_memory_ts || 0,
        last_session_memory_ts: data.meta?.last_session_memory_ts || 0,
        last_generation_run_ts: data.meta?.last_generation_run_ts || 0,
      },
      version:
        typeof data.version === "number" ? data.version : MEMORY_STORE_VERSION,
    };
    // Ensure JSONFile.data points at our normalized state object.
    gJSONFile.data = gState;

    // If memories were migrated from an older schema, save
    if (isMemoriesMigrated) {
      gJSONFile?.saveSoon();
      Services.obs.notifyObservers(null, MEMORY_STORE_CHANGED);
    }
  }

  gInitialized = true;
}

// Public API object
export const MemoryStore = {
  // Observer notification topic
  MEMORY_STORE_CHANGED,

  // Embeddings cache for semantic memory search
  embeddingsGenerator: null,
  memoryEmbeddingsCache: null,
  memoryCacheKey: null,

  /**
   * Initialize the store: set up JSONFile and load from disk.
   *
   * @returns {Promise<void>}
   */
  async ensureInitialized() {
    if (gInitialized) {
      return;
    }

    if (!gInitPromise) {
      gInitPromise = loadMemories();
    }

    await gInitPromise;
  },

  /**
   * Request a debounced write of the current in-memory state to disk.
   */
  async requestSave() {
    await this.ensureInitialized();
    gJSONFile?.saveSoon();
  },

  /**
   * Force writing current in-memory state to disk immediately.
   *
   * This is intended for test only.
   */
  async testOnlyFlush() {
    await this.ensureInitialized();
    if (!gJSONFile) {
      return;
    }
    await gJSONFile._save();
  },

  /**
   * @typedef {object} Memory
   * @property {string} id - Unique identifier for the memory.
   * @property {string} type - Type of the memory.
   * @property {Array[string]} sources - List of where the memory originated (history or conversation).
   * @property {object} source_ids - Specific browsing history and conversations from which the memory was generated.
   * @property {string} sensitivity_category - Sensitivity type of the memory.
   * @property {boolean} is_deleted - Whether the memory is marked as deleted.
   * @property {string} memory_summary - Short human-readable summary of the memory.
   * @property {string} reasoning - Explanation of why this memory was created.
   * @property {Array[string]} tags - List of tags for the memory.
   * @property {Array[string]} keywords - List of keywords for the memory.
   * @property {Array[object]} component_summaries - List of {memory_summary, reasoning} pairs from the memories merged to create this one.
   * @property {number} created_at - When the memory was created in milliseconds since Unix epoch.
   * @property {number} updated_at - When the memory was last aged by memory maintenance, in milliseconds since Unix epoch. This doubles as the memory's maintenance clock.
   * @property {number} last_accessed - Last-accessed time in milliseconds since Unix epoch.
   * @property {object} recent_accessed_counts - Rolling 7-day count of how often the memory was used.
   * @property {number} lifetime_accessed_count - Total number of times the memory was used across its life
   * @property {number} frecency - Computed frecency for the memory.
   * @property {number} strength - Computed strength for the memory, driving its type tier and decay.
   * @property {number} last_merged - When the memory was last merged with others.
   * @property {number} merge_count - How many merge operations contributed to this memory.
   */
  /**
   * @typedef {object} MemoryPartial
   * @property {string} id - Unique identifier for the memory.
   * @property {string} type - Type of the memory.
   * @property {Array[string]} sources - List of where the memory originated (history or conversation).
   * @property {object} source_ids - Specific browsing history and conversations from which the memory was generated.
   * @property {string} sensitivity_category - Sensitivity type of the memory.
   * @property {boolean} is_deleted - Whether the memory is marked as deleted.
   * @property {string} memory_summary - Short human-readable summary of the memory.
   * @property {string} reasoning - Explanation of why this memory was created.
   * @property {Array[string]} tags - List of tags for the memory.
   * @property {Array[string]} keywords - List of keywords for the memory.
   * @property {Array[object]} component_summaries - List of {memory_summary, reasoning} pairs from the memories merged to create this one.
   * @property {number} created_at - When the memory was created in milliseconds since Unix epoch.
   * @property {number} updated_at - When the memory was last aged by memory maintenance, in milliseconds since Unix epoch. This doubles as the memory's maintenance clock.
   * @property {number} last_accessed - Last-accessed time in milliseconds since Unix epoch.
   * @property {object} recent_accessed_counts - Rolling 7-day count of how often the memory was used.
   * @property {number} lifetime_accessed_count - Total number of times the memory was used across its life
   * @property {number} frecency - Computed frecency for the memory.
   * @property {number} strength - Computed strength for the memory, driving its type tier and decay.
   * @property {number} last_merged - When the memory was last merged with others.
   * @property {number} merge_count - How many merge operations contributed to this memory.
   */
  /**
   * Add a new memory, or update an existing one with the same id.
   *
   * Any missing fields on {@link MemoryPartial} are defaulted.
   *
   * @param {MemoryPartial} memoryPartial
   * @returns {Promise<Memory>}
   */
  async addMemory(memoryPartial) {
    await this.ensureInitialized();

    const now = Date.now();
    const id = makeMemoryId(memoryPartial);

    // Try to update an existing memory first
    let memory = await this.updateMemory(id, memoryPartial);

    // Otherwise create a new one
    if (!memory) {
      const source_ids = normalizeSourceIds(memoryPartial.source_ids);
      let sources;
      // If the partial memory doesn't have a sources list, impute it
      if (!memoryPartial.sources) {
        if (
          source_ids.history_source_ids.length &&
          source_ids.conversation_source_ids.length
        ) {
          sources = [SESSION];
        } else if (source_ids.history_source_ids.length) {
          sources = [HISTORY];
        } else if (source_ids.conversation_source_ids.length) {
          sources = [CONVERSATION];
        } else {
          // Can't identify the partial memory's sources
          sources = [];
        }
      } else {
        sources = memoryPartial.sources;
      }

      memory = {
        // System fields
        id,
        type: memoryPartial.type || MEMORY_TYPE_SHORT_TERM_MEMORY,
        sources,
        source_ids,
        sensitivity_category:
          memoryPartial.sensitivity_category ||
          MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
        is_deleted: memoryPartial.is_deleted ?? false,

        // Descriptive fields
        memory_summary: memoryPartial.memory_summary || "",
        reasoning: memoryPartial.reasoning || "",
        tags: memoryPartial.tags || [],
        keywords: memoryPartial.keywords || [],
        component_summaries: memoryPartial.component_summaries || [],

        // Tracker fields
        created_at: memoryPartial.created_at || now,
        updated_at: memoryPartial.updated_at || now,
        last_accessed: memoryPartial.last_accessed || null,
        recent_accessed_counts:
          memoryPartial.recent_accessed_counts ||
          Object.fromEntries(
            Array.from({ length: MEMORY_FRECENCY_MAX_DAYS }, (_, i) => [i, 0])
          ),
        lifetime_accessed_count: memoryPartial.lifetime_accessed_count || 0,
        frecency: memoryPartial.frecency || 0,
        strength: memoryPartial.strength || 0,
        last_merged: memoryPartial.last_merged || null,
        merge_count: memoryPartial.merge_count || 0,
      };

      gState.memories.push(memory);
      gJSONFile?.saveSoon();
      Services.obs.notifyObservers(null, MEMORY_STORE_CHANGED);
    }
    updateMemoriesCountMetric();
    return memory;
  },

  /**
   * Update an existing memory by id.
   *
   * @param {string} id
   * @param {object} updates
   * @returns {Promise<Memory|null>}
   */
  async updateMemory(id, updates) {
    await this.ensureInitialized();

    const memory = gState.memories.find(i => i.id === id);
    if (!memory) {
      return null;
    }

    // Validate that each propery in the memory updates has the right shape before using it
    const validatedProperties = [
      // System fields
      ["type", v => typeof v === "string" && MEMORY_TYPES.includes(v)],
      ["sources", v => Array.isArray(v) && v.every(i => typeof i === "string")],
      [
        "source_ids",
        v =>
          !!v &&
          typeof v === "object" &&
          !Array.isArray(v) &&
          Object.values(v).every(
            val => Array.isArray(val) && val.every(i => typeof i === "string")
          ),
      ],
      [
        "sensitivity_category",
        v => typeof v === "string" && MEMORY_SENSIVITITY_CATEGORIES.includes(v),
      ],
      ["is_deleted", v => typeof v === "boolean"],

      // Descriptive fields
      ["memory_summary", v => typeof v === "string"],
      ["reasoning", v => typeof v === "string"],
      ["tags", v => Array.isArray(v) && v.every(i => typeof i === "string")],
      [
        "keywords",
        v => Array.isArray(v) && v.every(i => typeof i === "string"),
      ],

      // Tracker fields
      ["created_at", v => Number.isFinite(v)],
      ["last_accessed", v => Number.isFinite(v)],
      [
        "recent_accessed_counts",
        v =>
          !!v &&
          typeof v === "object" &&
          !Array.isArray(v) &&
          Object.values(v).every(val => Number.isFinite(val)),
      ],
      ["lifetime_accessed_count", v => Number.isFinite(v)],
      ["frecency", v => Number.isFinite(v)],
      ["strength", v => Number.isFinite(v)],
      ["last_merged", v => Number.isFinite(v)],
      ["merge_count", v => Number.isFinite(v)],
    ];

    for (const [prop, validator] of validatedProperties) {
      if (prop in updates && validator(updates[prop])) {
        if (Array.isArray(updates[prop])) {
          // Merge the update with the existing value if arrays
          memory[prop] = [...new Set([...memory[prop], ...updates[prop]])];
        } else {
          // Otherwise overwrite
          memory[prop] = updates[prop];
        }
      }
    }

    if (updates.source_ids) {
      // Smarter merging for source IDs
      memory.source_ids = unionSourceIds(memory.source_ids, updates.source_ids);
    }

    if (isComponentSummaries(updates.component_summaries)) {
      // Component summaries are objects, so they dedupe on their summary
      memory.component_summaries = [
        ...new Map(
          [...memory.component_summaries, ...updates.component_summaries].map(
            component => [component.memory_summary, component]
          )
        ).values(),
      ];
    }

    memory.updated_at ||= updates.updated_at || Date.now();

    gJSONFile?.saveSoon();
    Services.obs.notifyObservers(null, MEMORY_STORE_CHANGED);
    return memory;
  },

  /**
   * Soft delete an memory (set is_deleted = true).
   *
   *  soft deleted memories will be filtered from getMemories
   *
   * @param {string} id
   * @returns {Promise<Memory|null>}
   */
  async softDeleteMemory(id) {
    let memory = await this.updateMemory(id, { is_deleted: true });
    Services.obs.notifyObservers(null, MEMORY_STORE_CHANGED);
    updateMemoriesCountMetric();
    return memory;
  },

  /**
   * hard delete (remove from array).
   *
   * @param {string} id
   * @param {string} trigger
   * @param {number|null} inUse
   * @returns {Promise<boolean>}
   */
  async hardDeleteMemory(id, trigger = "other", inUse = null) {
    await this.ensureInitialized();
    const idx = gState.memories.findIndex(i => i.id === id);
    if (idx === -1) {
      return false;
    }
    gState.memories.splice(idx, 1);
    gJSONFile?.saveSoon();
    Glean.smartWindow.memoryRemovedPanel.record({
      memories: gState.memories.length,
      trigger,
      in_use: inUse,
    });
    Services.obs.notifyObservers(null, MEMORY_STORE_CHANGED);
    updateMemoriesCountMetric();
    return true;
  },

  /**
   * Computes a hash of memories for cache invalidation.
   * Uses incremental FNV-1a hashing to avoid allocating large concatenated strings
   * based on https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
   *
   * @param {Array} memories  Array of memory objects with id and memory_summary fields
   * @returns {number}        32-bit hash representing the memories state
   */
  computeMemoriesHash(memories) {
    // FNV-1a offset basis (32-bit)
    let hash = 0x811c9dc5;

    for (const m of memories) {
      const str = `${m.id}-${m.memory_summary}`;
      for (let i = 0; i < str.length; i++) {
        hash ^= str.charCodeAt(i);
        // FNV prime, keep 32-bit
        hash = (hash * 0x01000193) >>> 0;
      }
    }

    return hash;
  },

  /**
   * Clears the embeddings cache. Used for testing.
   */
  _clearEmbeddingsCache() {
    this.memoryEmbeddingsCache = null;
    this.memoryCacheKey = null;
  },

  /**
   * Fetches relevant memories for a given user message using semantic similarity.
   * Uses embeddings and cosine similarity for fast, accurate memory retrieval.
   *
   * @param {string} message                  User message to find relevant memories for
   * @param {number} topK                     Number of top relevant memories to return
   *                                          (default: {@link DEFAULT_RELEVANT_MEMORIES_TOP_K})
   * @param {number} similarityThreshold      Minimum similarity score (0-1) to include
   *                                          (default: {@link DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD})
   * @returns {Promise<Array<object>>}        List of relevant memories sorted by similarity
   */
  async getRelevantMemories(
    message,
    topK = DEFAULT_RELEVANT_MEMORIES_TOP_K,
    similarityThreshold = DEFAULT_RELEVANT_MEMORIES_SIMILARITY_THRESHOLD
  ) {
    const memories = await this.getMemories({ includeSoftDeleted: false });

    if (memories.length === 0) {
      return [];
    }

    // Lazy initialize embeddings generator
    if (!this.embeddingsGenerator) {
      this.embeddingsGenerator = embeddingsGeneratorFactory.forGeneral();
    }

    // Re-embed memories only if cache is invalid
    const currentCacheKey = this.computeMemoriesHash(memories);
    if (
      !this.memoryEmbeddingsCache ||
      this.memoryCacheKey !== currentCacheKey
    ) {
      const memoryTexts = memories.map(m => {
        const summary = m.memory_summary?.toLowerCase() || "";
        const reasoning = m.reasoning?.toLowerCase() || "";
        const tags = m.tags?.join(" ").toLowerCase() || "";
        return [tags, summary, reasoning]
          .filter(part => part?.trim())
          .join(". ")
          .toLowerCase();
      });
      const result = await this.embeddingsGenerator.embedMany(memoryTexts);
      this.memoryEmbeddingsCache = result.output || result;
      this.memoryCacheKey = currentCacheKey;
    }

    const queryResult = await this.embeddingsGenerator.embed(
      message.toLowerCase()
    );
    let queryEmbedding = queryResult.output || queryResult;

    if (Array.isArray(queryEmbedding) && queryEmbedding.length === 1) {
      queryEmbedding = queryEmbedding[0];
    }

    // Calculate cosine similarity
    const similarities = this.memoryEmbeddingsCache.map((memEmb, idx) => ({
      ...memories[idx],
      similarity: cosSim(queryEmbedding, memEmb),
    }));

    // Filter by threshold, sort by similarity, and return top K
    return similarities
      .filter(m => m.similarity >= similarityThreshold)
      .sort((a, b) => b.similarity - a.similarity)
      .slice(0, topK);
  },

  /**
   * Expands a day list like "0-2,4" into a deduped array of day indices, dropping
   * any out of the [0, MEMORY_FRECENCY_MAX_DAYS) window. An empty list yields an empty array.
   * Used for the recent_accessed_counts attribute.
   *
   * @param {string} dayList  // String list of day indices (i.e. [1], [1,2], [1-3,5])
   * @returns {number[]}      // Parsed integer list of indicies
   */
  parseAggregateDays(dayList) {
    const days = new Set();
    for (const token of dayList.split(",")) {
      const trimmed = token.trim();
      if (!trimmed) {
        continue;
      }
      const range = trimmed.match(/^(\d+)-(\d+)$/);
      if (range) {
        for (let day = Number(range[1]); day <= Number(range[2]); day++) {
          days.add(day);
        }
      } else {
        days.add(Number(trimmed));
      }
    }
    return [...days].filter(day => day >= 0 && day < MEMORY_FRECENCY_MAX_DAYS);
  },

  /**
   * Resolves a filter field against a memory.
   * Plain fields return `memory[field]` directly. Aggregate fields (i.e. recent_accessed_counts)
   * sum the requested components (i.e. days) of the referenced count map into a single number
   * to filter against.
   *
   * @param {object} memory   // Memory object from which to retrieve field value
   * @param {string} field    // Memory attribute
   * @returns {*}             // Resolved memory attribute value
   */
  resolveFilterField(memory, field) {
    // Check if the field needs to be aggregated
    const match = field.match(AGGREGATE_FIELD_REGEX);

    // Return directly if not
    if (!match) {
      return memory[field];
    }

    // Otherwise, aggregate the field's value and return
    const [, mapField, dayList] = match;
    const map = memory[mapField] ?? {};
    return this.parseAggregateDays(dayList).reduce(
      (sum, day) => sum + (map[day] ?? 0),
      0
    );
  },

  /**
   * Get all memories (optionally filtered and sorted).
   *
   * @param {object} [options]
   *   Optional sorting options.
   * @param {"score"|"updated_at"} [options.sortBy="updated_at"]
   *   Field to sort by.
   * @param {"asc"|"desc"} [options.sortDir="desc"]
   *   Sort direction.
   * @param {boolean} [options.includeSoftDeleted=false]
   *   Whether to include soft-deleted memories.
   * @param {Set<string>} [options.memoryIds=new Set()]
   *   Optional set of memory IDs; will return all if set is empty
   * @param {Array<object>} [options.attributeFilters]
   *   Optional set of filter objects for filtering on specific memory attributes, each containing:
   *    1. field: The memory field (attribute) on which the filter should be applied.
   *       A field may also address into a day-keyed count map and sum a window
   *       of days, e.g. "recent_accessed_counts.[0]" (today) or
   *       "recent_accessed_counts.[0-2,4]" (days 0, 1, 2 and 4 summed).
   *    2. value: The value to filter against
   *    3. comparator: The comparator to filter field against value.
   *    4. options: Optional comparator-specific options. For the LIKE comparator, these are
   *       forwarded to getRelevantMemories: `{ topK, similarityThreshold }`. Either may be
   *       omitted to fall back to the getRelevantMemories defaults.
   *
   *
   *    Valid comparators are:
   *
   *    ** Arrays against individual values **
   *    - INCLUDES: Memory field is an array and includes the filter value
   *    - IN: Filter value is an array and includes the memory field
   *
   *    ** Individual values against individual values **
   *    - GREATER_THAN: Memory field is greater than the filter value
   *    - LESS_THAN: Memory field is less than the filter value
   *    - GREATER_THAN_OR_EQUAL_TO: Memory field is greater than or equal to the filter value
   *    - LESS_THAN_OR_EQUAL_TO: Memory field is less than or equal to the filter value
   *    - EQUAL_TO: Memory field is exactly the same as the filter value
   *    - LIKE: Memory field is semantically similar to the filter value (only memory_summary supported)
   *
   *    ** Arrays against arrays **
   *    - SOME: Both the memory field and value are **arrays** and the memory field array includes some of the filter value array
   *    - ALL: Both the memory field and value are **arrays** and the memory field array is the same as the filter value array
   * @returns {Promise<Memory[]>}
   */
  async getMemories({
    sortBy = "updated_at",
    sortDir = "desc",
    includeSoftDeleted = false,
    memoryIds = new Set(),
    attributeFilters = [],
  } = {}) {
    await this.ensureInitialized();

    let res = gState.memories;

    // Filter soft deletion first
    if (!includeSoftDeleted) {
      res = res.filter(i => !i.is_deleted);
    }

    // Then by specific memory IDs
    if (memoryIds.size) {
      res = res.filter(i => memoryIds.has(i.id));
    }

    // Next, filter by attribute filters
    for (const { field, comparator, value, options } of attributeFilters) {
      // Validate filters first
      // Invalid comparator
      if (!Object.values(MEMORY_FILTER_COMPARATOR).includes(comparator)) {
        lazy.console.error(`Invalid filtering comparator: "${comparator}"`);
        return [];
      }
      const validators = MEMORY_FILTER_VALIDATORS[comparator];
      // Invalid filter field
      if (!validators.field(field)) {
        lazy.console.error(
          `Invalid field for comparator "${comparator}": "${field}"`
        );
        return [];
      }
      // Invalid filter value
      if (!validators.value(value)) {
        lazy.console.error(
          `Invalid value for comparator "${comparator}": "${value}"`
        );
        return [];
      }

      // Apply filters
      // Special case for semantic filters
      if (comparator === MEMORY_FILTER_COMPARATOR.LIKE) {
        const { topK, similarityThreshold } = options ?? {};
        const semanticMatches = new Set(
          (
            await this.getRelevantMemories(value, topK, similarityThreshold)
          ).map(mem => mem[field])
        );
        res = res.filter(memory => semanticMatches.has(memory[field]));
        continue;
      }
      // Other non-semantic filters
      const predicate = MEMORY_FILTER_PREDICATES[comparator];
      if (predicate) {
        res = res.filter(memory =>
          predicate(this.resolveFilterField(memory, field), value)
        );
      }
    }

    // Sort the results
    if (sortBy) {
      res = [...res].sort((a, b) => {
        const av = a[sortBy] ?? 0;
        const bv = b[sortBy] ?? 0;
        if (av === bv) {
          return 0;
        }
        const cmp = av < bv ? -1 : 1;
        return sortDir === "asc" ? cmp : -cmp;
      });
    }

    return res;
  },

  /**
   * Get current meta block.
   *
   * @returns {Promise<object>}
   */
  async getMeta() {
    await this.ensureInitialized();
    return structuredClone(gState.meta);
  },

  /**
   * Update meta information (last timestamps, top_* info, etc).
   *
   * Example payload:
   * {
   * last_history_memory_ts: 12345,
   * }
   *
   * @param {object} partialMeta
   * @returns {Promise<void>}
   */
  async updateMeta(partialMeta) {
    await this.ensureInitialized();
    const meta = gState.meta;
    const validatedProps = [
      ["last_history_memory_ts", v => Number.isFinite(v)],
      ["last_chat_memory_ts", v => Number.isFinite(v)],
      ["last_session_memory_ts", v => Number.isFinite(v)],
      ["last_generation_run_ts", v => Number.isFinite(v)],
    ];

    for (const [prop, validator] of validatedProps) {
      if (prop in partialMeta && validator(partialMeta[prop])) {
        meta[prop] = partialMeta[prop];
      }
    }

    gJSONFile?.saveSoon();
    updateMemoriesLastUpdatedMetric();
  },
};

function updateMemoriesCountMetric() {
  let historyCount = 0;
  let conversationCount = 0;
  let sessionCount = 0;

  for (const memory of gState.memories) {
    if (memory.is_deleted) {
      continue;
    }
    if (memory.sources?.includes(CONVERSATION)) {
      conversationCount++;
    } else if (memory.sources?.includes(HISTORY)) {
      historyCount++;
    } else if (memory.sources?.includes(SESSION)) {
      sessionCount++;
    }
  }

  Glean.smartWindow.memoriesCount.history.set(historyCount);
  Glean.smartWindow.memoriesCount.conversation.set(conversationCount);
  Glean.smartWindow.memoriesCount.session.set(sessionCount);

  updateMemoriesLastUpdatedMetric();
}

function updateMemoriesLastUpdatedMetric() {
  const lastUpdated =
    Math.max(
      gState.meta.last_history_memory_ts || 0,
      gState.meta.last_chat_memory_ts || 0,
      gState.meta.last_session_memory_ts || 0
    ) || Date.now();
  if (!lastUpdated || lastUpdated <= 0) {
    return;
  }
  Glean.smartWindow.memoriesLastUpdated.set(new Date(lastUpdated));
}

/**
 * Simple deterministic hash of a string → 8-char hex.
 * Based on a 32-bit FNV-1a-like hash.
 *
 * @param {string} str
 * @returns {string}
 */
function hashStringToHex(str) {
  // FNV offset basis
  let hash = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    hash ^= str.charCodeAt(i);
    // FNV prime, keep 32-bit
    hash = (hash * 0x01000193) >>> 0;
  }
  // Convert to 8-digit hex
  return hash.toString(16).padStart(8, "0");
}

/**
 * Build a deterministic memory id from its core fields.
 * If the caller passes an explicit id, we honor that instead.
 *
 * @param {object} memoryPartial
 */
export function makeMemoryId(memoryPartial) {
  if (memoryPartial.id) {
    return memoryPartial.id;
  }

  const hex = hashStringToHex(
    (memoryPartial.memory_summary || "").trim().toLowerCase()
  );
  return `mem.${hex}`;
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  MemoryStore:
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  CATEGORIES_LIST:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs",
  INTENTS_LIST:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs",
});

/**
 * Normalize a single memory entry into the shape expected by
 * MemoryStore.addMemory. Returns `null` for entries that cannot be coerced
 * into a meaningful summary.
 *
 * @param {unknown} entry
 */
function normalizeMemoryEntry(entry) {
  if (typeof entry === "string") {
    const trimmed = entry.trim();
    if (!trimmed) {
      return null;
    }
    return {
      category: "Hobbies & Leisure",
      memory_summary: trimmed,
      intent: "Research / Learn",
      score: 5,
      updated_at: Date.now(),
      is_deleted: false,
    };
  }

  if (!entry || typeof entry !== "object") {
    return null;
  }

  let category = (entry.category && String(entry.category).trim()) || "";
  if (!lazy.CATEGORIES_LIST.includes(category)) {
    category = "Hobbies & Leisure";
  }

  let intent = entry.intent ? String(entry.intent).trim() : "";
  if (!lazy.INTENTS_LIST.includes(intent)) {
    intent = "Research / Learn";
  }

  const summary =
    (entry.memory_summary && String(entry.memory_summary)) ||
    (entry.insight_summary && String(entry.insight_summary)) ||
    (entry.summary && String(entry.summary)) ||
    "";
  if (!summary) {
    return null;
  }

  return {
    category,
    memory_summary: summary,
    intent,
    score:
      typeof entry.score === "number" ? entry.score : Number(entry.score) || 5,
    updated_at:
      entry.updated_at && typeof entry.updated_at === "number"
        ? entry.updated_at
        : Date.now(),
    is_deleted: Boolean(entry.is_deleted),
  };
}

/**
 * Normalize and store memories via MemoryStore.addMemory.
 *
 * @param {Array<object|string> | { memoriesDataByCategory?: object } | object} rawMemories
 * @returns {Promise<{ ok: true, count: number, memories: object[] }>}
 */
export async function setMemories(rawMemories) {
  let memories;
  if (Array.isArray(rawMemories)) {
    memories = rawMemories.map(normalizeMemoryEntry).filter(Boolean);
  } else if (rawMemories && typeof rawMemories === "object") {
    if (rawMemories.memoriesDataByCategory) {
      memories = Object.values(rawMemories.memoriesDataByCategory)
        .flat()
        .map(normalizeMemoryEntry)
        .filter(Boolean);
    } else {
      const normalized = normalizeMemoryEntry(rawMemories);
      memories = normalized ? [normalized] : [];
    }
  } else {
    throw new Error("Memories payload must be an array or object");
  }

  const addedMemories = [];
  for (const memory of memories) {
    try {
      const added = await lazy.MemoryStore.addMemory(memory);
      addedMemories.push(added);
    } catch (err) {
      console.warn("Failed to add memory:", memory, err);
    }
  }

  return {
    ok: true,
    count: addedMemories.length,
    memories: addedMemories,
  };
}

/**
 * Get every memory via MemoriesManager.
 *
 * @returns {Promise<{ ok: true, memories: object[] }>}
 */
export async function getAllMemories() {
  const memories = await lazy.MemoriesManager.getAllMemories();
  return { ok: true, memories };
}

/**
 * Fetch k memories semantically relevant to `query` above the given
 * similarity threshold.
 *
 * @param {string} query
 * @param {number} semanticSimilarityThreshold
 * @param {number} k
 * @returns {Promise<{ ok: true, relevantMemories: object[] }>}
 */
export async function getRelevantMemories(
  query,
  semanticSimilarityThreshold,
  k
) {
  const relevantMemories = await lazy.MemoriesManager.getRelevantMemories(
    query,
    k,
    semanticSimilarityThreshold
  );
  return { ok: true, relevantMemories };
}

/**
 * Hard-delete every memory in MemoryStore (including soft-deleted rows).
 *
 * @returns {Promise<{ ok: true, deleted: number }>}
 */
export async function clearMemories() {
  const allMemories = await lazy.MemoryStore.getMemories({
    includeSoftDeleted: true,
  });
  let deleted = 0;
  for (const memory of allMemories) {
    try {
      await lazy.MemoryStore.hardDeleteMemory(memory.id);
      deleted++;
    } catch (err) {
      console.warn("Failed to delete memory:", memory.id, err);
    }
  }
  return { ok: true, deleted };
}

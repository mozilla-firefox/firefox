/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module handles the user message extraction from chat store
 */

import {
  ChatStore,
  MESSAGE_ROLE,
} from "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs";
import { BlockListManager } from "chrome://global/content/ml/Utils.sys.mjs";
import { SensitiveInfoDetector } from "moz-src:///browser/components/aiwindow/models/memories/SensitiveInfoDetector.sys.mjs";

/** @typedef {import("./MemoriesConstants.sys.mjs").MemoriesGenerationRunStats} MemoriesGenerationRunStats */

// Chat fetch defaults
const DEFAULT_MAX_RESULTS = 50;
const DEFAULT_HALF_LIFE_DAYS = 7;
export const MESSAGE_LENGTH_THRESHOLD = 1000;
const MS_PER_SEC = 1_000;
const SEC_PER_MIN = 60;
const MINS_PER_HOUR = 60;
const HOURS_PER_DAY = 24;

let _mgr = BlockListManager.initializeFromDefault({ language: "en" });
let _sensitiveInfoDetector = new SensitiveInfoDetector();

/**
 * Fetch recent user chat messages from the ChatStore and compute a freshness
 * score for each one.
 *
 * Messages are fetched between `startTime` and "now" (Date.now()) and limited
 * to the most recent `maxResults` entries. A per-message `freshness_score`
 * in [0, 1] is computed using an exponential half-life decay over age in days.
 *
 * @param {number} [startTime=0]
 *        Inclusive start time in milliseconds since Unix epoch.
 * @param {number} [maxResults=DEFAULT_MAX_RESULTS]
 *        Maximum number of most recent messages to return.
 * @param {number} [halfLifeDays=DEFAULT_HALF_LIFE_DAYS]
 *        Half-life in days for the freshness decay function.
 * @param {MemoriesGenerationRunStats} [stats]
 *        Stats object to record telemetry information
 * @returns {Promise<Array<{
 *   createdDate: number,
 *   role: string,
 *   content: any,
 *   pageUrl: string | null,
 *   convId: string | null,
 *   freshness_score: number
 * }>>}
 *        Promise resolving to an array of mapped chat message objects.
 */
export async function getRecentChats(
  startTime = 0,
  maxResults = DEFAULT_MAX_RESULTS,
  halfLifeDays = DEFAULT_HALF_LIFE_DAYS,
  stats
) {
  // Underlying Chatstore uses Date type but MemoriesStore maintains in TS
  const startDate = new Date(startTime);
  const endDate = new Date();
  const messages = await ChatStore.findMessagesByDate(
    startDate,
    endDate,
    MESSAGE_ROLE.USER,
    maxResults
  );

  const filtered = messages.filter(msg => {
    const body = msg.content?.body;
    if (!body || typeof body !== "string") {
      return true;
    }
    if (
      _mgr.matchAtWordBoundary({ text: body.toLowerCase() }) ||
      _sensitiveInfoDetector.containsSensitiveInfo(body) ||
      _sensitiveInfoDetector.containsSensitiveKeywords(body)
    ) {
      return false;
    }
    return true;
  });

  if (stats) {
    stats.chat_messages_pulled = messages.length;
    stats.chat_messages_kept = filtered.length;
  }

  const chatMessages = filtered.map(msg => {
    const createdDate = msg.createdDate;
    const freshness_score = computeFreshnessScore(createdDate, halfLifeDays);
    let content = msg.content?.body ?? null;
    if (content && content.length > MESSAGE_LENGTH_THRESHOLD) {
      content = content.substring(0, MESSAGE_LENGTH_THRESHOLD);
    }
    return {
      createdDate,
      role: msg.role,
      content,
      pageUrl: msg.pageUrl,
      convId: msg.convId ?? null,
      freshness_score,
    };
  });

  return chatMessages;
}

/**
 * Compute a freshness score for a message based on its age, using an
 * exponential decay with a configurable half-life.
 *
 * The score is:
 *   -> 1.0 for messages with ageDays <= 0 (now or in the future)
 *   -> exp(-ln(2) * (ageDays / halfLifeDays)) for older messages,
 *   clamped into the [0, 1] range.
 *
 * @param {number|Date} createdDate
 *        Message creation time, either as a millisecond timestamp or a Date.
 * @param {number} [halfLifeDays=DEFAULT_HALF_LIFE_DAYS]
 *        Half-life in days; larger values decay more slowly.
 * @returns {number}
 *          Freshness score in the range [0, 1].
 */
export function computeFreshnessScore(
  createdDate,
  halfLifeDays = DEFAULT_HALF_LIFE_DAYS
) {
  const now = Date.now();
  const createdMs =
    typeof createdDate === "number" ? createdDate : createdDate.getTime();
  const ageMs = now - createdMs;
  const ageDays =
    ageMs / (MS_PER_SEC * SEC_PER_MIN * MINS_PER_HOUR * HOURS_PER_DAY);
  if (ageDays <= 0) {
    return 1;
  }
  const raw = Math.exp(-Math.LN2 * (ageDays / halfLifeDays));
  return Math.max(0, Math.min(1, raw));
}

export function _setBlockListManagerForTesting(mgr) {
  _mgr = mgr;
}

/**
 * Return historical chats by id
 *
 * @param {Array<string>} conversationIds - Array of conversation IDs to fetch.
 * @returns {Promise<Array<ChatConversation>>} Array of ChatConversation objects
 */
export async function getConversationsById(conversationIds) {
  const conversations = await Promise.all(
    conversationIds.map(id => ChatStore.findConversationById(id))
  );
  return conversations.filter(Boolean);
}

/**
 * Convenience function to extract conversation source IDs from a memory object.
 *
 * @param {object} memory
 * @returns {Array<string>} Array of conversation source IDs, or empty array if not present.
 */
export function getConversationSourceIdsFromMemory(memory) {
  return memory.source_ids?.conversation_source_ids ?? [];
}

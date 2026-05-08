/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  resolveChatModelChoice:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "modelChoice",
  "browser.smartwindow.firstrun.modelChoice",
  ""
);

/**
 * The current SQLite database schema version
 */
export const CURRENT_SCHEMA_VERSION = 7;

/**
 * The directory that the SQLite database lives in
 */
export const DB_FOLDER_PATH = PathUtils?.profileDir ?? "./";

/**
 * The name of the SQLite database file
 */
export const DB_FILE_NAME = "chat-store.sqlite";

/**
 * Preference branch for the Chat storage location
 */
export const PREF_BRANCH = "browser.smartwindow.chatHistory";

/**
 * Fallback model data - matches Remote Settings shape
 * Used when Remote Settings lookup fails
 */
export const FALLBACK_MODELS = {
  0: { model: "custom-model", ownerName: "" },
  1: {
    model: "gemini-2.5-flash-lite",
    ownerName: "Google",
  },
  2: {
    model: "qwen3-235b-a22b-instruct-2507-maas",
    ownerName: "Alibaba",
  },
  3: {
    model: "gpt-oss-120b",
    ownerName: "OpenAI",
  },
};

/**
 * Gets model metadata for a choice ID, with fallback
 *
 * @param {string} choiceId - Model choice ID (e.g., "1", "2", "3", "0")
 * @returns {Promise<{model: string, ownerName: string}|null>} null if choiceId is falsy
 */
export async function getModelForChoice(choiceId = lazy.modelChoice) {
  if (!choiceId) {
    return null;
  }

  const resolved = await lazy.resolveChatModelChoice(choiceId);
  if (resolved) {
    return resolved;
  }

  if (choiceId in FALLBACK_MODELS) {
    return FALLBACK_MODELS[choiceId];
  }

  return { model: "unknown", ownerName: "unknown" };
}

export function getCurrentModelName() {
  return FALLBACK_MODELS[lazy.modelChoice]?.model ?? "";
}

/**
 * Gets metadata for all models, with fallback
 *
 * @returns {Promise<{[key: string]: {model: string, ownerName: string}}>}
 */
export async function getAllModelsData() {
  const modelData = { ...FALLBACK_MODELS };
  // RS reads from a local dump. Only the first call sets up RS state,
  // subsequent calls are cached
  const entries = await Promise.all(
    ["1", "2", "3"].map(async id => [id, await getModelForChoice(id)])
  );
  for (const [id, data] of entries) {
    modelData[id] = data;
  }
  return modelData;
}

export {
  CONVERSATION_STATUS,
  MESSAGE_ROLE,
  MEMORIES_FLAG_SOURCE,
  SYSTEM_PROMPT_TYPE,
} from "./ChatEnums.sys.mjs";

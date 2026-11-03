/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/**
 * The current SQLite database schema version
 */
export const CURRENT_SCHEMA_VERSION = 13;

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

export {
  CONVERSATION_STATUS,
  MESSAGE_ROLE,
  MEMORIES_FLAG_SOURCE,
  SYSTEM_PROMPT_TYPE,
  TOOL_RESULT_TYPE,
} from "./ChatEnums.sys.mjs";

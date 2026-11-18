/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { migrations } from "moz-src:///browser/components/aiwindow/ui/modules/ConversationMigrations.sys.mjs";
import {
  CONVERSATION_TABLE,
  CONVERSATION_UPDATED_DATE_INDEX,
  CONVERSATION_UPSERT,
  GET_CONVERSATION_BY_ID,
  DELETE_CONVERSATION_BY_ID,
  MESSAGE_TABLE,
  MESSAGE_CONV_ID_INDEX,
  MESSAGE_INSERT,
  GET_MESSAGES_BY_CONV_ID,
  DELETE_REMOVED_MESSAGES,
} from "moz-src:///browser/components/aiwindow/ui/modules/ConversationSql.sys.mjs";
import { SQLiteStoreBase } from "moz-src:///browser/components/aiwindow/ui/modules/SQLiteStoreBase.sys.mjs";
import {
  parseJSONOrNull,
  toJSONOrNull,
} from "moz-src:///browser/components/aiwindow/ui/modules/ChatUtils.sys.mjs";
import { Conversation } from "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs";
import { Message } from "moz-src:///browser/components/aiwindow/models/Message.sys.mjs";

/**
 * The current SQLite database schema version
 */
export const CURRENT_SCHEMA_VERSION = 1;

/**
 * The name of the SQLite database file
 */
export const DB_FILE_NAME = "conversation-store.sqlite";

/**
 * Preference branch for the Conversation storage location
 */
export const PREF_BRANCH = "browser.smartwindow.conversationHistory";

/**
 * Store for base `Conversation` records. The store↔model relationship mirrors
 * ChatStore↔ChatConversation: this persists the generic conversation columns
 * owned by the base `Conversation` model. A `ChatConversation` (which extends
 * `Conversation`) can be persisted here too; only its base slice is stored.
 *
 * @augments {SQLiteStoreBase}
 */
class ConversationStore extends SQLiteStoreBase {
  get logPrefix() {
    return "ConversationStore";
  }

  get logLevelPref() {
    return "browser.smartwindow.conversationStore.logLevel";
  }

  get shutdownBlockerName() {
    return "ConversationStore: Shutdown";
  }

  get CURRENT_SCHEMA_VERSION() {
    return CURRENT_SCHEMA_VERSION;
  }

  get databaseFileName() {
    return DB_FILE_NAME;
  }

  get prefBranch() {
    return PREF_BRANCH;
  }

  get createEntityStatements() {
    return [
      CONVERSATION_TABLE,
      CONVERSATION_UPDATED_DATE_INDEX,
      MESSAGE_TABLE,
      MESSAGE_CONV_ID_INDEX,
    ];
  }

  get migrations() {
    return migrations;
  }

  /**
   * Inserts or updates a conversation and its messages. created_date is set on
   * insert only; other columns are refreshed on conflict. The conversation and
   * its messages are written in a single transaction.
   *
   * @param {Conversation} conversation
   */
  async updateConversation(conversation) {
    await this.ensureDatabase();

    await this.connection
      .executeTransaction(async () => {
        await this.connection.executeCached(CONVERSATION_UPSERT, {
          conv_id: conversation.id,
          created_date: conversation.createdDate,
          updated_date: conversation.updatedDate,
          feature: conversation.feature,
          security_properties: toJSONOrNull(conversation.securityProperties),
          seen_urls: JSON.stringify(Array.from(conversation.seenUrls ?? [])),
          serp_urls_for_anonymous_fetch: JSON.stringify(
            Array.from(conversation.serpUrlsForAnonymousFetch ?? [])
          ),
        });

        const messages = conversation.messages.map(m => ({
          message_id: m.id,
          conv_id: conversation.id,
          created_date: m.createdDate,
          ordinal: m.ordinal,
          role: m.role,
          content: toJSONOrNull(m.content),
          turn_index: m.turnIndex,
          parent_message_id: m.parentMessageId,
          model_id: m.modelId,
          params: toJSONOrNull(m.params),
          usage: toJSONOrNull(m.usage),
          tool_call_id: m.toolCallId,
          tool_name: m.toolName,
        }));

        // Drop rows for messages removed in memory (retry, truncate, clear) so
        // they don't linger and get resurrected on load, then upsert the ones
        // still present. Deleting only the removed rows avoids rewriting every
        // message on each save.
        await this.connection.executeCached(DELETE_REMOVED_MESSAGES, {
          conv_id: conversation.id,
          keep_ids: JSON.stringify(messages.map(m => m.message_id)),
        });

        if (messages.length) {
          await this.connection.executeCached(MESSAGE_INSERT, messages);
        }
      })
      .catch(e => {
        this.log.error("Could not update conversation", e.message, e.stack);
        throw e;
      });
  }

  /**
   * Gets a conversation by its id.
   *
   * @param {string} id
   * @returns {Promise<?Conversation>} The conversation, or null if none exists
   */
  async findConversationById(id) {
    await this.ensureDatabase();

    const rows = await this.connection.executeCached(GET_CONVERSATION_BY_ID, {
      conv_id: id,
    });
    if (!rows.length) {
      return null;
    }

    const conversation = this.#parseRow(rows[0]);
    const messageRows = await this.connection.executeCached(
      GET_MESSAGES_BY_CONV_ID,
      { conv_id: id }
    );
    conversation.messages = messageRows.map(row => this.#parseMessageRow(row));
    return conversation;
  }

  /**
   * Deletes a conversation by its id.
   *
   * @param {string} id
   */
  async deleteConversationById(id) {
    await this.ensureDatabase();

    await this.connection.execute(DELETE_CONVERSATION_BY_ID, { conv_id: id });
  }

  /**
   * Rehydrates a conversation row into a `Conversation`. The constructor wraps
   * the URL arrays in Sets and runs `SecurityProperties.fromJSON`, so the raw
   * parsed values can be passed straight through.
   *
   * @param {mozIStorageRow} row
   * @returns {Conversation}
   */
  #parseRow(row) {
    return new Conversation({
      id: row.getResultByName("conv_id"),
      createdDate: row.getResultByName("created_date"),
      updatedDate: row.getResultByName("updated_date"),
      feature: row.getResultByName("feature"),
      securityProperties: parseJSONOrNull(
        row.getResultByName("security_properties")
      ),
      seenUrls: parseJSONOrNull(row.getResultByName("seen_urls")) ?? [],
      serpUrlsForAnonymousFetch:
        parseJSONOrNull(row.getResultByName("serp_urls_for_anonymous_fetch")) ??
        [],
    });
  }

  /**
   * Rehydrates a message row into a base `Message`.
   *
   * @param {mozIStorageRow} row
   * @returns {Message}
   */
  #parseMessageRow(row) {
    return new Message({
      id: row.getResultByName("message_id"),
      createdDate: row.getResultByName("created_date"),
      ordinal: row.getResultByName("ordinal"),
      role: row.getResultByName("role"),
      content: parseJSONOrNull(row.getResultByName("content")),
      turnIndex: row.getResultByName("turn_index"),
      parentMessageId: row.getResultByName("parent_message_id"),
      modelId: row.getResultByName("model_id"),
      params: parseJSONOrNull(row.getResultByName("params")),
      usage: parseJSONOrNull(row.getResultByName("usage")),
      toolCallId: row.getResultByName("tool_call_id"),
      toolName: row.getResultByName("tool_name"),
    });
  }
}

const conversationStore = new ConversationStore();
export { conversationStore as ConversationStore };

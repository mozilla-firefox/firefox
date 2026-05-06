/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", function () {
  return console.createInstance({
    prefix: "ChatStore",
    maxLogLevelPref: "browser.smartwindow.chatStore.loglevel",
  });
});

import {
  CONVERSATION_TABLE,
  CONVERSATION_UPDATED_DATE_INDEX,
  CONVERSATION_INSERT,
  MESSAGE_TABLE,
  MESSAGE_ORDINAL_INDEX,
  MESSAGE_URL_INDEX,
  MESSAGE_CREATED_DATE_INDEX,
  MESSAGE_CONV_ID_INDEX,
  MESSAGE_INSERT,
  CONVERSATIONS_MOST_RECENT,
  CONVERSATION_BY_ID,
  CONVERSATIONS_BY_DATE,
  CONVERSATIONS_BY_URL,
  CONVERSATIONS_CONTENT_SEARCH,
  CONVERSATIONS_CONTENT_SEARCH_BY_ROLE,
  CONVERSATIONS_HISTORY_SEARCH,
  MESSAGES_BY_DATE,
  MESSAGES_BY_DATE_AND_ROLE,
  DELETE_CONVERSATION_BY_ID,
  DELETE_CONVERSATIONS_BY_DATE,
  DELETE_ALL_CONVERSATIONS,
  CONVERSATIONS_OLDEST,
  CONVERSATION_HISTORY,
  ESCAPE_CHAR,
  REMOVE_ALL_SITE_URLS_FROM_MESSAGES,
  REMOVE_SITE_URL_FROM_MESSAGES,
  getConversationMessagesSql,
  getDeleteMessagesByIdsSql,
  getDeleteEmptyConversationsSql,
  LLM_TELEMETRY_TABLE,
  GET_LLM_TELEMETRY_BY_CONV_ID,
  GET_LLM_TELEMETRY_DATA_BY_CONV_ID,
  UPSERT_LLM_TELEMETRY,
  MARK_LLM_TELEMETRY_UNPROCESSED,
  MARK_LLM_TELEMETRY_PROCESSED,
  GET_CONVERSATIONS_FOR_TELEMETRY
} from "./ChatSql.sys.mjs";

import { ChatMinimal } from "./ChatMessage.sys.mjs";

export { ChatConversation } from "./ChatConversation.sys.mjs";
export { ChatMessage, ChatMinimal } from "./ChatMessage.sys.mjs";
export {
  CONVERSATION_STATUS,
  MESSAGE_ROLE,
  MEMORIES_FLAG_SOURCE,
} from "./AIWindowConstants.sys.mjs";

import {
  CURRENT_SCHEMA_VERSION,
  DB_FOLDER_PATH,
  DB_FILE_NAME,
  PREF_BRANCH,
  CONVERSATION_STATUS,
} from "./AIWindowConstants.sys.mjs";

import {
  parseConversationRow,
  parseMessageRows,
  parseChatHistoryViewRows,
  toJSONOrNull,
} from "./ChatUtils.sys.mjs";

// NOTE: Reference to migrations file, migrations.mjs has an example
// migration function set up for a migration, and the eslint-disable-next-line
// should be removed once we create the first migration.
//
// eslint-disable-next-line no-unused-vars
import { migrations } from "./ChatMigrations.sys.mjs";

const MAX_DB_SIZE_BYTES = 75 * 1024 * 1024;
const SORTS = ["ASC", "DESC"];

/**
 * Simple interface to store and retrieve chat conversations and messages.
 *
 * @todo Bug 2005409
 * Move this documentation to Firefox source docs
 *
 * See: https://docs.google.com/document/d/1VlwmGbMhPIe-tmeKWinHuPh50VC9QrWEeQQ5V-UvEso/edit?tab=t.klqqibndv3zk
 *
 * @example
 * let { ChatStore, ChatConversation, ChatMessage, MESSAGE_ROLE } =
 *   ChromeUtils.importESModule("resource:///modules/aiwindow/ui/modules/ChatStore.sys.mjs");
 * const chatStore = ChatStore;
 * const conversation = new ChatConversation({
 *   title: "title",
 *   description: "description",
 *   pageUrl: new URL("https://mozilla.com/"),
 *   pageMeta: { one: 1, two: 2 },
 * });
 * const msg1 = new ChatMessage({
 *   ordinal: 0,
 *   role: MESSAGE_ROLE.USER,
 *   modelId: "test",
 *   params: { one: "one" },
 *   usage: { two: "two", content: "some content" },
 * });
 * const msg2 = new ChatMessage({
 *   ordinal: 1,
 *   role: MESSAGE_ROLE.ASSISTANT,
 *   modelId: "test",
 *   params: { one: "one" },
 *   usage: { two: "two", content: "some content 2" },
 * });
 * conversation.messages = [msg1, msg2];
 * await chatStore.updateConversation(conversation);
 * // Or findConversationsByDate, findConversationsByURL.
 * const foundConversation =
 *   await chatStore.findConversationById(conversation.id);
 *
 * @typedef {object} ChatStore
 *
 * @property {*} x ?
 */
class ChatStore {
  #asyncShutdownBlocker;
  #conn;
  #promiseConn;
  #lastRecordedSize;

  constructor() {
    this.#asyncShutdownBlocker = async () => {
      await this.#closeConnection();
    };
    this.#lastRecordedSize = null;
  }

  /**
   * Updates a conversation's saved state in the SQLite db
   *
   * @param {ChatConversation} conversation
   */
  async updateConversation(conversation) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const pageUrl = URL.parse(conversation.pageUrl);

    await this.#conn
      .executeTransaction(async () => {
        await this.#conn.executeCached(CONVERSATION_INSERT, {
          conv_id: conversation.id,
          title: conversation.title,
          description: conversation.description,
          page_url: pageUrl?.href ?? null,
          page_meta: toJSONOrNull(conversation.pageMeta),
          created_date: conversation.createdDate,
          updated_date: conversation.updatedDate,
          status: conversation.status,
          active_branch_tip_message_id: conversation.activeBranchTipMessageId,
          security_properties: toJSONOrNull(conversation.securityProperties),
          seen_urls: JSON.stringify(Array.from(conversation.seenUrls ?? [])),
          memories_toggled: conversation.memoriesToggled,
        });

        const messages = conversation.messages.map(m => ({
          message_id: m.id,
          conv_id: conversation.id,
          created_date: m.createdDate,
          parent_message_id: m.parentMessageId,
          revision_root_message_id: m.revisionRootMessageId,
          ordinal: m.ordinal,
          is_active_branch: m.isActiveBranch ? 1 : 0,
          role: m.role,
          model_id: m.modelId,
          params: toJSONOrNull(m.params),
          content: toJSONOrNull(m.content),
          usage: toJSONOrNull(m.usage),
          page_url: m.pageUrl?.href || "",
          turn_index: m.turnIndex,
          memories_enabled: m.memoriesEnabled,
          memories_flag_source: m.memoriesFlagSource,
          memories_applied_jsonb: toJSONOrNull(m.memoriesApplied),
          web_search_queries_jsonb: toJSONOrNull(m.webSearchQueries),
        }));
        await this.#conn.executeCached(MESSAGE_INSERT, messages);
      })
      .catch(e => {
        lazy.log.error("Transaction failed to execute", e.message, e.stack);
        throw e;
      });

    this.#recordDatabaseSize();
  }

  /**
   * Replaces all page_url entries with `null` for all messages .
   * Additionally, the `page_history_deleted` column will be set to `true`.
   *
   */
  async deleteAllUrlsFromMessages() {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn
      .executeCached(REMOVE_ALL_SITE_URLS_FROM_MESSAGES)
      .catch(e => {
        lazy.log.error(
          "Could not remove all site urls from messages",
          e.message,
          e.stack
        );

        throw e;
      });

    this.#recordDatabaseSize();
  }

  /**
   * Replaces all page_url values that match the specific page_url with a
   * `null` value. Additionally, the `page_history_deleted` column
   * will be set to `true`.
   *
   * @param {string} page_url - A URL to remove from messages
   */
  async deleteUrlFromMessages(page_url) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn
      .executeCached(REMOVE_SITE_URL_FROM_MESSAGES, { page_url })
      .catch(e => {
        lazy.log.error(
          `Could not remove 'page_url' entries for ${page_url}`,
          e.message,
          e.stack
        );

        throw e;
      });

    this.#recordDatabaseSize();
  }

  /**
   * Gets a list of oldest conversations
   *
   * @param {number} numberOfConversations - How many conversations to retrieve
   * @returns {Array<ChatMinimal>} - List of ChatMinimal items
   */
  async findOldestConversations(numberOfConversations) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const rows = await this.#conn
      .executeCached(CONVERSATIONS_OLDEST, {
        limit: numberOfConversations,
      })
      .catch(e => {
        lazy.log.error(
          "Could not retrieve oldest conversations.",
          e.message,
          e.stack
        );
        throw e;
      });

    return rows.map(row => {
      return new ChatMinimal({
        convId: row.getResultByName("conv_id"),
        title: row.getResultByName("title"),
      });
    });
  }

  /**
   * Gets a list of most recent conversations
   *
   * @param {number} numberOfConversations - How many conversations to retrieve
   * @returns {Array<ChatMinimal>} - List of ChatMinimal items
   */
  async findRecentConversations(numberOfConversations) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const rows = await this.#conn
      .executeCached(CONVERSATIONS_MOST_RECENT, {
        limit: numberOfConversations,
      })
      .catch(e => {
        lazy.log.error(
          "Could not retrieve most recent conversations.",
          e.message,
          e.stack
        );
        throw e;
      });

    return rows.map(row => {
      return new ChatMinimal({
        convId: row.getResultByName("conv_id"),
        title: row.getResultByName("title"),
      });
    });
  }

  /**
   * Gets a Conversation using it's id
   *
   * @param {string} conversationId - The ID of the conversation to retrieve
   *
   * @returns {ChatConversation} - The conversation and its messages
   */
  async findConversationById(conversationId) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const conversations = await this.#findConversationsWithMessages(
      CONVERSATION_BY_ID,
      {
        conv_id: conversationId,
      }
    ).catch(() => []);

    return conversations[0] ?? null;
  }

  /**
   * Finds conversations between a specified start and end date
   *
   * @param {number} startDate - Start time epoch format
   * @param {number} endDate - End time epoch format
   *
   * @returns {Array<ChatConversation>} - The conversations and their messages
   */
  async findConversationsByDate(startDate, endDate) {
    return this.#findConversationsWithMessages(CONVERSATIONS_BY_DATE, {
      start_date: startDate,
      end_date: endDate,
    });
  }

  /**
   * Finds conversations between a specified start and end date
   *
   * @param {URL} pageUrl - The URL to find conversations for
   *
   * @returns {Array<ChatConversation>} - The conversations and their messages
   */
  async findConversationsByURL(pageUrl) {
    return this.#findConversationsWithMessages(CONVERSATIONS_BY_URL, {
      page_url: pageUrl.href,
    });
  }

  /**
   * Search for messages that happened between the specified start
   * and end dates, optionally, filter the messages by a specific
   * message role type.
   *
   * @param {Date} startDate - The start date, inclusive
   * @param {Date} [endDate=new Date()] - The end date, inclusive
   * @param {MessageRole} [role=-1] - The message role type to filter by, one of 0|1|2|3
   * as defined by the constant MESSAGE_ROLE
   * @param {number} [limit=-1] - The max number of messages to retrieve
   * @param {number} [offset=-1] - The number or messages to skip from the result set
   *
   * @returns {Array<ChatMessage>} - An array of ChatMessage entries
   */
  async findMessagesByDate(
    startDate,
    endDate = new Date(),
    role = -1,
    limit = -1,
    offset = -1
  ) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const params = {
      start_date: startDate.getTime(),
      end_date: endDate.getTime(),
      limit,
      offset,
    };

    let sql = MESSAGES_BY_DATE;
    if (role > -1) {
      sql = MESSAGES_BY_DATE_AND_ROLE;
      params.role = role;
    }

    let rows = await this.#conn.executeCached(sql, params);

    return parseMessageRows(rows);
  }

  /**
   * Search for messages for the most recent messages, optionally,
   * filter the messages by a specific message role type.
   *
   * This is a wrapper around findMessagesByDate that defines the largest possible start and end date range.
   *
   * @param {MessageRole} [role=-1] - The message role type to filter by, one of 0|1|2|3
   * as defined by the constant MESSAGE_ROLE
   * @param {number} [limit=-1] - The max number of messages to retrieve
   *
   * @returns {Array<ChatMessage>} - An array of ChatMessage entries
   */
  async getMostRecentMessages(role = -1, limit = 5) {
    return this.findMessagesByDate(new Date(0), new Date(), role, limit, -1);
  }

  #escapeForLike(searchString) {
    return searchString
      .replaceAll(ESCAPE_CHAR, `${ESCAPE_CHAR}${ESCAPE_CHAR}`)
      .replaceAll("%", `${ESCAPE_CHAR}%`)
      .replaceAll("_", `${ESCAPE_CHAR}_`);
  }

  /**
   * Searches through the message.content JSON object to find a particular
   * object path that contains a partial string match of a value.
   *
   * @param {string} keyChain - The object key chain to look through,
   * like obj.field1.field2
   * @param {MessageRole} [role=-1] - A message role to search for
   *
   * @returns {Array<ChatConversation>} - An array of conversations with messages
   * that contain a message that matches the search string at the given content
   * object path
   */
  async searchContent(keyChain, role = -1) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const path = `$.${keyChain}`;

    const query =
      role > -1
        ? CONVERSATIONS_CONTENT_SEARCH_BY_ROLE
        : CONVERSATIONS_CONTENT_SEARCH;

    const params = { path };

    if (role > -1) {
      params.role = role;
    }

    const rows = await this.#conn.executeCached(query, params);

    if (!rows.length) {
      return [];
    }

    const conversations = rows.map(parseConversationRow);

    return await this.#getMessagesForConversations(conversations);
  }

  /**
   * Searches for conversations where the conversation title or a user/assistant
   * message contains a partial match of the search string in the
   * message.content.body field.
   *
   * @param {string} searchString - The string to search with for conversations
   * @param {boolean} [includeMessages=true] - Whether to fetch messages for each conversation
   *
   * @returns {Array<ChatConversation>} - An array of conversations with or without messages
   * that match the search string in title or message body. Each conversation may
   * have a `matchingSnippet` property containing the raw markdown text of the
   * first message body that matched the search string.
   */
  async search(searchString, includeMessages = true) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const path = `$.body`;
    const pattern = `%${this.#escapeForLike(searchString)}%`;

    const rows = await this.#conn.executeCached(CONVERSATIONS_HISTORY_SEARCH, {
      path,
      pattern,
    });

    if (!rows.length) {
      return [];
    }

    const conversations = rows.map(row => {
      const conv = parseConversationRow(row);
      conv.matchingSnippet = row.getResultByName("matching_snippet");
      return conv;
    });

    if (!includeMessages) {
      return conversations;
    }

    return await this.#getMessagesForConversations(conversations);
  }

  /**
   * Gets a list of chat history items to display in Chat History view.
   *
   * @param {number} [pageNumber=1] - The page number to get, 1 based indexing
   * @param {number} [pageSize=20] - Number of items to get per page
   * @param {string} [sort="desc"] - desc|asc The sorting order based on updated_date for conversations
   */
  async chatHistoryView(pageNumber = 1, pageSize = 20, sort = "desc") {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const sorting = SORTS.find(item => item === sort.toUpperCase()) ?? "DESC";
    const offset = pageSize * (pageNumber - 1);
    const limit = pageSize;
    const params = {
      limit,
      offset,
    };

    const rows = await this.#conn.executeCached(
      CONVERSATION_HISTORY.replace("{sort}", sorting),
      params
    );

    return parseChatHistoryViewRows(rows);
  }

  /**
   * Prunes the database of old conversations in order to get the
   * database file size to the specified maximum size.
   *
   * @todo Bug 2005411
   * Review the requirements for db pruning and set up invocation schedule, and refactor
   * to use dbstat
   *
   * @param {number} [reduceByPercentage=0.05] - Percentage to reduce db file size by
   * @param {number} [maxDbSizeBytes=MAX_DB_SIZE_BYTES] - Db max file size
   */
  async pruneDatabase(
    reduceByPercentage = 0.05,
    maxDbSizeBytes = MAX_DB_SIZE_BYTES
  ) {
    if (!IOUtils.exists(this.databaseFilePath)) {
      return;
    }

    const DELETE_BATCH_SIZE = 50;

    const getPragmaInt = async name => {
      const result = await this.#conn.execute(`PRAGMA ${name}`);
      return result[0].getInt32(0);
    };

    // compute the logical DB size in bytes using SQLite's page_size,
    // page_count, and freelist_count
    const getLogicalDbSizeBytes = async () => {
      const pageSize = await getPragmaInt("page_size");
      const pageCount = await getPragmaInt("page_count");
      const freelistCount = await getPragmaInt("freelist_count");

      // Logical used pages = total pages - free pages
      const usedPages = pageCount - freelistCount;
      const lSize = usedPages * pageSize;

      return lSize;
    };

    let logicalSize = await getLogicalDbSizeBytes();
    if (logicalSize < maxDbSizeBytes) {
      return;
    }

    const targetLogicalSize = Math.max(
      0,
      logicalSize * (1 - reduceByPercentage)
    );

    const MAX_ITERATIONS = 100;
    // how many "no file size change" batches we tolerate
    const MAX_STAGNANT = 5;
    let iterations = 0;
    let stagnantIterations = 0;

    while (
      logicalSize > targetLogicalSize &&
      iterations < MAX_ITERATIONS &&
      stagnantIterations < MAX_STAGNANT
    ) {
      iterations++;

      const recentChats = await this.findOldestConversations(DELETE_BATCH_SIZE);

      if (!recentChats.length) {
        break;
      }

      for (const chat of recentChats) {
        await this.deleteConversationById(chat.id);
      }

      const newLogicalSize = await getLogicalDbSizeBytes();
      if (newLogicalSize >= logicalSize) {
        stagnantIterations++;
      } else {
        stagnantIterations = 0;
      }

      logicalSize = newLogicalSize;
    }

    // Actually reclaim disk space.
    await this.#conn.execute("PRAGMA incremental_vacuum;");
  }

  /**
   * Deletes messages from a conversation
   *
   * @param {Array<ChatMessage>} messages
   */
  async deleteMessages(messages) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const chunkSize = 250;
    const chunks = [];
    for (let i = 0; i < messages.length; i += chunkSize) {
      chunks.push(messages.slice(i, i + chunkSize));
    }

    for (const chunk of chunks) {
      const conversations = Array.from(
        chunk.reduce((convs, message) => {
          convs.add(message.convId);

          return convs;
        }, new Set())
      );

      await this.#conn.executeTransaction(async () => {
        const deleteMessagesSql = getDeleteMessagesByIdsSql(chunk.length);
        await this.#conn.execute(
          deleteMessagesSql,
          chunk.map(m => m.id)
        );

        const deleteConvsSql = getDeleteEmptyConversationsSql(
          conversations.length
        );
        await this.#conn.execute(deleteConvsSql, conversations);
      });
    }

    this.#recordDatabaseSize();
  }

  /**
   * Returns the file size of the database.
   * Establishes a connection first to make sure the
   * database exists.
   *
   * @returns {number} - The file size in bytes
   */
  async getDatabaseSize() {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const stats = await IOUtils.stat(this.databaseFilePath);
    return stats.size;
  }

  /**
   * Deletes a particular conversation using it's id
   *
   * @param {string} id - The conv_id of a conversation row to delete
   */
  async deleteConversationById(id) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn.execute(DELETE_CONVERSATION_BY_ID, {
      conv_id: id,
    });

    this.#recordDatabaseSize();
  }

  /**
   * Deletes all conversations created within the given date range.
   * Messages are cascade-deleted via foreign key constraints.
   *
   * @param {Date} startDate - The start date, inclusive
   * @param {Date} endDate - The end date, inclusive
   */
  async deleteConversationsByDateRange(startDate, endDate) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn.execute(DELETE_CONVERSATIONS_BY_DATE, {
      start_date: startDate.getTime(),
      end_date: endDate.getTime(),
    });
  }

  /**
   * Deletes all conversations and their messages.
   */
  async deleteAllConversations() {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn.execute(DELETE_ALL_CONVERSATIONS);
  }

  /**
   * This method is meant to only be used for testing cleanup
   */
  async destroyDatabase() {
    await this.#removeDatabaseFiles();
    this.#promiseConn = null;
    this.#recordDatabaseSizeValue(0);
  }

  async #recordDatabaseSize() {
    let size = null;
    try {
      size = await this.getDatabaseSize();
    } catch (e) {
      lazy.log.error("Could not record database size", e.message, e.stack);
      return;
    }

    this.#recordDatabaseSizeValue(size);
  }

  #recordDatabaseSizeValue(size) {
    if (size === this.#lastRecordedSize) {
      return;
    }
    this.#lastRecordedSize = size;
    Glean.smartWindow.chatStorage.set(size);
  }

  /**
   * Gets the version of the schema currently set in the database.
   *
   * @returns {number}
   */
  async getDatabaseSchemaVersion() {
    if (!this.#conn) {
      await this.#ensureDatabase();
    }

    return this.#conn.getSchemaVersion();
  }

  async #getMessagesForConversations(conversations) {
    const convs = conversations.reduce((convMap, conv) => {
      convMap[conv.id] = conv;

      return convMap;
    }, {});

    // Find all the messages for all the conversations.
    const rows = await this.#conn
      .executeCached(
        getConversationMessagesSql(conversations.length),
        conversations.map(c => c.id)
      )
      .catch(e => {
        lazy.log.error(
          "Could not retrieve messages for conversatons",
          e.message,
          e.stack
        );

        return [];
      });

    // TODO: retrieve TTL content.

    parseMessageRows(rows).forEach(message => {
      const conversation = convs[message.convId];
      if (conversation) {
        conversation.messages.push(message);
      }
    });

    return conversations;
  }

  async #openConnection() {
    lazy.log.debug("Opening new connection");

    try {
      const confConfig = { path: this.databaseFilePath };
      this.#conn = await lazy.Sqlite.openConnection(confConfig);
    } catch (e) {
      lazy.log.error("openConnection() could not open db:", e.message, e.stack);
      throw e;
    }

    lazy.Sqlite.shutdown.addBlocker(
      "ChatStore: Shutdown",
      this.#asyncShutdownBlocker
    );

    try {
      // TODO: remove this after switching pruneDatabase() to use dbstat
      await this.#conn.execute("PRAGMA page_size = 4096;");
      // Setup WAL journaling, as it is generally faster.
      await this.#conn.execute("PRAGMA journal_mode = WAL;");
      await this.#conn.execute("PRAGMA wal_autocheckpoint = 16;");

      // Store VACUUM information to be used by the VacuumManager.
      await this.#conn.execute("PRAGMA auto_vacuum = INCREMENTAL;");
      await this.#conn.execute("PRAGMA foreign_keys = ON;");
    } catch (e) {
      lazy.log.warn("Configuring SQLite PRAGMA settings: ", e.message);
    }
  }

  async #closeConnection() {
    if (!this.#conn) {
      return;
    }

    lazy.log.debug("Closing connection");
    lazy.Sqlite.shutdown.removeBlocker(this.#asyncShutdownBlocker);
    try {
      await this.#conn.close();
    } catch (e) {
      lazy.log.warn(`Error closing connection: ${e.message}`);
    }
    this.#conn = null;
  }

  /**
   * @todo Bug 2005412
   * Discuss implications of multiple instances of ChatStore
   * and the potential issues with migrations/schemas.
   */
  async #ensureDatabase() {
    if (this.#promiseConn) {
      return this.#promiseConn;
    }

    let deferred = Promise.withResolvers();
    this.#promiseConn = deferred.promise;
    if (this.#removeDatabaseOnStartup) {
      lazy.log.debug("Removing database on startup");
      try {
        await this.#removeDatabaseFiles();
      } catch (e) {
        deferred.reject(new Error("Could not remove the database files"));
        return deferred.promise;
      }
    }

    try {
      await this.#openConnection();
    } catch (e) {
      if (
        e.result == Cr.NS_ERROR_FILE_CORRUPTED ||
        e.errors?.some(error => error.result == Ci.mozIStorageError.NOTADB)
      ) {
        lazy.log.warn("Invalid database detected, removing it.", e);
        await this.#removeDatabaseFiles();
      }
    }

    if (!this.#conn) {
      try {
        await this.#openConnection();
      } catch (e) {
        lazy.log.error(
          "Could not open the database connection.",
          e.message,
          e.stack
        );
        deferred.reject(new Error("Could not open the database connection"));
        return deferred.promise;
      }
    }

    try {
      await this.#initializeSchema();
    } catch (e) {
      lazy.log.warn(
        "Failed to initialize the database schema, recreating the database.",
        e
      );
      // If the schema cannot be initialized try to create a new database file.
      await this.#removeDatabaseFiles();
    }

    deferred.resolve(this.#conn);
    return this.#promiseConn;
  }

  async setSchemaVersion(version) {
    await this.#conn.setSchemaVersion(version);
  }

  async #initializeSchema() {
    const version = await this.getDatabaseSchemaVersion();

    if (version == this.CURRENT_SCHEMA_VERSION) {
      return;
    }

    if (version > this.CURRENT_SCHEMA_VERSION) {
      await this.setSchemaVersion(this.CURRENT_SCHEMA_VERSION);
      return;
    }

    // Must migrate the schema.
    await this.#conn.executeTransaction(async () => {
      if (version == 0) {
        // This is a newly created database, just create the entities.
        await this.#createDatabaseEntities();
        await this.#conn.setSchemaVersion(this.CURRENT_SCHEMA_VERSION);
        // eslint-disable-next-line no-useless-return
        return;
      }

      await this.applyMigrations(version);
      await this.setSchemaVersion(this.CURRENT_SCHEMA_VERSION);
    });
  }

  async applyMigrations(version) {
    for (const migration of migrations) {
      if (typeof migration !== "function") {
        continue;
      }

      await migration(this.#conn, version);
    }
  }

  async #removeDatabaseFiles() {
    lazy.log.debug("Removing database files");
    await this.#closeConnection();
    try {
      for (let file of [
        this.databaseFilePath,
        PathUtils.join(DB_FOLDER_PATH, this.databaseFileName + "-wal"),
        PathUtils.join(DB_FOLDER_PATH, this.databaseFileName + "-shm"),
      ]) {
        lazy.log.debug(`Removing ${file}`);
        await IOUtils.remove(file, {
          retryReadonly: true,
          recursive: true,
          ignoreAbsent: true,
        });
      }
      this.#removeDatabaseOnStartup = false;
    } catch (e) {
      lazy.log.warn("Failed to remove database files", e);
      // Try to clear on next startup.
      this.#removeDatabaseOnStartup = true;
      // Re-throw the exception for the caller.
      throw e;
    }
  }

  async #findConversationsWithMessages(sql, queryParams) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );

      return [];
    });

    let rows = await this.#conn.executeCached(sql, queryParams);

    const conversations = rows.map(parseConversationRow);

    return await this.#getMessagesForConversations(conversations);
  }

  /**
   * Marks a conversation's LLM telemetry as unprocessed.
   *
   * If the record does not exist, it will be created with an empty prompts object.
   * If it exists, only the processed flag is updated to 0.
   *
   * @param {string} conversationId - The ID of the conversation
   */
  async markLLMTelemetryUnprocessed(conversationId) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn
      .executeCached(MARK_LLM_TELEMETRY_UNPROCESSED, {
        conv_id: conversationId,
      })
      .catch(e => {
        lazy.log.error(
          `Could not mark LLM telemetry as unprocessed for ${conversationId}`,
          e.message,
          e.stack
        );
        throw e;
      });

    this.#recordDatabaseSize();
  }

  /**
   * Merges new LLM telemetry prompts/probabilities into the existing mappings and
   * marks the conversation as processed (optional).
   *
   * @param {string} conversationId - The ID of the conversation to update
   * @param {object} prompts - New telemetry prompt attributes to merge
   * @param {object} probabilities - New telemetry probability attributes to merge
   * @param {number} processed - Processed flag value, 0 for unprocessed and 1 for processed
   */
  async updateLLMTelemetryRecord(
    conversationId,
    prompts = {},
    probabilities = {},
    processed = 0
  ) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    await this.#conn
      .executeTransaction(async () => {
        const rows = await this.#conn.executeCached(
          GET_LLM_TELEMETRY_DATA_BY_CONV_ID,
          { conv_id: conversationId }
        );

        let existingPrompts = {};
        let existingProbabilities = {};

        if (rows.length) {
          try {
            existingPrompts = JSON.parse(
              rows[0].getResultByName("telemetry_prompts") || "{}"
            );
          } catch (e) {
            lazy.log.warn(
              `Could not parse LLM telemetry prompts for ${conversationId}`,
              e.message
            );
          }

          try {
            existingProbabilities = JSON.parse(
              rows[0].getResultByName("telemetry_probabilitiies") || "{}"
            );
          } catch (e) {
            lazy.log.warn(
              `Could not parse LLM telemetry probabilities for ${conversationId}`,
              e.message
            );
          }
        }

        const mergedPrompts = {
          ...existingPrompts,
          ...(prompts ?? {}),
        };

        const mergedProbabilities = {
          ...existingProbabilities,
          ...(probabilities ?? {}),
        };

        await this.#conn.executeCached(UPSERT_LLM_TELEMETRY, {
          conv_id: conversationId,
          telemetry_prompts: JSON.stringify(mergedPrompts),
          telemetry_probabilitiies: JSON.stringify(mergedProbabilities),
          processed_time: Date.now(),
          processed,
        });
      })
      .catch(e => {
        lazy.log.error(
          `Could not update LLM telemetry prompts for ${conversationId}`,
          e.message,
          e.stack
        );
        throw e;
      });

    this.#recordDatabaseSize();
  }

  /**
   * Gets LLM telemetry for a conversation. (Mainly for tests)
   *
   * @param {string} conversationId - The ID of the conversation
   * @returns {object|null} - LLM telemetry row, or null if none exists
   */
  async findLLMTelemetryByConversationId(conversationId) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error(
        "Could not ensure a database connection.",
        e.message,
        e.stack
      );
      throw e;
    });

    const rows = await this.#conn
      .executeCached(GET_LLM_TELEMETRY_BY_CONV_ID, {
        conv_id: conversationId,
      })
      .catch(e => {
        lazy.log.error(
          `Could not retrieve LLM telemetry for ${conversationId}`,
          e.message,
          e.stack
        );
        throw e;
      });

    if (!rows.length) {
      return null;
    }

    return {
      convId: rows[0].getResultByName("conv_id"),
      telemetryPrompts: JSON.parse(
        rows[0].getResultByName("telemetry_prompts") || "{}"
      ),
      telemetryProbabilities: JSON.parse(
        rows[0].getResultByName("telemetry_probabilitiies") || "{}"
      ),
      processedTime: rows[0].getResultByName("processed_time"),
      processed: rows[0].getResultByName("processed"),
    };
  }

  async markLLMTelemetryProcessed(convId, telemetryPrompts, turnIndex) {
    await this.#ensureDatabase().catch(e => {
      lazy.log.error("Could not ensure a database connection.", e.message, e.stack);
      throw e;
    });

    const updatedPrompts = Object.fromEntries(
      Object.keys(telemetryPrompts).map(name => [name, turnIndex])
    );

    await this.#conn
      .executeCached(MARK_LLM_TELEMETRY_PROCESSED, {
        conv_id: convId,
        processed_time: Date.now(),
        telemetry_prompts: JSON.stringify(updatedPrompts),
      })
      .catch(e => {
        lazy.log.error(`Could not mark telemetry processed for ${convId}`, e.message, e.stack);
        throw e;
      });
  }

  async getConversationsForTelemetry() {
  const rows = await this.#conn
    .executeCached(GET_CONVERSATIONS_FOR_TELEMETRY)
    .catch(e => {
      lazy.log.error("Failed to fetch conversations for telemetry", e.message, e.stack);
      throw e;
    });

  return rows.map(row => ({
    convId: row.getResultByName("conv_id"),
    telemetryJobs: JSON.parse(row.getResultByName("telemetryJobs") ?? "{}"),
    telemetryProbs: JSON.parse(row.getResultByName("telemetryProbs") ?? "{}"),
    modelId: row.getResultByName("model_id"),
    turnIndex: row.getResultByName("turn_index")
  }));
}


  async #createDatabaseEntities() {
    await this.#conn.execute(CONVERSATION_TABLE);
    await this.#conn.execute(CONVERSATION_UPDATED_DATE_INDEX);
    await this.#conn.execute(MESSAGE_TABLE);
    await this.#conn.execute(MESSAGE_ORDINAL_INDEX);
    await this.#conn.execute(MESSAGE_URL_INDEX);
    await this.#conn.execute(MESSAGE_CREATED_DATE_INDEX);
    await this.#conn.execute(MESSAGE_CONV_ID_INDEX);
    await this.#conn.execute(LLM_TELEMETRY_TABLE);
  }

  get #removeDatabaseOnStartup() {
    return Services.prefs.getBoolPref(
      `${PREF_BRANCH}.removeDatabaseOnStartup`,
      false
    );
  }

  set #removeDatabaseOnStartup(value) {
    lazy.log.debug(`Setting removeDatabaseOnStartup to ${value}`);
    Services.prefs.setBoolPref(`${PREF_BRANCH}.removeDatabaseOnStartup`, value);
  }

  static get CONVERSATION_STATUS() {
    return CONVERSATION_STATUS;
  }

  get CURRENT_SCHEMA_VERSION() {
    return CURRENT_SCHEMA_VERSION;
  }

  get connection() {
    return this.#conn;
  }

  get databaseFileName() {
    return DB_FILE_NAME;
  }

  get databaseFilePath() {
    return PathUtils.join(PathUtils.profileDir, this.databaseFileName);
  }
}

const chatStore = new ChatStore();
export { chatStore as ChatStore };

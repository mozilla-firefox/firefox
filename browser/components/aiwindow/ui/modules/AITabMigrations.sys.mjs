/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  ADD_PAGE_TYPE_COLUMN,
  ADD_TOOL_CONV_ID_COLUMN,
  AITAB_PAGES_SLUG_VERSION_INDEX,
} from "moz-src:///browser/components/aiwindow/ui/modules/AITabSql.sys.mjs";

// Each migration receives the schema version the database is currently on and
// returns without doing anything if it does not apply.
export const migrations = [
  /**
   * v2: idx_aitab_pages_slug_version became UNIQUE, so a slug can no longer be
   * claimed by more than one conversation. Databases created under v1 carry
   * the non-unique index and have to have it rebuilt.
   *
   * The store had no production writer while v1 was current, so no v1 database
   * can hold rows that would violate the new constraint.
   *
   * @param {object} connection - The open database connection.
   * @param {number} version - Schema version the database is migrating from.
   */
  async (connection, version) => {
    if (version >= 2) {
      return;
    }

    await connection.execute(
      "DROP INDEX IF EXISTS idx_aitab_pages_slug_version;"
    );
    await connection.execute(AITAB_PAGES_SLUG_VERSION_INDEX);
  },

  /**
   * v3: tool_conv_id records the conversation a page was composed in, which
   * until now was only reachable from the chat it was requested in — a
   * different id, in a different database file.
   *
   * Rows from v2 keep a NULL, which is a state the schema allows anyway (see
   * ADD_TOOL_CONV_ID_COLUMN), so there is nothing to backfill: a page whose
   * generation conversation is unknown cannot be modified in place and is
   * generated again instead.
   *
   * @param {object} connection - The open database connection.
   * @param {number} version - Schema version the database is migrating from.
   */
  async (connection, version) => {
    if (version >= 3) {
      return;
    }

    // A downgrade lowers the schema version but leaves the column in place, so
    // the upgrade after it replays this against a table that already has one.
    // SQLite has no ADD COLUMN IF NOT EXISTS, and the failure would roll back
    // the whole migration transaction, so the column is looked for first.
    const columns = await connection.execute("PRAGMA table_info(aitab_pages)");
    if (columns.some(row => row.getResultByName("name") == "tool_conv_id")) {
      return;
    }

    await connection.execute(ADD_TOOL_CONV_ID_COLUMN);
  },

  /**
   * v4: page_type records which feature owns a page (see AITAB_PAGE_TYPES).
   * Every page stored before it was a chat page, which is the column's
   * default, so there is nothing to backfill.
   *
   * @param {object} connection - The open database connection.
   * @param {number} version - Schema version the database is migrating from.
   */
  async (connection, version) => {
    if (version >= 4) {
      return;
    }

    // Looked for first for the same reason as tool_conv_id in v3: a
    // downgrade leaves the column behind.
    const columns = await connection.execute("PRAGMA table_info(aitab_pages)");
    if (columns.some(row => row.getResultByName("name") == "page_type")) {
      return;
    }

    await connection.execute(ADD_PAGE_TYPE_COLUMN);
  },
];

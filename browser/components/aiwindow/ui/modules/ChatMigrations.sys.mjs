import { MESSAGE_CONV_ID_INDEX } from "./ChatSql.sys.mjs";

/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/**
 * Please refer to sql.mjs for details on creating new migrations.
 *
 * - List each change here and what it's for.
 *
 * @param {OpenedConnection} conn - The SQLite connection to use for the migration
 * @param {number} version - The version number of the current schema
 */
async function applyV2(conn, version) {
  if (version < 2) {
    await conn.execute(MESSAGE_CONV_ID_INDEX);
  }
}

/**
 * Retrieve column names for a table in order to determine whether
 * a schema migration is necessary.
 *
 * Uses PRAGMA table_info to inspect the schema directly instead of
 * relying on a SELECT error, which helps distinguish missing
 * columns from unrelated database errors.
 *
 * @param {Connection} conn
 * @param {string} tableName
 */
async function getColumns(conn, tableName) {
  const columns = await conn.execute(`PRAGMA table_info(${tableName})`);
  return new Set(columns.map(c => c.name));
}

// Rename insights to memories
async function applyV3(conn, version) {
  if (version >= 3) {
    return;
  }

  const columns = await getColumns(conn, "message");
  if (columns.has("memories_enabled")) {
    return;
  }

  await conn.execute(
    "ALTER TABLE message RENAME COLUMN insights_enabled TO memories_enabled"
  );

  await conn.execute(
    "ALTER TABLE message RENAME COLUMN insights_flag_source TO memories_flag_source"
  );

  await conn.execute(
    "ALTER TABLE message RENAME COLUMN insights_applied_jsonb TO memories_applied_jsonb"
  );
}

// Add page_history_deleted to flag if the page_url value for the message
// has been removed due to a history delete type action
async function applyV4(conn, version) {
  if (version >= 4) {
    return;
  }

  const columns = await getColumns(conn, "message");
  if (columns.has("page_history_deleted")) {
    return;
  }

  await conn.execute(
    "ALTER TABLE message ADD COLUMN page_history_deleted BOOLEAN NOT NULL DEFAULT false"
  );
}

// Persist securityProperties flags to conversation table (Bug 2019693)
async function applyV5(conn, version) {
  if (version >= 5) {
    return;
  }

  const columns = await getColumns(conn, "conversation");
  if (columns.has("security_properties_jsonb")) {
    return;
  }

  await conn.execute(
    "ALTER TABLE conversation ADD COLUMN security_properties_jsonb BLOB"
  );
}

// Persist seen URLs to conversation table (Bug 2023001)
async function applyV6(conn, version) {
  if (version >= 6) {
    return;
  }

  const columns = await getColumns(conn, "conversation");
  if (columns.has("seen_urls_jsonb")) {
    return;
  }

  await conn.execute(
    "ALTER TABLE conversation ADD COLUMN seen_urls_jsonb BLOB"
  );
}

// Add memories_toggled to conversation table and populate from last user message
async function applyV7(conn, version) {
  if (version >= 7) {
    return;
  }

  const columns = await getColumns(conn, "conversation");
  if (columns.has("memories_toggled")) {
    return;
  }

  await conn.execute(
    "ALTER TABLE conversation ADD COLUMN memories_toggled BOOLEAN"
  );

  // Populate memories_toggled based on the last user message's memories_enabled flag
  // Uses a CTE to find the latest user message per conversation in one pass
  await conn.execute(`
    UPDATE conversation AS c
    SET memories_toggled = latest_user_msg_per_conv.memories_enabled
    FROM (
      SELECT conv_id, memories_enabled
      FROM (
        SELECT
          m.conv_id,
          m.memories_enabled,
          ROW_NUMBER() OVER (
            PARTITION BY m.conv_id
            ORDER BY m.ordinal DESC
          ) AS rn
        FROM message AS m
        WHERE m.role = 0
      )
      WHERE rn = 1
    ) AS latest_user_msg_per_conv
    WHERE latest_user_msg_per_conv.conv_id = c.conv_id;
  `);
}

// Create a new table for LLM telemetry
async function applyV8(conn, version) {
  if (version >= 8) {
    return;
  }

  await conn.execute(`
    CREATE TABLE llm_telemetry (
      conv_id TEXT PRIMARY KEY,
      telemetry_prompts BLOB,
      telemetry_probabilitiies BLOB,
      processed_time TIMESTAMP,
      processed INTEGER DEFAULT 0
    )
  `);
}

/**
 * Array of migration functions to run in the order they should be run in.
 *
 * @returns {Array<Function>}
 */
export const migrations = [
  applyV2,
  applyV3,
  applyV4,
  applyV5,
  applyV6,
  applyV7,
  applyV8,
];

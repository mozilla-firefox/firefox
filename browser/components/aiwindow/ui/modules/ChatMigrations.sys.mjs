import {
  MESSAGE_CONV_ID_INDEX,
  LLM_TELEMETRY_TABLE,
  TOOL_RESULT_TABLE,
  TOOL_RESULT_HISTORY_URL_INDEX,
  MESSAGE_ROLE_CREATED_DATE_INDEX,
  MESSAGE_PARENT_ID_INDEX,
  MESSAGE_REVISION_ROOT_INDEX,
  MESSAGE_ORDINAL_INDEX_DROP,
} from "./ChatSql.sys.mjs";

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
  return new Set(columns.map(c => c.getResultByName("name")));
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

// Add tool_ui_data_jsonb column to the message table
// so toolUIData rendered proper UI type from ADD_UI_TOOL
async function applyV8(conn, version) {
  if (version >= 8) {
    return;
  }

  const columns = await getColumns(conn, "message");
  if (columns.has("tool_ui_data_jsonb")) {
    return;
  }

  await conn.execute("ALTER TABLE message ADD COLUMN tool_ui_data_jsonb BLOB");
}

// Create a new table for LLM telemetry
async function applyV9(conn, version) {
  if (version >= 9) {
    return;
  }

  await conn.execute(LLM_TELEMETRY_TABLE);
}

// Persist serp URLs to conversation table
async function applyV10(conn, version) {
  if (version >= 10) {
    return;
  }

  const columns = await getColumns(conn, "conversation");
  if (columns.has("serp_urls_for_anonymous_fetch_jsonb")) {
    return;
  }

  await conn.execute(
    "ALTER TABLE conversation ADD COLUMN serp_urls_for_anonymous_fetch_jsonb BLOB"
  );
}

// Create the tool_result table to store tool-call result data and migrate
// tool_ui_data_jsonb into tool_result table.
async function applyV11(conn, version) {
  if (version >= 11) {
    return;
  }

  await conn.execute(TOOL_RESULT_TABLE);
  await conn.execute(TOOL_RESULT_HISTORY_URL_INDEX);

  // Backfill rows from tool_ui_data_jsonb
  const tool_ui_type = 0;
  await conn.execute(`
    INSERT INTO tool_result (message_id, type, ordinal, payload_jsonb)
    SELECT message_id, ${tool_ui_type}, 0, tool_ui_data_jsonb
    FROM message
    WHERE tool_ui_data_jsonb IS NOT NULL
    ON CONFLICT(message_id, type, ordinal) DO NOTHING
  `);
}

// Index the two self-referencing message foreign keys, which cascade on delete,
// and (role, created_date) for MESSAGES_BY_DATE_AND_ROLE. Drop the unused
// standalone ordinal index.
async function applyV12(conn, version) {
  if (version >= 12) {
    return;
  }

  await conn.execute(MESSAGE_PARENT_ID_INDEX);
  await conn.execute(MESSAGE_REVISION_ROOT_INDEX);
  await conn.execute(MESSAGE_ROLE_CREATED_DATE_INDEX);
  await conn.execute(MESSAGE_ORDINAL_INDEX_DROP);
}

// Persist the telemetry triggers that fired for a conversation
async function applyV13(conn, version) {
  if (version >= 13) {
    return;
  }

  const columns = await getColumns(conn, "llm_telemetry");
  if (columns.has("triggers")) {
    return;
  }

  await conn.execute("ALTER TABLE llm_telemetry ADD COLUMN triggers BLOB");
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
  applyV9,
  applyV10,
  applyV11,
  applyV12,
  applyV13,
];

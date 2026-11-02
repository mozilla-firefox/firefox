/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Every time the schema or the underlying data changes, you must bump up the
// schema version.

// Remember to:
// 1. Bump up the version number
// 2. Add a migration function to migrate the data to the new schema.
// 3. Update #createDatabaseEntities and #checkDatabaseHealth
// 4. Add a test to check that the migration works correctly.

// Note: migrations should be reasonably re-entry-friendly. If the user
// downgrades, the schema version is decreased, and upon a subsequent upgrade,
// the migration step is reapplied.
// This ensures that any necessary conversions are performed, even for entries
// added after the downgrade.
// In practice, schema changes should be additive, allowing newer versions to
// operate on older schemas, albeit with potentially reduced functionality.

export const ESCAPE_CHAR = "/";

export const CONVERSATION_TABLE = `
CREATE TABLE conversation (
  conv_id TEXT PRIMARY KEY,
  title TEXT,
  description TEXT,
  page_url TEXT,
  page_meta_jsonb BLOB,
  created_date INTEGER NOT NULL,
  updated_date INTEGER NOT NULL,
  status INTEGER NOT NULL DEFAULT 0,
  active_branch_tip_message_id TEXT, -- no foreign here, as we insert messages later.
  security_properties_jsonb BLOB,
  seen_urls_jsonb BLOB,
  memories_toggled BOOLEAN,
  serp_urls_for_anonymous_fetch_jsonb BLOB
) WITHOUT ROWID;
`;

export const CONVERSATION_UPDATED_DATE_INDEX = `
CREATE INDEX conversation_updated_date_idx ON conversation(updated_date);
`;

export const MESSAGE_TABLE = `
CREATE TABLE message (
  message_id TEXT PRIMARY KEY,
  conv_id TEXT NOT NULL REFERENCES conversation(conv_id) ON DELETE CASCADE,
  created_date INTEGER NOT NULL,
  parent_message_id TEXT REFERENCES message(message_id) ON DELETE CASCADE,
  revision_root_message_id TEXT REFERENCES message(message_id) ON DELETE CASCADE,
  ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
  is_active_branch INTEGER NOT NULL,
  role INTEGER NOT NULL,
  model_id TEXT,
  params_jsonb BLOB,
  content_jsonb BLOB,
  usage_jsonb BLOB,
  page_url TEXT,
  turn_index INTEGER,
  memories_enabled BOOLEAN,
  memories_flag_source INTEGER,
  memories_applied_jsonb BLOB,
  web_search_queries_jsonb BLOB,
  page_history_deleted BOOLEAN NOT NULL DEFAULT false,
  tool_ui_data_jsonb BLOB -- Deprecated in v11 schema; migrated to tool_result
) WITHOUT ROWID;
`;

// TODO Bug 2050716 - clean up history_results rows when cleaning URL references
// for 'Forget this site'
export const TOOL_RESULT_TABLE = `
CREATE TABLE IF NOT EXISTS tool_result (
  message_id TEXT NOT NULL REFERENCES message(message_id) ON DELETE CASCADE,
  type INTEGER NOT NULL,
  ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
  payload_jsonb BLOB,
  PRIMARY KEY (message_id, type, ordinal)
) WITHOUT ROWID;
`;

// Partial index over the scalar $.url path of history_results rows so a
// URL can be found without scanning the whole table.
// 1 - TOOL_RESULT_TYPE.HISTORY_RESULTS
export const TOOL_RESULT_HISTORY_URL_INDEX = `
CREATE INDEX IF NOT EXISTS tool_result_history_url_idx
  ON tool_result(json_extract(payload_jsonb, '$.url'))
  WHERE type = 1;
`;

// Dropped in v12: ordinal is only ever ordered by within a single conversation,
// which conv_id already narrows, so the planner never chose this index.
export const MESSAGE_ORDINAL_INDEX_DROP = `
DROP INDEX IF EXISTS message_ordinal_idx;
`;

// @todo Bug 2005423
// Maybe add hashed url column to optimize message_url_idx
export const MESSAGE_URL_INDEX = `
CREATE INDEX message_url_idx ON message(page_url);
`;

export const MESSAGE_CREATED_DATE_INDEX = `
CREATE INDEX message_created_date_idx ON message(created_date);
`;

export const MESSAGE_CONV_ID_INDEX = `
CREATE INDEX IF NOT EXISTS message_conv_id_idx ON message(conv_id);
`;

// Serves MESSAGES_BY_DATE_AND_ROLE: role is an equality match and created_date
// a range, so role must lead for the range to be a seek rather than a filter.
export const MESSAGE_ROLE_CREATED_DATE_INDEX = `
CREATE INDEX IF NOT EXISTS message_role_created_date_idx
  ON message(role, created_date);
`;

// parent_message_id and revision_root_message_id are ON DELETE CASCADE with no
// covering index, so SQLite scans all of `message` twice per cascaded row.
// Partial because neither column is ever read by a SELECT and both are NULL for
// many rows; SQLite still uses a partial index to enforce the foreign key.
export const MESSAGE_PARENT_ID_INDEX = `
CREATE INDEX IF NOT EXISTS message_parent_id_idx
  ON message(parent_message_id)
  WHERE parent_message_id IS NOT NULL;
`;

export const MESSAGE_REVISION_ROOT_INDEX = `
CREATE INDEX IF NOT EXISTS message_revision_root_idx
  ON message(revision_root_message_id)
  WHERE revision_root_message_id IS NOT NULL;
`;

export const CONVERSATION_INSERT = `
INSERT INTO conversation (
  conv_id, title, description, page_url, page_meta_jsonb,
  created_date, updated_date, status, active_branch_tip_message_id,
  security_properties_jsonb, seen_urls_jsonb, memories_toggled,
  serp_urls_for_anonymous_fetch_jsonb
) VALUES (
  :conv_id, :title, :description, :page_url, jsonb(:page_meta),
  :created_date, :updated_date, :status, :active_branch_tip_message_id,
  jsonb(:security_properties), jsonb(:seen_urls), :memories_toggled,
  jsonb(:serp_urls_for_anonymous_fetch)
)
ON CONFLICT(conv_id) DO UPDATE
  SET title = :title,
      updated_date = :updated_date,
      status = :status,
      active_branch_tip_message_id = :active_branch_tip_message_id,
      security_properties_jsonb = jsonb(:security_properties),
      seen_urls_jsonb = jsonb(:seen_urls),
      memories_toggled = :memories_toggled,
      serp_urls_for_anonymous_fetch_jsonb = jsonb(:serp_urls_for_anonymous_fetch);
`;

export const MESSAGE_INSERT = `
INSERT INTO message (
  message_id, conv_id, created_date, parent_message_id,
  revision_root_message_id, ordinal, is_active_branch, role,
  model_id, params_jsonb, content_jsonb, usage_jsonb, page_url, turn_index,
  memories_enabled, memories_flag_source, memories_applied_jsonb,
  web_search_queries_jsonb
) VALUES (
  :message_id, :conv_id, :created_date, :parent_message_id,
  :revision_root_message_id, :ordinal, :is_active_branch, :role,
  :model_id, jsonb(:params), jsonb(:content), jsonb(:usage), :page_url, :turn_index,
  :memories_enabled, :memories_flag_source, jsonb(:memories_applied_jsonb),
  jsonb(:web_search_queries_jsonb)
)
ON CONFLICT(message_id) DO UPDATE SET
  is_active_branch = :is_active_branch,
  memories_applied_jsonb = jsonb(:memories_applied_jsonb),
  content_jsonb = jsonb(:content),
  web_search_queries_jsonb = jsonb(:web_search_queries_jsonb);
`;

export const TOOL_RESULT_INSERT = `
INSERT INTO tool_result (message_id, type, ordinal, payload_jsonb)
VALUES (:message_id, :type, :ordinal, jsonb(:payload))
ON CONFLICT(message_id, type, ordinal) DO UPDATE SET
  payload_jsonb = jsonb(:payload);
`;

// Folds a message's tool_result rows into one JSON object keyed by type, each
// value an ordinal-ordered array of payloads. Correlates on the outer `message`
// table, so it works on any query using SELECT with `FROM message`.
export const TOOL_RESULTS_SUBQUERY = `(
  SELECT json_group_object(t.type, json(t.payloads))
  FROM (
    SELECT type, json_group_array(json(payload_jsonb) ORDER BY ordinal) AS payloads
    FROM tool_result
    WHERE tool_result.message_id = message.message_id
    GROUP BY type
  ) t
) AS tool_results`;

// page_url is the most recent real (non-empty, non-about:) page the chat was
// about, so the history menu can show that page's favicon; it falls back to the
// conversation's own page_url. substr avoids a LIKE clause, which mozStorage
// rejects unless the pattern is a bound parameter.
export const CONVERSATIONS_MOST_RECENT = `
SELECT c.conv_id, c.title,
  COALESCE(
    (SELECT m.page_url FROM message m
     WHERE m.conv_id = c.conv_id
       AND m.is_active_branch
       AND m.page_url != ''
       AND substr(m.page_url, 1, 6) != 'about:'
     ORDER BY m.created_date DESC
     LIMIT 1),
    c.page_url
  ) AS page_url
FROM conversation c
ORDER BY c.updated_date DESC
LIMIT :limit;
`;

export const CONVERSATIONS_OLDEST = `
SELECT conv_id, title
FROM conversation
ORDER BY updated_date ASC
LIMIT :limit;
`;

export const CONVERSATION_BY_ID = `
SELECT conv_id, title, description, page_url,
  json(page_meta_jsonb) AS page_meta, created_date, updated_date,
  status, active_branch_tip_message_id,
  json(security_properties_jsonb) AS security_properties,
  json(seen_urls_jsonb) AS seen_urls, memories_toggled,
  json(serp_urls_for_anonymous_fetch_jsonb) AS serp_urls_for_anonymous_fetch
FROM conversation WHERE conv_id = :conv_id;
`;

export const CONVERSATIONS_BY_DATE = `
SELECT conv_id, title, description, page_url,
  json(page_meta_jsonb) AS page_meta, created_date, updated_date,
  status, active_branch_tip_message_id,
  json(security_properties_jsonb) AS security_properties,
  json(seen_urls_jsonb) AS seen_urls, memories_toggled,
  json(serp_urls_for_anonymous_fetch_jsonb) AS serp_urls_for_anonymous_fetch
FROM conversation
WHERE updated_date >= :start_date AND updated_date <= :end_date
ORDER BY updated_date DESC;
`;

export const CONVERSATIONS_BY_URL = `
SELECT c.conv_id, c.title, c.description, c.page_url,
  json(c.page_meta_jsonb) AS page_meta, c.created_date, c.updated_date,
  c.status, c.active_branch_tip_message_id,
  json(c.security_properties_jsonb) AS security_properties,
  json(c.seen_urls_jsonb) AS seen_urls, c.memories_toggled,
  json(c.serp_urls_for_anonymous_fetch_jsonb) AS serp_urls_for_anonymous_fetch
FROM conversation c
WHERE EXISTS (
  SELECT 1
  FROM message m
  WHERE m.conv_id = c.conv_id
  AND m.page_url = :page_url
)
ORDER BY c.updated_date DESC;
`;

export const REMOVE_ALL_SITE_URLS_FROM_MESSAGES = `
UPDATE message
SET page_url = NULL,
    page_history_deleted = 1,
    content_jsonb = CASE
      WHEN json_type(content_jsonb, '$.contextMentions') = 'array'
      THEN jsonb_set(
        content_jsonb,
        '$.contextMentions',
        (
          SELECT jsonb_group_array(
            jsonb_set(value, '$.historyDeleted', jsonb('true'))
            ORDER BY key
          )
          FROM jsonb_each(content_jsonb, '$.contextMentions')
        )
      )
      ELSE content_jsonb
    END
`;

export const REMOVE_SITE_URL_FROM_MESSAGES = `
UPDATE message
SET
  page_url = CASE
    WHEN page_url = :page_url THEN NULL
    ELSE page_url
  END,

  page_history_deleted = CASE
    WHEN page_url = :page_url THEN 1
    ELSE page_history_deleted
  END,

  content_jsonb = CASE
    WHEN json_type(content_jsonb, '$.contextMentions') = 'array'
    THEN jsonb_set(
      content_jsonb,
      '$.contextMentions',
      (
        SELECT jsonb_group_array(
          CASE
            WHEN json_extract(value, '$.url') = :page_url
            THEN jsonb_set(value, '$.historyDeleted', jsonb('true'))
            ELSE value
          END
          ORDER BY key
        )
        FROM jsonb_each(content_jsonb, '$.contextMentions')
      )
    )
    ELSE content_jsonb
  END
WHERE page_url = :page_url
   OR EXISTS (
     SELECT 1
     FROM jsonb_each(content_jsonb, '$.contextMentions')
     WHERE json_extract(value, '$.url') = :page_url
   );
`;

/**
 * Get all messages for multiple conversations
 *
 * @param {number} amount - The number of conversation IDs to get messages for
 */
export function getConversationMessagesSql(amount) {
  return `
    SELECT
      message_id, created_date, parent_message_id, revision_root_message_id,
      ordinal, is_active_branch, role, model_id, conv_id,
      json(params_jsonb) AS params, json(usage_jsonb) AS usage,
      page_url, turn_index, memories_enabled, memories_flag_source,
      json(memories_applied_jsonb) AS memories_applied,
      json(web_search_queries_jsonb) AS web_search_queries,
      json(content_jsonb) AS content, page_history_deleted,
      ${TOOL_RESULTS_SUBQUERY}
      FROM message
      WHERE conv_id IN(${new Array(amount).fill("?").join(",")})
      ORDER BY ordinal ASC;
  `;
}

export function getDeleteMessagesByIdsSql(amount) {
  return `
    DELETE FROM message WHERE message.message_id IN(${new Array(amount).fill("?").join(",")})
  `;
}

export function getDeleteConversationsByIdsSql(amount) {
  return `
    DELETE FROM conversation
    WHERE conversation.conv_id IN(${new Array(amount).fill("?").join(",")})
  `;
}

export function getDeleteEmptyConversationsSql(amount) {
  return `
    DELETE FROM conversation
    WHERE conversation.conv_id IN(${new Array(amount).fill("?").join(",")})
      AND NOT EXISTS(
        SELECT 1
        FROM message m
        WHERE m.conv_id = conversation.conv_id
      )
  `;
}

export const CONVERSATIONS_CONTENT_SEARCH = `
SELECT c.conv_id, c.title, c.description, c.page_url,
  json(c.page_meta_jsonb) AS page_meta, c.created_date, c.updated_date,
  c.status, c.active_branch_tip_message_id,
  json(c.security_properties_jsonb) AS security_properties,
  json(c.seen_urls_jsonb) AS seen_urls, c.memories_toggled,
  json(c.serp_urls_for_anonymous_fetch_jsonb) AS serp_urls_for_anonymous_fetch
FROM conversation c
JOIN message m ON m.conv_id = c.conv_id
WHERE json_type(m.content_jsonb, :path) IS NOT NULL;
`;

export const CONVERSATIONS_CONTENT_SEARCH_BY_ROLE = `
SELECT c.conv_id, c.title, c.description, c.page_url,
  json(c.page_meta_jsonb) AS page_meta, c.created_date, c.updated_date,
  c.status, c.active_branch_tip_message_id,
  json(c.security_properties_jsonb) AS security_properties,
  json(c.seen_urls_jsonb) AS seen_urls, c.memories_toggled,
  json(c.serp_urls_for_anonymous_fetch_jsonb) AS serp_urls_for_anonymous_fetch
FROM conversation c
JOIN message m ON m.conv_id = c.conv_id
WHERE m.role = :role
  AND json_type(m.content_jsonb, :path) IS NOT NULL;
`;

export const CONVERSATIONS_HISTORY_SEARCH = `
SELECT
  c.conv_id,
  c.title,
  c.description,
  c.page_url,
  json(c.page_meta_jsonb) AS page_meta,
  c.created_date,
  c.updated_date,
  c.status,
  c.active_branch_tip_message_id,
  json(c.security_properties_jsonb) AS security_properties,
  json(c.seen_urls_jsonb) AS seen_urls,
  c.memories_toggled,
  json(c.serp_urls_for_anonymous_fetch_jsonb) AS serp_urls_for_anonymous_fetch,
  json_extract(m.content_jsonb, :path) AS matching_snippet
FROM conversation AS c
LEFT JOIN message AS m
  ON m.message_id = (
    SELECT mm.message_id
    FROM message AS mm
    WHERE mm.conv_id = c.conv_id
      AND mm.role IN (0,1) /* USER, ASSISTANT */
      AND json_extract(mm.content_jsonb, :path) LIKE :pattern ESCAPE '/'
    ORDER BY mm.created_date DESC
    LIMIT 1
  )
WHERE c.title LIKE :pattern ESCAPE '/'
   OR m.message_id IS NOT NULL;
`;

export const MESSAGES_BY_DATE = `
SELECT
  message_id, created_date, parent_message_id, revision_root_message_id,
  ordinal, is_active_branch, role, model_id, conv_id,
  json(params_jsonb) AS params, json(usage_jsonb) AS usage,
  page_url, turn_index, memories_enabled, memories_flag_source,
  json(memories_applied_jsonb) AS memories_applied,
  json(web_search_queries_jsonb) AS web_search_queries,
  json(content_jsonb) AS content, page_history_deleted,
  ${TOOL_RESULTS_SUBQUERY}
FROM message
WHERE created_date >= :start_date AND created_date <= :end_date
ORDER BY created_date DESC
LIMIT :limit OFFSET :offset;
`;

export const MESSAGES_BY_DATE_AND_ROLE = `
SELECT
  message_id, created_date, parent_message_id, revision_root_message_id,
  ordinal, is_active_branch, role, model_id, conv_id,
  json(params_jsonb) AS params, json(usage_jsonb) AS usage,
  page_url, turn_index, memories_enabled, memories_flag_source,
  json(memories_applied_jsonb) AS memories_applied,
  json(web_search_queries_jsonb) AS web_search_queries,
  json(content_jsonb) AS content, page_history_deleted,
  ${TOOL_RESULTS_SUBQUERY}
FROM message
WHERE role = :role
  AND created_date >= :start_date AND created_date <= :end_date
ORDER BY created_date DESC
LIMIT :limit OFFSET :offset;
`;

export const DELETE_CONVERSATIONS_BY_DATE = `
DELETE FROM conversation
WHERE created_date >= :start_date AND created_date <= :end_date;
`;

export const DELETE_ALL_CONVERSATIONS = `
DELETE FROM conversation;
`;

export const DELETE_CONVERSATION_BY_ID = `
DELETE FROM conversation WHERE conv_id = :conv_id;
`;

export const CONVERSATION_HISTORY = `
SELECT c.conv_id, c.title, c.created_date, c.updated_date, (
  SELECT json_group_array(t.page_url)
  FROM (
    SELECT
      m.page_url
    FROM message m
    WHERE m.conv_id = c.conv_id
      AND m.page_url IS NOT NULL
    GROUP BY m.page_url
    ORDER BY MAX(m.created_date) ASC
  ) AS t
) AS urls
FROM conversation c
WHERE EXISTS (
  SELECT 1
  FROM message AS m
  WHERE m.conv_id = c.conv_id
)
ORDER BY c.updated_date {sort}
LIMIT :limit OFFSET :offset;
`;

export const LLM_TELEMETRY_TABLE = `
CREATE TABLE IF NOT EXISTS llm_telemetry (
  conv_id TEXT PRIMARY KEY,
  telemetry_prompts BLOB,
  telemetry_probabilities BLOB,
  uniform_sampling_probability REAL DEFAULT 0.0,
  processed_time TIMESTAMP,
  processed INTEGER DEFAULT 0,
  triggers BLOB
)
`;

// The incoming values are merged into the stored ones by json_patch rather
// than by a read-modify-write in JS. Note the argument order differs between
// the columns: json_patch's second argument wins, so prompts take the
// incoming turn index while probabilities keep the value first recorded.
// Triggers are a list of names, merged as the union of stored and incoming.
// The columns are nullable and json_patch propagates NULL, so coalesce keeps
// a NULL from silently blanking the merged result.
export const UPSERT_LLM_TELEMETRY = `
INSERT INTO llm_telemetry (
  conv_id,
  telemetry_prompts,
  telemetry_probabilities,
  uniform_sampling_probability,
  processed_time,
  processed,
  triggers
)
VALUES (
  :conv_id,
  :telemetry_prompts,
  :telemetry_probabilities,
  :uniform_sampling_probability,
  :processed_time,
  :processed,
  :triggers
)
ON CONFLICT(conv_id) DO UPDATE SET
  telemetry_prompts = json_patch(
    coalesce(telemetry_prompts, '{}'),
    excluded.telemetry_prompts
  ),
  telemetry_probabilities = json_patch(
    excluded.telemetry_probabilities,
    coalesce(telemetry_probabilities, '{}')
  ),
  triggers = (
    SELECT json_group_array(value) FROM (
      SELECT value FROM json_each(coalesce(llm_telemetry.triggers, '[]'))
      UNION
      SELECT value FROM json_each(excluded.triggers)
    )
  ),
  processed_time = excluded.processed_time,
  processed = excluded.processed
`;

export const MARK_LLM_TELEMETRY_UNPROCESSED = `
  UPDATE llm_telemetry SET processed = 0 WHERE conv_id = :conv_id
`;

export const MARK_LLM_TELEMETRY_PROCESSED = `
UPDATE llm_telemetry
SET
  processed = 1,
  processed_time = :processed_time,
  telemetry_prompts = json_patch(
    coalesce(telemetry_prompts, '{}'),
    :telemetry_prompts
  )
WHERE conv_id = :conv_id
`;

export const GET_LLM_TELEMETRY_BY_CONV_ID = `
SELECT
  conv_id,
  telemetry_prompts,
  telemetry_probabilities,
  uniform_sampling_probability,
  processed_time,
  processed,
  triggers
FROM llm_telemetry
WHERE conv_id = :conv_id
`;

/**
 * Get uniform_sampling_probability for multiple conversations. Used on
 * conversation reload to rehydrate the in-memory telemetry sampling state
 * (_telemetryUniformSample / _telemetryUniformProbability).
 *
 * @param {number} amount - The number of conversation IDs to look up
 */
export function getUniformSamplingByConvIdsSql(amount) {
  return `
    SELECT conv_id, uniform_sampling_probability
    FROM llm_telemetry
    WHERE conv_id IN(${new Array(amount).fill("?").join(",")});
  `;
}

export const GET_CONVERSATIONS_FOR_TELEMETRY = `
SELECT
  m.conv_id,
  t.telemetry_prompts AS telemetryJobs,
  t.telemetry_probabilities AS telemetryProbs,
  t.uniform_sampling_probability,
  t.triggers,
  m.model_id,
  m.turn_index
FROM llm_telemetry t
JOIN (
  SELECT conv_id, MAX(created_date) AS last_message_time
  FROM message
  WHERE role = 1 -- assistant
  GROUP BY conv_id
) lm
  ON t.conv_id = lm.conv_id
JOIN message m
  ON m.conv_id = lm.conv_id
 AND m.created_date = lm.last_message_time
WHERE t.processed = 0
  AND lm.last_message_time < strftime('%s', 'now', '-5 hours') * 1000
ORDER BY lm.last_message_time ASC
LIMIT :limit;
`;

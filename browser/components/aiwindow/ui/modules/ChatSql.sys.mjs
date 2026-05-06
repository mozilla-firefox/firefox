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
  memories_toggled BOOLEAN
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
  page_history_deleted BOOLEAN NOT NULL DEFAULT false
) WITHOUT ROWID;
`;

export const MESSAGE_ORDINAL_INDEX = `
CREATE INDEX message_ordinal_idx ON message(ordinal);
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

export const CONVERSATION_INSERT = `
INSERT INTO conversation (
  conv_id, title, description, page_url, page_meta_jsonb,
  created_date, updated_date, status, active_branch_tip_message_id,
  security_properties_jsonb, seen_urls_jsonb, memories_toggled
) VALUES (
  :conv_id, :title, :description, :page_url, jsonb(:page_meta),
  :created_date, :updated_date, :status, :active_branch_tip_message_id,
  jsonb(:security_properties), jsonb(:seen_urls), :memories_toggled
)
ON CONFLICT(conv_id) DO UPDATE
  SET title = :title,
      updated_date = :updated_date,
      status = :status,
      active_branch_tip_message_id = :active_branch_tip_message_id,
      security_properties_jsonb = jsonb(:security_properties),
      seen_urls_jsonb = jsonb(:seen_urls),
      memories_toggled = :memories_toggled;
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

export const CONVERSATIONS_MOST_RECENT = `
SELECT conv_id, title
FROM conversation
ORDER BY updated_date DESC
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
  json(seen_urls_jsonb) AS seen_urls, memories_toggled
FROM conversation WHERE conv_id = :conv_id;
`;

export const CONVERSATIONS_BY_DATE = `
SELECT conv_id, title, description, page_url,
  json(page_meta_jsonb) AS page_meta, created_date, updated_date,
  status, active_branch_tip_message_id,
  json(security_properties_jsonb) AS security_properties,
  json(seen_urls_jsonb) AS seen_urls, memories_toggled
FROM conversation
WHERE updated_date >= :start_date AND updated_date <= :end_date
ORDER BY updated_date DESC;
`;

export const CONVERSATIONS_BY_URL = `
SELECT c.conv_id, c.title, c.description, c.page_url,
  json(c.page_meta_jsonb) AS page_meta, c.created_date, c.updated_date,
  c.status, c.active_branch_tip_message_id,
  json(c.security_properties_jsonb) AS security_properties,
  json(c.seen_urls_jsonb) AS seen_urls, c.memories_toggled
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
      json(content_jsonb) AS content, page_history_deleted
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
  json(c.seen_urls_jsonb) AS seen_urls, c.memories_toggled
FROM conversation c
JOIN message m ON m.conv_id = c.conv_id
WHERE json_type(m.content_jsonb, :path) IS NOT NULL;
`;

export const CONVERSATIONS_CONTENT_SEARCH_BY_ROLE = `
SELECT c.conv_id, c.title, c.description, c.page_url,
  json(c.page_meta_jsonb) AS page_meta, c.created_date, c.updated_date,
  c.status, c.active_branch_tip_message_id,
  json(c.security_properties_jsonb) AS security_properties,
  json(c.seen_urls_jsonb) AS seen_urls, c.memories_toggled
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
  json(content_jsonb) AS content, page_history_deleted
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
  json(content_jsonb) AS content, page_history_deleted
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
  SELECT group_concat(t.page_url)
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
CREATE TABLE llm_telemetry (
  conv_id TEXT PRIMARY KEY,
  telemetry_prompts BLOB,
  telemetry_probabilitiies BLOB,
  processed_time TIMESTAMP,
  processed INTEGER DEFAULT 0
)
`;

export const GET_LLM_TELEMETRY_DATA_BY_CONV_ID = `
SELECT
  telemetry_prompts,
  telemetry_probabilitiies
FROM llm_telemetry
WHERE conv_id = :conv_id
`;

export const UPSERT_LLM_TELEMETRY = `
INSERT INTO llm_telemetry (
  conv_id,
  telemetry_prompts,
  telemetry_probabilitiies,
  processed_time,
  processed
)
VALUES (
  :conv_id,
  :telemetry_prompts,
  :telemetry_probabilitiies,
  :processed_time,
  :processed
)
ON CONFLICT(conv_id) DO UPDATE SET
  telemetry_prompts = excluded.telemetry_prompts,
  telemetry_probabilitiies = excluded.telemetry_probabilitiies,
  processed_time = excluded.processed_time,
  processed = excluded.processed
`;

export const MARK_LLM_TELEMETRY_UNPROCESSED = `
  INSERT INTO llm_telemetry (
    conv_id,
    telemetry_prompts,
    telemetry_probabilitiies,
    processed_time,
    processed
  )
  VALUES (
    :conv_id,
    '{}',
    '{}',
    :processed_time,
    0
  )
  ON CONFLICT(conv_id) DO UPDATE SET
    processed = 0
`;

export const MARK_LLM_TELEMETRY_PROCESSED = `
UPDATE llm_telemetry
SET
  processed = 1,
  processed_time = :processed_time,
  telemetry_prompts = :telemetry_prompts
WHERE conv_id = :conv_id
`;

export const GET_LLM_TELEMETRY_BY_CONV_ID = `
SELECT
  conv_id,
  telemetry_prompts,
  telemetry_probabilitiies,
  processed_time,
  processed
FROM llm_telemetry
WHERE conv_id = :conv_id
`;

export const GET_CONVERSATIONS_FOR_TELEMETRY = `
SELECT
  m.conv_id,
  t.telemetry_prompts AS telemetryJobs,
  t.telemetry_probabilitiies AS telemetryProbs,
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
  AND lm.last_message_time < strftime('%s', 'now', '-5 hours') * 1000;
`
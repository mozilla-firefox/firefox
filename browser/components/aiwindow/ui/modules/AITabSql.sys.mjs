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

import { DEFAULT_AITAB_PAGE_TYPE } from "moz-src:///browser/components/aiwindow/ui/modules/AITabPageTypes.sys.mjs";

export const AITAB_PAGES_TABLE = `
CREATE TABLE aitab_pages (
  uuid TEXT PRIMARY KEY,
  conv_id TEXT NOT NULL,
  tool_conv_id TEXT,
  slug TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  title TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  context_jsonb BLOB,
  components_jsonb BLOB,
  localstate_jsonb BLOB,
  page_type INTEGER NOT NULL DEFAULT ${DEFAULT_AITAB_PAGE_TYPE}
) WITHOUT ROWID;
`;

export const AITAB_PAGE_INSERT = `
INSERT INTO aitab_pages (
  uuid, conv_id, tool_conv_id, slug, version, title, created_at, updated_at,
  context_jsonb, components_jsonb, localstate_jsonb, page_type
) VALUES (
  :uuid, :conv_id, :tool_conv_id, :slug, :version, :title, :created_at,
  :updated_at, jsonb(:context), jsonb(:components), jsonb(:localstate),
  :page_type
);
`;

// conv_id is NOT NULL and tool_conv_id is nullable because the two are not
// the same kind of reference: a page cannot exist outside the chat it was
// requested in, whereas the conversation that composed it is a resource that
// can be cleared independently (see DELETE_AITAB_PAGES_BY_SLUG) and is absent
// on rows written before the column existed.
export const ADD_TOOL_CONV_ID_COLUMN = `
ALTER TABLE aitab_pages ADD COLUMN tool_conv_id TEXT;
`;

// One of AITAB_PAGE_TYPES. The default is what every row stored before the
// column existed is backfilled with, and they were all chat pages.
export const ADD_PAGE_TYPE_COLUMN = `
ALTER TABLE aitab_pages ADD COLUMN page_type INTEGER NOT NULL DEFAULT ${DEFAULT_AITAB_PAGE_TYPE};
`;

// Compound index on (slug, version): serves both "latest version for a slug"
// (walk the index backwards, no separate sort) and "a specific slug + version"
// (direct seek). By the leftmost-prefix rule it also covers plain slug-only
// lookups, so no separate single-column slug index is needed.
//
// UNIQUE on the pair, not on the slug column: a tab keeps every version under
// one slug, so the column alone cannot be unique. Constraining the pair still
// stops a second conversation from claiming a slug another one already uses,
// which is what makes a slug safe to treat as a page identity.
export const AITAB_PAGES_SLUG_VERSION_INDEX = `
CREATE UNIQUE INDEX idx_aitab_pages_slug_version ON aitab_pages (slug, version);
`;

// Keyed on slug, to match the UNIQUE index the insert has to satisfy. On
// conv_id it would number versions per conversation while uniqueness was
// enforced per slug, so one conversation could only ever hold one page.
//
// page_type is a bare column next to MAX(), which SQLite reads from the row
// MAX() picked: the type of the latest version, NULL when there is none.
export const GET_NEXT_VERSION = `
SELECT COALESCE(MAX(version), 0) + 1 AS next_version, page_type
FROM aitab_pages
WHERE slug = :slug;
`;

// Whether any version already holds a slug, for minting a free one. Uses the
// (slug, version) index by its leftmost prefix, and EXISTS stops at the first
// row rather than counting them.
export const SLUG_EXISTS = `
SELECT EXISTS (SELECT 1 FROM aitab_pages WHERE slug = :slug) AS taken;
`;

const AITAB_PAGE_COLUMNS = `
  uuid, conv_id, tool_conv_id, slug, version, title, created_at, updated_at,
  page_type,
  json(context_jsonb) AS context,
  json(components_jsonb) AS components,
  json(localstate_jsonb) AS localstate
`;

// Latest version for a slug: the common "load the page" query, since a
// slug-only URL implies the newest version. Uses the (slug, version) index.
export const GET_AITAB_BY_SLUG = `
SELECT ${AITAB_PAGE_COLUMNS}
FROM aitab_pages
WHERE slug = :slug
ORDER BY version DESC
LIMIT 1;
`;

export const GET_AITAB_BY_SLUG_AND_VERSION = `
SELECT ${AITAB_PAGE_COLUMNS}
FROM aitab_pages
WHERE slug = :slug AND version = :version;
`;

// Available version numbers for a slug, newest first. Selects only `version`,
// so the (slug, version) index fully covers it (no table access).
export const GET_AITAB_VERSIONS_BY_SLUG = `
SELECT version
FROM aitab_pages
WHERE slug = :slug
ORDER BY version DESC;
`;

export const GET_AITAB_PAGES_BY_CONV_ID = `
SELECT ${AITAB_PAGE_COLUMNS}
FROM aitab_pages
WHERE conv_id = :conv_id
ORDER BY version ASC;
`;

// Every version of a slug below a given one, so a write can leave just the
// version it wrote. Uses the (slug, version) index, and cannot reach another
// tab's rows for the same reason DELETE_AITAB_PAGES_BY_SLUG cannot.
export const DELETE_AITAB_VERSIONS_BEFORE = `
DELETE FROM aitab_pages
WHERE slug = :slug AND version < :version;
`;

// Keyed on slug so it can use idx_aitab_pages_slug_version; conv_id has no
// index and would scan the table. UNIQUE on (slug, version) is what makes this
// safe: a slug cannot be claimed by a second conversation, so every row it
// matches belongs to the one tab being deleted.
//
// The conversation named by tool_conv_id lives in conversation-store.sqlite, a
// different database file, so no foreign key cascades into it: callers must
// delete it through ConversationStore as well.
export const DELETE_AITAB_PAGES_BY_SLUG = `
DELETE FROM aitab_pages
WHERE slug = :slug;
`;

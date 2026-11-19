/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

do_get_profile();

const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);
const { AITAB_PAGE_TYPES, DEFAULT_AITAB_PAGE_TYPE } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/AITabPageTypes.sys.mjs"
  );

const EXPECTED_COLUMNS = {
  uuid: "TEXT",
  conv_id: "TEXT",
  tool_conv_id: "TEXT",
  slug: "TEXT",
  version: "INTEGER",
  title: "TEXT",
  created_at: "INTEGER",
  updated_at: "INTEGER",
  context_jsonb: "BLOB",
  components_jsonb: "BLOB",
  localstate_jsonb: "BLOB",
  page_type: "INTEGER",
};

async function getTableColumns(conn, table) {
  const rows = await conn.execute(`PRAGMA table_info(${table})`);
  const columns = {};
  for (const row of rows) {
    columns[row.getResultByName("name")] = {
      type: row.getResultByName("type"),
      notNull: !!row.getResultByName("notnull"),
      pk: !!row.getResultByName("pk"),
    };
  }
  return columns;
}

registerCleanupFunction(async () => {
  await AITabStore.destroyDatabase();
});

add_task(async function test_database_initializes() {
  await AITabStore.ensureDatabase();
  Assert.ok(AITabStore.connection, "A database connection is opened");
});

add_task(async function test_schema_version() {
  const version = await AITabStore.getDatabaseSchemaVersion();
  Assert.equal(
    version,
    AITabStore.CURRENT_SCHEMA_VERSION,
    "Schema version matches the store's current version"
  );
  Assert.equal(version, 4, "Schema version is 4 after the page_type migration");
});

add_task(async function test_aitab_pages_columns() {
  const columns = await getTableColumns(AITabStore.connection, "aitab_pages");

  Assert.deepEqual(
    Object.keys(columns).sort(),
    Object.keys(EXPECTED_COLUMNS).sort(),
    "aitab_pages has exactly the expected columns"
  );

  for (const [name, type] of Object.entries(EXPECTED_COLUMNS)) {
    Assert.equal(columns[name].type, type, `Column ${name} has type ${type}`);
  }

  Assert.ok(columns.uuid.pk, "uuid is the primary key");
  Assert.ok(columns.conv_id.notNull, "conv_id is NOT NULL");
  Assert.ok(columns.slug.notNull, "slug is NOT NULL");
  Assert.ok(columns.title.notNull, "title is NOT NULL");
  Assert.ok(columns.page_type.notNull, "page_type is NOT NULL");

  // The chat a page belongs to is not optional; the conversation it was
  // composed in is unknown for every row written before the column existed.
  Assert.ok(
    !columns.tool_conv_id.notNull,
    "tool_conv_id is nullable, so a page with no known generation " +
      "conversation is still storable"
  );
});

/**
 * A downgrade lowers the schema version without undoing the schema, so the
 * next upgrade replays every migration over a database that already has the
 * changes. Adding a column is the step that does not tolerate that by itself.
 */
add_task(async function test_migrations_can_be_replayed() {
  await AITabStore.applyMigrations(1);

  const columns = await getTableColumns(AITabStore.connection, "aitab_pages");
  Assert.deepEqual(
    Object.keys(columns).sort(),
    Object.keys(EXPECTED_COLUMNS).sort(),
    "Replaying the migrations leaves the columns as they were"
  );
});

add_task(async function test_slug_version_index_is_unique() {
  const indexes = await AITabStore.connection.execute(
    "PRAGMA index_list(aitab_pages)"
  );
  const slugIndex = indexes.find(
    row => row.getResultByName("name") == "idx_aitab_pages_slug_version"
  );

  Assert.ok(slugIndex, "The (slug, version) index exists");
  Assert.equal(
    slugIndex.getResultByName("unique"),
    1,
    "It is UNIQUE, so two conversations cannot claim the same slug"
  );
});

function todaySlugDate() {
  const now = new Date();
  const month = String(now.getMonth() + 1).padStart(2, "0");
  const day = String(now.getDate()).padStart(2, "0");
  return `${now.getFullYear()}-${month}-${day}`;
}

add_task(async function test_a_slug_cannot_be_reused_by_another_tab() {
  const first = await AITabStore.create({
    convId: "conv-slug-owner",
    slug: "hotels-in-lisbon",
    title: "Hotels in Lisbon",
  });

  // Slugs come from page titles, which repeat, so a second conversation
  // asking for one that is taken is ordinary. It gets its own rather than
  // failing, and the slug still points at exactly one tab.
  const second = await AITabStore.create({
    convId: "conv-slug-squatter",
    slug: "hotels-in-lisbon",
    title: "Hotels in Lisbon",
  });

  const dated = `hotels-in-lisbon_${todaySlugDate()}`;
  Assert.equal(first.slug, "hotels-in-lisbon", "The first claim keeps it");
  Assert.equal(second.slug, dated, "The second gets today's date appended");
  Assert.equal(
    (await AITabStore.getBySlug("hotels-in-lisbon")).uuid,
    first.uuid,
    "The contested slug still resolves to the tab that claimed it"
  );
  Assert.equal(
    (await AITabStore.getBySlug(dated)).uuid,
    second.uuid,
    "And the second tab is reachable under the slug it was given"
  );
});

add_task(async function test_minted_slugs_keep_counting() {
  const dated = `weekend-in-porto_${todaySlugDate()}`;
  for (const slug of [
    "weekend-in-porto",
    dated,
    `${dated}_1`,
    `${dated}_2`,
    `${dated}_3`,
  ]) {
    const page = await AITabStore.create({
      convId: `conv-${slug}`,
      slug: "weekend-in-porto",
      title: "Weekend in Porto",
    });
    Assert.equal(page.slug, slug, `The next free slug is ${slug}`);
  }
});

add_task(async function test_create_inserts_first_version() {
  const created = await AITabStore.create({
    convId: "conv-create",
    toolConvId: "tool-conv-create",
    slug: "slug-create",
    title: "Created page",
    context: { source: "tabs" },
    components: [{ type: "header" }],
    localState: { checked: true },
  });

  Assert.equal(created.version, 1, "A new tab starts at version 1");
  Assert.ok(created.uuid, "A uuid is generated");

  const rows = await AITabStore.getAITabPagesByConvId("conv-create");
  Assert.equal(rows.length, 1, "One row persisted for the new tab");
  Assert.equal(rows[0].version, 1, "Persisted version is 1");
  Assert.equal(rows[0].slug, "slug-create", "slug persisted verbatim");
  Assert.equal(
    rows[0].toolConvId,
    "tool-conv-create",
    "the generation conversation is persisted alongside the chat's id"
  );
  Assert.equal(created.toolConvId, "tool-conv-create", "and is returned");
  Assert.equal(rows[0].title, "Created page", "title persisted");
  Assert.deepEqual(rows[0].context, { source: "tabs" }, "context round-trips");
  Assert.deepEqual(
    rows[0].components,
    [{ type: "header" }],
    "components round-trips"
  );
  Assert.deepEqual(
    rows[0].localState,
    { checked: true },
    "localState round-trips"
  );
});

add_task(async function test_tool_conv_id_defaults_to_null() {
  const created = await AITabStore.create({
    convId: "conv-no-tool",
    slug: "slug-no-tool",
    title: "No generation conversation",
  });

  Assert.equal(created.toolConvId, null, "The field defaults to null");
  Assert.equal(
    (await AITabStore.getBySlug("slug-no-tool")).toolConvId,
    null,
    "And reads back as null rather than undefined"
  );
});

add_task(async function test_edit_appends_new_version() {
  await AITabStore.create({
    convId: "conv-edit",
    slug: "slug-edit",
    title: "First",
  });

  const edited = await AITabStore.edit({
    convId: "conv-edit",
    slug: "slug-edit",
    title: "Second",
  });

  Assert.equal(edited.version, 2, "An edit appends version 2");

  const rows = await AITabStore.getAITabPagesByConvId("conv-edit");
  Assert.equal(rows.length, 2, "Both versions are retained");
  Assert.deepEqual(
    rows.map(r => r.version),
    [1, 2],
    "Versions are 1 then 2"
  );
  Assert.equal(rows[0].title, "First", "The original version is untouched");
  Assert.equal(rows[1].title, "Second", "The new version has the edited title");
  Assert.notEqual(rows[0].uuid, rows[1].uuid, "Each version is its own row");
});

add_task(async function test_versions_are_per_slug() {
  const a = await AITabStore.create({
    convId: "conv-a",
    slug: "slug-a",
    title: "A",
  });
  await AITabStore.edit({ convId: "conv-a", slug: "slug-a", title: "A2" });
  const b = await AITabStore.create({
    convId: "conv-b",
    slug: "slug-b",
    title: "B",
  });

  Assert.equal(a.version, 1, "slug-a starts at version 1");
  Assert.equal(b.version, 1, "slug-b starts at version 1 independently");
  Assert.deepEqual(
    await AITabStore.getVersionsBySlug("slug-a"),
    [2, 1],
    "and a revision to slug-a does not move slug-b's numbering"
  );
});

add_task(async function test_get_by_slug_and_version() {
  await AITabStore.create({ convId: "conv-sv", slug: "sv-slug", title: "V1" });
  await AITabStore.edit({ convId: "conv-sv", slug: "sv-slug", title: "V2" });

  const v1 = await AITabStore.getBySlugAndVersion("sv-slug", 1);
  const v2 = await AITabStore.getBySlugAndVersion("sv-slug", 2);
  const missing = await AITabStore.getBySlugAndVersion("sv-slug", 99);

  Assert.equal(v1.title, "V1", "version 1 fetched by slug + version");
  Assert.equal(v2.title, "V2", "version 2 fetched by slug + version");
  Assert.equal(missing, null, "A missing version returns null");
});

add_task(async function test_create_never_appends_to_an_existing_tab() {
  const first = await AITabStore.create({
    convId: "conv-guard",
    slug: "lisbon_food_guide",
    title: "First",
  });
  const again = await AITabStore.create({
    convId: "conv-guard",
    slug: "lisbon_food_guide",
    title: "Again",
  });

  Assert.equal(again.version, 1, "create() always writes a version 1");
  Assert.notEqual(again.slug, first.slug, "under a slug of its own");
  Assert.equal(
    (await AITabStore.getBySlug("lisbon_food_guide")).title,
    "First",
    "so the existing tab is not replaced by it"
  );
});

add_task(async function test_one_conversation_can_hold_several_tabs() {
  await AITabStore.create({
    convId: "conv-two-pages",
    slug: "first-page",
    title: "First",
  });

  const second = await AITabStore.create({
    convId: "conv-two-pages",
    slug: "second-page",
    title: "Second",
  });

  Assert.equal(second.version, 1, "The second tab starts its own versioning");
  Assert.equal(
    (await AITabStore.getBySlug("first-page")).title,
    "First",
    "The first tab is untouched"
  );
});

add_task(async function test_page_type_defaults_to_chat() {
  const created = await AITabStore.create({
    convId: "conv-default-type",
    slug: "slug-default-type",
    title: "No page type given",
  });

  Assert.equal(
    created.pageType,
    AITAB_PAGE_TYPES.CHAT,
    "A page created without a type is a chat page"
  );
  Assert.equal(
    (await AITabStore.getBySlug("slug-default-type")).pageType,
    AITAB_PAGE_TYPES.CHAT,
    "And reads back as one"
  );
});

add_task(async function test_create_stores_the_page_type() {
  const created = await AITabStore.create({
    convId: "conv-digest",
    slug: "slug-digest",
    title: "Digest",
    pageType: AITAB_PAGE_TYPES.DIGEST,
  });

  Assert.equal(created.pageType, AITAB_PAGE_TYPES.DIGEST, "It is returned");
  Assert.equal(
    (await AITabStore.getBySlug("slug-digest")).pageType,
    AITAB_PAGE_TYPES.DIGEST,
    "And persisted"
  );
});

add_task(async function test_create_rejects_unknown_page_type() {
  await Assert.rejects(
    AITabStore.create({
      convId: "conv-bad-type",
      slug: "slug-bad-type",
      title: "Unknown type",
      pageType: 999,
    }),
    /unknown page type/,
    "create() with a page type that is not registered is rejected"
  );
  Assert.equal(
    await AITabStore.getBySlug("slug-bad-type"),
    null,
    "And nothing is stored"
  );
});

add_task(async function test_edit_keeps_the_page_type() {
  await AITabStore.create({
    convId: "conv-keep-type",
    slug: "slug-keep-type",
    title: "Chat page",
  });

  const edited = await AITabStore.edit({
    convId: "conv-keep-type",
    slug: "slug-keep-type",
    title: "Chat page, revised",
    pageType: AITAB_PAGE_TYPES.DIGEST,
  });

  Assert.equal(
    edited.pageType,
    AITAB_PAGE_TYPES.CHAT,
    "An edit keeps the stored type rather than the one it was given"
  );
  Assert.equal(
    (await AITabStore.getBySlug("slug-keep-type")).pageType,
    AITAB_PAGE_TYPES.CHAT,
    "And so does the version it wrote"
  );
});

add_task(async function test_edit_rejects_page_types_that_cannot_be_edited() {
  for (const [name, pageType] of [
    ["research", AITAB_PAGE_TYPES.RESEARCH],
    ["digest", AITAB_PAGE_TYPES.DIGEST],
  ]) {
    const slug = `slug-locked-${name}`;
    await AITabStore.create({
      convId: `conv-locked-${name}`,
      slug,
      title: "Locked",
      pageType,
    });

    await Assert.rejects(
      AITabStore.edit({
        convId: `conv-locked-${name}`,
        slug,
        title: "Rewritten",
      }),
      /cannot be edited/,
      `edit() on a ${name} page is rejected`
    );
    Assert.deepEqual(
      await AITabStore.getVersionsBySlug(slug),
      [1],
      `And no new version of the ${name} page is written`
    );
  }
});

add_task(async function test_edit_rejects_unknown_tab() {
  await Assert.rejects(
    AITabStore.edit({
      convId: "conv-never-created",
      slug: "ghost",
      title: "Nope",
    }),
    /use create/,
    "edit() on an unknown tab is rejected"
  );
});

add_task(async function test_get_versions_by_slug() {
  await AITabStore.create({
    convId: "conv-hist",
    slug: "hist-slug",
    title: "V1",
  });
  await AITabStore.edit({
    convId: "conv-hist",
    slug: "hist-slug",
    title: "V2",
  });
  await AITabStore.edit({
    convId: "conv-hist",
    slug: "hist-slug",
    title: "V3",
  });

  const versions = await AITabStore.getVersionsBySlug("hist-slug");
  Assert.deepEqual(
    versions,
    [3, 2, 1],
    "All version numbers are returned, newest first"
  );

  const none = await AITabStore.getVersionsBySlug("no-such-slug");
  Assert.deepEqual(none, [], "An unknown slug returns an empty array");
});

add_task(async function test_get_by_slug_returns_latest() {
  await AITabStore.create({
    convId: "conv-latest",
    slug: "latest-slug",
    title: "V1",
  });
  await AITabStore.edit({
    convId: "conv-latest",
    slug: "latest-slug",
    title: "V2",
  });

  const page = await AITabStore.getBySlug("latest-slug");
  Assert.equal(page.version, 2, "getBySlug returns the latest version");
  Assert.equal(page.title, "V2", "The latest title is returned");

  const missing = await AITabStore.getBySlug("no-such-slug");
  Assert.equal(missing, null, "An unknown slug returns null");
});

add_task(async function test_delete_by_slug_removes_every_version() {
  await AITabStore.create({
    convId: "conv-delete",
    slug: "delete-slug",
    title: "V1",
  });
  await AITabStore.edit({
    convId: "conv-delete",
    slug: "delete-slug",
    title: "V2",
  });

  await AITabStore.deleteBySlug("delete-slug");

  Assert.deepEqual(
    await AITabStore.getAITabPagesByConvId("conv-delete"),
    [],
    "Every version of the tab is gone"
  );
  Assert.equal(
    await AITabStore.getBySlug("delete-slug"),
    null,
    "The slug no longer resolves, so the page cannot be resurrected"
  );
});

add_task(async function test_delete_by_slug_is_scoped_to_one_tab() {
  await AITabStore.create({
    convId: "conv-target",
    slug: "target-slug",
    title: "Delete me",
  });
  await AITabStore.create({
    convId: "conv-bystander",
    slug: "bystander-slug",
    title: "Keep me",
  });

  await AITabStore.deleteBySlug("target-slug");

  Assert.equal(
    await AITabStore.getBySlug("target-slug"),
    null,
    "The targeted tab is gone"
  );
  Assert.equal(
    (await AITabStore.getBySlug("bystander-slug"))?.title,
    "Keep me",
    "A tab belonging to another conversation is left alone"
  );
});

add_task(async function test_delete_versions_before_keeps_the_newest() {
  await AITabStore.create({
    convId: "conv-prune",
    slug: "prune-slug",
    title: "V1",
  });
  await AITabStore.edit({
    convId: "conv-prune",
    slug: "prune-slug",
    title: "V2",
  });
  const kept = await AITabStore.edit({
    convId: "conv-prune",
    slug: "prune-slug",
    title: "V3",
  });

  await AITabStore.deleteVersionsBefore("prune-slug", kept.version);

  Assert.deepEqual(
    await AITabStore.getVersionsBySlug("prune-slug"),
    [3],
    "Only the version that was kept is left"
  );
  Assert.equal(
    (await AITabStore.getBySlug("prune-slug")).title,
    "V3",
    "The page still loads by slug"
  );

  // The numbering is not reset by the prune, so a later write cannot collide
  // with a version that was already handed out.
  const next = await AITabStore.edit({
    convId: "conv-prune",
    slug: "prune-slug",
    title: "V4",
  });
  Assert.equal(
    next.version,
    4,
    "The next version continues above the kept one"
  );
});

add_task(async function test_delete_versions_before_spares_other_slugs() {
  await AITabStore.create({
    convId: "conv-spare",
    slug: "spare-slug",
    title: "V1",
  });
  const kept = await AITabStore.edit({
    convId: "conv-spare",
    slug: "spare-slug",
    title: "V2",
  });
  await AITabStore.create({
    convId: "conv-spare-other",
    slug: "spare-other-slug",
    title: "V1",
  });

  await AITabStore.deleteVersionsBefore("spare-slug", kept.version);

  Assert.deepEqual(
    await AITabStore.getVersionsBySlug("spare-other-slug"),
    [1],
    "Another tab's only version is not pruned by its version number"
  );
});

/**
 * Migrates a v2 database forward for real. Every other test here runs against
 * a database created from the current schema, which already has the column, so
 * nothing else executes the ALTER. Stands the v2 shape back up on the live
 * connection instead of shipping a fixture database, which is why it goes
 * last: dropping the column discards the values the tests above stored in it.
 */
add_task(async function test_page_type_is_added_to_a_v3_database() {
  await AITabStore.connection.execute(
    "ALTER TABLE aitab_pages DROP COLUMN page_type"
  );
  await AITabStore.connection.execute(
    `INSERT INTO aitab_pages (uuid, conv_id, slug, version, title, created_at, updated_at)
     VALUES ('uuid-v3', 'conv-v3', 'slug-v3', 1, 'Stored under v3', 0, 0)`
  );

  await AITabStore.applyMigrations(3);

  const columns = await getTableColumns(AITabStore.connection, "aitab_pages");
  Assert.ok(columns.page_type, "The column is added to a v3 database");
  Assert.equal(columns.page_type.type, "INTEGER", "It is added as INTEGER");
  Assert.ok(columns.page_type.notNull, "It is NOT NULL");
  Assert.equal(
    (await AITabStore.getBySlug("slug-v3")).pageType,
    DEFAULT_AITAB_PAGE_TYPE,
    "A page stored before the column existed reads back as the default type"
  );
});

add_task(async function test_tool_conv_id_is_added_to_a_v2_database() {
  await AITabStore.connection.execute(
    "ALTER TABLE aitab_pages DROP COLUMN tool_conv_id"
  );

  await AITabStore.applyMigrations(2);

  const columns = await getTableColumns(AITabStore.connection, "aitab_pages");
  Assert.ok(columns.tool_conv_id, "The column is added to a v2 database");
  Assert.equal(columns.tool_conv_id.type, "TEXT", "It is added as TEXT");
  Assert.ok(
    !columns.tool_conv_id.notNull,
    "It is nullable, so the rows already there stay valid without a backfill"
  );

  // The rows that predate the column read back with no generation
  // conversation rather than failing to parse, which is what lets a page
  // stored under v2 still load.
  const page = await AITabStore.create({
    convId: "conv-migrated",
    slug: "migrated-slug",
    title: "Stored after the migration",
  });
  Assert.equal(page.toolConvId, null, "A page still stores without one");
  Assert.equal(
    (await AITabStore.getBySlug("migrated-slug")).toolConvId,
    null,
    "And reads back as null"
  );
});

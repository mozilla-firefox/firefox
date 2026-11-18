/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function setup() {
  let path = await setupPlacesDatabase("places_v88.sqlite");
  let faviconsPath = await setupPlacesDatabase(
    "favicons_v41.sqlite",
    "favicons.sqlite"
  );

  let db = await Sqlite.openConnection({ path });
  Assert.equal(
    await db.getSchemaVersion(),
    88,
    "The fixture should be at schema version 88"
  );
  await db.close();

  db = await Sqlite.openConnection({ path: faviconsPath });
  let rows = await db.execute(
    "SELECT 1 FROM sqlite_master WHERE type = 'index' AND name = 'moz_icons_to_pages_iconindex'"
  );
  Assert.equal(rows.length, 0, "The index should not exist before migration");
  await db.close();
});

add_task(async function database_is_valid() {
  // Accessing the database for the first time triggers migration.
  Assert.equal(
    PlacesUtils.history.databaseStatus,
    PlacesUtils.history.DATABASE_STATUS_UPGRADED
  );

  const db = await PlacesUtils.promiseDBConnection();
  Assert.equal(await db.getSchemaVersion(), CURRENT_SCHEMA_VERSION);
});

add_task(async function moz_icons_to_pages_iconindex_created() {
  const db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(
    "SELECT name FROM favicons.sqlite_master WHERE type='index' AND name='moz_icons_to_pages_iconindex'"
  );
  Assert.equal(
    rows.length,
    1,
    "moz_icons_to_pages_iconindex should exist after migration"
  );
});

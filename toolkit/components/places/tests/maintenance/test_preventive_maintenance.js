/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test preventive maintenance
 * For every maintenance query create an uncoherent db and check that we take
 * correct fix steps, without polluting valid data.
 */

// ------------------------------------------------------------------------------
// Helpers

var defaultBookmarksMaxId = 0;
async function cleanDatabase() {
  // First clear any bookmarks the "proper way" to ensure caches like GuidHelper
  // are properly cleared.
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.withConnectionWrapper("cleanDatabase", async db => {
    await db.executeTransaction(async () => {
      await db.executeCached("DELETE FROM moz_places");
      await db.executeCached("DELETE FROM moz_origins");
      await db.executeCached("DELETE FROM moz_historyvisits");
      await db.executeCached("DELETE FROM moz_anno_attributes");
      await db.executeCached("DELETE FROM moz_annos");
      await db.executeCached("DELETE FROM moz_inputhistory");
      await db.executeCached("DELETE FROM moz_keywords");
      await db.executeCached("DELETE FROM moz_icons");
      await db.executeCached("DELETE FROM moz_pages_w_icons");
      await db.executeCached(
        "DELETE FROM moz_bookmarks WHERE id > " + defaultBookmarksMaxId
      );
      await db.executeCached("DELETE FROM moz_bookmarks_deleted");
      await db.executeCached("DELETE FROM moz_places_metadata_search_queries");
    });
  });
}

async function addPlace(
  aUrl,
  aFavicon,
  aGuid = PlacesUtils.history.makeGuid(),
  aHash = null
) {
  let href = new URL(
    aUrl || `http://www.mozilla.org/${encodeURIComponent(aGuid)}`
  ).href;
  let id;
  await PlacesUtils.withConnectionWrapper("cleanDatabase", async db => {
    await db.executeTransaction(async () => {
      id = (
        await db.executeCached(
          `INSERT INTO moz_places (url, url_hash, guid)
           VALUES (:url, IFNULL(:hash, hash(:url)), :guid)
           RETURNING id`,
          {
            url: href,
            hash: aHash,
            guid: aGuid,
          }
        )
      )[0].getResultByIndex(0);

      if (aFavicon) {
        await db.executeCached(
          `INSERT INTO moz_pages_w_icons (page_url, page_url_hash)
           VALUES (:url, IFNULL(:hash, hash(:url)))`,
          {
            url: href,
            hash: aHash,
          }
        );
        await db.executeCached(
          `INSERT INTO moz_icons_to_pages (page_id, icon_id)
           VALUES (
            (SELECT id FROM moz_pages_w_icons WHERE page_url_hash = IFNULL(:hash, hash(:url))),
            :favicon
           )`,
          {
            url: href,
            hash: aHash,
            favicon: aFavicon,
          }
        );
      }
    });
  });
  return id;
}

async function addBookmark(
  aPlaceId,
  aType,
  aParentGuid = PlacesUtils.bookmarks.unfiledGuid,
  aKeywordId,
  aTitle,
  aGuid = PlacesUtils.history.makeGuid(),
  aSyncStatus = PlacesUtils.bookmarks.SYNC_STATUS.NEW,
  aSyncChangeCounter = 0
) {
  return PlacesUtils.withConnectionWrapper("addBookmark", async db => {
    return (
      await db.executeCached(
        `INSERT INTO moz_bookmarks (fk, type, parent, keyword_id,
                                  title, guid, syncStatus, syncChangeCounter)
         VALUES (:place_id, :type,
                 (SELECT id FROM moz_bookmarks WHERE guid = :parent), :keyword_id,
                  :title, :guid, :sync_status, :change_counter)
         RETURNING id`,
        {
          place_id: aPlaceId || null,
          type: aType || null,
          parent: aParentGuid,
          keyword_id: aKeywordId || null,
          title: typeof aTitle == "string" ? aTitle : null,
          guid: aGuid,
          sync_status: aSyncStatus,
          change_counter: aSyncChangeCounter,
        }
      )
    )[0].getResultByIndex(0);
  });
}

// ------------------------------------------------------------------------------
// Tests

var tests = [];

// ------------------------------------------------------------------------------

tests.push({
  name: "A.1",
  desc: "Remove obsolete annotations from moz_annos",

  _obsoleteWeaveAttribute: "weave/test",
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid.
    this._placeId = await addPlace();
    // Add an obsolete attribute.
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeTransaction(async () => {
        db.executeCached(
          "INSERT INTO moz_anno_attributes (name) VALUES (:anno)",
          { anno: this._obsoleteWeaveAttribute }
        );

        db.executeCached(
          `INSERT INTO moz_annos (place_id, anno_attribute_id)
           VALUES (:place_id,
             (SELECT id FROM moz_anno_attributes WHERE name = :anno)
           )`,
          {
            place_id: this._placeId,
            anno: this._obsoleteWeaveAttribute,
          }
        );
      });
    });
  },

  async check() {
    // Check that the obsolete annotation has been removed.
    Assert.strictEqual(
      await PlacesTestUtils.getDatabaseValue("moz_anno_attributes", "id", {
        name: this._obsoleteWeaveAttribute,
      }),
      undefined
    );
  },
});

tests.push({
  name: "A.3",
  desc: "Remove unused attributes",

  _usedPageAttribute: "usedPage",
  _unusedAttribute: "unused",
  _placeId: null,
  _bookmarkId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // add a bookmark
    this._bookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK
    );
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeTransaction(async () => {
        // Add a used attribute and an unused one.
        await db.executeCached(
          `INSERT INTO moz_anno_attributes (name)
           VALUES (:anno1), (:anno2)`,
          {
            anno1: this._usedPageAttribute,
            anno2: this._unusedAttribute,
          }
        );
        await db.executeCached(
          `INSERT INTO moz_annos (place_id, anno_attribute_id)
           VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))`,
          {
            place_id: this._placeId,
            anno: this._usedPageAttribute,
          }
        );
      });
    });
  },

  async check() {
    // Check that used attributes are still there
    let value = await PlacesTestUtils.getDatabaseValue(
      "moz_anno_attributes",
      "id",
      {
        name: this._usedPageAttribute,
      }
    );
    Assert.notStrictEqual(value, undefined);
    // Check that unused attribute has been removed
    value = await PlacesTestUtils.getDatabaseValue(
      "moz_anno_attributes",
      "id",
      {
        name: this._unusedAttribute,
      }
    );
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "B.1",
  desc: "Remove annotations with an invalid attribute",

  _usedPageAttribute: "usedPage",
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeTransaction(async () => {
        // Add a used attribute.
        await db.executeCached(
          "INSERT INTO moz_anno_attributes (name) VALUES (:anno)",
          { anno: this._usedPageAttribute }
        );
        await db.executeCached(
          `INSERT INTO moz_annos (place_id, anno_attribute_id)
           VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))`,
          {
            place_id: this._placeId,
            anno: this._usedPageAttribute,
          }
        );
        // Add an annotation with a nonexistent attribute
        await db.executeCached(
          `INSERT INTO moz_annos (place_id, anno_attribute_id)
           VALUES(:place_id, 1337)`,
          { place_id: this._placeId }
        );
      });
    });
  },

  async check() {
    // Check that used attribute is still there
    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(
      "SELECT id FROM moz_anno_attributes WHERE name = :anno",
      { anno: this._usedPageAttribute }
    );
    Assert.equal(rows.length, 1);
    // check that annotation with valid attribute is still there
    rows = await db.executeCached(
      `SELECT id FROM moz_annos
       WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes WHERE name = :anno)`,
      { anno: this._usedPageAttribute }
    );
    Assert.equal(rows.length, 1);
    // Check that annotation with bogus attribute has been removed
    let value = await PlacesTestUtils.getDatabaseValue("moz_annos", "id", {
      anno_attribute_id: 1337,
    });
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "B.2",
  desc: "Remove orphan page annotations",

  _usedPageAttribute: "usedPage",
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeTransaction(async () => {
        // Add a used attribute.
        await db.executeCached(
          "INSERT INTO moz_anno_attributes (name) VALUES (:anno)",
          { anno: this._usedPageAttribute }
        );
        await db.executeCached(
          `INSERT INTO moz_annos (place_id, anno_attribute_id)
           VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))`,
          { place_id: this._placeId, anno: this._usedPageAttribute }
        );
        // Add an annotation to a nonexistent page
        await db.executeCached(
          `INSERT INTO moz_annos (place_id, anno_attribute_id)
          VALUES(:place_id, (SELECT id FROM moz_anno_attributes WHERE name = :anno))`,
          { place_id: 1337, anno: this._usedPageAttribute }
        );
      });
    });
  },

  async check() {
    // Check that used attribute is still there
    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(
      "SELECT id FROM moz_anno_attributes WHERE name = :anno",
      { anno: this._usedPageAttribute }
    );
    Assert.equal(rows.length, 1);
    // check that annotation with valid attribute is still there
    rows = await db.executeCached(
      `SELECT id FROM moz_annos
       WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes WHERE name = :anno)`,
      { anno: this._usedPageAttribute }
    );
    Assert.equal(rows.length, 1);
    // Check that an annotation to a nonexistent page has been removed
    let value = await PlacesTestUtils.getDatabaseValue("moz_annos", "id", {
      place_id: 1337,
    });
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.9",
  desc: "Remove items without a valid place",

  _validItemId: null,
  _invalidItemId: null,
  _invalidSyncedItemId: null,
  placeId: null,

  _changeCounterStmt: null,
  _menuChangeCounter: -1,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this.placeId = await addPlace();
    // Insert a valid bookmark
    this._validItemId = await addBookmark(
      this.placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK
    );
    // Insert a bookmark with an invalid place
    this._invalidItemId = await addBookmark(
      1337,
      PlacesUtils.bookmarks.TYPE_BOOKMARK
    );
    // Insert a synced bookmark with an invalid place. We should write a
    // tombstone when we remove it.
    this._invalidSyncedItemId = await addBookmark(
      1337,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      PlacesUtils.bookmarks.menuGuid,
      null,
      null,
      "bookmarkAAAA",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );
    // Insert a folder referencing a nonexistent place ID. D.5 should convert
    // it to a bookmark; D.9 should remove it.
    this._invalidWrongTypeItemId = await addBookmark(
      1337,
      PlacesUtils.bookmarks.TYPE_FOLDER
    );

    let value = await PlacesTestUtils.getDatabaseValue(
      "moz_bookmarks",
      "syncChangeCounter",
      {
        guid: PlacesUtils.bookmarks.menuGuid,
      }
    );
    Assert.equal(value, 0);
    this._menuChangeCounter = value;
  },

  async check() {
    // Check that valid bookmark is still there
    let value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "id", {
      id: this._validItemId,
    });
    Assert.notStrictEqual(value, undefined);
    // Check that invalid bookmarks have been removed
    for (let id of [
      this._invalidItemId,
      this._invalidSyncedItemId,
      this._invalidWrongTypeItemId,
    ]) {
      value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "id", {
        id,
      });
      Assert.strictEqual(value, undefined);
    }

    value = await PlacesTestUtils.getDatabaseValue(
      "moz_bookmarks",
      "syncChangeCounter",
      { guid: PlacesUtils.bookmarks.menuGuid }
    );
    Assert.equal(value, 1);
    Assert.equal(value, this._menuChangeCounter + 1);

    let tombstones = await PlacesTestUtils.fetchSyncTombstones();
    Assert.deepEqual(
      tombstones.map(info => info.guid),
      ["bookmarkAAAA"]
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.1",
  desc: "Remove items that are not uri bookmarks from tag containers",

  _tagId: null,
  _bookmarkId: null,
  _separatorId: null,
  _folderId: null,
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Create a tag
    this._tagId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.tagsGuid
    );
    let tagGuid = await PlacesTestUtils.promiseItemGuid(this._tagId);
    // Insert a bookmark in the tag
    this._bookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      tagGuid
    );
    // Insert a separator in the tag
    this._separatorId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_SEPARATOR,
      tagGuid
    );
    // Insert a folder in the tag
    this._folderId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      tagGuid
    );
  },

  async check() {
    // Check that valid bookmark is still there
    let value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "id", {
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      parent: this._tagId,
    });
    Assert.notStrictEqual(value, undefined);
    // Check that separator is no more there
    value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "id", {
      type: PlacesUtils.bookmarks.TYPE_SEPARATOR,
      parent: this._tagId,
    });
    Assert.equal(value, undefined);
    // Check that folder is no more there
    value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "id", {
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      parent: this._tagId,
    });
    Assert.equal(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.2",
  desc: "Remove empty tags",

  _tagId: null,
  _bookmarkId: null,
  _emptyTagId: null,
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Create a tag
    this._tagId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.tagsGuid
    );
    let tagGuid = await PlacesTestUtils.promiseItemGuid(this._tagId);
    // Insert a bookmark in the tag
    this._bookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      tagGuid
    );
    // Create another tag (empty)
    this._emptyTagId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.tagsGuid
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    // Check that valid bookmark is still there
    let value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "id", {
      id: this._bookmarkId,
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      parent: this._tagId,
    });
    Assert.notStrictEqual(value, undefined);
    let rows = await db.executeCached(
      `SELECT b.id FROM moz_bookmarks b
      JOIN moz_bookmarks p ON p.id = b.parent
      WHERE b.id = :id AND b.type = :type AND p.guid = :parent`,
      {
        id: this._tagId,
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        parent: PlacesUtils.bookmarks.tagsGuid,
      }
    );
    Assert.equal(rows.length, 1);
    rows = await db.executeCached(
      `SELECT b.id FROM moz_bookmarks b
      JOIN moz_bookmarks p ON p.id = b.parent
      WHERE b.id = :id AND b.type = :type AND p.guid = :parent`,
      {
        id: this._emptyTagId,
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        parent: PlacesUtils.bookmarks.tagsGuid,
      }
    );
    Assert.equal(rows.length, 0);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.3",
  desc: "Move orphan items to unsorted folder",

  _orphanBookmarkId: null,
  _orphanSeparatorId: null,
  _orphanFolderId: null,
  _bookmarkId: null,
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Insert an orphan bookmark
    this._orphanBookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      8888
    );
    // Insert an orphan separator
    this._orphanSeparatorId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_SEPARATOR,
      8888
    );
    // Insert a orphan folder
    this._orphanFolderId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      8888
    );
    let folderGuid = await PlacesTestUtils.promiseItemGuid(
      this._orphanFolderId
    );
    // Create a child of the last created folder
    this._bookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      folderGuid
    );
  },

  async check() {
    // Check that bookmarks are now children of a real folder (unfiled)
    let expectedInfos = [
      {
        id: this._orphanBookmarkId,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        syncChangeCounter: 1,
      },
      {
        id: this._orphanSeparatorId,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        syncChangeCounter: 1,
      },
      {
        id: this._orphanFolderId,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        syncChangeCounter: 1,
      },
      {
        id: this._bookmarkId,
        parent: await PlacesTestUtils.promiseItemGuid(this._orphanFolderId),
        syncChangeCounter: 0,
      },
      {
        id: await PlacesTestUtils.promiseItemId(
          PlacesUtils.bookmarks.unfiledGuid
        ),
        parent: PlacesUtils.bookmarks.rootGuid,
        syncChangeCounter: 3,
      },
    ];
    let db = await PlacesUtils.promiseDBConnection();
    for (let { id, parent, syncChangeCounter } of expectedInfos) {
      let rows = await db.executeCached(
        `
        SELECT b.id, b.syncChangeCounter
        FROM moz_bookmarks b
        JOIN moz_bookmarks p ON b.parent = p.id
        WHERE b.id = :item_id
          AND p.guid = :parent`,
        { item_id: id, parent }
      );
      Assert.equal(rows.length, 1);

      let actualChangeCounter = rows[0].getResultByName("syncChangeCounter");
      Assert.equal(actualChangeCounter, syncChangeCounter);
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.5",
  desc: "Fix wrong item types | folders and separators",

  _separatorId: null,
  _separatorGuid: null,
  _folderId: null,
  _folderGuid: null,
  _syncedFolderId: null,
  _syncedFolderGuid: null,
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Add a separator with a fk
    this._separatorId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_SEPARATOR
    );
    this._separatorGuid = await PlacesTestUtils.promiseItemGuid(
      this._separatorId
    );
    // Add a folder with a fk
    this._folderId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_FOLDER
    );
    this._folderGuid = await PlacesTestUtils.promiseItemGuid(this._folderId);
    // Add a synced folder with a fk
    this._syncedFolderId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      null,
      "itemAAAAAAAA",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );
    this._syncedFolderGuid = await PlacesTestUtils.promiseItemGuid(
      this._syncedFolderId
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    // Check that items with an fk have been converted to bookmarks
    let rows = await db.executeCached(
      `SELECT id, guid, syncChangeCounter
       FROM moz_bookmarks
       WHERE id = :item_id AND type = :type`,
      { item_id: this._separatorId, type: PlacesUtils.bookmarks.TYPE_BOOKMARK }
    );
    Assert.equal(rows.length, 1);
    let expected = [
      {
        id: this._folderId,
        oldGuid: this._folderGuid,
      },
      {
        id: this._syncedFolderId,
        oldGuid: this._syncedFolderGuid,
      },
    ];
    for (let { id, oldGuid } of expected) {
      rows = await db.executeCached(
        `SELECT id, guid, syncChangeCounter
         FROM moz_bookmarks
         WHERE id = :item_id AND type = :type`,
        { item_id: id, type: PlacesUtils.bookmarks.TYPE_BOOKMARK }
      );
      Assert.equal(rows.length, 1);
      Assert.notEqual(rows[0].getResultByName("guid"), oldGuid);
      Assert.equal(rows[0].getResultByName("syncChangeCounter"), 1);
    }

    let tombstones = await PlacesTestUtils.fetchSyncTombstones();
    Assert.deepEqual(
      tombstones.map(info => info.guid),
      ["itemAAAAAAAA"]
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.6",
  desc: "Fix wrong item types | bookmarks",

  _validBookmarkId: null,
  _validBookmarkGuid: null,
  _invalidBookmarkId: null,
  _invalidBookmarkGuid: null,
  _invalidSyncedBookmarkId: null,
  _invalidSyncedBookmarkGuid: null,
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Add a bookmark with a valid place id
    this._validBookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK
    );
    this._validBookmarkGuid = await PlacesTestUtils.promiseItemGuid(
      this._validBookmarkId
    );
    // Add a bookmark with a null place id
    this._invalidBookmarkId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_BOOKMARK
    );
    this._invalidBookmarkGuid = await PlacesTestUtils.promiseItemGuid(
      this._invalidBookmarkId
    );
    // Add a synced bookmark with a null place id
    this._invalidSyncedBookmarkId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      null,
      "bookmarkAAAA",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );
    this._invalidSyncedBookmarkGuid = await PlacesTestUtils.promiseItemGuid(
      this._invalidSyncedBookmarkId
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    // Check valid bookmark
    let rows = await db.executeCached(
      `SELECT id, guid, syncChangeCounter
       FROM moz_bookmarks
       WHERE id = :item_id AND type = :type`,
      {
        item_id: this._validBookmarkId,
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      }
    );
    Assert.equal(rows.length, 1);
    Assert.equal(rows[0].getResultByName("syncChangeCounter"), 0);
    Assert.equal(
      await PlacesTestUtils.promiseItemId(this._validBookmarkGuid),
      this._validBookmarkId
    );

    // Check invalid bookmarks have been converted to folders
    let expected = [
      {
        id: this._invalidBookmarkId,
        oldGuid: this._invalidBookmarkGuid,
      },
      {
        id: this._invalidSyncedBookmarkId,
        oldGuid: this._invalidSyncedBookmarkGuid,
      },
    ];
    for (let { id, oldGuid } of expected) {
      rows = await db.executeCached(
        `SELECT id, guid, syncChangeCounter
         FROM moz_bookmarks
         WHERE id = :item_id AND type = :type`,
        { item_id: id, type: PlacesUtils.bookmarks.TYPE_FOLDER }
      );
      Assert.equal(rows.length, 1);
      Assert.notEqual(rows[0].getResultByName("guid"), oldGuid);
      Assert.equal(rows[0].getResultByName("syncChangeCounter"), 1);
    }

    let tombstones = await PlacesTestUtils.fetchSyncTombstones();
    Assert.deepEqual(
      tombstones.map(info => info.guid),
      ["bookmarkAAAA"]
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.7",
  desc: "Fix missing item types",

  _placeId: null,
  _bookmarkId: null,
  _bookmarkGuid: null,
  _syncedBookmarkId: null,
  _syncedBookmarkGuid: null,
  _folderId: null,
  _folderGuid: null,
  _syncedFolderId: null,
  _syncedFolderGuid: null,

  async setup() {
    // Item without a type but with a place ID; should be converted to a
    // bookmark. The synced bookmark should be handled the same way, but with
    // a tombstone.
    this._placeId = await addPlace();
    this._bookmarkId = await addBookmark(this._placeId);
    this._bookmarkGuid = await PlacesTestUtils.promiseItemGuid(
      this._bookmarkId
    );
    this._syncedBookmarkId = await addBookmark(
      this._placeId,
      null,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      null,
      "bookmarkAAAA",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );
    this._syncedBookmarkGuid = await PlacesTestUtils.promiseItemGuid(
      this._syncedBookmarkId
    );

    // Item without a type and without a place ID; should be converted to a
    // folder.
    this._folderId = await addBookmark();
    this._folderGuid = await PlacesTestUtils.promiseItemGuid(this._folderId);
    this._syncedFolderId = await addBookmark(
      null,
      null,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      null,
      "folderBBBBBB",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );
    this._syncedFolderGuid = await PlacesTestUtils.promiseItemGuid(
      this._syncedFolderId
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let expected = [
      {
        id: this._bookmarkId,
        oldGuid: this._bookmarkGuid,
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        syncChangeCounter: 1,
      },
      {
        id: this._syncedBookmarkId,
        oldGuid: this._syncedBookmarkGuid,
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        syncChangeCounter: 1,
      },
      {
        id: this._folderId,
        oldGuid: this._folderGuid,
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        syncChangeCounter: 1,
      },
      {
        id: this._syncedFolderId,
        oldGuid: this._syncedFolderGuid,
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        syncChangeCounter: 1,
      },
    ];
    for (let { id, oldGuid, type, syncChangeCounter } of expected) {
      let rows = await db.executeCached(
        `SELECT id, guid, type, syncChangeCounter
         FROM moz_bookmarks
         WHERE id = :item_id`,
        { item_id: id }
      );
      Assert.equal(rows.length, 1);
      Assert.equal(rows[0].getResultByName("type"), type);
      Assert.equal(
        rows[0].getResultByName("syncChangeCounter"),
        syncChangeCounter
      );
      Assert.notEqual(rows[0].getResultByName("guid"), oldGuid);
    }

    let tombstones = await PlacesTestUtils.fetchSyncTombstones();
    Assert.deepEqual(
      tombstones.map(info => info.guid),
      ["bookmarkAAAA", "folderBBBBBB"]
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.8",
  desc: "Fix wrong parents",

  _bookmarkId: null,
  _separatorId: null,
  _bookmarkId1: null,
  _bookmarkId2: null,
  _placeId: null,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Insert a bookmark
    this._bookmarkId = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK
    );
    // Insert a separator
    this._separatorId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_SEPARATOR
    );
    // Create 3 children of these items
    let bookmarkGuid = await PlacesTestUtils.promiseItemGuid(this._bookmarkId);
    this._bookmarkId1 = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      bookmarkGuid
    );
    let separatorGuid = await PlacesTestUtils.promiseItemGuid(
      this._separatorId
    );
    this._bookmarkId2 = await addBookmark(
      this._placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      separatorGuid
    );
  },

  async check() {
    // Check that bookmarks are now children of a real folder (unfiled)
    let expectedInfos = [
      {
        id: this._bookmarkId1,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        syncChangeCounter: 1,
      },
      {
        id: this._bookmarkId2,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        syncChangeCounter: 1,
      },
      {
        id: await PlacesTestUtils.promiseItemId(
          PlacesUtils.bookmarks.unfiledGuid
        ),
        parent: PlacesUtils.bookmarks.rootGuid,
        syncChangeCounter: 2,
      },
    ];
    let db = await PlacesUtils.promiseDBConnection();
    for (let { id, parent, syncChangeCounter } of expectedInfos) {
      let rows = await db.executeCached(
        `
        SELECT b.id, b.syncChangeCounter
        FROM moz_bookmarks b
        JOIN moz_bookmarks p ON b.parent = p.id
        WHERE b.id = :item_id AND p.guid = :parent`,
        { item_id: id, parent }
      );
      Assert.equal(rows.length, 1);

      let actualChangeCounter = rows[0].getResultByName("syncChangeCounter");
      Assert.equal(actualChangeCounter, syncChangeCounter);
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.10",
  desc: "Recalculate positions",

  _unfiledBookmarks: [],
  _toolbarBookmarks: [],

  async setup() {
    const NUM_BOOKMARKS = 20;
    let children = [];
    for (let i = 0; i < NUM_BOOKMARKS; i++) {
      children.push({
        title: "testbookmark",
        url: "http://example.com",
      });
    }

    // Add bookmarks to two folders to better perturbate the table.
    await PlacesUtils.bookmarks.insertTree({
      guid: PlacesUtils.bookmarks.unfiledGuid,
      children,
      source: PlacesUtils.bookmarks.SOURCES.SYNC,
    });
    await PlacesUtils.bookmarks.insertTree({
      guid: PlacesUtils.bookmarks.toolbarGuid,
      children,
      source: PlacesUtils.bookmarks.SOURCES.SYNC,
    });

    async function randomize_positions(aParent, aResultArray) {
      await PlacesUtils.withConnectionWrapper("setup", async db => {
        await db.executeTransaction(async () => {
          for (let i = 0; i < NUM_BOOKMARKS / 2; i++) {
            await db.executeCached(
              `UPDATE moz_bookmarks SET position = :rand
               WHERE id IN (
                 SELECT b.id FROM moz_bookmarks b
                 JOIN moz_bookmarks p ON b.parent = p.id
                 WHERE p.guid = :parent
                 ORDER BY RANDOM() LIMIT 1
               )`,
              {
                parent: aParent,
                rand: Math.round(Math.random() * (NUM_BOOKMARKS - 1)),
              }
            );
          }

          // Build the expected ordered list of bookmarks.
          let rows = await db.executeCached(
            `SELECT b.id
             FROM moz_bookmarks b
             JOIN moz_bookmarks p ON b.parent = p.id
             WHERE p.guid = :parent
             ORDER BY b.position ASC, b.ROWID ASC`,
            { parent: aParent }
          );
          rows.forEach(r => {
            aResultArray.push(r.getResultByName("id"));
          });
          await PlacesTestUtils.dumpTable({
            db,
            table: "moz_bookmarks",
            columns: ["id", "parent", "position"],
          });
        });
      });
    }

    // Set random positions for the added bookmarks.
    await randomize_positions(
      PlacesUtils.bookmarks.unfiledGuid,
      this._unfiledBookmarks
    );
    await randomize_positions(
      PlacesUtils.bookmarks.toolbarGuid,
      this._toolbarBookmarks
    );

    let syncInfos = await PlacesTestUtils.fetchBookmarkSyncFields(
      PlacesUtils.bookmarks.unfiledGuid,
      PlacesUtils.bookmarks.toolbarGuid
    );
    Assert.ok(syncInfos.every(info => info.syncChangeCounter === 0));
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();

    async function check_order(aParent, aResultArray) {
      // Build the expected ordered list of bookmarks.
      let childRows = await db.executeCached(
        `SELECT b.id, b.position, b.syncChangeCounter
         FROM moz_bookmarks b
         JOIN moz_bookmarks p ON b.parent = p.id
         WHERE p.guid = :parent
         ORDER BY b.position ASC`,
        { parent: aParent }
      );
      for (let row of childRows) {
        let id = row.getResultByName("id");
        let position = row.getResultByName("position");
        if (aResultArray.indexOf(id) != position) {
          info("Expected order: " + aResultArray);
          await PlacesTestUtils.dumpTable({
            db,
            table: "moz_bookmarks",
            columns: ["id", "parent", "position"],
          });
          do_throw(`Unexpected bookmarks order for ${aParent}.`);
        }
      }

      let parentRows = await db.executeCached(
        `SELECT syncChangeCounter FROM moz_bookmarks
         WHERE guid = :parent`,
        { parent: aParent }
      );
      for (let row of parentRows) {
        let actualChangeCounter = row.getResultByName("syncChangeCounter");
        Assert.ok(actualChangeCounter > 0);
      }
    }

    await check_order(
      PlacesUtils.bookmarks.unfiledGuid,
      this._unfiledBookmarks
    );
    await check_order(
      PlacesUtils.bookmarks.toolbarGuid,
      this._toolbarBookmarks
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "D.13",
  desc: "Fix empty-named tags",
  _taggedItemIds: {},

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    let placeId = await addPlace();
    // Create a empty-named tag.
    this._untitledTagId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.tagsGuid,
      null,
      ""
    );
    // Insert a bookmark in the tag, otherwise it will be removed.
    let untitledTagGuid = await PlacesTestUtils.promiseItemGuid(
      this._untitledTagId
    );
    await addBookmark(
      placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      untitledTagGuid
    );
    // Create a empty-named folder.
    this._untitledFolderId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      ""
    );
    // Create a titled tag.
    this._titledTagId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.tagsGuid,
      null,
      "titledTag"
    );
    // Insert a bookmark in the tag, otherwise it will be removed.
    let titledTagGuid = await PlacesTestUtils.promiseItemGuid(
      this._titledTagId
    );
    await addBookmark(
      placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      titledTagGuid
    );
    // Create a titled folder.
    this._titledFolderId = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      "titledFolder"
    );

    // Create two tagged bookmarks in different folders.
    this._taggedItemIds.inMenu = await addBookmark(
      placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      PlacesUtils.bookmarks.menuGuid,
      null,
      "Tagged bookmark in menu"
    );
    this._taggedItemIds.inToolbar = await addBookmark(
      placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      PlacesUtils.bookmarks.toolbarGuid,
      null,
      "Tagged bookmark in toolbar"
    );
  },

  async check() {
    // Check that valid bookmark is still there
    let value = await PlacesTestUtils.getDatabaseValue(
      "moz_bookmarks",
      "title",
      { id: this._untitledTagId }
    );
    Assert.equal(value, "(notitle)");
    value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "title", {
      id: this._untitledFolderId,
    });
    Assert.equal(value, "");
    value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "title", {
      id: this._titledTagId,
    });
    Assert.equal(value, "titledTag");
    value = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "title", {
      id: this._titledFolderId,
    });
    Assert.equal(value, "titledFolder");

    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(
      `SELECT syncChangeCounter FROM moz_bookmarks
       WHERE id IN (:taggedInMenu, :taggedInToolbar)`,
      {
        taggedInMenu: this._taggedItemIds.inMenu,
        taggedInToolbar: this._taggedItemIds.inToolbar,
      }
    );
    for (let row of rows) {
      Assert.greaterOrEqual(row.getResultByName("syncChangeCounter"), 1);
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "E.1",
  desc: "Remove orphan icon entries",

  _placeId: null,

  async setup() {
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeTransaction(async () => {
        // Insert favicon entries
        await db.executeCached(
          `INSERT INTO moz_icons (id, icon_url, fixed_icon_url_hash, root) VALUES(:favicon_id, :url, hash(fixup_url(:url)), :root)`,
          [
            {
              favicon_id: 1,
              url: "http://www1.mozilla.org/favicon.ico",
              root: 0,
            },
            {
              favicon_id: 2,
              url: "http://www2.mozilla.org/favicon.ico",
              root: 0,
            },
            {
              favicon_id: 3,
              url: "http://www3.mozilla.org/favicon.ico",
              root: 1,
            },
          ]
        );

        // Insert orphan page.
        await db.executeCached(
          `INSERT INTO moz_pages_w_icons (id, page_url, page_url_hash)
           VALUES(:page_id, :url, hash(:url))`,
          { page_id: 99, url: "http://w99.mozilla.org/" }
        );
      });
    });

    // Insert a place using the existing favicon entry
    this._placeId = await addPlace("http://www.mozilla.org", 1);
  },

  async check() {
    // Check that used icon is still there
    let value = await PlacesTestUtils.getDatabaseValue("moz_icons", "id", {
      id: 1,
    });
    Assert.notStrictEqual(value, undefined);
    // Check that unused icon has been removed
    value = await PlacesTestUtils.getDatabaseValue("moz_icons", "id", {
      id: 2,
    });
    Assert.strictEqual(value, undefined);
    // Check that unused icon has been removed
    value = await PlacesTestUtils.getDatabaseValue("moz_icons", "id", {
      id: 3,
    });
    Assert.strictEqual(value, undefined);
    // Check that the orphan page is gone.
    value = await PlacesTestUtils.getDatabaseValue("moz_pages_w_icons", "id", {
      id: 99,
    });
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "F.1",
  desc: "Remove orphan visits",

  _placeId: null,
  _invalidPlaceId: 1337,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Add a valid visit and an invalid one
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeCached(
        `INSERT INTO moz_historyvisits(place_id)
         VALUES (:place_id_1), (:place_id_2)`,
        { place_id_1: this._placeId, place_id_2: this._invalidPlaceId }
      );
    });
  },

  async check() {
    // Check that valid visit is still there
    let value = await PlacesTestUtils.getDatabaseValue(
      "moz_historyvisits",
      "id",
      {
        place_id: this._placeId,
      }
    );
    Assert.notStrictEqual(value, undefined);
    // Check that invalid visit has been removed
    value = await PlacesTestUtils.getDatabaseValue("moz_historyvisits", "id", {
      place_id: this._invalidPlaceId,
    });
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "G.1",
  desc: "Remove orphan input history",

  _placeId: null,
  _invalidPlaceId: 1337,

  async setup() {
    // Add a place to ensure place_id = 1 is valid
    this._placeId = await addPlace();
    // Add input history entries
    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeCached(
        `INSERT INTO moz_inputhistory (place_id, input)
         VALUES (:place_id_1, :input_1), (:place_id_2, :input_2)`,
        {
          place_id_1: this._placeId,
          input_1: "moz",
          place_id_2: this._invalidPlaceId,
          input_2: "moz",
        }
      );
    });
  },

  async check() {
    // Check that inputhistory on valid place is still there
    let value = await PlacesTestUtils.getDatabaseValue(
      "moz_inputhistory",
      "place_id",
      { place_id: this._placeId }
    );
    Assert.notStrictEqual(value, undefined);
    // Check that inputhistory on invalid place has gone
    value = await PlacesTestUtils.getDatabaseValue(
      "moz_inputhistory",
      "place_id",
      { place_id: this._invalidPlaceId }
    );
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "I.1",
  desc: "Remove unused keywords",

  _bookmarkId: null,
  _placeId: null,

  async setup() {
    // Insert 2 keywords
    let bm = await PlacesUtils.bookmarks.insert({
      url: "http://testkw.moz.org/",
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    });
    await PlacesUtils.keywords.insert({
      url: bm.url,
      keyword: "used",
    });

    await PlacesUtils.withConnectionWrapper("setup", async db => {
      await db.executeCached(
        `INSERT INTO moz_keywords (id, keyword, place_id)
         VALUES(NULL, :keyword, :place_id)`,
        { keyword: "unused", place_id: 100 }
      );
    });
  },

  async check() {
    // Check that "used" keyword is still there
    let value = await PlacesTestUtils.getDatabaseValue("moz_keywords", "id", {
      keyword: "used",
    });
    Assert.notStrictEqual(value, undefined);
    // Check that "unused" keyword has gone
    value = await PlacesTestUtils.getDatabaseValue("moz_keywords", "id", {
      keyword: "unused",
    });
    Assert.strictEqual(value, undefined);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "L.1",
  desc: "remove duplicate URLs",
  _placeA: -1,
  _placeD: -1,
  _placeE: -1,
  _bookmarkIds: [],

  async setup() {
    // Place with visits, an autocomplete history entry, anno, and a bookmark.
    this._placeA = await addPlace("http://example.com", null, "placeAAAAAAA");

    // Duplicate Place with different visits and a keyword.
    let placeB = await addPlace("http://example.com", null, "placeBBBBBBB");

    // Another duplicate with conflicting autocomplete history entry and
    // two more bookmarks.
    let placeC = await addPlace("http://example.com", null, "placeCCCCCCC");

    // Unrelated, unique Place.
    this._placeD = await addPlace(
      "http://example.net",
      null,
      "placeDDDDDDD",
      1234
    );

    // Another unrelated Place, with the same hash as D, but different URL.
    this._placeE = await addPlace(
      "http://example.info",
      null,
      "placeEEEEEEE",
      1234
    );

    let visits = [
      {
        placeId: this._placeA,
        date: new Date(2017, 1, 2),
        type: PlacesUtils.history.TRANSITIONS.LINK,
      },
      {
        placeId: this._placeA,
        date: new Date(2018, 3, 4),
        type: PlacesUtils.history.TRANSITIONS.TYPED,
      },
      {
        placeId: placeB,
        date: new Date(2016, 5, 6),
        type: PlacesUtils.history.TRANSITIONS.TYPED,
      },
      {
        // Duplicate visit; should keep both when we merge.
        placeId: placeB,
        date: new Date(2018, 3, 4),
        type: PlacesUtils.history.TRANSITIONS.TYPED,
      },
      {
        placeId: this._placeD,
        date: new Date(2018, 7, 8),
        type: PlacesUtils.history.TRANSITIONS.LINK,
      },
      {
        placeId: this._placeE,
        date: new Date(2018, 8, 9),
        type: PlacesUtils.history.TRANSITIONS.TYPED,
      },
    ];

    let inputs = [
      {
        placeId: this._placeA,
        input: "exam",
        count: 4,
      },
      {
        placeId: placeC,
        input: "exam",
        count: 3,
      },
      {
        placeId: placeC,
        input: "ex",
        count: 5,
      },
      {
        placeId: this._placeD,
        input: "amp",
        count: 3,
      },
    ];

    let annos = [
      {
        name: "anno",
        placeId: this._placeA,
        content: "splish",
      },
      {
        // Anno that's already set on A; should be ignored when we merge.
        name: "anno",
        placeId: placeB,
        content: "oops",
      },
      {
        name: "other-anno",
        placeId: placeB,
        content: "splash",
      },
      {
        name: "other-anno",
        placeId: this._placeD,
        content: "sploosh",
      },
    ];

    let bookmarks = [
      {
        placeId: this._placeA,
        parentGuid: PlacesUtils.bookmarks.unfiledGuid,
        title: "A",
        guid: "bookmarkAAAA",
      },
      {
        placeId: placeB,
        parentGuid: PlacesUtils.bookmarks.mobileGuid,
        title: "B",
        guid: "bookmarkBBBB",
      },
      {
        placeId: placeC,
        parentGuid: PlacesUtils.bookmarks.menuGuid,
        title: "C1",
        guid: "bookmarkCCC1",
      },
      {
        placeId: placeC,
        parentGuid: PlacesUtils.bookmarks.toolbarGuid,
        title: "C2",
        guid: "bookmarkCCC2",
      },
      {
        placeId: this._placeD,
        parentGuid: PlacesUtils.bookmarks.toolbarGuid,
        title: "D",
        guid: "bookmarkDDDD",
      },
      {
        placeId: this._placeE,
        parentGuid: PlacesUtils.bookmarks.unfiledGuid,
        title: "E",
        guid: "bookmarkEEEE",
      },
    ];

    let keywords = [
      {
        placeId: placeB,
        keyword: "hi",
      },
      {
        placeId: this._placeD,
        keyword: "bye",
      },
    ];

    for (let { placeId, parentGuid, title, guid } of bookmarks) {
      let itemId = await addBookmark(
        placeId,
        PlacesUtils.bookmarks.TYPE_BOOKMARK,
        parentGuid,
        null,
        title,
        guid
      );
      this._bookmarkIds.push(itemId);
    }

    await PlacesUtils.withConnectionWrapper(
      "L.1: Insert foreign key refs",
      function (db) {
        return db.executeTransaction(async function () {
          for (let { placeId, date, type } of visits) {
            await db.executeCached(
              `INSERT INTO moz_historyvisits(place_id, visit_date, visit_type)
               VALUES(:placeId, :date, :type)`,
              { placeId, date: PlacesUtils.toPRTime(date), type }
            );
          }

          for (let params of inputs) {
            await db.executeCached(
              `INSERT INTO moz_inputhistory(place_id, input, use_count)
               VALUES(:placeId, :input, :count)`,
              params
            );
          }

          for (let { name, placeId, content } of annos) {
            await db.executeCached(
              `INSERT OR IGNORE INTO moz_anno_attributes(name)
               VALUES(:name)`,
              { name }
            );

            await db.executeCached(
              `INSERT INTO moz_annos(place_id, anno_attribute_id, content)
               VALUES(:placeId, (SELECT id FROM moz_anno_attributes
                                 WHERE name = :name), :content)`,
              { placeId, name, content }
            );
          }

          for (let param of keywords) {
            await db.executeCached(
              `INSERT INTO moz_keywords(keyword, place_id)
               VALUES(:keyword, :placeId)`,
              param
            );
          }
        });
      }
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();

    let placeRows = await db.execute(`
      SELECT id, guid, foreign_count FROM moz_places
      ORDER BY guid`);
    let placeInfos = placeRows.map(row => ({
      id: row.getResultByName("id"),
      guid: row.getResultByName("guid"),
      foreignCount: row.getResultByName("foreign_count"),
    }));
    Assert.deepEqual(
      placeInfos,
      [
        {
          id: this._placeA,
          guid: "placeAAAAAAA",
          foreignCount: 5, // 4 bookmarks + 1 keyword
        },
        {
          id: this._placeD,
          guid: "placeDDDDDDD",
          foreignCount: 2, // 1 bookmark + 1 keyword
        },
        {
          id: this._placeE,
          guid: "placeEEEEEEE",
          foreignCount: 1, // 1 bookmark
        },
      ],
      "Should remove duplicate Places B and C"
    );

    let visitRows = await db.execute(`
      SELECT place_id, visit_date, visit_type FROM moz_historyvisits
      ORDER BY visit_date`);
    let visitInfos = visitRows.map(row => ({
      placeId: row.getResultByName("place_id"),
      date: PlacesUtils.toDate(row.getResultByName("visit_date")),
      type: row.getResultByName("visit_type"),
    }));
    Assert.deepEqual(
      visitInfos,
      [
        {
          placeId: this._placeA,
          date: new Date(2016, 5, 6),
          type: PlacesUtils.history.TRANSITIONS.TYPED,
        },
        {
          placeId: this._placeA,
          date: new Date(2017, 1, 2),
          type: PlacesUtils.history.TRANSITIONS.LINK,
        },
        {
          placeId: this._placeA,
          date: new Date(2018, 3, 4),
          type: PlacesUtils.history.TRANSITIONS.TYPED,
        },
        {
          placeId: this._placeA,
          date: new Date(2018, 3, 4),
          type: PlacesUtils.history.TRANSITIONS.TYPED,
        },
        {
          placeId: this._placeD,
          date: new Date(2018, 7, 8),
          type: PlacesUtils.history.TRANSITIONS.LINK,
        },
        {
          placeId: this._placeE,
          date: new Date(2018, 8, 9),
          type: PlacesUtils.history.TRANSITIONS.TYPED,
        },
      ],
      "Should merge history visits"
    );

    let inputRows = await db.execute(`
      SELECT place_id, input, use_count FROM moz_inputhistory
      ORDER BY use_count ASC`);
    let inputInfos = inputRows.map(row => ({
      placeId: row.getResultByName("place_id"),
      input: row.getResultByName("input"),
      count: row.getResultByName("use_count"),
    }));
    Assert.deepEqual(
      inputInfos,
      [
        {
          placeId: this._placeD,
          input: "amp",
          count: 3,
        },
        {
          placeId: this._placeA,
          input: "ex",
          count: 5,
        },
        {
          placeId: this._placeA,
          input: "exam",
          count: 7,
        },
      ],
      "Should merge autocomplete history"
    );

    let annoRows = await db.execute(`
      SELECT a.place_id, n.name, a.content FROM moz_annos a
      JOIN moz_anno_attributes n ON n.id = a.anno_attribute_id
      ORDER BY n.name, a.content ASC`);
    let annoInfos = annoRows.map(row => ({
      placeId: row.getResultByName("place_id"),
      name: row.getResultByName("name"),
      content: row.getResultByName("content"),
    }));
    Assert.deepEqual(
      annoInfos,
      [
        {
          placeId: this._placeA,
          name: "anno",
          content: "splish",
        },
        {
          placeId: this._placeA,
          name: "other-anno",
          content: "splash",
        },
        {
          placeId: this._placeD,
          name: "other-anno",
          content: "sploosh",
        },
      ],
      "Should merge page annos"
    );

    let itemRows = await db.execute(
      `
      SELECT guid, fk, syncChangeCounter FROM moz_bookmarks
      WHERE id IN (${new Array(this._bookmarkIds.length).fill("?").join(",")})
      ORDER BY guid ASC`,
      this._bookmarkIds
    );
    let itemInfos = itemRows.map(row => ({
      guid: row.getResultByName("guid"),
      placeId: row.getResultByName("fk"),
      syncChangeCounter: row.getResultByName("syncChangeCounter"),
    }));
    Assert.deepEqual(
      itemInfos,
      [
        {
          guid: "bookmarkAAAA",
          placeId: this._placeA,
          syncChangeCounter: 1,
        },
        {
          guid: "bookmarkBBBB",
          placeId: this._placeA,
          syncChangeCounter: 1,
        },
        {
          guid: "bookmarkCCC1",
          placeId: this._placeA,
          syncChangeCounter: 1,
        },
        {
          guid: "bookmarkCCC2",
          placeId: this._placeA,
          syncChangeCounter: 1,
        },
        {
          guid: "bookmarkDDDD",
          placeId: this._placeD,
          syncChangeCounter: 0,
        },
        {
          guid: "bookmarkEEEE",
          placeId: this._placeE,
          syncChangeCounter: 0,
        },
      ],
      "Should merge bookmarks and bump change counter"
    );

    let keywordRows = await db.execute(`
      SELECT keyword, place_id FROM moz_keywords
      ORDER BY keyword ASC`);
    let keywordInfos = keywordRows.map(row => ({
      keyword: row.getResultByName("keyword"),
      placeId: row.getResultByName("place_id"),
    }));
    Assert.deepEqual(
      keywordInfos,
      [
        {
          keyword: "bye",
          placeId: this._placeD,
        },
        {
          keyword: "hi",
          placeId: this._placeA,
        },
      ],
      "Should merge all keywords"
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "L.2",
  desc: "Recalculate visit_count and last_visit_date",

  async setup() {
    async function setVisitCount(aURL, aValue) {
      await PlacesUtils.withConnectionWrapper("setVisitCount", async db => {
        await db.executeCached(
          `UPDATE moz_places SET visit_count = :count
           WHERE url_hash = hash(:url) AND url = :url`,
          { count: aValue, url: aURL }
        );
      });
    }
    async function setLastVisitDate(aURL, aValue) {
      await PlacesUtils.withConnectionWrapper("setVisitCount", async db => {
        await db.executeCached(
          `UPDATE moz_places SET last_visit_date = :date
           WHERE url_hash = hash(:url) AND url = :url`,
          { date: aValue, url: aURL }
        );
      });
    }

    let now = Date.now() * 1000;
    // Add a page with 1 visit.
    let url = "http://1.moz.org/";
    await PlacesTestUtils.addVisits({ uri: uri(url), visitDate: now++ });
    // Add a page with 1 visit and set wrong visit_count.
    url = "http://2.moz.org/";
    await PlacesTestUtils.addVisits({ uri: uri(url), visitDate: now++ });
    await setVisitCount(url, 10);
    // Add a page with 1 visit and set wrong last_visit_date.
    url = "http://3.moz.org/";
    await PlacesTestUtils.addVisits({ uri: uri(url), visitDate: now++ });
    await setLastVisitDate(url, now++);
    // Add a page with 1 visit and set wrong stats.
    url = "http://4.moz.org/";
    await PlacesTestUtils.addVisits({ uri: uri(url), visitDate: now++ });
    await setVisitCount(url, 10);
    await setLastVisitDate(url, now++);

    // Add a page without visits.
    url = "http://5.moz.org/";
    await addPlace(url);
    // Add a page without visits and set wrong visit_count.
    url = "http://6.moz.org/";
    await addPlace(url);
    await setVisitCount(url, 10);
    // Add a page without visits and set wrong last_visit_date.
    url = "http://7.moz.org/";
    await addPlace(url);
    await setLastVisitDate(url, now++);
    // Add a page without visits and set wrong stats.
    url = "http://8.moz.org/";
    await addPlace(url);
    await setVisitCount(url, 10);
    await setLastVisitDate(url, now++);
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(
      `SELECT h.id, h.last_visit_date as v_date
       FROM moz_places h
       LEFT JOIN moz_historyvisits v ON v.place_id = h.id AND visit_type NOT IN (0,4,7,8,9)
       GROUP BY h.id HAVING h.visit_count <> count(v.id)
       UNION ALL
       SELECT h.id, MAX(v.visit_date) as v_date
       FROM moz_places h
       LEFT JOIN moz_historyvisits v ON v.place_id = h.id
       GROUP BY h.id HAVING h.last_visit_date IS NOT v_date`
    );
    Assert.equal(rows.length, 0);
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "L.3",
  desc: "recalculate hidden for redirects.",

  async setup() {
    await PlacesTestUtils.addVisits([
      {
        uri: NetUtil.newURI("http://l3.moz.org/"),
        transition: TRANSITION_TYPED,
      },
      {
        uri: NetUtil.newURI("http://l3.moz.org/redirecting/"),
        transition: TRANSITION_TYPED,
      },
      {
        uri: NetUtil.newURI("http://l3.moz.org/redirecting2/"),
        transition: TRANSITION_REDIRECT_TEMPORARY,
        referrer: NetUtil.newURI("http://l3.moz.org/redirecting/"),
      },
      {
        uri: NetUtil.newURI("http://l3.moz.org/target/"),
        transition: TRANSITION_REDIRECT_PERMANENT,
        referrer: NetUtil.newURI("http://l3.moz.org/redirecting2/"),
      },
    ]);
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.executeCached(
      "SELECT h.url FROM moz_places h WHERE h.hidden = 1"
    );
    Assert.equal(rows.length, 2);
    for (let row of rows) {
      let url = row.getResultByIndex(0);
      Assert.ok(/redirecting/.test(url));
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "L.4",
  desc: "recalculate foreign_count.",

  async setup() {
    this._pageGuid = (
      await PlacesUtils.history.insert({
        url: "http://l4.moz.org/",
        visits: [{ date: new Date() }],
      })
    ).guid;
    await PlacesUtils.bookmarks.insert({
      url: "http://l4.moz.org/",
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    });
    await PlacesUtils.keywords.insert({
      url: "http://l4.moz.org/",
      keyword: "kw",
    });
    Assert.equal(
      await PlacesTestUtils.getDatabaseValue("moz_places", "foreign_count", {
        guid: this._pageGuid,
      }),
      2
    );
  },

  async check() {
    Assert.equal(
      await PlacesTestUtils.getDatabaseValue("moz_places", "foreign_count", {
        guid: this._pageGuid,
      }),
      2
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "L.5",
  desc: "recalculate hashes when missing.",

  async setup() {
    this._pageGuid = (
      await PlacesUtils.history.insert({
        url: "http://l5.moz.org/",
        visits: [{ date: new Date() }],
      })
    ).guid;
    Assert.greater(
      await PlacesTestUtils.getDatabaseValue("moz_places", "url_hash", {
        guid: this._pageGuid,
      }),
      0
    );
    await PlacesUtils.withConnectionWrapper(
      "change url hash",
      async function (db) {
        await db.execute(`UPDATE moz_places SET url_hash = 0`);
      }
    );
    Assert.equal(
      await PlacesTestUtils.getDatabaseValue("moz_places", "url_hash", {
        guid: this._pageGuid,
      }),
      0
    );
  },

  async check() {
    Assert.greater(
      await PlacesTestUtils.getDatabaseValue("moz_places", "url_hash", {
        guid: this._pageGuid,
      }),
      0
    );
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "L.6",
  desc: "fix invalid Place GUIDs",
  _placeIds: [],

  async setup() {
    let placeWithValidGuid = await addPlace(
      "http://example.com/a",
      null,
      "placeAAAAAAA"
    );
    this._placeIds.push(placeWithValidGuid);

    let placeWithEmptyGuid = await addPlace("http://example.com/b", null, "");
    this._placeIds.push(placeWithEmptyGuid);

    let placeWithoutGuid = await addPlace("http://example.com/c", null, null);
    this._placeIds.push(placeWithoutGuid);

    let placeWithInvalidGuid = await addPlace(
      "http://example.com/c",
      null,
      "{123456}"
    );
    this._placeIds.push(placeWithInvalidGuid);
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let updatedRows = await db.execute(
      `
      SELECT id, guid
      FROM moz_places
      WHERE id IN (?, ?, ?, ?)`,
      this._placeIds
    );

    for (let row of updatedRows) {
      let id = row.getResultByName("id");
      let guid = row.getResultByName("guid");
      if (id == this._placeIds[0]) {
        Assert.equal(guid, "placeAAAAAAA");
      } else {
        Assert.ok(PlacesUtils.isValidGuid(guid));
      }
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "S.1",
  desc: "fix invalid GUIDs for synced bookmarks",
  _bookmarkInfos: [],

  async setup() {
    let folderWithInvalidGuid = await addBookmark(
      null,
      PlacesUtils.bookmarks.TYPE_FOLDER,
      PlacesUtils.bookmarks.menuGuid,
      /* aKeywordId */ null,
      "NORMAL folder with invalid GUID",
      "{123456}",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );

    let placeIdForBookmarkWithoutGuid = await addPlace();
    let bookmarkWithoutGuid = await addBookmark(
      placeIdForBookmarkWithoutGuid,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      "{123456}",
      /* aKeywordId */ null,
      "NEW bookmark without GUID",
      /* aGuid */ null
    );

    let placeIdForBookmarkWithInvalidGuid = await addPlace();
    let bookmarkWithInvalidGuid = await addBookmark(
      placeIdForBookmarkWithInvalidGuid,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      "{123456}",
      /* aKeywordId */ null,
      "NORMAL bookmark with invalid GUID",
      "bookmarkAAAA\n",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );

    let placeIdForBookmarkWithValidGuid = await addPlace();
    let bookmarkWithValidGuid = await addBookmark(
      placeIdForBookmarkWithValidGuid,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      "{123456}",
      /* aKeywordId */ null,
      "NORMAL bookmark with valid GUID",
      "bookmarkBBBB",
      PlacesUtils.bookmarks.SYNC_STATUS.NORMAL
    );

    this._bookmarkInfos.push(
      {
        id: await PlacesTestUtils.promiseItemId(PlacesUtils.bookmarks.menuGuid),
        syncChangeCounter: 1,
        syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NORMAL,
      },
      {
        id: folderWithInvalidGuid,
        syncChangeCounter: 3,
        syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NEW,
      },
      {
        id: bookmarkWithoutGuid,
        syncChangeCounter: 1,
        syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NEW,
      },
      {
        id: bookmarkWithInvalidGuid,
        syncChangeCounter: 1,
        syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NEW,
      },
      {
        id: bookmarkWithValidGuid,
        syncChangeCounter: 0,
        syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NORMAL,
      }
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let updatedRows = await db.execute(
      `SELECT id, guid, syncChangeCounter, syncStatus
       FROM moz_bookmarks
       WHERE id IN (?, ?, ?, ?, ?)`,
      this._bookmarkInfos.map(info => info.id)
    );

    for (let row of updatedRows) {
      let id = row.getResultByName("id");
      let guid = row.getResultByName("guid");
      Assert.ok(PlacesUtils.isValidGuid(guid));

      let cachedGuid = await PlacesTestUtils.promiseItemGuid(id);
      Assert.equal(cachedGuid, guid);

      let expectedInfo = this._bookmarkInfos.find(info => info.id == id);

      let syncChangeCounter = row.getResultByName("syncChangeCounter");
      Assert.equal(syncChangeCounter, expectedInfo.syncChangeCounter);

      let syncStatus = row.getResultByName("syncStatus");
      Assert.equal(syncStatus, expectedInfo.syncStatus);
    }

    let tombstones = await PlacesTestUtils.fetchSyncTombstones();
    Assert.deepEqual(
      tombstones.map(info => info.guid),
      ["bookmarkAAAA\n", "{123456}"]
    );
  },
});

tests.push({
  name: "S.2",
  desc: "drop tombstones for bookmarks that aren't deleted",

  async setup() {
    let placeId = await addPlace();
    await addBookmark(
      placeId,
      PlacesUtils.bookmarks.TYPE_BOOKMARK,
      PlacesUtils.bookmarks.menuGuid,
      null,
      "",
      "bookmarkAAAA"
    );

    await PlacesUtils.withConnectionWrapper("Insert tombstones", db =>
      db.executeTransaction(async function () {
        for (let guid of ["bookmarkAAAA", "bookmarkBBBB"]) {
          await db.executeCached(
            `INSERT INTO moz_bookmarks_deleted(guid)
             VALUES(:guid)`,
            { guid }
          );
        }
      })
    );
  },

  async check() {
    let tombstones = await PlacesTestUtils.fetchSyncTombstones();
    Assert.deepEqual(
      tombstones.map(info => info.guid),
      ["bookmarkBBBB"]
    );
  },
});

tests.push({
  name: "S.3",
  desc: "set missing added and last modified dates",
  _placeVisits: [],
  _bookmarksWithDates: [],

  async setup() {
    let placeIdWithVisits = await addPlace();
    let placeIdWithZeroVisit = await addPlace();
    this._placeVisits.push(
      {
        placeId: placeIdWithVisits,
        visitDate: PlacesUtils.toPRTime(new Date(2017, 9, 4)),
      },
      {
        placeId: placeIdWithVisits,
        visitDate: PlacesUtils.toPRTime(new Date(2017, 9, 8)),
      },
      {
        placeId: placeIdWithZeroVisit,
        visitDate: 0,
      }
    );

    this._bookmarksWithDates.push(
      {
        guid: "bookmarkAAAA",
        placeId: null,
        parent: PlacesUtils.bookmarks.menuGuid,
        dateAdded: null,
        lastModified: PlacesUtils.toPRTime(new Date(2017, 9, 1)),
      },
      {
        guid: "bookmarkBBBB",
        placeId: null,
        parent: PlacesUtils.bookmarks.menuGuid,
        dateAdded: PlacesUtils.toPRTime(new Date(2017, 9, 2)),
        lastModified: null,
      },
      {
        guid: "bookmarkCCCC",
        placeId: null,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        dateAdded: null,
        lastModified: null,
      },
      {
        guid: "bookmarkDDDD",
        placeId: placeIdWithVisits,
        parent: PlacesUtils.bookmarks.mobileGuid,
        dateAdded: null,
        lastModified: null,
      },
      {
        guid: "bookmarkEEEE",
        placeId: placeIdWithVisits,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        dateAdded: PlacesUtils.toPRTime(new Date(2017, 9, 3)),
        lastModified: PlacesUtils.toPRTime(new Date(2017, 9, 6)),
      },
      {
        guid: "bookmarkFFFF",
        placeId: placeIdWithZeroVisit,
        parent: PlacesUtils.bookmarks.unfiledGuid,
        dateAdded: 0,
        lastModified: 0,
      }
    );

    await PlacesUtils.withConnectionWrapper(
      "S.3: Insert bookmarks and visits",
      db =>
        db.executeTransaction(async () => {
          await db.execute(
            `INSERT INTO moz_historyvisits(place_id, visit_date)
             VALUES(:placeId, :visitDate)`,
            this._placeVisits
          );

          await db.execute(
            `INSERT INTO moz_bookmarks(fk, type, parent, guid, dateAdded,
                                       lastModified)
             VALUES(:placeId, 1, (SELECT id FROM moz_bookmarks WHERE guid = :parent),
                    :guid, :dateAdded, :lastModified)`,
            this._bookmarksWithDates
          );

          await db.execute(
            `UPDATE moz_bookmarks SET dateAdded = 0, lastModified = NULL
             WHERE guid = :toolbarFolder`,
            { toolbarFolder: PlacesUtils.bookmarks.toolbarGuid }
          );
        })
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let updatedRows = await db.execute(
      `SELECT guid, dateAdded, lastModified
       FROM moz_bookmarks
       WHERE guid = :guid`,
      [
        { guid: PlacesUtils.bookmarks.toolbarGuid },
        ...this._bookmarksWithDates.map(({ guid }) => ({ guid })),
      ]
    );

    for (let row of updatedRows) {
      let guid = row.getResultByName("guid");

      let dateAdded = row.getResultByName("dateAdded");
      Assert.ok(Number.isInteger(dateAdded));

      let lastModified = row.getResultByName("lastModified");
      Assert.ok(Number.isInteger(lastModified));

      switch (guid) {
        // Last modified date exists, so we should use it for date added.
        case "bookmarkAAAA": {
          let expectedInfo = this._bookmarksWithDates[0];
          Assert.equal(dateAdded, expectedInfo.lastModified);
          Assert.equal(lastModified, expectedInfo.lastModified);
          break;
        }

        // Date added exists, so we should use it for last modified date.
        case "bookmarkBBBB": {
          let expectedInfo = this._bookmarksWithDates[1];
          Assert.equal(dateAdded, expectedInfo.dateAdded);
          Assert.equal(lastModified, expectedInfo.dateAdded);
          break;
        }

        // C has no visits, date added, or last modified time, F has zeros for
        // all, and the toolbar has a zero date added and no last modified time.
        // In all cases, we should fall back to the current time.
        case "bookmarkCCCC":
        case "bookmarkFFFF":
        case PlacesUtils.bookmarks.toolbarGuid: {
          let nowAsPRTime = PlacesUtils.toPRTime(new Date());
          Assert.greater(dateAdded, 0);
          Assert.equal(dateAdded, lastModified);
          Assert.ok(dateAdded <= nowAsPRTime);
          break;
        }

        // Neither date added nor last modified exists, but we have two
        // visits, so we should fall back to the earliest and latest visit
        // dates.
        case "bookmarkDDDD": {
          let oldestVisit = this._placeVisits[0];
          Assert.equal(dateAdded, oldestVisit.visitDate);
          let newestVisit = this._placeVisits[1];
          Assert.equal(lastModified, newestVisit.visitDate);
          break;
        }

        // We have two visits, but both date added and last modified exist,
        // so we shouldn't update them.
        case "bookmarkEEEE": {
          let expectedInfo = this._bookmarksWithDates[4];
          Assert.equal(dateAdded, expectedInfo.dateAdded);
          Assert.equal(lastModified, expectedInfo.lastModified);
          break;
        }

        default:
          throw new Error(`Unexpected row for bookmark ${guid}`);
      }
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "S.4",
  desc: "reset added dates that are ahead of last modified dates",
  _bookmarksWithDates: [],

  async setup() {
    this._bookmarksWithDates.push({
      guid: "bookmarkGGGG",
      parent: PlacesUtils.bookmarks.unfiledGuid,
      dateAdded: PlacesUtils.toPRTime(new Date(2017, 9, 6)),
      lastModified: PlacesUtils.toPRTime(new Date(2017, 9, 3)),
    });

    await PlacesUtils.withConnectionWrapper(
      "S.4: Insert bookmarks and visits",
      db =>
        db.executeTransaction(async () => {
          await db.execute(
            `INSERT INTO moz_bookmarks(type, parent, guid, dateAdded,
                                       lastModified)
             VALUES(1, (SELECT id FROM moz_bookmarks WHERE guid = :parent),
                    :guid, :dateAdded, :lastModified)`,
            this._bookmarksWithDates
          );
        })
    );
  },

  async check() {
    let db = await PlacesUtils.promiseDBConnection();
    let updatedRows = await db.execute(
      `SELECT guid, dateAdded, lastModified
       FROM moz_bookmarks
       WHERE guid = :guid`,
      this._bookmarksWithDates.map(({ guid }) => ({ guid }))
    );

    for (let row of updatedRows) {
      let guid = row.getResultByName("guid");
      let dateAdded = row.getResultByName("dateAdded");
      let lastModified = row.getResultByName("lastModified");
      switch (guid) {
        case "bookmarkGGGG": {
          let expectedInfo = this._bookmarksWithDates[0];
          Assert.equal(dateAdded, expectedInfo.lastModified);
          Assert.equal(lastModified, expectedInfo.lastModified);
          break;
        }

        default:
          throw new Error(`Unexpected row for bookmark ${guid}`);
      }
    }
  },
});

// ------------------------------------------------------------------------------

tests.push({
  name: "Z",
  desc: "Sanity: Preventive maintenance does not touch valid items",

  _uri1: uri("http://www1.mozilla.org"),
  _uri2: uri("http://www2.mozilla.org"),
  _folder: null,
  _bookmark: null,
  _bookmarkId: null,
  _separator: null,

  async setup() {
    // use valid api calls to create a bunch of items
    await PlacesTestUtils.addVisits([{ uri: this._uri1 }, { uri: this._uri2 }]);

    let bookmarks = await PlacesUtils.bookmarks.insertTree({
      guid: PlacesUtils.bookmarks.toolbarGuid,
      children: [
        {
          title: "testfolder",
          type: PlacesUtils.bookmarks.TYPE_FOLDER,
          children: [
            {
              title: "testbookmark",
              url: this._uri1,
            },
          ],
        },
      ],
    });

    this._folder = bookmarks[0];
    this._bookmark = bookmarks[1];
    this._bookmarkId = await PlacesTestUtils.promiseItemId(bookmarks[1].guid);

    this._separator = await PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      type: PlacesUtils.bookmarks.TYPE_SEPARATOR,
    });

    PlacesUtils.tagging.tagURI(this._uri1, ["testtag"]);
    await PlacesTestUtils.setFaviconForPage(
      this._uri2,
      SMALLPNG_DATA_URI,
      SMALLPNG_DATA_URI
    );
    await PlacesUtils.keywords.insert({
      url: this._uri1.spec,
      keyword: "testkeyword",
    });
    await PlacesUtils.history.update({
      url: this._uri2,
      annotations: new Map([["anno", "anno"]]),
    });
  },

  async check() {
    // Check that all items are correct
    let isVisited = await PlacesUtils.history.hasVisits(this._uri1);
    Assert.ok(isVisited);
    isVisited = await PlacesUtils.history.hasVisits(this._uri2);
    Assert.ok(isVisited);

    Assert.equal(
      (await PlacesUtils.bookmarks.fetch(this._bookmark.guid)).url,
      this._uri1.spec
    );
    let folder = await PlacesUtils.bookmarks.fetch(this._folder.guid);
    Assert.equal(folder.index, 0);
    Assert.equal(folder.type, PlacesUtils.bookmarks.TYPE_FOLDER);
    Assert.equal(
      (await PlacesUtils.bookmarks.fetch(this._separator.guid)).type,
      PlacesUtils.bookmarks.TYPE_SEPARATOR
    );

    Assert.equal(PlacesUtils.tagging.getTagsForURI(this._uri1).length, 1);
    Assert.equal(
      (await PlacesUtils.keywords.fetch({ url: this._uri1.spec })).keyword,
      "testkeyword"
    );
    let pageInfo = await PlacesUtils.history.fetch(this._uri2, {
      includeAnnotations: true,
    });
    Assert.equal(pageInfo.annotations.get("anno"), "anno");

    let favicon = await PlacesTestUtils.getFaviconForPage(this._uri2);
    Assert.equal(favicon.uri.spec, SMALLPNG_DATA_URI.spec);
  },
});

// ------------------------------------------------------------------------------

add_task(async function test_preventive_maintenance() {
  let db = await PlacesUtils.promiseDBConnection();
  // Get current bookmarks max ID for cleanup
  defaultBookmarksMaxId = (
    await db.executeCached("SELECT MAX(id) FROM moz_bookmarks")
  )[0].getResultByIndex(0);
  Assert.ok(defaultBookmarksMaxId > 0);

  for (let test of tests) {
    await PlacesTestUtils.markBookmarksAsSynced();

    info("\nExecuting test: " + test.name + "\n*** " + test.desc + "\n");
    await test.setup();

    Services.prefs.clearUserPref("places.database.lastMaintenance");
    await PlacesDBUtils.maintenanceOnIdle();

    // Check the lastMaintenance time has been saved.
    Assert.notEqual(
      Services.prefs.getIntPref("places.database.lastMaintenance"),
      null
    );

    await test.check();

    await cleanDatabase();
  }

  // Sanity check: all roots should be intact
  Assert.strictEqual(
    (await PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.rootGuid))
      .parentGuid,
    undefined
  );
  Assert.deepEqual(
    (await PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.menuGuid))
      .parentGuid,
    PlacesUtils.bookmarks.rootGuid
  );
  Assert.deepEqual(
    (await PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.tagsGuid))
      .parentGuid,
    PlacesUtils.bookmarks.rootGuid
  );
  Assert.deepEqual(
    (await PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.unfiledGuid))
      .parentGuid,
    PlacesUtils.bookmarks.rootGuid
  );
  Assert.deepEqual(
    (await PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.toolbarGuid))
      .parentGuid,
    PlacesUtils.bookmarks.rootGuid
  );
});

// ------------------------------------------------------------------------------

add_task(async function test_idle_daily() {
  const { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );
  const sandbox = sinon.createSandbox();
  sandbox.stub(PlacesDBUtils, "maintenanceOnIdle");
  Services.prefs.clearUserPref("places.database.lastMaintenance");
  Cc["@mozilla.org/places/databaseUtilsIdleMaintenance;1"]
    .getService(Ci.nsIObserver)
    .observe(null, "idle-daily", "");
  Assert.ok(
    PlacesDBUtils.maintenanceOnIdle.calledOnce,
    "maintenanceOnIdle was invoked"
  );
  sandbox.restore();
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { BookmarkJSONUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/BookmarkJSONUtils.sys.mjs"
);

// An object representing the contents of bookmarks.json.
var test_bookmarks = {
  menu: [
    {
      guid: "OCyeUO5uu9FF",
      title: "Mozilla Firefox",
      children: [
        {
          guid: "OCyeUO5uu9FG",
          title: "Help and Tutorials",
          url: "http://en-us.www.mozilla.com/en-US/firefox/help/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          guid: "OCyeUO5uu9FH",
          title: "Customize Firefox",
          url: "http://en-us.www.mozilla.com/en-US/firefox/customize/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          guid: "OCyeUO5uu9FI",
          title: "Get Involved",
          url: "http://en-us.www.mozilla.com/en-US/firefox/community/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          guid: "OCyeUO5uu9FJ",
          title: "About Us",
          url: "http://en-us.www.mozilla.com/en-US/about/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          guid: "QFM-QnE2ZpMz",
          title: "Test null postData",
          url: "http://example.com/search?q=%s&suggid=",
        },
      ],
    },
    {
      guid: "OCyeUO5uu9FK",
      type: Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR,
    },
    {
      guid: "OCyeUO5uu9FL",
      title: "test",
      dateAdded: 1177541020000000,
      lastModified: 1177541050000000,
      children: [
        {
          guid: "OCyeUO5uu9GX",
          title: "test post keyword",
          dateAdded: 1177375336000000,
          lastModified: 1177375423000000,
          keyword: "test",
          postData: "hidden1%3Dbar&text1%3D%25s",
          charset: "ISO-8859-1",
        },
      ],
    },
  ],
  toolbar: [
    {
      guid: "OCyeUO5uu9FB",
      title: "Getting Started",
      url: "http://en-us.www.mozilla.com/en-US/firefox/central/",
      icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
    },
    {
      guid: "OCyeUO5uu9FR",
      title: "Latest Headlines",
      // This used to be a livemark, but we don't import them anymore, instead
      // it will be imported as an empty folder, because the json format stores
      // it like that: an empty folder with a couple annotations. Since
      // annotations will go away, there won't be a clean way to import it as a
      // bookmark instead.
      // Note: date gets truncated to milliseconds, whereas the value in bookmarks.json
      // has full microseconds.
      dateAdded: 1361551979451000,
      lastModified: 1361551979457000,
    },
  ],
  unfiled: [
    { guid: "OCyeUO5uu9FW", title: "Example.tld", url: "http://example.tld/" },
    {
      guid: "Cfkety492Afk",
      title: "test tagged bookmark",
      dateAdded: 1507025843703000,
      lastModified: 1507025844703000,
      url: "http://example.tld/tagged",
      tags: ["foo"],
    },
    {
      guid: "lOZGoFR1eXbl",
      title: "Bookmarks Toolbar Shortcut",
      dateAdded: 1507025843703000,
      lastModified: 1507025844703000,
      url: `place:parent=${PlacesUtils.bookmarks.toolbarGuid}`,
    },
    {
      guid: "7yJWnBVhjRtP",
      title: "Folder Shortcut",
      dateAdded: 1507025843703000,
      lastModified: 1507025844703000,
      url: `place:parent=OCyeUO5uu9FF`,
    },
    {
      guid: "vm5QXWuWc12l",
      title: "Folder Shortcut 2",
      dateAdded: 1507025843703000,
      lastModified: 1507025844703000,
      url: "place:invalidOldParentId=6123443&excludeItems=1",
    },
    {
      guid: "Icg1XlIozA1D",
      title: "Folder Shortcut 3",
      dateAdded: 1507025843703000,
      lastModified: 1507025844703000,
      url: `place:parent=OCyeUO5uu9FF&parent=${PlacesUtils.bookmarks.menuGuid}`,
    },
  ],
};

// Exported bookmarks file pointer.
var bookmarksExportedFile;

add_task(async function test_import_bookmarks_disallowed_url() {
  await Assert.rejects(
    BookmarkJSONUtils.importFromURL("http://example.com/bookmarks.json"),
    /importFromURL can only be used with/,
    "Should reject importing from an http based url"
  );
  await Assert.rejects(
    BookmarkJSONUtils.importFromURL("https://example.com/bookmarks.json"),
    /importFromURL can only be used with/,
    "Should reject importing from an https based url"
  );
});

add_task(async function test_import_bookmarks_count() {
  // Ensure the bookmarks count is correct when importing in various cases
  await PlacesUtils.bookmarks.eraseEverything();
  let bookmarksFile = PathUtils.join(do_get_cwd().path, "bookmarks.json");
  bookmarksExportedFile = PathUtils.join(
    PathUtils.profileDir,
    "bookmarks.exported.json"
  );

  let count = await BookmarkJSONUtils.importFromFile(bookmarksFile, {
    replace: true,
  });
  Assert.equal(
    count,
    13,
    "There should be 13 imported bookmarks when importing from an empty database"
  );

  await BookmarkJSONUtils.exportToFile(bookmarksExportedFile);
  count = -1;
  count = await BookmarkJSONUtils.importFromFile(bookmarksExportedFile, {
    replace: true,
  });
  Assert.equal(
    count,
    13,
    "There should be 13 imported bookmarks when replacing existing bookmarks"
  );

  await PlacesUtils.bookmarks.eraseEverything();
  count = -1;
  let bookmarksUrl = PathUtils.toFileURI(bookmarksFile);
  count = await BookmarkJSONUtils.importFromURL(bookmarksUrl);
  Assert.equal(
    count,
    13,
    "There should be 13 imported bookmarks when importing from a URL"
  );

  // Clean up task
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_import_bookmarks() {
  let bookmarksFile = PathUtils.join(do_get_cwd().path, "bookmarks.json");

  await BookmarkJSONUtils.importFromFile(bookmarksFile, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();
});

add_task(async function test_export_bookmarks() {
  bookmarksExportedFile = PathUtils.join(
    PathUtils.profileDir,
    "bookmarks.exported.json"
  );
  await BookmarkJSONUtils.exportToFile(bookmarksExportedFile);
  await PlacesTestUtils.promiseAsyncUpdates();
});

add_task(async function test_import_exported_bookmarks() {
  await PlacesUtils.bookmarks.eraseEverything();
  await BookmarkJSONUtils.importFromFile(bookmarksExportedFile, {
    replace: true,
  });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();
});

add_task(async function test_import_ontop() {
  await PlacesUtils.bookmarks.eraseEverything();
  await BookmarkJSONUtils.importFromFile(bookmarksExportedFile, {
    replace: true,
  });
  await PlacesTestUtils.promiseAsyncUpdates();
  await BookmarkJSONUtils.exportToFile(bookmarksExportedFile);
  await PlacesTestUtils.promiseAsyncUpdates();
  await BookmarkJSONUtils.importFromFile(bookmarksExportedFile, {
    replace: true,
  });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();
});

add_task(async function test_import_iconuri() {
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();

  let bookmarksFile = PathUtils.join(
    do_get_cwd().path,
    "bookmarks_iconuri.json"
  );

  await BookmarkJSONUtils.importFromFile(bookmarksFile, {
    replace: true,
  });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();
});

add_task(async function test_export_bookmarks_with_iconuri() {
  bookmarksExportedFile = PathUtils.join(
    PathUtils.profileDir,
    "bookmarks.exported.json"
  );
  await BookmarkJSONUtils.exportToFile(bookmarksExportedFile);
  await PlacesTestUtils.promiseAsyncUpdates();
});

add_task(async function test_import_exported_bookmarks_with_iconuri() {
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesUtils.history.clear();
  await BookmarkJSONUtils.importFromFile(bookmarksExportedFile, {
    replace: true,
  });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();
});

add_task(async function test_clean() {
  await PlacesUtils.bookmarks.eraseEverything();
});

async function testImportedBookmarks() {
  for (let group in test_bookmarks) {
    info("[testImportedBookmarks()] Checking group '" + group + "'");

    let root = PlacesUtils.getFolderContents(
      PlacesUtils.bookmarks[`${group}Guid`]
    ).root;

    let items = test_bookmarks[group];
    Assert.equal(root.childCount, items.length);

    for (let key in items) {
      await checkItem(items[key], root.getChild(key));
    }

    root.containerOpen = false;
  }
}

async function checkItem(aExpected, aNode) {
  let bookmark = await PlacesUtils.bookmarks.fetch(aNode.bookmarkGuid);

  for (let prop in aExpected) {
    switch (prop) {
      case "type":
        Assert.equal(aNode.type, aExpected.type);
        break;
      case "title":
        Assert.equal(aNode.title, aExpected.title);
        break;
      case "dateAdded":
        Assert.equal(
          PlacesUtils.toPRTime(bookmark.dateAdded),
          aExpected.dateAdded
        );
        break;
      case "lastModified":
        Assert.equal(
          PlacesUtils.toPRTime(bookmark.lastModified),
          aExpected.lastModified
        );
        break;
      case "url":
        Assert.equal(aNode.uri, aExpected.url);
        break;
      case "icon": {
        let { dataURI: base64Icon } = await PlacesTestUtils.getFaviconForPage(
          aExpected.url
        );
        Assert.equal(base64Icon.spec, aExpected.icon);
        break;
      }
      case "keyword": {
        let entry = await PlacesUtils.keywords.fetch({ url: aNode.uri });
        Assert.equal(entry.keyword, aExpected.keyword);
        break;
      }
      case "guid":
        Assert.equal(bookmark.guid, aExpected.guid);
        break;
      case "postData": {
        let entry = await PlacesUtils.keywords.fetch({ url: aNode.uri });
        Assert.equal(entry.postData, aExpected.postData);
        break;
      }
      case "charset": {
        let pageInfo = await PlacesUtils.history.fetch(aNode.uri, {
          includeAnnotations: true,
        });
        Assert.equal(
          pageInfo.annotations.get(PlacesUtils.CHARSET_ANNO),
          aExpected.charset
        );
        break;
      }
      case "children": {
        let folder = aNode.QueryInterface(Ci.nsINavHistoryContainerResultNode);
        Assert.equal(folder.hasChildren, !!aExpected.children.length);
        folder.containerOpen = true;
        Assert.equal(folder.childCount, aExpected.children.length);

        for (let index = 0; index < aExpected.children.length; index++) {
          await checkItem(aExpected.children[index], folder.getChild(index));
        }

        folder.containerOpen = false;
        break;
      }
      case "tags": {
        let uri = Services.io.newURI(aNode.uri);
        Assert.deepEqual(
          PlacesUtils.tagging.getTagsForURI(uri),
          aExpected.tags,
          "should have the expected tags"
        );
        break;
      }
      default:
        throw new Error("Unknown property");
    }
  }
}

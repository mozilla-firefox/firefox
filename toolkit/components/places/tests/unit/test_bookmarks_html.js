/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// An object representing the contents of bookmarks.preplaces.html.
var test_bookmarks = {
  menu: [
    {
      title: "Mozilla Firefox",
      children: [
        {
          title: "Help and Tutorials",
          url: "http://en-us.www.mozilla.com/en-US/firefox/help/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          title: "Customize Firefox",
          url: "http://en-us.www.mozilla.com/en-US/firefox/customize/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          title: "Get Involved",
          url: "http://en-us.www.mozilla.com/en-US/firefox/community/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
        {
          title: "About Us",
          url: "http://en-us.www.mozilla.com/en-US/about/",
          icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
        },
      ],
    },
    {
      type: Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR,
    },
    {
      title: "test",
      dateAdded: 1177541020000000,
      lastModified: 1177541050000000,
      children: [
        {
          title: "test post keyword",
          dateAdded: 1177375336000000,
          lastModified: 1177375423000000,
          keyword: "test",
          postData: "hidden1%3Dbar&text1%3D%25s",
          charset: "ISO-8859-1",
          url: "http://test/post",
        },
      ],
    },
  ],
  toolbar: [
    {
      title: "Getting Started",
      url: "http://en-us.www.mozilla.com/en-US/firefox/central/",
      icon: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAAK/INwWK6QAAABl0RVh0U29mdHdhcmUAQWRvYmUgSW1hZ2VSZWFkeXHJZTwAAAHWSURBVHjaYvz//z8DJQAggJiQOe/fv2fv7Oz8rays/N+VkfG/iYnJfyD/1+rVq7ffu3dPFpsBAAHEAHIBCJ85c8bN2Nj4vwsDw/8zQLwKiO8CcRoQu0DxqlWrdsHUwzBAAIGJmTNnPgYa9j8UqhFElwPxf2MIDeIrKSn9FwSJoRkAEEAM0DD4DzMAyPi/G+QKY4hh5WAXGf8PDQ0FGwJ22d27CjADAAIIrLmjo+MXA9R2kAHvGBA2wwx6B8W7od6CeQcggKCmCEL8bgwxYCbUIGTDVkHDBia+CuotgACCueD3TDQN75D4xmAvCoK9ARMHBzAw0AECiBHkAlC0Mdy7x9ABNA3obAZXIAa6iKEcGlMVQHwWyjYuL2d4v2cPg8vZswx7gHyAAAK7AOif7SAbOqCmn4Ha3AHFsIDtgPq/vLz8P4MSkJ2W9h8ggBjevXvHDo4FQUQg/kdypqCg4H8lUIACnQ/SOBMYI8bAsAJFPcj1AAEEjwVQqLpAbXmH5BJjqI0gi9DTAAgDBBCcAVLkgmQ7yKCZxpCQxqUZhAECCJ4XgMl493ug21ZD+aDAXH0WLM4A9MZPXJkJIIAwTAR5pQMalaCABQUULttBGCCAGCnNzgABBgAMJ5THwGvJLAAAAABJRU5ErkJggg==",
    },
    {
      title: "Latest Headlines",
      url: "http://en-us.fxfeeds.mozilla.com/en-US/firefox/livebookmarks/",
      feedUrl: "http://en-us.fxfeeds.mozilla.com/en-US/firefox/headlines.xml",
    },
    // This will be ignored, because it has no url.
    {
      title: "Latest Headlines No Site",
      feedUrl: "http://en-us.fxfeeds.mozilla.com/en-US/firefox/headlines.xml",
      ignore: true,
    },
  ],
  unfiled: [{ title: "Example.tld", url: "http://example.tld/" }],
};

// Pre-Places bookmarks.html file pointer.
var gBookmarksFileOld;
// Places bookmarks.html file pointer.
var gBookmarksFileNew;

add_task(async function setup() {
  // File pointer to legacy bookmarks file.
  gBookmarksFileOld = PathUtils.join(
    do_get_cwd().path,
    "bookmarks.preplaces.html"
  );

  // File pointer to a new Places-exported bookmarks file.
  gBookmarksFileNew = PathUtils.join(
    PathUtils.profileDir,
    "bookmarks.exported.html"
  );
  await IOUtils.remove(gBookmarksFileNew, { ignoreAbsent: true });

  // This test must be the first one, since it setups the new bookmarks.html.
  // Test importing a pre-Places canonical bookmarks file.
  // 1. import bookmarks.preplaces.html
  // 2. run the test-suite
  // Note: we do not empty the db before this import to catch bugs like 380999
  await BookmarkHTMLUtils.importFromFile(gBookmarksFileOld, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();

  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_import_count() {
  // Ensure the bookmarks count is correct when importing in various cases
  let count = await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, {
    replace: true,
  });
  Assert.equal(
    count,
    8,
    "There should be 8 imported bookmarks when importing from an empty database"
  );
  await PlacesTestUtils.promiseAsyncUpdates();
  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();

  count = -1;
  count = await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, {
    replace: true,
  });
  Assert.equal(
    count,
    8,
    "There should be 8 imported bookmarks when replacing existing bookmarks"
  );
  await PlacesTestUtils.promiseAsyncUpdates();

  count = -1;
  count = await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew);
  Assert.equal(
    count,
    8,
    "There should be 8 imported bookmarks even when we are not replacing existing bookmarks"
  );
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_import_new() {
  // Test importing a Places bookmarks.html file.
  // 1. import bookmarks.exported.html
  // 2. run the test-suite
  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();

  await testImportedBookmarks();
  await PlacesTestUtils.promiseAsyncUpdates();

  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_emptytitle_export() {
  // Test exporting and importing with an empty-titled bookmark.
  // 1. import bookmarks
  // 2. create an empty-titled bookmark.
  // 3. export to bookmarks.exported.html
  // 4. empty bookmarks db
  // 5. import bookmarks.exported.html
  // 6. run the test-suite
  // 7. remove the empty-titled bookmark
  // 8. export to bookmarks.exported.html
  // 9. empty bookmarks db and continue

  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();

  const NOTITLE_URL = "http://notitle.mozilla.org/";
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: NOTITLE_URL,
  });
  test_bookmarks.unfiled.push({ title: "", url: NOTITLE_URL });

  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesUtils.bookmarks.eraseEverything();

  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();

  // Cleanup.
  test_bookmarks.unfiled.pop();
  // HTML imports don't restore GUIDs yet.
  let reimportedBookmark = await PlacesUtils.bookmarks.fetch({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    index: PlacesUtils.bookmarks.DEFAULT_INDEX,
  });
  Assert.equal(reimportedBookmark.url.href, bookmark.url.href);
  await PlacesUtils.bookmarks.remove(reimportedBookmark);

  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_import_chromefavicon() {
  // Test exporting and importing with a bookmark pointing to a chrome favicon.
  // 1. import bookmarks
  // 2. create a bookmark pointing to a chrome favicon.
  // 3. export to bookmarks.exported.html
  // 4. empty bookmarks db
  // 5. import bookmarks.exported.html
  // 6. run the test-suite
  // 7. remove the bookmark pointing to a chrome favicon.
  // 8. export to bookmarks.exported.html
  // 9. empty bookmarks db and continue

  const PAGE_URI = NetUtil.newURI("http://example.com/chromefavicon_page");
  const CHROME_FAVICON_URI = NetUtil.newURI(
    "chrome://global/skin/icons/delete.svg"
  );
  const CHROME_FAVICON_URI_2 = NetUtil.newURI(
    "chrome://global/skin/icons/error.svg"
  );

  info("Importing from html");
  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();

  info("Insert bookmark");
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: PAGE_URI,
    title: "Test",
  });

  info("Set favicon");
  let dataURL =
    await PlacesTestUtils.getFaviconDataURLFromNetwork(CHROME_FAVICON_URI);
  await PlacesTestUtils.setFaviconForPage(
    PAGE_URI,
    CHROME_FAVICON_URI,
    dataURL
  );

  let { dataURI: base64Icon } =
    await PlacesTestUtils.getFaviconForPage(PAGE_URI);

  test_bookmarks.unfiled.push({
    title: "Test",
    url: PAGE_URI.spec,
    icon: base64Icon.spec,
  });

  info("Export to html");
  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();

  info("Set favicon");
  // Change the favicon to check it's really imported again later.
  let dataURL_2 =
    await PlacesTestUtils.getFaviconDataURLFromNetwork(CHROME_FAVICON_URI_2);
  await PlacesTestUtils.setFaviconForPage(
    PAGE_URI,
    CHROME_FAVICON_URI_2,
    dataURL_2
  );

  info("import from html");
  await PlacesUtils.bookmarks.eraseEverything();
  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();

  info("Test imported bookmarks");
  await testImportedBookmarks();

  // Cleanup.
  test_bookmarks.unfiled.pop();
  // HTML imports don't restore GUIDs yet.
  let reimportedBookmark = await PlacesUtils.bookmarks.fetch({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    index: PlacesUtils.bookmarks.DEFAULT_INDEX,
  });
  await PlacesUtils.bookmarks.remove(reimportedBookmark);

  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test_import_ontop() {
  // Test importing the exported bookmarks.html file *on top of* the existing
  // bookmarks.
  // 1. empty bookmarks db
  // 2. import the exported bookmarks file
  // 3. export to file
  // 3. import the exported bookmarks file
  // 4. run the test-suite

  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();
  await BookmarkHTMLUtils.exportToFile(gBookmarksFileNew);
  await PlacesTestUtils.promiseAsyncUpdates();

  await BookmarkHTMLUtils.importFromFile(gBookmarksFileNew, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();
  await testImportedBookmarks();
  await PlacesTestUtils.promiseAsyncUpdates();
  await PlacesUtils.bookmarks.eraseEverything();
});

async function testImportedBookmarks() {
  for (let group in test_bookmarks) {
    info("[testImportedBookmarks()] Checking group '" + group + "'");

    let root = PlacesUtils.getFolderContents(
      PlacesUtils.bookmarks[`${group}Guid`]
    ).root;

    let items = test_bookmarks[group].filter(b => !b.ignore);
    Assert.equal(root.childCount, items.length);

    for (let key in items) {
      await checkItem(items[key], root.getChild(key));
    }

    root.containerOpen = false;
  }
}

function checkItem(aExpected, aNode) {
  return (async function () {
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
          Assert.ok(base64Icon.spec == aExpected.icon);
          break;
        }
        case "keyword": {
          let entry = await PlacesUtils.keywords.fetch({ url: aNode.uri });
          Assert.equal(entry.keyword, aExpected.keyword);
          break;
        }
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
        case "feedUrl":
          // No more supported.
          break;
        case "children": {
          let folder = aNode.QueryInterface(
            Ci.nsINavHistoryContainerResultNode
          );
          Assert.equal(folder.hasChildren, !!aExpected.children.length);
          folder.containerOpen = true;
          Assert.equal(folder.childCount, aExpected.children.length);

          for (let index = 0; index < aExpected.children.length; index++) {
            await checkItem(aExpected.children[index], folder.getChild(index));
          }

          folder.containerOpen = false;
          break;
        }
        default:
          throw new Error("Unknown property");
      }
    }
  })();
}

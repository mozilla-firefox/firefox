/*
 * This test ensures that importing/exporting to HTML does not stop
 * if a malformed uri is found.
 */

const TEST_FAVICON_PAGE_URL =
  "http://en-US.www.mozilla.com/en-US/firefox/central/";
const TEST_FAVICON_DATA_SIZE = 580;

add_task(async function test_corrupt_file() {
  // Import bookmarks from the corrupt file.
  let corruptHtml = PathUtils.join(do_get_cwd().path, "bookmarks.corrupt.html");
  await BookmarkHTMLUtils.importFromFile(corruptHtml, { replace: true });

  // Check that bookmarks that are not corrupt have been imported.
  await PlacesTestUtils.promiseAsyncUpdates();
  await database_check();
});

add_task(async function test_corrupt_database() {
  // Create corruption in the database, then export.
  let corruptBookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    url: "http://test.mozilla.org",
    title: "We love belugas",
  });
  await PlacesUtils.withConnectionWrapper("test", async function (db) {
    await db.execute("UPDATE moz_bookmarks SET fk = NULL WHERE guid = :guid", {
      guid: corruptBookmark.guid,
    });
  });

  let bookmarksFile = PathUtils.join(
    PathUtils.profileDir,
    "bookmarks.exported.html"
  );
  await IOUtils.remove(bookmarksFile, { ignoreAbsent: true });
  await BookmarkHTMLUtils.exportToFile(bookmarksFile);

  // Import again and check for correctness.
  await PlacesUtils.bookmarks.eraseEverything();
  await BookmarkHTMLUtils.importFromFile(bookmarksFile, { replace: true });
  await PlacesTestUtils.promiseAsyncUpdates();
  await database_check();
});

/*
 * Check for imported bookmarks correctness
 *
 * @return {Promise}
 * @resolves When the checks are finished.
 * @rejects Never.
 */
var database_check = async function () {
  // BOOKMARKS MENU
  let root = PlacesUtils.getFolderContents(PlacesUtils.bookmarks.menuGuid).root;
  Assert.equal(root.childCount, 2);

  let folderNode = root.getChild(1);
  Assert.equal(folderNode.type, folderNode.RESULT_TYPE_FOLDER);
  Assert.equal(folderNode.title, "test");

  let bookmark = await PlacesUtils.bookmarks.fetch({
    guid: folderNode.bookmarkGuid,
  });
  Assert.equal(PlacesUtils.toPRTime(bookmark.dateAdded), 1177541020000000);
  Assert.equal(PlacesUtils.toPRTime(bookmark.lastModified), 1177541050000000);

  // open test folder, and test the children
  PlacesUtils.asQuery(folderNode);
  Assert.equal(folderNode.hasChildren, true);
  folderNode.containerOpen = true;
  Assert.equal(folderNode.childCount, 1);

  let bookmarkNode = folderNode.getChild(0);
  Assert.equal("http://test/post", bookmarkNode.uri);
  Assert.equal("test post keyword", bookmarkNode.title);

  let entry = await PlacesUtils.keywords.fetch({ url: bookmarkNode.uri });
  Assert.equal("test", entry.keyword);
  Assert.equal("hidden1%3Dbar&text1%3D%25s", entry.postData);

  Assert.equal(bookmarkNode.dateAdded, 1177375336000000);
  Assert.equal(bookmarkNode.lastModified, 1177375423000000);

  let pageInfo = await PlacesUtils.history.fetch(bookmarkNode.uri, {
    includeAnnotations: true,
  });
  Assert.equal(
    pageInfo.annotations.get(PlacesUtils.CHARSET_ANNO),
    "ISO-8859-1",
    "Should have the correct charset"
  );

  // clean up
  folderNode.containerOpen = false;
  root.containerOpen = false;

  // BOOKMARKS TOOLBAR
  root = PlacesUtils.getFolderContents(PlacesUtils.bookmarks.toolbarGuid).root;
  Assert.equal(root.childCount, 3);

  // cleanup
  root.containerOpen = false;

  // UNFILED BOOKMARKS
  root = PlacesUtils.getFolderContents(PlacesUtils.bookmarks.unfiledGuid).root;
  Assert.equal(root.childCount, 1);
  root.containerOpen = false;

  // favicons
  let favicon = await PlacesTestUtils.getFaviconForPage(TEST_FAVICON_PAGE_URL);
  // aURI should never be null when rawData.length > 0.
  Assert.notEqual(favicon.uri, null);
  // Favicon data is stored in the bookmarks file as a "data:" URI.  For
  // simplicity, instead of converting the data we receive to a "data:" URI
  // and comparing it, we just check the data size.
  Assert.equal(TEST_FAVICON_DATA_SIZE, favicon.rawData.length);
};

/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

var gAccumulator = {
  get callback() {
    this.results = [];
    return result => this.results.push(result);
  },
};

add_task(async function invalid_input_throws() {
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch(),
    /Input should be a valid object/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch(null),
    /Input should be a valid object/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guid: "123456789012", index: 0 }),
    /The following properties were expected: parentGuid/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({}),
    /Unexpected number of conditions provided: 0/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    }),
    /Unexpected number of conditions provided: 0/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({
      guid: "123456789012",
      parentGuid: "012345678901",
      index: 0,
    }),
    /Unexpected number of conditions provided: 2/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({
      guid: "123456789012",
      url: "http://example.com",
    }),
    /Unexpected number of conditions provided: 2/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch("test"),
    /Invalid value for property 'guid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch(123),
    /Invalid value for property 'guid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guid: "test" }),
    /Invalid value for property 'guid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guid: null }),
    /Invalid value for property 'guid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guid: 123 }),
    /Invalid value for property 'guid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guidPrefix: "" }),
    /Invalid value for property 'guidPrefix'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guidPrefix: null }),
    /Invalid value for property 'guidPrefix'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guidPrefix: 123 }),
    /Invalid value for property 'guidPrefix'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guidPrefix: "123456789012" }),
    /Invalid value for property 'guidPrefix'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ guidPrefix: "@" }),
    /Invalid value for property 'guidPrefix'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ parentGuid: "test", index: 0 }),
    /Invalid value for property 'parentGuid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ parentGuid: null, index: 0 }),
    /Invalid value for property 'parentGuid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ parentGuid: 123, index: 0 }),
    /Invalid value for property 'parentGuid'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ parentGuid: "123456789012", index: "0" }),
    /Invalid value for property 'index'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ parentGuid: "123456789012", index: null }),
    /Invalid value for property 'index'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ parentGuid: "123456789012", index: -10 }),
    /Invalid value for property 'index'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ url: "http://te st/" }),
    /Invalid value for property 'url'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ url: null }),
    /Invalid value for property 'url'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch({ url: -10 }),
    /Invalid value for property 'url'/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch("123456789012", "test"),
    /onResult callback must be a valid function/
  );
  await Assert.rejects(
    PlacesUtils.bookmarks.fetch("123456789012", {}),
    /onResult callback must be a valid function/
  );
});

add_task(async function fetch_nonexistent_guid() {
  let bm = await PlacesUtils.bookmarks.fetch(
    { guid: "123456789012" },
    gAccumulator.callback
  );
  Assert.equal(bm, null);
  Assert.equal(gAccumulator.results.length, 0);
});

add_task(async function fetch_bookmark() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    url: "http://example.com/",
    title: "a bookmark",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(bm1.guid, gAccumulator.callback);
  checkBookmarkObject(bm2);
  Assert.equal(gAccumulator.results.length, 1);
  checkBookmarkObject(gAccumulator.results[0]);
  Assert.deepEqual(gAccumulator.results[0], bm1);

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.parentGuid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(bm2.index, 0);
  Assert.deepEqual(bm2.dateAdded, bm2.lastModified);
  Assert.equal(bm2.type, PlacesUtils.bookmarks.TYPE_BOOKMARK);
  Assert.equal(bm2.url.href, "http://example.com/");
  Assert.equal(bm2.title, "a bookmark");
  Assert.strictEqual(bm2.childCount, undefined);

  await PlacesUtils.bookmarks.remove(bm1.guid);
});

add_task(async function fetch_bookmar_empty_title() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    url: "http://example.com/",
    title: "",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(bm1.guid);
  checkBookmarkObject(bm2);

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.index, 0);
  Assert.strictEqual(bm2.title, "");

  await PlacesUtils.bookmarks.remove(bm1.guid);
});

add_task(async function fetch_folder() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "a folder",
  });
  checkBookmarkObject(bm1);
  Assert.deepEqual(bm1.dateAdded, bm1.lastModified);

  // Inserting a child updates both the childCount and lastModified of bm1,
  // though the bm1 object is static once fetched, thus later we'll manually
  // update it.
  await PlacesUtils.bookmarks.insert({
    parentGuid: bm1.guid,
    url: "https://www.mozilla.org/",
    title: "",
  });

  let bm2 = await PlacesUtils.bookmarks.fetch(bm1.guid);
  checkBookmarkObject(bm2);

  Assert.equal(bm2.childCount, 1);
  bm1.childCount = bm2.childCount;
  bm1.lastModified = bm2.lastModified;

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.parentGuid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(bm2.index, 0);
  Assert.equal(bm2.type, PlacesUtils.bookmarks.TYPE_FOLDER);
  Assert.equal(bm2.title, "a folder");
  Assert.ok(!("url" in bm2));

  await PlacesUtils.bookmarks.remove(bm1.guid);
});

add_task(async function fetch_folder_empty_title() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(bm1.guid);
  checkBookmarkObject(bm2);

  Assert.equal(bm2.childCount, 0);
  // Insert doesn't populate childCount (it would always be 0 anyway), so set
  // it to be able to just use deepEqual.
  bm1.childCount = bm2.childCount;

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.index, 0);
  Assert.strictEqual(bm2.title, "");

  await PlacesUtils.bookmarks.remove(bm1.guid);
});

add_task(async function fetch_separator() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_SEPARATOR,
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(bm1.guid);
  checkBookmarkObject(bm2);

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.parentGuid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(bm2.index, 0);
  Assert.deepEqual(bm2.dateAdded, bm2.lastModified);
  Assert.equal(bm2.type, PlacesUtils.bookmarks.TYPE_SEPARATOR);
  Assert.ok(!("url" in bm2));
  Assert.strictEqual(bm2.title, "");

  await PlacesUtils.bookmarks.remove(bm1.guid);
});

add_task(async function fetch_byguid_prefix() {
  const PREFIX = "PREFIX-";

  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    guid: PlacesUtils.generateGuidWithPrefix(PREFIX),
    url: "http://bm1.example.com/",
    title: "bookmark 1",
  });
  checkBookmarkObject(bm1);
  Assert.ok(bm1.guid.startsWith(PREFIX));

  let bm2 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    guid: PlacesUtils.generateGuidWithPrefix(PREFIX),
    url: "http://bm2.example.com/",
    title: "bookmark 2",
  });
  checkBookmarkObject(bm2);
  Assert.ok(bm2.guid.startsWith(PREFIX));

  let bm3 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    guid: PlacesUtils.generateGuidWithPrefix(PREFIX),
    title: "a folder",
  });
  await PlacesUtils.bookmarks.insert({
    parentGuid: bm3.guid,
    url: "https://www.mozilla.org/",
    title: "",
  });
  checkBookmarkObject(bm3);
  Assert.ok(bm3.guid.startsWith(PREFIX));

  // Bookmark 4 has the same guid prefix in a different case, so it shouldn't
  // be returned in the results.
  let bm4 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    guid: PlacesUtils.generateGuidWithPrefix(PREFIX.toLowerCase()),
    url: "http://bm3.example.com/",
    title: "bookmark 4",
  });
  checkBookmarkObject(bm4);
  Assert.ok(!bm4.guid.startsWith(PREFIX));

  await PlacesUtils.bookmarks.fetch(
    { guidPrefix: PREFIX },
    gAccumulator.callback
  );

  Assert.equal(gAccumulator.results.length, 3);

  // The results are returned by most recent first, so the first bookmark
  // inserted is the last one in the returned array.
  Assert.deepEqual(bm1, gAccumulator.results[2]);
  Assert.deepEqual(bm2, gAccumulator.results[1]);
  Assert.equal(gAccumulator.results[0].childCount, 1);
  bm3.childCount = gAccumulator.results[0].childCount;
  bm3.lastModified = gAccumulator.results[0].lastModified;
  Assert.deepEqual(bm3, gAccumulator.results[0]);

  Assert.equal(bm3.childCount, 1);

  await PlacesUtils.bookmarks.remove(bm1);
  await PlacesUtils.bookmarks.remove(bm2);
  await PlacesUtils.bookmarks.remove(bm3);
  await PlacesUtils.bookmarks.remove(bm4);
});

add_task(async function fetch_byposition_nonexisting_parentGuid() {
  let bm = await PlacesUtils.bookmarks.fetch(
    { parentGuid: "123456789012", index: 0 },
    gAccumulator.callback
  );
  Assert.equal(bm, null);
  Assert.equal(gAccumulator.results.length, 0);
});

add_task(async function fetch_byposition_nonexisting_index() {
  let bm = await PlacesUtils.bookmarks.fetch(
    { parentGuid: PlacesUtils.bookmarks.unfiledGuid, index: 100 },
    gAccumulator.callback
  );
  Assert.equal(bm, null);
  Assert.equal(gAccumulator.results.length, 0);
});

add_task(async function fetch_byposition() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    url: "http://example.com/",
    title: "a bookmark",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(
    { parentGuid: bm1.parentGuid, index: bm1.index },
    gAccumulator.callback
  );
  checkBookmarkObject(bm2);
  Assert.equal(gAccumulator.results.length, 1);
  checkBookmarkObject(gAccumulator.results[0]);
  Assert.deepEqual(gAccumulator.results[0], bm1);

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.parentGuid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(bm2.index, 0);
  Assert.deepEqual(bm2.dateAdded, bm2.lastModified);
  Assert.equal(bm2.type, PlacesUtils.bookmarks.TYPE_BOOKMARK);
  Assert.equal(bm2.url.href, "http://example.com/");
  Assert.equal(bm2.title, "a bookmark");
});

add_task(async function fetch_byposition_default_index() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    url: "http://example.com/last",
    title: "last child",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(
    { parentGuid: bm1.parentGuid, index: PlacesUtils.bookmarks.DEFAULT_INDEX },
    gAccumulator.callback
  );
  checkBookmarkObject(bm2);
  Assert.equal(gAccumulator.results.length, 1);
  checkBookmarkObject(gAccumulator.results[0]);
  Assert.deepEqual(gAccumulator.results[0], bm1);

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.parentGuid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(bm2.index, 1);
  Assert.deepEqual(bm2.dateAdded, bm2.lastModified);
  Assert.equal(bm2.type, PlacesUtils.bookmarks.TYPE_BOOKMARK);
  Assert.equal(bm2.url.href, "http://example.com/last");
  Assert.equal(bm2.title, "last child");

  await PlacesUtils.bookmarks.remove(bm1.guid);
});

add_task(async function fetch_byurl_nonexisting() {
  let bm = await PlacesUtils.bookmarks.fetch(
    { url: "http://nonexisting.com/" },
    gAccumulator.callback
  );
  Assert.equal(bm, null);
  Assert.equal(gAccumulator.results.length, 0);
});

add_task(async function fetch_byurl() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    url: "http://byurl.com/",
    title: "a bookmark",
  });
  checkBookmarkObject(bm1);

  // Also ensure that fecth-by-url excludes the tags folder.
  PlacesUtils.tagging.tagURI(bm1.url.URI, ["Test Tag"]);

  let bm2 = await PlacesUtils.bookmarks.fetch(
    { url: bm1.url },
    gAccumulator.callback
  );
  checkBookmarkObject(bm2);
  Assert.equal(gAccumulator.results.length, 1);
  checkBookmarkObject(gAccumulator.results[0]);
  Assert.deepEqual(gAccumulator.results[0], bm1);

  Assert.deepEqual(bm1, bm2);
  Assert.equal(bm2.parentGuid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.deepEqual(bm2.dateAdded, bm2.lastModified);
  Assert.equal(bm2.type, PlacesUtils.bookmarks.TYPE_BOOKMARK);
  Assert.equal(bm2.url.href, "http://byurl.com/");
  Assert.equal(bm2.title, "a bookmark");

  let bm3 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
    url: "http://byurl.com/",
    title: "a bookmark",
  });
  let bm4 = await PlacesUtils.bookmarks.fetch(
    { url: bm1.url },
    gAccumulator.callback
  );
  checkBookmarkObject(bm4);
  Assert.deepEqual(bm3, bm4);
  Assert.equal(gAccumulator.results.length, 2);
  gAccumulator.results.forEach(checkBookmarkObject);
  Assert.deepEqual(gAccumulator.results[0], bm4);

  // After an update the returned bookmark should change.
  await PlacesUtils.bookmarks.update({ guid: bm1.guid, title: "new title" });
  let bm5 = await PlacesUtils.bookmarks.fetch(
    { url: bm1.url },
    gAccumulator.callback
  );
  checkBookmarkObject(bm5);
  // Cannot use deepEqual cause lastModified changed.
  Assert.equal(bm1.guid, bm5.guid);
  Assert.greater(bm5.lastModified, bm1.lastModified);
  Assert.equal(gAccumulator.results.length, 2);
  gAccumulator.results.forEach(checkBookmarkObject);
  Assert.deepEqual(gAccumulator.results[0], bm5);

  // cleanup
  PlacesUtils.tagging.untagURI(bm1.url.URI, ["Test Tag"]);
});

add_task(async function fetch_concurrent() {
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "http://concurrent.url.com/",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(
    { url: bm1.url },
    gAccumulator.callback,
    { concurrent: true }
  );
  checkBookmarkObject(bm2);
  Assert.equal(gAccumulator.results.length, 1);
  Assert.deepEqual(gAccumulator.results[0], bm1);
  Assert.deepEqual(bm1, bm2);
  let bm3 = await PlacesUtils.bookmarks.fetch(
    { url: bm1.url },
    gAccumulator.callback,
    { concurrent: false }
  );
  checkBookmarkObject(bm3);
  Assert.equal(gAccumulator.results.length, 1);
  Assert.deepEqual(gAccumulator.results[0], bm1);
  Assert.deepEqual(bm1, bm3);
  let bm4 = await PlacesUtils.bookmarks.fetch(
    { url: bm1.url },
    gAccumulator.callback,
    {}
  );
  checkBookmarkObject(bm4);
  Assert.equal(gAccumulator.results.length, 1);
  Assert.deepEqual(gAccumulator.results[0], bm1);
  Assert.deepEqual(bm1, bm4);
});

add_task(async function fetch_by_parent() {
  let folder1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "a folder",
  });
  checkBookmarkObject(folder1);

  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder1.guid,
    url: "http://bm1.example.com/",
    title: "bookmark 1",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder1.guid,
    url: "http://bm2.example.com/",
    title: "bookmark 2",
  });
  checkBookmarkObject(bm2);

  let bm3 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder1.guid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "sub folder",
    index: 0,
  });
  checkBookmarkObject(bm2);
  await PlacesUtils.bookmarks.insert({
    parentGuid: bm3.guid,
    url: "http://mozilla.org/",
    title: "sub bookmark",
  });

  await PlacesUtils.bookmarks.fetch(
    { parentGuid: folder1.guid },
    gAccumulator.callback
  );

  Assert.equal(gAccumulator.results.length, 3);

  Assert.equal(gAccumulator.results[0].childCount, 1);
  bm3.childCount = gAccumulator.results[0].childCount;
  bm3.lastModified = gAccumulator.results[0].lastModified;
  Assert.deepEqual(bm3, gAccumulator.results[0]);
  Assert.equal(bm1.url.href, gAccumulator.results[1].url.href);
  Assert.equal(bm2.url.href, gAccumulator.results[2].url.href);

  await PlacesUtils.bookmarks.remove(folder1);
});

add_task(async function fetch_with_bookmark_path() {
  let folder1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Parent",
  });
  checkBookmarkObject(folder1);

  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder1.guid,
    url: "http://bookmarkpath.example.com/",
    title: "Child Bookmark",
  });
  checkBookmarkObject(bm1);

  let bm2 = await PlacesUtils.bookmarks.fetch(
    { guid: bm1.guid },
    gAccumulator.callback,
    { includePath: true }
  );
  checkBookmarkObject(bm2);
  Assert.equal(gAccumulator.results.length, 1);
  Assert.equal(bm2.path.length, 2);
  Assert.equal(bm2.path[0].guid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(bm2.path[0].title, "unfiled");
  Assert.equal(bm2.path[1].guid, folder1.guid);
  Assert.equal(bm2.path[1].title, folder1.title);

  await PlacesUtils.bookmarks.remove(folder1);
});

add_task(async function fetch_with_bookmark_path_multiple_bookmarks() {
  const TEST_URL = "http://bookmarkpath.example.com/multi";

  let folder1 = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Parent",
  });
  let folder2 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder1.guid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Nested Parent",
  });

  // Two bookmarks sharing the same url but living at different depths and
  // under different parents, to verify that paths are not mixed up when
  // multiple starting guids are resolved by the same query.
  let bm1 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder1.guid,
    url: TEST_URL,
    title: "Shallow Bookmark",
  });
  let bm2 = await PlacesUtils.bookmarks.insert({
    parentGuid: folder2.guid,
    url: TEST_URL,
    title: "Deep Bookmark",
  });

  await PlacesUtils.bookmarks.fetch({ url: TEST_URL }, gAccumulator.callback, {
    includePath: true,
  });

  Assert.equal(gAccumulator.results.length, 2);

  let resultsByGuid = new Map(gAccumulator.results.map(r => [r.guid, r]));
  let result1 = resultsByGuid.get(bm1.guid);
  let result2 = resultsByGuid.get(bm2.guid);

  Assert.equal(result1.path.length, 2);
  Assert.equal(result1.path[0].guid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(result1.path[1].guid, folder1.guid);
  Assert.equal(result1.path[1].title, folder1.title);

  Assert.equal(result2.path.length, 3);
  Assert.equal(result2.path[0].guid, PlacesUtils.bookmarks.unfiledGuid);
  Assert.equal(result2.path[1].guid, folder1.guid);
  Assert.equal(result2.path[1].title, folder1.title);
  Assert.equal(result2.path[2].guid, folder2.guid);
  Assert.equal(result2.path[2].title, folder2.title);

  await PlacesUtils.bookmarks.remove(folder1);
});

add_task(async function fetch_with_bookmark_path_of_root() {
  let root = await PlacesUtils.bookmarks.fetch(
    { guid: PlacesUtils.bookmarks.unfiledGuid },
    gAccumulator.callback,
    { includePath: true }
  );

  Assert.deepEqual(root.path, []);
});

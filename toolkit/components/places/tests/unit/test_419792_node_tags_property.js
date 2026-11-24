/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// get services
var histsvc = Cc["@mozilla.org/browser/nav-history-service;1"].getService(
  Ci.nsINavHistoryService
);
var tagssvc = Cc["@mozilla.org/browser/tagging-service;1"].getService(
  Ci.nsITaggingService
);

add_task(async function test_query_node_tags_property() {
  // get toolbar node
  var options = histsvc.getNewQueryOptions();
  var query = histsvc.getNewQuery();
  query.setParents([PlacesUtils.bookmarks.toolbarGuid]);
  var result = histsvc.executeQuery(query, options);
  var toolbarNode = result.root;
  toolbarNode.containerOpen = true;

  // add a bookmark
  var bookmarkURI = uri("http://foo.com");
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "",
    url: bookmarkURI,
  });

  // get the node for the new bookmark
  var node = toolbarNode.getChild(toolbarNode.childCount - 1);
  Assert.equal(node.bookmarkGuid, bookmark.guid);

  // confirm there's no tags via the .tags property
  Assert.equal(node.tags, null);

  // add a tag
  tagssvc.tagURI(bookmarkURI, ["foo"]);
  Assert.equal(node.tags, "foo");

  // add another tag, to test delimiter and sorting
  tagssvc.tagURI(bookmarkURI, ["bar"]);
  Assert.equal(node.tags, "bar,foo");

  // remove the tags, confirming the property is cleared
  tagssvc.untagURI(bookmarkURI, null);
  Assert.equal(node.tags, null);

  toolbarNode.containerOpen = false;
});

add_task(async function test_folder_children_tags() {
  let folder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "folder",
  });
  let taggedURL = Services.io.newURI("http://tagged.example.com/");
  await PlacesUtils.bookmarks.insertTree({
    guid: folder.guid,
    children: [
      { url: taggedURL },
      { url: "http://untagged.example.com/" },
      { type: PlacesUtils.bookmarks.TYPE_SEPARATOR },
      { type: PlacesUtils.bookmarks.TYPE_FOLDER, title: "subfolder" },
    ],
  });
  // Bookmark the tagged page elsewhere too, it must not affect its tags.
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    url: taggedURL,
  });
  PlacesUtils.tagging.tagURI(taggedURL, ["foo", "bar"]);

  let root = PlacesUtils.getFolderContents(folder.guid).root;
  Assert.deepEqual(
    Array.from({ length: root.childCount }, (_, i) => root.getChild(i).tags),
    ["bar,foo", null, null, null],
    "Check tags of the folder children"
  );
  root.containerOpen = false;

  root = PlacesUtils.getFolderContents(folder.guid, true).root;
  Assert.deepEqual(
    Array.from({ length: root.childCount }, (_, i) => root.getChild(i).tags),
    [null],
    "Check tags of the folder children with excludeItems"
  );
  root.containerOpen = false;

  await PlacesUtils.bookmarks.eraseEverything();
});

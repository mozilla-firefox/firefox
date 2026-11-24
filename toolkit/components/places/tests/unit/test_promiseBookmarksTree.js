/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

async function check_has_child(aParentGuid, aChildGuid) {
  let parentTree = await PlacesUtils.promiseBookmarksTree(aParentGuid);
  Assert.ok("children" in parentTree);
  Assert.notEqual(
    parentTree.children.find(e => e.guid == aChildGuid),
    null
  );
}

async function compareToNode(aItem, aNode, aIsRootItem, aExcludedGuids = []) {
  // itemId==-1 indicates a non-bookmark node, which is unexpected.
  Assert.notEqual(aNode.itemId, -1);

  function check_unset(...aProps) {
    for (let p of aProps) {
      if (p in aItem) {
        Assert.ok(false, `Unexpected property ${p} with value ${aItem[p]}`);
      }
    }
  }

  function compare_prop(aItemProp, aNodeProp = aItemProp, aOptional = false) {
    if (aOptional && aNode[aNodeProp] === null) {
      check_unset(aItemProp);
    } else {
      Assert.strictEqual(aItem[aItemProp], aNode[aNodeProp]);
    }
  }

  // Bug 1013053 - bookmarkIndex is unavailable for the query's root
  if (aNode.bookmarkIndex == -1) {
    let bookmark = await PlacesUtils.bookmarks.fetch(aNode.bookmarkGuid);
    Assert.strictEqual(aItem.index, bookmark.index);
  } else {
    compare_prop("index", "bookmarkIndex");
  }

  compare_prop("dateAdded");
  compare_prop("lastModified");

  if (aIsRootItem && aNode.bookmarkGuid != PlacesUtils.bookmarks.rootGuid) {
    Assert.ok("parentGuid" in aItem);
    await check_has_child(aItem.parentGuid, aItem.guid);
  } else {
    check_unset("parentGuid");
  }

  const BOOKMARK_ONLY_PROPS = ["uri", "iconUri", "tags", "charset", "keyword"];
  const FOLDER_ONLY_PROPS = ["children", "root"];

  let nodesCount = 1;

  switch (aNode.type) {
    case Ci.nsINavHistoryResultNode.RESULT_TYPE_FOLDER: {
      Assert.equal(aItem.type, PlacesUtils.TYPE_X_MOZ_PLACE_CONTAINER);
      Assert.equal(aItem.typeCode, PlacesUtils.bookmarks.TYPE_FOLDER);
      compare_prop("title", "title", true);
      check_unset(...BOOKMARK_ONLY_PROPS);

      let expectedChildrenNodes = [];

      PlacesUtils.asContainer(aNode);
      if (!aNode.containerOpen) {
        aNode.containerOpen = true;
      }

      for (let i = 0; i < aNode.childCount; i++) {
        let childNode = aNode.getChild(i);
        if (
          childNode.itemId == PlacesUtils.tagsFolderId ||
          aExcludedGuids.includes(childNode.bookmarkGuid)
        ) {
          continue;
        }
        expectedChildrenNodes.push(childNode);
      }

      if (expectedChildrenNodes.length) {
        Assert.ok(Array.isArray(aItem.children));
        Assert.equal(aItem.children.length, expectedChildrenNodes.length);
        for (let i = 0; i < aItem.children.length; i++) {
          nodesCount += await compareToNode(
            aItem.children[i],
            expectedChildrenNodes[i],
            false,
            aExcludedGuids
          );
        }
      } else {
        check_unset("children");
      }

      let rootName = mapItemGuidToInternalRootName(aItem.guid);
      if (rootName) {
        Assert.equal(aItem.root, rootName);
      } else {
        check_unset("root");
      }
      break;
    }
    case Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR:
      Assert.equal(aItem.type, PlacesUtils.TYPE_X_MOZ_PLACE_SEPARATOR);
      Assert.equal(aItem.typeCode, PlacesUtils.bookmarks.TYPE_SEPARATOR);
      check_unset(...BOOKMARK_ONLY_PROPS, ...FOLDER_ONLY_PROPS);
      break;
    default: {
      Assert.equal(aItem.type, PlacesUtils.TYPE_X_MOZ_PLACE);
      Assert.equal(aItem.typeCode, PlacesUtils.bookmarks.TYPE_BOOKMARK);
      compare_prop("uri");
      // node.tags's format is "a, b" whilst promiseBoookmarksTree is "a,b"
      if (aNode.tags === null) {
        check_unset("tags");
      } else {
        Assert.equal(aItem.tags, aNode.tags.replace(/, /g, ","));
      }

      if (aNode.icon) {
        try {
          await compareFavicons(aNode.icon, aItem.iconUri);
        } catch (ex) {
          info(ex);
          todo_check_true(false);
        }
      } else {
        check_unset(aItem.iconUri);
      }

      check_unset(...FOLDER_ONLY_PROPS);

      let pageInfo = await PlacesUtils.history.fetch(aNode.uri, {
        includeAnnotations: true,
      });
      let expectedCharset = pageInfo.annotations.get(PlacesUtils.CHARSET_ANNO);
      if (expectedCharset) {
        Assert.equal(aItem.charset, expectedCharset);
      } else {
        check_unset("charset");
      }

      let entry = await PlacesUtils.keywords.fetch({ url: aNode.uri });
      if (entry) {
        Assert.equal(aItem.keyword, entry.keyword);
      } else {
        check_unset("keyword");
      }

      if ("title" in aItem) {
        compare_prop("title");
      } else {
        Assert.equal(null, aNode.title);
      }
    }
  }

  if (aIsRootItem) {
    Assert.strictEqual(aItem.itemsCount, nodesCount);
  }

  return nodesCount;
}

var itemsCount = 0;
async function new_bookmark(aInfo) {
  ++itemsCount;
  if (!("url" in aInfo)) {
    aInfo.url = uri("http://test.item.y" + itemsCount);
  }

  if (!("title" in aInfo)) {
    aInfo.title = "Test Item (bookmark) " + itemsCount;
  }

  await PlacesTransactions.NewBookmark(aInfo).transact();
}

function new_folder(aInfo) {
  if (!("title" in aInfo)) {
    aInfo.title = "Test Item (folder) " + itemsCount;
  }
  return PlacesTransactions.NewFolder(aInfo).transact();
}

// Walks a result nodes tree and test promiseBookmarksTree for each node.
// DO NOT COPY THIS LOGIC:  It is done here to accomplish a more comprehensive
// test of the API (the entire hierarchy data is available in the very test).
async function test_promiseBookmarksTreeForEachNode(
  aNode,
  aOptions,
  aExcludedGuids
) {
  Assert.ok(aNode.bookmarkGuid && !!aNode.bookmarkGuid.length);
  let item = await PlacesUtils.promiseBookmarksTree(
    aNode.bookmarkGuid,
    aOptions
  );
  await compareToNode(item, aNode, true, aExcludedGuids);

  if (!PlacesUtils.nodeIsContainer(aNode)) {
    return item;
  }

  for (let i = 0; i < aNode.childCount; i++) {
    let child = aNode.getChild(i);
    if (child.itemId != PlacesUtils.tagsFolderId) {
      await test_promiseBookmarksTreeForEachNode(
        child,
        { includeItemIds: true, includeIcons: true },
        aExcludedGuids
      );
    }
  }
  return item;
}

async function test_promiseBookmarksTreeAgainstResult(
  aItemGuid = PlacesUtils.bookmarks.rootGuid,
  aOptions = { includeItemIds: true, includeIcons: true },
  aExcludedGuids
) {
  let node = PlacesUtils.getFolderContents(aItemGuid).root;
  return test_promiseBookmarksTreeForEachNode(node, aOptions, aExcludedGuids);
}

add_task(async function () {
  // Add some bookmarks to cover various use cases.
  await new_bookmark({ parentGuid: PlacesUtils.bookmarks.toolbarGuid });
  await new_folder({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    annotations: [
      { name: "TestAnnoA", value: "TestVal" },
      { name: "TestAnnoB", value: 0 },
    ],
  });
  let sepInfo = { parentGuid: PlacesUtils.bookmarks.menuGuid };
  await PlacesTransactions.NewSeparator(sepInfo).transact();
  let folderGuid = await new_folder({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
  });
  await new_bookmark({
    title: null,
    parentGuid: folderGuid,
    keyword: "test_keyword",
    tags: ["TestTagA", "TestTagB"],
    annotations: [{ name: "TestAnnoA", value: "TestVal2" }],
  });
  let urlWithCharsetAndFavicon = uri("http://charset.and.favicon");
  await new_bookmark({ parentGuid: folderGuid, url: urlWithCharsetAndFavicon });
  await PlacesUtils.history.update({
    url: urlWithCharsetAndFavicon,
    annotations: new Map([[PlacesUtils.CHARSET_ANNO, "UTF-16"]]),
  });
  await PlacesTestUtils.setFaviconForPage(
    urlWithCharsetAndFavicon,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );
  // Test the default places root without specifying it.
  await test_promiseBookmarksTreeAgainstResult();

  // Do specify it
  await test_promiseBookmarksTreeAgainstResult(PlacesUtils.bookmarks.rootGuid);

  // Exclude the bookmarks menu.
  // The calllback should be four times - once for the toolbar, once for
  // the bookmark we inserted under, and once for the menu (and not
  // at all for any of its descendants) and once for the unsorted bookmarks
  // folder.  However, promiseBookmarksTree is called multiple times, so
  // rather than counting the calls, we count the number of unique items
  // passed in.
  let guidsPassedToExcludeCallback = new Set();
  let placesRootWithoutTheMenu = await test_promiseBookmarksTreeAgainstResult(
    PlacesUtils.bookmarks.rootGuid,
    {
      excludeItemsCallback: aItem => {
        guidsPassedToExcludeCallback.add(aItem.guid);
        return aItem.root == "bookmarksMenuFolder";
      },
      includeItemIds: true,
      includeIcons: true,
    },
    [PlacesUtils.bookmarks.menuGuid]
  );
  Assert.equal(guidsPassedToExcludeCallback.size, 5);
  Assert.equal(placesRootWithoutTheMenu.children.length, 3);

  let findIconBookmark = tree =>
    tree.children.find(c => c.uri == urlWithCharsetAndFavicon.spec);
  let tree = await PlacesUtils.promiseBookmarksTree(folderGuid);
  Assert.ok(
    !("iconUri" in findIconBookmark(tree)),
    "Icons are not fetched by default"
  );
  tree = await PlacesUtils.promiseBookmarksTree(folderGuid, {
    includeIcons: true,
  });
  Assert.ok(
    "iconUri" in findIconBookmark(tree),
    "Icons are fetched when requested"
  );
});

/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let gMainFolder;

add_setup(async function () {
  // Ensure default bookmarks don't influence the test.
  let placesInitCompleteObserved = TestUtils.topicObserved(
    "places-browser-init-complete"
  );
  Cc["@mozilla.org/browser/browserglue;1"]
    .getService(Ci.nsIObserver)
    .observe(null, "browser-glue-test", "places-browser-init-complete");
  await placesInitCompleteObserved;

  await PlacesUtils.bookmarks.eraseEverything();

  registerCleanupFunction(async function () {
    await PlacesUtils.bookmarks.eraseEverything();
  });

  gMainFolder = await PlacesUtils.bookmarks.insert({
    title: "mainFolder",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
  });

  info("Create tree of: folderA => subFolderA => 3 bookmarkItems");
  await PlacesUtils.bookmarks.insertTree({
    guid: gMainFolder.guid,
    children: [
      {
        title: "FolderA",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        children: [
          {
            title: "subFolderA",
            type: PlacesUtils.bookmarks.TYPE_FOLDER,
            children: [
              {
                title: "firstBM",
                url: "https://example.com/1",
              },
              {
                title: "secondBM",
                url: "https://example.com/2",
              },
              {
                title: "thirdBM",
                url: "https://example.com/3",
              },
            ],
          },
        ],
      },
    ],
  });

  if (AppConstants.platform === "win") {
    if (Services.env.get("MOZ_AUTOMATION")) {
      // When running in CI, pre-emptively kill Windows Explorer, using system
      // from the standard library, since it sometimes holds the clipboard for
      // long periods, thereby breaking the test (bug 1921759).
      const { ctypes } = ChromeUtils.importESModule(
        "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
      );
      let libc = ctypes.open("ucrtbase.dll");
      let exec = libc.declare(
        "system",
        ctypes.default_abi,
        ctypes.int,
        ctypes.char.ptr
      );
      let rv = exec(
        '"powershell -command "&{&Stop-Process -ProcessName explorer}"'
      );
      libc.close();
      is(rv, 0, "Launched powershell to stop explorer.exe");
    } else {
      info(
        "Skipping terminating Windows Explorer since we are not running in automation"
      );
    }
  }
});

add_task(async function test_sidebar() {
  await withSidebarTree("bookmarks", async function (tree) {
    const selectedNodeComparator = {
      equalTitle: itemNode => {
        Assert.equal(
          tree.selectedNode.title,
          itemNode.title,
          "Select expected title"
        );
      },
      equalNode: itemNode => {
        Assert.equal(
          tree.selectedNode.bookmarkGuid,
          itemNode.guid,
          "Selected the expected node"
        );
      },
      equalType: itemType => {
        Assert.equal(tree.selectedNode.type, itemType, "Correct type");
      },

      equalChildCount: childrenAmount => {
        Assert.equal(
          tree.selectedNode.childCount,
          childrenAmount,
          `${childrenAmount} children`
        );
      },
    };
    let urlType = Ci.nsINavHistoryResultNode.RESULT_TYPE_URI;

    info("Sanity check folderA, subFolderA, bookmarkItems");
    tree.selectItems([gMainFolder.guid]);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalTitle(gMainFolder);
    selectedNodeComparator.equalChildCount(1);

    let sourceFolder = tree.selectedNode.getChild(0);
    tree.selectNode(sourceFolder);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalTitle(sourceFolder);
    selectedNodeComparator.equalChildCount(1);

    let subSourceFolder = tree.selectedNode.getChild(0);
    tree.selectNode(subSourceFolder);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalTitle(subSourceFolder);
    selectedNodeComparator.equalChildCount(3);

    let bm_2 = tree.selectedNode.getChild(1);
    tree.selectNode(bm_2);
    selectedNodeComparator.equalTitle(bm_2);

    info(
      "Copy folder tree from sourceFolder (folderA, subFolderA, bookmarkItems)"
    );
    tree.selectNode(sourceFolder);
    await promiseClipboard(() => {
      tree.controller.copy();
    }, PlacesUtils.TYPE_X_MOZ_PLACE);

    tree.selectItems([gMainFolder.guid]);

    info("Paste copy of folderA");
    await tree.controller.paste();

    info("Sanity check copy/paste operation - mainFolder has 2 children");
    tree.selectItems([gMainFolder.guid]);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalChildCount(2);

    info("Sanity check copy of folderA");
    let copySourceFolder = tree.selectedNode.getChild(1);
    tree.selectNode(copySourceFolder);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalTitle(copySourceFolder);
    selectedNodeComparator.equalChildCount(1);

    info("Sanity check copy subFolderA");
    let copySubSourceFolder = tree.selectedNode.getChild(0);
    tree.selectNode(copySubSourceFolder);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalTitle(copySubSourceFolder);
    selectedNodeComparator.equalChildCount(3);

    info("Sanity check copy BookmarkItem");
    let copyBm_1 = tree.selectedNode.getChild(0);
    tree.selectNode(copyBm_1);
    selectedNodeComparator.equalTitle(copyBm_1);

    info("Undo copy operation");
    await PlacesTransactions.undo();
    tree.selectItems([gMainFolder.guid]);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;

    info("Sanity check undo operation - mainFolder has 1 child");
    selectedNodeComparator.equalChildCount(1);

    info("Redo copy operation");
    await PlacesTransactions.redo();
    tree.selectItems([gMainFolder.guid]);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;

    info("Sanity check redo operation - mainFolder has 2 children");
    selectedNodeComparator.equalChildCount(2);

    info("Sanity check copy of folderA");
    copySourceFolder = tree.selectedNode.getChild(1);
    tree.selectNode(copySourceFolder);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalChildCount(1);

    info("Sanity check copy subFolderA");
    copySubSourceFolder = tree.selectedNode.getChild(0);
    tree.selectNode(copySubSourceFolder);
    PlacesUtils.asContainer(tree.selectedNode).containerOpen = true;
    selectedNodeComparator.equalTitle(copySubSourceFolder);
    selectedNodeComparator.equalChildCount(3);

    info("Sanity check copy BookmarkItem");
    let copyBm_2 = tree.selectedNode.getChild(1);
    tree.selectNode(copyBm_2);
    selectedNodeComparator.equalTitle(copyBm_2);
    selectedNodeComparator.equalType(urlType);
  });
});

add_task(async function test_library_text() {
  let library = await promiseLibrary();
  registerCleanupFunction(async function () {
    await promiseLibraryClosed(library);
  });

  let PlacesOrganizer = library.PlacesOrganizer;
  PlacesOrganizer.selectLeftPaneBuiltIn("BookmarksToolbar");
  // Remove any child node up to gMainFolder, as it may be some default
  // bookmark.
  let treeView = PlacesOrganizer._places;
  for (;;) {
    let child = treeView.selectedNode.getChild(0);
    if (child.bookmarkGuid == gMainFolder.guid) {
      break;
    }
    info("remove" + child.bookmarkGuid);
    await PlacesUtils.bookmarks.remove({ guid: child.bookmarkGuid });
  }

  info("Insert a looping folder shortcut");
  await PlacesUtils.bookmarks.insert({
    parentGuid: gMainFolder.guid,
    url: `place:parent=${PlacesUtils.bookmarks.toolbarGuid}`,
    title: "Toolbar shortcut",
  });

  for (let testCase of [
    {
      flavor: PlacesUtils.TYPE_PLAINTEXT,
      expectedData:
        "toolbar\nmainFolder\nFolderA\nsubFolderA\nhttps://example.com/1\nhttps://example.com/2\nhttps://example.com/3\nFolderA\nsubFolderA\nhttps://example.com/1\nhttps://example.com/2\nhttps://example.com/3\nToolbar shortcut\n",
    },
    {
      flavor: PlacesUtils.TYPE_HTML,
      expectedData:
        '<DL><DT>toolbar</DT>\n<DD>\n<DL><DT>mainFolder</DT>\n<DD>\n<DL><DT>FolderA</DT>\n<DD>\n<DL><DT>subFolderA</DT>\n<DD>\n<A HREF="https://example.com/1">firstBM</A>\n</DD>\n<DD>\n<A HREF="https://example.com/2">secondBM</A>\n</DD>\n<DD>\n<A HREF="https://example.com/3">thirdBM</A>\n</DD>\n</DL>\n</DD>\n</DL>\n</DD>\n<DD>\n<DL><DT>FolderA</DT>\n<DD>\n<DL><DT>subFolderA</DT>\n<DD>\n<A HREF="https://example.com/1">firstBM</A>\n</DD>\n<DD>\n<A HREF="https://example.com/2">secondBM</A>\n</DD>\n<DD>\n<A HREF="https://example.com/3">thirdBM</A>\n</DD>\n</DL>\n</DD>\n</DL>\n</DD>\n<DD>\n<DL><DT>Toolbar shortcut</DT>\n</DL>\n</DD>\n</DL>\n</DD>\n</DL>\n',
    },
  ]) {
    await promiseClipboard(function () {
      library.PlacesOrganizer._places.controller.copy();
    }, PlacesUtils.TYPE_X_MOZ_PLACE);

    info("get data from the clipboard");
    let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);
    // This order matters here!  It controls the preferred flavors for this
    // paste operation.
    [testCase.flavor].forEach(type => xferable.addDataFlavor(type));
    Services.clipboard.getData(xferable, Ci.nsIClipboard.kGlobalClipboard);
    let data = {};
    let type = {};
    xferable.getAnyTransferData(type, data);
    data = data.value.QueryInterface(Ci.nsISupportsString).data;
    type = type.value;
    Assert.equal(type, testCase.flavor);
    if (testCase.flavor == PlacesUtils.TYPE_HTML) {
      // There's a couple facts to consider here:
      //   1. on Windows the DataObj component adds html header and footer
      //   2. the newline characters (\r\n) usage varies per platform
      // Thus we don't use a direct comparison here.
      Assert.ok(
        data
          .replaceAll(/(\n|\r)/g, "")
          .includes(testCase.expectedData.replaceAll(/\n/g, ""))
      );
    } else {
      Assert.equal(data, testCase.expectedData);
    }
  }
});

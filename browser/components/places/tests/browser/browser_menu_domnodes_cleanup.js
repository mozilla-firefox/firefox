/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Checks that PlacesMenu stops tracking in _domNodes the Places nodes whose
 * menu items have been removed or replaced, so that long-lived menus with live
 * results don't accumulate stale nodes and their DOM.
 */

add_setup(async function () {
  registerCleanupFunction(() => PlacesUtils.bookmarks.eraseEverything());
  await PlacesUtils.bookmarks.eraseEverything();
});

async function createTree() {
  let parent = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Parent",
  });
  let sub = await PlacesUtils.bookmarks.insert({
    parentGuid: parent.guid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    title: "Sub",
  });
  await PlacesUtils.bookmarks.insertTree({
    guid: sub.guid,
    children: [
      { url: "https://example.com/sub1", title: "Sub 1" },
      { url: "https://example.com/sub2", title: "Sub 2" },
    ],
  });
  await PlacesUtils.bookmarks.insert({
    parentGuid: parent.guid,
    url: "https://example.com/keep",
    title: "Keep",
  });
  return { parent, sub };
}

function createMenu(place) {
  let menu = document.createXULElement("menu");
  let popup = document.createXULElement("menupopup");
  menu.appendChild(popup);
  document.getElementById("mainPopupSet").appendChild(menu);
  let view = new PlacesMenu({ target: popup, originalTarget: popup }, place);
  return { menu, popup, view };
}

function showPopup(popup) {
  popup.dispatchEvent(new MouseEvent("popupshowing", { bubbles: true }));
}

add_task(async function test_removed_folder_is_forgotten() {
  let { parent, sub } = await createTree();
  let { menu, popup, view } = createMenu(`place:parent=${parent.guid}`);
  try {
    showPopup(popup.querySelector("menu").menupopup);
    // The root, the subfolder with its two children, and the bookmark.
    Assert.equal(view._domNodes.size, 5, "All the built nodes are tracked");

    await PlacesUtils.bookmarks.remove(sub.guid);
    await BrowserTestUtils.waitForMutationCondition(
      popup,
      { childList: true },
      () => !popup.querySelector("menu")
    );
    Assert.equal(
      view._domNodes.size,
      2,
      "The removed folder and its children are no longer tracked"
    );
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.eraseEverything();
  }
});

add_task(async function test_rebuilt_popup_forgets_old_nodes() {
  let { parent } = await createTree();
  let { menu, popup, view } = createMenu(`place:parent=${parent.guid}`);
  try {
    let sizeBefore = view._domNodes.size;
    Assert.equal(sizeBefore, 3, "The root and its children are tracked");

    // Reopening the container replaces its children with new result nodes.
    view._resultNode.containerOpen = false;
    view._resultNode.containerOpen = true;
    Assert.ok(!popup._built, "The popup needs to be rebuilt");
    showPopup(popup);

    Assert.equal(
      view._domNodes.size,
      sizeBefore,
      "Nodes replaced by the rebuild are no longer tracked"
    );
  } finally {
    view.uninit();
    menu.remove();
    await PlacesUtils.bookmarks.eraseEverything();
  }
});

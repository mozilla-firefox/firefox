/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Tests the "Forget About This Site" button from the libary view
const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { ForgetAboutSite } = ChromeUtils.importESModule(
  "resource://gre/modules/ForgetAboutSite.sys.mjs"
);

const TEST_URIs = [
  { title: "0", uri: "https://example.com" },
  { title: "1", uri: "https://www.mozilla.org/test1" },
  { title: "2", uri: "https://www.mozilla.org/test2" },
  { title: "3", uri: "https://192.168.200.1/login.html" },
];

async function setup() {
  registerCleanupFunction(async function () {
    // Clean up any leftover stubs.
    sinon.restore();
  });

  let places = [];
  let transition = PlacesUtils.history.TRANSITION_TYPED;
  TEST_URIs.forEach(({ title, uri }) =>
    places.push({ uri: Services.io.newURI(uri), transition, title })
  );
  await PlacesTestUtils.addVisits(places);
}

async function teardown(organizer) {
  // Close the library window.
  await promiseLibraryClosed(organizer);
  await PlacesUtils.history.clear();
}

// Selects the sites specified by sitesToSelect
// If multiple sites are selected they can't be forgotten
// Should forget selects the answer in the confirmation dialogue
// removedEntries specifies which entries should be forgotten
async function testForgetAboutThisSite(
  sitesToSelect,
  shouldForget,
  removedEntries,
  cancelConfirmWithEsc = false
) {
  if (cancelConfirmWithEsc) {
    ok(
      !shouldForget,
      "If cancelConfirmWithEsc is set we don't expect to clear entries."
    );
  }

  ok(PlacesUtils, "checking PlacesUtils, running in chrome context?");
  await setup();
  let organizer = await promiseHistoryView();
  let doc = organizer.document;
  let tree = doc.getElementById("placeContent");

  //Sort by name in descreasing order
  tree.view._result.sortingMode =
    Ci.nsINavHistoryQueryOptions.SORT_BY_TITLE_ASCENDING;

  let selection = tree.view.selection;
  selection.clearSelection();
  sitesToSelect.forEach(index => selection.rangedSelect(index, index, true));

  let selectionCount = sitesToSelect.length;
  is(
    selection.count,
    selectionCount,
    "The selected range is as big as expected"
  );
  // Open the context menu.
  let contextmenu = doc.getElementById("placesContext");
  let popupShown = promisePopupShown(contextmenu);

  // Get cell coordinates.
  let rect = tree.getCoordsForCellItem(
    sitesToSelect[0],
    tree.columns[0],
    "text"
  );
  // Initiate a context menu for the selected cell.
  EventUtils.synthesizeMouse(
    tree.body,
    rect.x + rect.width / 2,
    rect.y + rect.height / 2,
    { type: "contextmenu", button: 2 },
    organizer
  );
  await popupShown;

  let forgetThisSite = doc.getElementById("placesContext_deleteHost");
  let hideForgetThisSite = selectionCount > 1;
  is(
    forgetThisSite.hidden,
    hideForgetThisSite,
    `The Forget this site menu item should ${
      hideForgetThisSite ? "" : "not "
    }be hidden with ${selectionCount} items selected`
  );
  if (hideForgetThisSite) {
    // Close the context menu.
    contextmenu.hidePopup();
    await teardown(organizer);
    return;
  }

  // Resolves once the confirmation dialog has been closed.
  let dialogPromise;

  // Cancel dialog via esc key. We have to get the dialog closed promise
  // ourselves.
  if (cancelConfirmWithEsc) {
    dialogPromise = BrowserTestUtils.promiseAlertDialogOpen(
      null,
      "chrome://browser/content/places/clearDataForSite.xhtml"
    ).then(dialog => {
      let dialogClosedPromise = BrowserTestUtils.waitForEvent(dialog, "unload");
      EventUtils.synthesizeKey("KEY_Escape", undefined, dialog);
      return dialogClosedPromise;
    });
  } else {
    dialogPromise = BrowserTestUtils.promiseAlertDialog(
      shouldForget ? "accept" : "cancel",
      "chrome://browser/content/places/clearDataForSite.xhtml"
    );
  }

  // If we cancel the dialog, create stubs to check that none of the clear
  // methods are called.
  if (!shouldForget) {
    sinon.stub(ForgetAboutSite, "removeDataFromBaseDomain").resolves();
  }

  let pageRemovedEventPromise;
  if (shouldForget) {
    pageRemovedEventPromise =
      PlacesTestUtils.waitForNotification("page-removed");
  }

  // Execute the delete command.
  contextmenu.activateItem(forgetThisSite);

  // Wait for dialog to be handled.
  await dialogPromise;

  // If we expect to remove items, wait the page-removed event to fire. If we
  // don't wait, we may test the list before any items have been removed.
  await pageRemovedEventPromise;

  if (!shouldForget) {
    ok(
      ForgetAboutSite.removeDataFromBaseDomain.notCalled,
      "Should not call ForgetAboutSite when the confirmation prompt is cancelled."
    );
    // Remove the stubs.
    sinon.restore();
  }

  // Check that the entries have been removed.
  await Promise.all(
    removedEntries.map(async ({ uri }) => {
      Assert.ok(
        !(await PlacesUtils.history.fetch(uri)),
        `History entry for ${uri} has been correctly removed`
      );
    })
  );
  await Promise.all(
    TEST_URIs.filter(x => !removedEntries.includes(x)).map(async ({ uri }) => {
      Assert.ok(
        await PlacesUtils.history.fetch(uri),
        `History entry for ${uri} has been kept`
      );
    })
  );

  // Cleanup.
  await teardown(organizer);
}

/*
 * Opens the history view in the PlacesOrganziner window
 * @returns {Promise}
 * @resolves The PlacesOrganizer
 */
async function promiseHistoryView() {
  let organizer = await promiseLibrary();

  // Select History in the left pane.
  let po = organizer.PlacesOrganizer;
  po.selectLeftPaneBuiltIn("History");

  let histContainer = po._places.selectedNode.QueryInterface(
    Ci.nsINavHistoryContainerResultNode
  );
  histContainer.containerOpen = true;
  po._places.selectNode(histContainer.getChild(0));

  return organizer;
}
/*
 * @returns {Promise}
 * @resolves once the popup is shown
 */
function promisePopupShown(popup) {
  return new Promise(resolve => {
    popup.addEventListener(
      "popupshown",
      function () {
        resolve();
      },
      { capture: true, once: true }
    );
  });
}

// This test makes sure that the Forget This Site command is hidden for multiple
// selections.
add_task(async function selectMultiple() {
  await testForgetAboutThisSite([0, 1]);
});

// This test makes sure that forgetting "https://www.mozilla.org/test2" also removes "https://www.mozilla.org/test1"
add_task(async function forgettingBasedomain() {
  await testForgetAboutThisSite([1], true, TEST_URIs.slice(1, 3));
});

// This test makes sure that forgetting by IP address works
add_task(async function forgettingIPAddress() {
  await testForgetAboutThisSite([3], true, TEST_URIs.slice(3, 4));
});

// This test makes sure that forgetting file URLs works
add_task(async function dontAlwaysForget() {
  await testForgetAboutThisSite([0], false, []);
});

// When cancelling the confirmation prompt via ESC key, no entries should be
// cleared.
add_task(async function cancelConfirmWithEsc() {
  await testForgetAboutThisSite([0], false, [], true);
});

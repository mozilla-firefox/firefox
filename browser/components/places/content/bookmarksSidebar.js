/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Shared Places Import - change other consumers if you change this: */
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

XPCOMUtils.defineLazyScriptGetter(
  this,
  "PlacesTreeView",
  "chrome://browser/content/places/treeView.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["PlacesInsertionPoint", "PlacesController", "PlacesControllerDragHelper"],
  "chrome://browser/content/places/controller.js"
);
/* End Shared Places Import */
var gCumulativeSearches = 0;

window.addEventListener("load", () => {
  let uidensity = window.top.document.documentElement.getAttribute("uidensity");
  if (uidensity) {
    document.documentElement.setAttribute("uidensity", uidensity);
  }

  let view = document.getElementById("bookmarks-view");
  view.place =
    "place:type=" + Ci.nsINavHistoryQueryOptions.RESULTS_AS_ROOTS_QUERY;
  view.addEventListener("keypress", event =>
    PlacesUIUtils.onSidebarTreeKeyPress(event)
  );
  view.addEventListener("click", event =>
    PlacesUIUtils.onSidebarTreeClick(event)
  );
  view.addEventListener("mousemove", event =>
    PlacesUIUtils.onSidebarTreeMouseMove(event)
  );
  view.addEventListener("mouseout", () =>
    PlacesUIUtils.setMouseoverURL("", window)
  );

  document
    .getElementById("search-box")
    .addEventListener("MozInputSearch:search", searchBookmarks);

  let bhTooltip = document.getElementById("bhTooltip");
  bhTooltip.addEventListener("popupshowing", event => {
    window.top.BookmarksEventHandler.fillInBHTooltip(bhTooltip, event);
  });
  bhTooltip.addEventListener("popuphiding", () =>
    bhTooltip.removeAttribute("position")
  );

  document
    .getElementById("sidebar-panel-close")
    .addEventListener("click", closeSidebarPanel);

  document
    .getElementById("placesCommands")
    .addEventListener("command", event => {
      let label;
      switch (event.target.id) {
        case "placesCmd_open:tab":
          label = "open_in_new_tab";
          break;
        case "placesCmd_open:window":
          label = "open_in_new_window";
          break;
        case "placesCmd_open:privatewindow":
          label = "open_in_private_window";
          break;
        case "placesCmd_show:info": {
          let node = document.getElementById("bookmarks-view").selectedNode;
          const labelPrefix =
            node && PlacesUtils.nodeIsFolderOrShortcut(node)
              ? "rename_bookmark_folder"
              : "edit_bookmark";
          recordDialogResult(labelPrefix);
          break;
        }
        case "placesCmd_cut":
          label = "cut_bookmark";
          break;
        case "placesCmd_copy":
          label = "copy_bookmark_url";
          break;
        case "placesCmd_new:bookmark":
          recordDialogResult("add_bookmark");
          break;
        case "placesCmd_new:folder":
          recordDialogResult("add_bookmark_folder");
          break;
        case "placesCmd_new:separator":
          label = "add_separator";
          break;
        case "placesCmd_sortBy:name":
          label = "sort_bookmarks_by_name";
          break;
      }
      if (label) {
        Glean.browserUiInteraction.sidebarBookmarks[label].add(1);
      }
    });

  document
    .getElementById("placesContext_open_newcontainertab_popup")
    .addEventListener("command", () => {
      Glean.browserUiInteraction.sidebarBookmarks.open_in_new_container_tab.add(
        1
      );
    });
});

/**
 * Wait for an in-flight bookmark dialog (add / edit / rename folder) to be
 * dismissed, then record the result in the respective Glean bucket.
 *
 * @param {string} labelPrefix
 *   One of "add_bookmark", "add_bookmark_folder", "edit_bookmark",
 *   "rename_bookmark_folder".
 */
async function recordDialogResult(labelPrefix) {
  const deferred = PlacesUIUtils.lastBookmarkDialogDeferred;
  const guid = await deferred.promise;
  // If user confirms the bookmark dialog, we will have the guid.
  // If they cancel, guid will be `undefined`.
  // (If saving resulted in an error, deferred promise wouldn't have resolved.)
  const outcome = guid ? "confirmed" : "cancelled";
  Glean.browserUiInteraction.sidebarBookmarks[`${labelPrefix}_${outcome}`].add(
    1
  );
}

function searchBookmarks(event) {
  let { value } = event.currentTarget;

  var tree = document.getElementById("bookmarks-view");
  if (!value) {
    // eslint-disable-next-line no-self-assign
    tree.place = tree.place;
  } else {
    Glean.sidebar.search.bookmarks.add(1);
    Glean.browserUiInteraction.sidebarBookmarks.search.add(1);
    gCumulativeSearches++;
    tree.applyFilter(value, PlacesUtils.bookmarks.userContentRoots);
  }
}

function updateTelemetry(urlsOpened = [], openAllBookmarks = false) {
  Glean.bookmarksSidebar.cumulativeSearches.accumulateSingleSample(
    gCumulativeSearches
  );
  clearCumulativeCounter();

  Glean.sidebar.link.bookmarks.add(urlsOpened.length);
  if (openAllBookmarks) {
    Glean.browserUiInteraction.sidebarBookmarks.open_all_bookmarks.add(1);
  }
}

function clearCumulativeCounter() {
  gCumulativeSearches = 0;
}

window.addEventListener("unload", () => {
  clearCumulativeCounter();
  PlacesUIUtils.setMouseoverURL("", window);
});

function closeSidebarPanel(e) {
  e.preventDefault();
  let view = e.target.getAttribute("view");
  window.browsingContext.embedderWindowGlobal.browsingContext.window.SidebarController.toggle(
    view
  );
}

window.addEventListener("SidebarFocused", () =>
  document.getElementById("search-box").focus()
);

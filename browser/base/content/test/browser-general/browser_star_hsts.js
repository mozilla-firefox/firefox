/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

var secureURL =
  "https://example.com/browser/browser/base/content/test/browser-general/browser_star_hsts.sjs";
var unsecureURL =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.com/browser/browser/base/content/test/browser-general/browser_star_hsts.sjs";

add_task(async function test_star_redirect() {
  registerCleanupFunction(async () => {
    // Ensure to remove example.com from the HSTS list.
    let sss = Cc["@mozilla.org/ssservice;1"].getService(
      Ci.nsISiteSecurityService
    );
    sss.resetState(
      // eslint-disable-next-line sdl/no-insecure-url
      NetUtil.newURI("http://example.com/"),
      { partitionKey: "(http,example.com)" }
    );
    await PlacesUtils.bookmarks.eraseEverything();
    gBrowser.removeCurrentTab();
  });

  let tab = (gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser));
  // This will add the page to the HSTS cache.
  await BrowserTestUtils.loadURIString({
    browser: tab.linkedBrowser,
    uriString: secureURL,
  });
  // This should transparently be redirected to the secure page.
  await BrowserTestUtils.loadURIString({
    browser: tab.linkedBrowser,
    uriString: unsecureURL,
    finalURI: secureURL,
  });

  await promiseStarState(BookmarkingUI.STATUS_UNSTARRED);

  StarUI._createPanelIfNeeded();
  let bookmarkPanel = document.getElementById("editBookmarkPanel");
  let shownPromise = promisePopupShown(bookmarkPanel);
  BookmarkingUI.star.click();
  await shownPromise;

  is(BookmarkingUI.status, BookmarkingUI.STATUS_STARRED, "The star is starred");

  let bookmarkAdded = PlacesTestUtils.waitForNotification("bookmark-added");
  let hiddenPromise = promisePopupHidden(bookmarkPanel);
  bookmarkPanel.hidePopup();
  await hiddenPromise;
  let [event] = await bookmarkAdded;
  is(event.url, secureURL, "The secure URL was bookmarked");
});

/**
 * Waits for the star to reflect the expected state.
 */
function promiseStarState(aValue) {
  return new Promise(resolve => {
    let expectedStatus = aValue
      ? BookmarkingUI.STATUS_STARRED
      : BookmarkingUI.STATUS_UNSTARRED;
    (function checkState() {
      if (
        BookmarkingUI.status == BookmarkingUI.STATUS_UPDATING ||
        BookmarkingUI.status != expectedStatus
      ) {
        info("Waiting for star button change.");
        setTimeout(checkState, 1000);
      } else {
        resolve();
      }
    })();
  });
}

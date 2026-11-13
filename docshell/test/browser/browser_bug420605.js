/* Test for Bug 420605
 * https://bugzilla.mozilla.org/show_bug.cgi?id=420605
 */

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

add_task(async function test() {
  registerCleanupFunction(() => PlacesUtils.history.clear());
  var pageurl =
    "http://mochi.test:8888/browser/docshell/test/browser/file_bug420605.html";
  var fragmenturl =
    "http://mochi.test:8888/browser/docshell/test/browser/file_bug420605.html#firefox";

  /* Queries nsINavHistoryService and returns a single history entry
   * for a given URI */
  function getNavHistoryEntry(aURI) {
    var options = PlacesUtils.history.getNewQueryOptions();
    options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY;
    options.maxResults = 1;

    var query = PlacesUtils.history.getNewQuery();
    query.uri = aURI;
    var result = PlacesUtils.history.executeQuery(query, options);
    result.root.containerOpen = true;

    if (!result.root.childCount) {
      return null;
    }
    return result.root.getChild(0);
  }

  // We'll save the favicon URL of the orignal page here and check that the
  // page with a hash has the same favicon.
  var originalFavicon;

  // Control flow in this test is a bit complicated.
  //
  // When the page loads, onPageLoad (the DOMContentLoaded handler) and
  // favicon-changed are both called, in some order.  Once
  // they've both run, we click a fragment link in the content page
  // (clickLinkIfReady), which should trigger another favicon-changed event,
  // this time for the fragment's URL.

  var _clickLinkTimes = 0;
  function clickLinkIfReady() {
    _clickLinkTimes++;
    if (_clickLinkTimes == 2) {
      BrowserTestUtils.synthesizeMouseAtCenter(
        "#firefox-link",
        {},
        gBrowser.selectedBrowser
      );
    }
  }

  function onPageLoad() {
    clickLinkIfReady();
  }

  // Make sure neither of the test pages haven't been loaded before.
  var info = getNavHistoryEntry(makeURI(pageurl));
  ok(!info, "The test page must not have been visited already.");
  info = getNavHistoryEntry(makeURI(fragmenturl));
  ok(!info, "The fragment test page must not have been visited already.");

  let promiseIcon1 = PlacesTestUtils.waitForNotification(
    "favicon-changed",
    events =>
      events.some(e => {
        if (e.url == pageurl) {
          ok(
            e.faviconUrl,
            "Favicon value is not null for page without fragment."
          );
          originalFavicon = e.faviconUrl;

          // Now that the favicon has loaded, click on fragment link.
          // This should trigger the |case fragmenturl| below.
          clickLinkIfReady();
          return true;
        }
        return false;
      })
  );
  let promiseIcon2 = PlacesTestUtils.waitForNotification(
    "favicon-changed",
    events =>
      events.some(e => {
        if (e.url == fragmenturl) {
          // If the fragment URL's favicon isn't set, this branch won't
          // be called and the test will time out.
          is(
            e.faviconUrl,
            originalFavicon,
            "New favicon should be same as original favicon."
          );
          ok(
            e.faviconUrl,
            "Favicon value is not null for page without fragment."
          );
          originalFavicon = e.faviconUrl;

          // Now that the favicon has loaded, click on fragment link.
          // This should trigger the |case fragmenturl| below.
          clickLinkIfReady();
          return true;
        }
        return false;
      })
  );

  // Now open the test page in a new tab.
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);
  BrowserTestUtils.waitForContentEvent(
    gBrowser.selectedBrowser,
    "DOMContentLoaded",
    true
  ).then(onPageLoad);
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, pageurl);

  await promiseIcon1;
  await promiseIcon2;

  // Let's explicitly check that we can get the favicon
  // from nsINavHistoryService now.
  info = getNavHistoryEntry(makeURI(fragmenturl));
  ok(info, "There must be a history entry for the fragment.");
  ok(info.icon, "The history entry must have an associated favicon.");
  gBrowser.removeCurrentTab();
});

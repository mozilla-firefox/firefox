/* Test for Bug 503832
 * https://bugzilla.mozilla.org/show_bug.cgi?id=503832
 */

add_task(async function () {
  registerCleanupFunction(() => PlacesUtils.history.clear());
  var pagetitle = "Page Title for Bug 503832";
  var pageurl =
    "http://mochi.test:8888/browser/docshell/test/browser/file_bug503832.html";
  var fragmenturl =
    "http://mochi.test:8888/browser/docshell/test/browser/file_bug503832.html#firefox";

  var historyService = Cc[
    "@mozilla.org/browser/nav-history-service;1"
  ].getService(Ci.nsINavHistoryService);

  let fragmentPromise = new Promise(resolve => {
    const listener = events => {
      const { url, title } = events[0];

      switch (url) {
        case pageurl:
          is(title, pagetitle, "Correct page title for " + url);
          return;
        case fragmenturl:
          is(title, pagetitle, "Correct page title for " + url);
          // If titles for fragment URLs aren't set, this code
          // branch won't be called and the test will timeout,
          // resulting in a failure
          PlacesObservers.removeListener(["page-title-changed"], listener);
          resolve();
      }
    };

    PlacesObservers.addListener(["page-title-changed"], listener);
  });

  /* Queries nsINavHistoryService and returns a single history entry
   * for a given URI */
  function getNavHistoryEntry(aURI) {
    var options = historyService.getNewQueryOptions();
    options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY;
    options.maxResults = 1;

    var query = historyService.getNewQuery();
    query.uri = aURI;

    var result = historyService.executeQuery(query, options);
    result.root.containerOpen = true;

    if (!result.root.childCount) {
      return null;
    }
    var node = result.root.getChild(0);
    result.root.containerOpen = false;
    return node;
  }

  // Make sure neither of the test pages haven't been loaded before.
  var info = getNavHistoryEntry(makeURI(pageurl));
  ok(!info, "The test page must not have been visited already.");
  info = getNavHistoryEntry(makeURI(fragmenturl));
  ok(!info, "The fragment test page must not have been visited already.");

  // Now open the test page in a new tab
  await BrowserTestUtils.openNewForegroundTab(gBrowser, pageurl);

  // Now that the page is loaded, click on fragment link
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#firefox-link",
    {},
    gBrowser.selectedBrowser
  );
  await fragmentPromise;

  gBrowser.removeCurrentTab();
});

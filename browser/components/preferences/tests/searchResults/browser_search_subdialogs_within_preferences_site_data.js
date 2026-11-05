/*
 * This file contains tests for the Preferences search bar.
 */

// Opening about:preferences repeatedly is very slow under the macOS
// test-verify chaos passes; see bug 2052897.
requestLongerTimeout(4);

add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, {
    leaveOpen: true,
  });
  await evaluateSearchResults("cookies", ["cookiesAndSiteData"]);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, {
    leaveOpen: true,
  });
  await evaluateSearchResults("site data", ["cookiesAndSiteData"]);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, {
    leaveOpen: true,
  });
  await evaluateSearchResults("cache", ["cookiesAndSiteData"]);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

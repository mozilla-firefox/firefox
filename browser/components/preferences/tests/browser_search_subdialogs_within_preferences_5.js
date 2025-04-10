/*
 * This file contains tests for the Preferences search bar.
 */

requestLongerTimeout(2);

/**
 * Test for searching for the "Fonts" subdialog.
 */
add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  // Oh, Canada:
  await evaluateSearchResults("Unified Canadian Syllabary", "fontsGroup");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

/**
 * Test for searching for the "Colors" subdialog.
 */
add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  await evaluateSearchResults("Link Colors", "contrastControlGroup");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

/**
 * Test for searching for the "Exceptions - Saved Logins" subdialog.
 */
add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  await evaluateSearchResults(
    "won’t save passwords for sites listed here",
    "passwordsGroup"
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

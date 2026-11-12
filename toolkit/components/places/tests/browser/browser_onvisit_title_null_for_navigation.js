/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_PATH = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

add_setup(async function () {
  await PlacesUtils.history.clear();
  registerCleanupFunction(PlacesUtils.history.clear);
});

add_task(async function checkTitleNotificationForNavigation() {
  const EXPECTED_URL = Services.io.newURI(TEST_PATH + "empty_page.html");

  const promiseVisit = PlacesTestUtils.waitForNotification(
    "page-visited",
    events => events[0].url === EXPECTED_URL.spec
  );

  const promiseTitle = PlacesTestUtils.waitForNotification(
    "page-title-changed",
    events => events[0].url === EXPECTED_URL.spec
  );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    EXPECTED_URL.spec
  );

  const visitEvents = await promiseVisit;
  Assert.equal(visitEvents.length, 1, "Right number of visits notified");
  Assert.equal(visitEvents[0].type, "page-visited");
  info("'page-visited': " + visitEvents[0].url);
  Assert.equal(visitEvents[0].lastKnownTitle, null, "Should not have a title");

  const titleEvents = await promiseTitle;
  Assert.equal(titleEvents.length, 1, "Right number of title changed notified");
  Assert.equal(titleEvents[0].type, "page-title-changed");
  info("'page-title-changed': " + titleEvents[0].url);
  Assert.equal(
    titleEvents[0].title,
    "I am an empty page",
    "Should have correct title in titlechanged notification"
  );

  BrowserTestUtils.removeTab(tab);
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

XPCOMUtils.defineLazyServiceGetter(
  this,
  "urlQueryStrippingListService",
  "@mozilla.org/query-stripping-list-service;1",
  Ci.nsIURLQueryStrippingListService
);

const COLLECTION_NAME = "query-stripping";

const TEST_URI = TEST_DOMAIN + TEST_PATH + "empty.html";
const TEST_THIRD_PARTY_URI = TEST_DOMAIN_2 + TEST_PATH + "empty.html";

// The Update Event here is used to listen the observer from the
// URLQueryStrippingListService. We need to use the event here so that the same
// observer can be called multiple times.
class UpdateEvent extends EventTarget {}

// The Observer that is registered needs to implement onQueryStrippingListUpdate
// like a nsIURLQueryStrippingListObserver does
class ListObserver {
  updateEvent = new UpdateEvent();

  onQueryStrippingListUpdate(stripList, allowList) {
    let event = new CustomEvent("update", { detail: { stripList, allowList } });
    this.updateEvent.dispatchEvent(event);
  }
}

function waitForEvent(element, eventName) {
  return BrowserTestUtils.waitForEvent(element, eventName).then(e => e.detail);
}

async function verifyQueryString(browser, expected) {
  await SpecialPowers.spawn(browser, [expected], expected => {
    // Strip the first question mark.
    let search = content.location.search.slice(1);

    is(search, expected, "The query string is correct.");
  });
}

async function check(query, expected) {
  // Open a tab with the query string.
  let testURI = TEST_URI + "?" + query;

  // Test for stripping in parent process.
  await BrowserTestUtils.withNewTab(testURI, async browser => {
    // Verify if the query string is expected in the new tab.
    await verifyQueryString(browser, expected);
  });

  testURI = TEST_URI + "?" + query;
  let expectedURI;
  if (expected != "") {
    expectedURI = TEST_URI + "?" + expected;
  } else {
    expectedURI = TEST_URI;
  }

  // Test for stripping in content processes. This will first open a third-party
  // page and create a link to the test uri. And then, click the link to
  // navigate the page, which will trigger the stripping in content processes.
  await BrowserTestUtils.withNewTab(TEST_THIRD_PARTY_URI, async browser => {
    // Create the promise to wait for the location change.
    let locationChangePromise = BrowserTestUtils.waitForLocationChange(
      gBrowser,
      expectedURI
    );

    // Create a link and click it to navigate.
    await SpecialPowers.spawn(browser, [testURI], async uri => {
      let link = content.document.createElement("a");
      link.setAttribute("href", uri);
      link.textContent = "Link";
      content.document.body.appendChild(link);
      link.click();
    });

    await locationChangePromise;

    // Verify the query string in the content window.
    await verifyQueryString(browser, expected);
  });
}

registerCleanupFunction(() => {
  Cc["@mozilla.org/query-stripping-list-service;1"]
    .getService(Ci.nsIURLQueryStrippingListService)
    .clearLists();
});

add_task(async function testPrefSettings() {
  // Enable query stripping and clear the prefs at the beginning.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.query_stripping.enabled", true],
      ["privacy.query_stripping.strip_list", ""],
      ["privacy.query_stripping.allow_list", ""],
      ["privacy.query_stripping.testing", true],
    ],
  });

  // Test if the observer been called when adding to the service.
  let obs = new ListObserver();
  let promise = waitForEvent(obs.updateEvent, "update");
  urlQueryStrippingListService.registerAndRunObserver(obs);
  let lists = await promise;
  is(lists.stripList, "", "No strip list at the beginning.");
  is(lists.allowList, "", "No allow list at the beginning.");

  // Verify that no query stripping happens.
  await check("pref_query1=123", "pref_query1=123");
  await check("pref_query2=456", "pref_query2=456");

  // Set pref for strip list
  promise = waitForEvent(obs.updateEvent, "update");
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.query_stripping.strip_list", "pref_query1 pref_query2"]],
  });
  lists = await promise;

  is(
    lists.stripList,
    "pref_query1 pref_query2",
    "There should be strip list entries."
  );
  is(lists.allowList, "", "There should be no allow list entries.");

  // The query string should be stripped.
  await check("pref_query1=123", "");
  await check("pref_query2=456", "");

  // Set the pref for allow list.
  promise = waitForEvent(obs.updateEvent, "update");
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.query_stripping.allow_list", "example.net"]],
  });
  lists = await promise;

  is(
    lists.stripList,
    "pref_query1 pref_query2",
    "There should be strip list entires."
  );
  is(lists.allowList, "example.net", "There should be one allow list entry.");

  // The query string shouldn't be stripped because this host is in allow list.
  await check("pref_query1=123", "pref_query1=123");
  await check("pref_query2=123", "pref_query2=123");

  urlQueryStrippingListService.unregisterObserver(obs);

  // Clear prefs.
  SpecialPowers.flushPrefEnv();
});

add_task(async function testRemoteSettings() {
  // Enable query stripping and clear the prefs at the beginning.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.query_stripping.enabled", true],
      ["privacy.query_stripping.strip_list", ""],
      ["privacy.query_stripping.allow_list", ""],
      ["privacy.query_stripping.testing", true],
    ],
  });

  // Add initial empty record.
  let db = RemoteSettings(COLLECTION_NAME).db;
  await db.importChanges({}, Date.now(), []);

  // Test if the observer been called when adding to the service.
  let obs = new ListObserver();
  let promise = waitForEvent(obs.updateEvent, "update");
  urlQueryStrippingListService.registerAndRunObserver(obs);
  let lists = await promise;
  is(lists.stripList, "", "No strip list at the beginning.");
  is(lists.allowList, "", "No allow list at the beginning.");

  // Verify that no query stripping happens.
  await check("remote_query1=123", "remote_query1=123");
  await check("remote_query2=456", "remote_query2=456");

  // Set record for strip list.
  promise = waitForEvent(obs.updateEvent, "update");
  await RemoteSettings(COLLECTION_NAME).emit("sync", {
    data: {
      current: [
        {
          id: "1",
          last_modified: 1000000000000001,
          stripList: ["remote_query1", "remote_query2"],
          allowList: [],
        },
      ],
    },
  });
  lists = await promise;

  is(
    lists.stripList,
    "remote_query1 remote_query2",
    "There should be strip list entries."
  );
  is(lists.allowList, "", "There should be no allow list entries.");

  // The query string should be stripped.
  await check("remote_query1=123", "");
  await check("remote_query2=456", "");

  // Set record for strip list and allow list.
  promise = waitForEvent(obs.updateEvent, "update");
  await RemoteSettings(COLLECTION_NAME).emit("sync", {
    data: {
      current: [
        {
          id: "2",
          last_modified: 1000000000000002,
          stripList: ["remote_query1", "remote_query2"],
          allowList: ["example.net"],
        },
      ],
    },
  });
  lists = await promise;

  is(
    lists.stripList,
    "remote_query1 remote_query2",
    "There should be strip list entries."
  );
  is(lists.allowList, "example.net", "There should be one allow list entry.");

  // The query string shouldn't be stripped because this host is in allow list.
  await check("remote_query1=123", "remote_query1=123");
  await check("remote_query2=123", "remote_query2=123");

  urlQueryStrippingListService.unregisterObserver(obs);

  // Clear the remote settings.
  await db.clear();
});

add_task(async function testUnregisterDuringInit() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.query_stripping.enabled", false],
      ["privacy.query_stripping.strip_on_share.enabled", false],
    ],
  });
  ok(
    !(await urlQueryStrippingListService.testWaitForInit()),
    "The service starts uninitialized."
  );

  // Register and unregister while init is pending.
  let obs = new ListObserver();
  urlQueryStrippingListService.registerAndRunObserver(obs);
  urlQueryStrippingListService.unregisterObserver(obs);

  // A full register and unregister cycle of another observer, so the
  // cancelled registration has had time to complete.
  let otherObs = new ListObserver();
  let promise = waitForEvent(otherObs.updateEvent, "update");
  urlQueryStrippingListService.registerAndRunObserver(otherObs);
  await promise;
  urlQueryStrippingListService.unregisterObserver(otherObs);
  ok(
    !urlQueryStrippingListService.testHasQPSObservers(),
    "Unregistering while init is pending removes the observer."
  );
  ok(
    !(await urlQueryStrippingListService.testWaitForInit()),
    "The service shuts down once its observers are unregistered."
  );

  // Register, unregister and register again while init is pending.
  urlQueryStrippingListService.registerAndRunObserver(obs);
  urlQueryStrippingListService.unregisterObserver(obs);
  urlQueryStrippingListService.registerAndRunObserver(obs);
  ok(
    await urlQueryStrippingListService.testWaitForInit(),
    "Registering again while init is pending keeps the service initialized."
  );

  urlQueryStrippingListService.unregisterObserver(obs);
  await SpecialPowers.popPrefEnv();
});

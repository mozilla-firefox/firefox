"use strict";

add_task(async function test_browserId_change_then_discard() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  const bc = tab.linkedBrowser.browsingContext;
  const oldId = bc.browserId;
  is(
    BrowsingContext.getCurrentTopByBrowserId(oldId),
    bc,
    "lookup by initial browserId"
  );

  const newId = oldId + 1000000;
  ok(
    !BrowsingContext.getCurrentTopByBrowserId(newId),
    "new browserId is unused"
  );
  bc.browserId = newId;

  const discarded = TestUtils.waitForCondition(() => bc.isDiscarded);
  BrowserTestUtils.removeTab(tab);
  await discarded;

  is(
    BrowsingContext.getCurrentTopByBrowserId(oldId),
    null,
    "initial browserId no longer resolves after discard"
  );
  is(
    BrowsingContext.getCurrentTopByBrowserId(newId),
    null,
    "new browserId does not resolve after discard"
  );
});

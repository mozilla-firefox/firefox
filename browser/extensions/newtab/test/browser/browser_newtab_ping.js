/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";
requestLongerTimeout(5);

const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);

let sendTriggerMessageSpy;
let sandbox;

add_setup(function () {
  sandbox = sinon.createSandbox();
  sendTriggerMessageSpy = sandbox.spy(ASRouter, "sendTriggerMessage");

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

/**
 * Hide EventStateManager's real idle ticks from the feed. They fire on a
 * deadline no test controls, and can say the user went idle in the middle of
 * a test that stands in for a user who stays active.
 *
 * @param {object} TelemetryFeed the feed under test
 * @returns {object} the stub, to restore before the test notifies idle itself
 */
function ignoreRealIdleTicks(TelemetryFeed) {
  const stub = sandbox
    .stub(TelemetryFeed, "observe")
    .callsFake((subject, topic, data) => {
      if (topic !== "user-interaction-inactive-non-synthesized") {
        stub.wrappedMethod.call(TelemetryFeed, subject, topic, data);
      }
    });
  return stub;
}

add_task(async function test_newtab_tab_close_sends_ping() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });

  Services.fog.testResetFOG();
  sendTriggerMessageSpy.resetHistory();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  TelemetryFeed.init(); // INIT action doesn't happen by default.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false // waitForLoad; about:newtab is cached so this would never resolve
  );

  await TestUtils.waitForCondition(
    () => sendTriggerMessageSpy.called,
    "After about:newtab finishes loading"
  );
  sendTriggerMessageSpy.resetHistory();

  await TestUtils.waitForCondition(
    () => !!Glean.newtab.opened.testGetValue("newtab"),
    "We expect the newtab open to be recorded"
  );
  let record = Glean.newtab.opened.testGetValue("newtab");
  Assert.equal(record.length, 1, "Should only be one open");
  const sessionId = record[0].extra.newtab_visit_id;
  Assert.ok(!!sessionId, "newtab_visit_id must be present");

  let pingSubmitted = false;
  GleanPings.newtab.testBeforeNextSubmit(reason => {
    pingSubmitted = true;
    Assert.equal(reason, "newtab_session_end");
    record = Glean.newtab.closed.testGetValue("newtab");
    Assert.equal(record.length, 1, "Should only have one close");
    Assert.equal(
      record[0].extra.newtab_visit_id,
      sessionId,
      "Should've closed the session we opened"
    );
    Assert.ok(Glean.newtabSearch.enabled.testGetValue());
    Assert.ok(Glean.topsites.enabled.testGetValue());
    // Highlights ("Recent Activity") is off by default.
    Assert.ok(!Glean.newtab.highlightsEnabled.testGetValue());
    // Sponsored topsites are turned off in tests to avoid making remote requests.
    Assert.ok(!Glean.topsites.sponsoredEnabled.testGetValue());
    Assert.ok(Glean.pocket.enabled.testGetValue());
    Assert.ok(Glean.pocket.sponsoredStoriesEnabled.testGetValue());
  });

  BrowserTestUtils.removeTab(tab);
  await TestUtils.waitForCondition(
    () => pingSubmitted,
    "We expect the ping to have submitted."
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_newtab_tab_nav_sends_ping() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });

  Services.fog.testResetFOG();
  sendTriggerMessageSpy.resetHistory();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  TelemetryFeed.init(); // INIT action doesn't happen by default.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false // waitForLoad; about:newtab is cached so this would never resolve
  );

  await TestUtils.waitForCondition(
    () => sendTriggerMessageSpy.called,
    "After about:newtab finishes loading"
  );
  sendTriggerMessageSpy.resetHistory();

  await TestUtils.waitForCondition(
    () => !!Glean.newtab.opened.testGetValue("newtab"),
    "We expect the newtab open to be recorded"
  );
  let record = Glean.newtab.opened.testGetValue("newtab");
  Assert.equal(record.length, 1, "Should only be one open");
  const sessionId = record[0].extra.newtab_visit_id;
  Assert.ok(!!sessionId, "newtab_visit_id must be present");

  let pingSubmitted = false;
  GleanPings.newtab.testBeforeNextSubmit(reason => {
    pingSubmitted = true;
    Assert.equal(reason, "newtab_session_end");
    record = Glean.newtab.closed.testGetValue("newtab");
    Assert.equal(record.length, 1, "Should only have one close");
    Assert.equal(
      record[0].extra.newtab_visit_id,
      sessionId,
      "Should've closed the session we opened"
    );
    Assert.ok(Glean.newtabSearch.enabled.testGetValue());
    Assert.ok(Glean.topsites.enabled.testGetValue());
    // Highlights ("Recent Activity") is off by default.
    Assert.ok(!Glean.newtab.highlightsEnabled.testGetValue());
    // Sponsored topsites are turned off in tests to avoid making remote requests.
    Assert.ok(!Glean.topsites.sponsoredEnabled.testGetValue());
    Assert.ok(Glean.pocket.enabled.testGetValue());
    Assert.ok(Glean.pocket.sponsoredStoriesEnabled.testGetValue());
  });

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, "about:mozilla");
  await TestUtils.waitForCondition(
    () => pingSubmitted,
    "We expect the ping to have submitted."
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_newtab_doesnt_send_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.newtabpage.activity-stream.telemetry", true],
      ["browser.newtabpage.ping.enabled", false],
    ],
  });

  Services.fog.testResetFOG();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  TelemetryFeed.init(); // INIT action doesn't happen by default.
  sendTriggerMessageSpy.resetHistory();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false // waitForLoad; about:newtab is cached so this would never resolve
  );

  await TestUtils.waitForCondition(
    () => sendTriggerMessageSpy.called,
    "After about:newtab finishes loading"
  );
  sendTriggerMessageSpy.resetHistory();

  await TestUtils.waitForCondition(
    () => !!Glean.newtab.opened.testGetValue("newtab"),
    "We expect the newtab open to be recorded"
  );
  let record = Glean.newtab.opened.testGetValue("newtab");
  Assert.equal(record.length, 1, "Should only be one open");
  const sessionId = record[0].extra.newtab_visit_id;
  Assert.ok(!!sessionId, "newtab_visit_id must be present");

  GleanPings.newtab.testBeforeNextSubmit(() => {
    Assert.ok(false, "Must not submit ping!");
  });
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, "about:mozilla");
  BrowserTestUtils.removeTab(tab);
  await TestUtils.waitForCondition(() => {
    let { sessions } =
      AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
    return !Array.from(sessions.entries()).filter(
      ([, v]) => v.session_id === sessionId
    ).length;
  }, "Waiting for sessions to clean up.");
  // Session ended without a ping being sent. Success!
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_newtab_categorization_sends_ping() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });

  Services.fog.testResetFOG();
  sendTriggerMessageSpy.resetHistory();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  let pingSent = false;
  GleanPings.newtab.testBeforeNextSubmit(reason => {
    pingSent = true;
    Assert.equal(reason, "component_init");
  });
  await TelemetryFeed.sendPageTakeoverData();
  Assert.ok(pingSent, "ping was sent");

  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that we set Glean.newtab.highlightsEnabled to true if the highlights
 * section is enabled.
 */
add_task(async function test_newtab_highlights_enabled_pref() {
  Services.fog.testResetFOG();
  sendTriggerMessageSpy.resetHistory();
  await GleanPings.newtab.testSubmission(
    () => {
      Assert.ok(
        Glean.newtab.highlightsEnabled.testGetValue(),
        "Highlights are reported as being enabled."
      );
    },
    async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.newtabpage.activity-stream.feeds.section.highlights", true],
          ["browser.newtabpage.activity-stream.telemetry", true],
        ],
      });
      let tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:newtab",
        false // waitForLoad; about:newtab is cached so this would never resolve
      );

      await TestUtils.waitForCondition(
        () => sendTriggerMessageSpy.called,
        "After about:newtab finishes loading"
      );
      sendTriggerMessageSpy.resetHistory();

      await TestUtils.waitForCondition(
        () => !!Glean.newtab.opened.testGetValue("newtab"),
        "We expect the newtab open to be recorded"
      );

      BrowserTestUtils.removeTab(tab);
    },
    5000 /* timeout to send the ping */
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that active time on a newtab reaches the newtab ping. Asserts at
 * submission time because testGetValue() alone cannot tell a value that rode
 * along in the ping from one recorded too late and cleared by the submit.
 *
 * This is the only coverage of the real isDwellTargetInForeground path, everything
 * else stubs it out.
 */
add_task(async function test_newtab_dwell_time_in_ping() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });

  Services.fog.testResetFOG();
  sendTriggerMessageSpy.resetHistory();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  TelemetryFeed.init();

  // The feed credits dwell only up to its last "active" notification, and
  // EventStateManager's idle tick fires on a deadline no test controls.
  let fakeNow = 0;
  let nowStub = sandbox.stub(TelemetryFeed, "now").callsFake(() => fakeNow);

  // Leave the feed's idea of the user idle again, so a later test does not
  // inherit a running stopwatch.
  registerCleanupFunction(() => {
    Services.obs.notifyObservers(
      null,
      "user-interaction-inactive-non-synthesized"
    );
  });

  // Dwell only accrues for the selected tab of the frontmost window. Assert it
  // here so a focus problem in CI fails with the reason rather than as a
  // missing metric later on.
  await SimpleTest.promiseFocus(window);
  Assert.equal(
    TelemetryFeed.getActiveChromeWindow(),
    window,
    "The test window holds focus"
  );

  await GleanPings.newtab.testSubmission(
    () => {
      let dwell = Glean.newtab.dwellTime.testGetValue("newtab");
      Assert.ok(dwell, "dwell_time is in the newtab ping");
      Assert.equal(dwell.count, 1, "One sample for the one visit");
      Assert.greater(dwell.sum, 0, "Some active time was measured");
    },
    async () => {
      let tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:newtab",
        false // waitForLoad. about:newtab is cached so this would never resolve
      );

      await TestUtils.waitForCondition(
        () => sendTriggerMessageSpy.called,
        "After about:newtab finishes loading"
      );
      sendTriggerMessageSpy.resetHistory();

      await TestUtils.waitForCondition(
        () => !!Glean.newtab.opened.testGetValue("newtab"),
        "Expect the newtab open to be recorded"
      );

      // Notify directly rather than waiting out the real EventStateManager
      // interval, twice with the clock advanced in between so that an idle tick
      // landing anywhere in here still has an interval to credit.
      Services.obs.notifyObservers(
        null,
        "user-interaction-active-non-synthesized"
      );
      fakeNow += 100;
      Services.obs.notifyObservers(
        null,
        "user-interaction-active-non-synthesized"
      );
      fakeNow += 100;

      BrowserTestUtils.removeTab(tab);
    },
    5000 /* timeout to send the ping */
  );

  nowStub.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that active time on a page opened from the newtab is recorded under
 * newtab.opened_page_dwell_time, labelled by what was clicked to open it, and
 * that it stays out of the newtab ping.
 *
 * It drives TelemetryFeed the way PlacesFeed does, rather than clicking a
 * real card, because the destination <browser> is what the measurement hangs
 * off.
 */
add_task(async function test_opened_page_dwell_time() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });

  Services.fog.testResetFOG();
  sendTriggerMessageSpy.resetHistory();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");

  // The train-hop variants run this file against whichever newtab the older
  // Firefox has, which is its built-in one whenever the add-on under test did
  // not take over. That one predates this measurement entirely.
  if (typeof TelemetryFeed.handleDwellLinkOpened !== "function") {
    Assert.ok(true, "Skipping: this newtab predates opened_page_dwell_time");
    return;
  }

  TelemetryFeed.init();

  let fakeNow = 0;
  let nowStub = sandbox.stub(TelemetryFeed, "now").callsFake(() => fakeNow);

  // Leave the feed's idea of the user idle again, so a later test doesn't
  // inherit a running stopwatch.
  registerCleanupFunction(() => {
    Services.obs.notifyObservers(
      null,
      "user-interaction-inactive-non-synthesized"
    );
  });

  await SimpleTest.promiseFocus(window);
  Assert.equal(
    TelemetryFeed.getActiveChromeWindow(),
    window,
    "The test window holds focus"
  );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  TelemetryFeed.handleDwellLinkOpened({
    data: {
      browser: tab.linkedBrowser,
      dwell_label: "story_organic",
    },
  });

  // The destination load is what starts the measurement.
  let loaded = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    "https://example.com/"
  );
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.com/"
  );
  await loaded;

  // Fire the notifications directly rather than waiting out the real
  // EventStateManager interval. Two of them, with the feed's clock advanced in
  // between, because the credit runs up to the most recent one.
  Services.obs.notifyObservers(null, "user-interaction-active-non-synthesized");
  fakeNow += 100;
  Services.obs.notifyObservers(null, "user-interaction-active-non-synthesized");

  BrowserTestUtils.removeTab(tab);
  Services.obs.notifyObservers(
    null,
    "user-interaction-inactive-non-synthesized"
  );

  let dwell =
    Glean.newtab.openedPageDwellTime.story_organic.testGetValue("metrics");
  Assert.ok(dwell, "The opened page's dwell was recorded");
  Assert.equal(dwell.count, 1, "One sample for the one page");
  Assert.greater(dwell.sum, 0, "Some active time was measured");
  Assert.equal(
    Glean.newtab.openedPageDwellTime.story_sponsored.testGetValue("metrics"),
    null,
    "Nothing landed under the wrong label"
  );
  Assert.equal(
    Glean.newtab.openedPageDwellTime.story_organic.testGetValue("newtab"),
    null,
    "The sample is not in newtab ping storage, so no newtab ping carries it"
  );

  nowStub.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * The same path, but with no activity notification after the page loads:
 * the real <browser>'s location changes and its tab's TabClose are what
 * start and end the measurement. A reload in between is the same visit.
 */
add_task(async function test_opened_page_dwell_time_follows_the_tab() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });

  Services.fog.testResetFOG();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  if (typeof TelemetryFeed.handleDwellLinkOpened !== "function") {
    Assert.ok(true, "Skipping: this newtab predates opened_page_dwell_time");
    return;
  }

  let fakeNow = 0;
  let nowStub = sandbox.stub(TelemetryFeed, "now").callsFake(() => fakeNow);
  let observeStub = ignoreRealIdleTicks(TelemetryFeed);
  registerCleanupFunction(() => {
    Services.obs.notifyObservers(
      null,
      "user-interaction-inactive-non-synthesized"
    );
  });

  await SimpleTest.promiseFocus(window);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  // The user is interacting before the click, and no notification follows.
  Services.obs.notifyObservers(null, "user-interaction-active-non-synthesized");
  TelemetryFeed.handleDwellLinkOpened({
    data: {
      browser: tab.linkedBrowser,
      dwell_label: "story_organic",
    },
  });

  let loaded = BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    "https://example.com/"
  );
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.com/"
  );
  await loaded;

  fakeNow = 1000;
  await BrowserTestUtils.reloadTab(tab);

  fakeNow = 3000;
  BrowserTestUtils.removeTab(tab);

  let dwell =
    Glean.newtab.openedPageDwellTime.story_organic.testGetValue("metrics");
  Assert.equal(dwell?.count, 1, "One sample, the reload did not split it");
  Assert.equal(
    dwell?.sum,
    3000 * 1000000,
    "Measured from the page loading to the tab closing"
  );

  observeStub.restore();
  nowStub.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Closing a window does not close its tabs one by one, so no TabClose fires
 * for an opened page still showing in it. The visit has to end anyway, both
 * to record it and so that tracking it cannot keep the closed window alive.
 */
add_task(async function test_opened_page_dwell_time_ends_with_its_window() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });
  Services.fog.testResetFOG();
  let TelemetryFeed =
    AboutNewTab.activityStream.store.feeds.get("feeds.telemetry");
  if (typeof TelemetryFeed.handleDwellLinkOpened !== "function") {
    Assert.ok(true, "Skipping: this newtab predates opened_page_dwell_time");
    return;
  }

  let fakeNow = 0;
  let nowStub = sandbox.stub(TelemetryFeed, "now").callsFake(() => fakeNow);
  let foregroundStub = sandbox
    .stub(TelemetryFeed, "isDwellTargetInForeground")
    .returns(true);
  let observeStub = ignoreRealIdleTicks(TelemetryFeed);

  let win = await BrowserTestUtils.openNewBrowserWindow();
  let browser = win.gBrowser.selectedBrowser;
  Services.obs.notifyObservers(null, "user-interaction-active-non-synthesized");
  TelemetryFeed.handleDwellLinkOpened({
    data: { browser, dwell_label: "story_organic" },
  });
  let loaded = BrowserTestUtils.browserLoaded(
    browser,
    false,
    "https://example.com/"
  );
  BrowserTestUtils.startLoadingURIString(browser, "https://example.com/");
  await loaded;

  fakeNow = 2000;
  await BrowserTestUtils.closeWindow(win);

  let dwell =
    Glean.newtab.openedPageDwellTime.story_organic.testGetValue("metrics");
  Assert.equal(dwell?.count, 1, "Closing the window ended the visit");
  Assert.equal(
    dwell?.sum,
    2000 * 1000000,
    "Measured from the page loading to the window closing"
  );

  observeStub.restore();
  Services.obs.notifyObservers(
    null,
    "user-interaction-inactive-non-synthesized"
  );
  foregroundStub.restore();
  nowStub.restore();
  await SpecialPowers.popPrefEnv();
});

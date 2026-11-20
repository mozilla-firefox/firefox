/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * setupDwellFeed plus one session the user has already seen.
 *
 * @param {object} sandbox a sinon sandbox
 * @returns {object} the instance, its session, and a setTime(ms) helper
 */
function setupDwellTest(sandbox) {
  let ctx = setupDwellFeed(sandbox);
  ctx.session = ctx.instance.addSession("port1");
  ctx.session.perf.visibility_event_rcvd_ts = 1;
  return ctx;
}

/**
 * End a session and return the dwell_time distribution as the newtab ping saw
 * it. Read at submission time because testGetValue() alone cannot tell a value
 * that rode along in the ping from one recorded too late and cleared by the
 * submit.
 *
 * @param {object} instance a TelemetryFeed
 * @param {string} portID the session to end
 * @returns {Promise<object|null>} null if no ping carried a sample
 */
async function endSessionAndReadDwell(instance, portID) {
  let dwell = null;
  GleanPings.newtab.testBeforeNextSubmit(() => {
    dwell = Glean.newtab.dwellTime.testGetValue("newtab");
  });
  await instance.endSession(portID);
  return dwell;
}

/**
 * Open a link and let the destination land, which takes an activity
 * notification for the feed to notice.
 *
 * @param {object} instance a TelemetryFeed
 * @param {object} browser the <browser> that receives the load
 * @param {object} [data] the dwell_label the link opted in with
 * @param {string} [url] where the link lands
 */
function openAndLand(
  instance,
  browser,
  data = {},
  url = "https://example.com/story"
) {
  openLinkFrom(instance, browser, data);
  browser.goTo(url);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
}

add_task(async function test_dwell_time_accrues_while_active_and_foreground() {
  info(
    "TelemetryFeed should accrue dwell time while the user is interacting " +
      "and the newtab is in the foreground"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellTest(sandbox);

  // Interaction starts at 0 and continues at 5000. The inactive tick at 10000
  // says the last interval had no input, so only 0-5000 is credited.
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  let dwell = await endSessionAndReadDwell(instance, "port1");

  Assert.ok(dwell, "dwell_time was recorded");
  Assert.equal(dwell.count, 1, "Exactly one sample per newtab session");
  Assert.equal(
    dwell.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "Credited up to the last notification that said the user was there"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_partial_credit_at_session_end() {
  info(
    "TelemetryFeed should credit a session that ends mid-interaction with " +
      "its real elapsed time, not a whole interval"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellTest(sandbox);

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(1200);

  let dwell = await endSessionAndReadDwell(instance, "port1");

  Assert.ok(dwell, "dwell_time was recorded");
  Assert.equal(
    dwell.sum,
    1200 * NS_PER_MS,
    "A visit shorter than one interval still records its real duration"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_sums_separate_runs() {
  info(
    "TelemetryFeed should add up the separate runs of interaction in one " +
      "newtab session"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellTest(sandbox);

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(5000);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(10000);
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  // A long pause, then a second run of the same length.
  setTime(30000);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(35000);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(40000);
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  let dwell = await endSessionAndReadDwell(instance, "port1");

  Assert.equal(dwell.count, 1, "Still one sample for the session");
  Assert.equal(
    dwell.sum,
    10000 * NS_PER_MS,
    "Both runs are counted, the idle stretch between them is not"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_stops_when_newtab_leaves_foreground() {
  info(
    "TelemetryFeed should stop accruing dwell time once the newtab is no " +
      "longer the foreground tab, even while the user keeps interacting"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellTest(sandbox);

  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  // The user switches to another tab and carries on interacting there. The
  // switch is only noticed at the next notification, so this newtab keeps the
  // interval it was left during.
  instance.isDwellTargetInForeground.returns(false);
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 4);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  let dwell = await endSessionAndReadDwell(instance, "port1");

  Assert.equal(
    dwell.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "Interaction in another tab is not credited to this newtab"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_absent_when_no_activity() {
  info(
    "TelemetryFeed should leave dwell_time absent for a newtab the user " +
      "never interacted with"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellTest(sandbox);

  setTime(60000);

  Assert.equal(
    await endSessionAndReadDwell(instance, "port1"),
    null,
    "dwell_time is absent, not zero"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_clamped_when_visible_after_cutoff() {
  info(
    "TelemetryFeed should credit nothing, rather than negative time, when a " +
      "newtab becomes visible after the last moment the user was known active"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);

  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  setTime(4000);
  let session = instance.addSession("port1");
  instance.saveSessionPerfData("port1", { visibility_event_rcvd_ts: 1 });
  Assert.equal(session.dwellStartedAt, 4000, "The stopwatch started");

  // The interval that just ended saw no input, so the cutoff is the previous
  // notification, which predates this newtab becoming visible.
  setTime(5000);
  instance.observe(null, USER_INTERACTION_INACTIVE, null);
  Assert.equal(session.dwellTimeMs, 0, "No negative time was accrued");

  Assert.equal(
    await endSessionAndReadDwell(instance, "port1"),
    null,
    "dwell_time is absent rather than negative"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_only_credits_foreground_session() {
  info(
    "TelemetryFeed should credit only the foreground session when several " +
      "newtabs are open"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);

  let foreground = instance.addSession("port-foreground");
  foreground.perf.visibility_event_rcvd_ts = 1;
  let background = instance.addSession("port-background");
  background.perf.visibility_event_rcvd_ts = 1;
  instance.isDwellTargetInForeground.callsFake(
    session => session === foreground
  );

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  Assert.equal(
    await endSessionAndReadDwell(instance, "port-background"),
    null,
    "The background session contributes nothing"
  );

  let dwell = await endSessionAndReadDwell(instance, "port-foreground");
  Assert.equal(dwell.count, 1, "Only the foreground session recorded a sample");
  Assert.equal(
    dwell.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "Credited to the session the user was actually looking at"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_starts_for_a_newtab_opened_mid_run() {
  info(
    "TelemetryFeed should start the stopwatch when a newtab becomes visible " +
      "during a run of interaction already under way"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);

  // The user is already interacting when the tab opens. The next notification
  // could be a whole interval away.
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  setTime(1000);
  let session = instance.addSession("port1");
  instance.saveSessionPerfData("port1", { visibility_event_rcvd_ts: 1 });
  Assert.equal(
    session.dwellStartedAt,
    1000,
    "The stopwatch started when the newtab became visible"
  );

  setTime(3000);
  let dwell = await endSessionAndReadDwell(instance, "port1");

  Assert.equal(
    dwell.sum,
    2000 * NS_PER_MS,
    "Timed from becoming visible, not from the next notification"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_dwell_time_not_recorded_when_telemetry_disabled() {
  info(
    "TelemetryFeed should not leave a dwell sample behind when no newtab " +
      "ping will be submitted for the session"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellTest(sandbox);
  Services.prefs.setBoolPref(PREF_TELEMETRY, false);

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(3000);
  await instance.endSession("port1");

  Assert.equal(
    Glean.newtab.dwellTime.testGetValue("newtab"),
    null,
    "Nothing is left in ping-lifetime storage for a later ping to pick up"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_isDwellTargetInForeground() {
  info(
    "TelemetryFeed.isDwellTargetInForeground should only accept the selected " +
      "browser of the focused window"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  let win = { closed: false, gBrowser: { selectedBrowser: null } };
  let browser = { documentGlobal: win };
  win.gBrowser.selectedBrowser = browser;
  let session = { browserRef: new WeakRef(browser) };
  sandbox.stub(instance, "getActiveChromeWindow").returns(win);

  Assert.ok(
    instance.isDwellTargetInForeground(session),
    "Selected browser of the focused window is in the foreground"
  );

  instance.getActiveChromeWindow.returns({});
  Assert.ok(
    !instance.isDwellTargetInForeground(session),
    "Not in the foreground when another window has focus"
  );

  instance.getActiveChromeWindow.returns(win);
  win.gBrowser.selectedBrowser = { documentGlobal: win };
  Assert.ok(
    !instance.isDwellTargetInForeground(session),
    "Not in the foreground when another tab is selected"
  );

  win.gBrowser.selectedBrowser = browser;
  win.closed = true;
  Assert.ok(
    !instance.isDwellTargetInForeground(session),
    "Not in the foreground once the window has closed"
  );

  win.closed = false;
  Assert.ok(
    !instance.isDwellTargetInForeground({}),
    "A session with no browser is never in the foreground"
  );

  sandbox.restore();
});

add_task(async function test_user_interaction_observers_registered() {
  info(
    "TelemetryFeed should observe the user interaction topics between init " +
      "and uninit"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  instance.init();

  sandbox.stub(instance, "isDwellTargetInForeground").returns(true);
  sandbox.stub(instance, "now").returns(0);
  let session = instance.addSession("port1");
  Services.obs.notifyObservers(null, USER_INTERACTION_ACTIVE);
  Assert.equal(
    session.dwellStartedAt,
    0,
    "A real notification drives the stopwatch"
  );

  instance.uninit();

  session.dwellStartedAt = null;
  Services.obs.notifyObservers(null, USER_INTERACTION_ACTIVE);
  Assert.equal(
    session.dwellStartedAt,
    null,
    "Notifications no longer reach the feed after uninit"
  );

  sandbox.restore();
});

add_task(async function test_wallpaper_reset_removes_upload_without_counts() {
  info("Resetting with the library off must not throw or record a remove");
  Services.fog.testResetFOG();

  const PORT_ID = "port-reset-no-library";
  let instance = new TelemetryFeed();
  instance.addSession(PORT_ID);

  instance.onAction({
    type: actionTypes.WALLPAPER_REMOVE_UPLOAD,
    meta: { fromTarget: PORT_ID },
  });

  Assert.equal(
    Glean.newtab.wallpaperSavedRemove.testGetValue(),
    null,
    "No wallpaper_saved_remove event without counts"
  );
});

add_task(async function test_wallpaper_category_click_reaches_its_handler() {
  const sandbox = sinon.createSandbox();
  const feed = new TelemetryFeed();
  const handler = sandbox.stub(feed, "handleWallpaperUserEvent");
  const setPref = sandbox.stub(feed, "handleSetPref");

  info("A category click must not fall through into the pref handler");

  await feed.onAction({
    type: actionTypes.WALLPAPER_CATEGORY_CLICK,
    data: {},
  });

  Assert.ok(handler.calledOnce, "The wallpaper handler is the one that runs");
  Assert.ok(setPref.notCalled, "And the pref handler is not");

  sandbox.restore();
});

add_task(async function test_opened_page_dwell_time_accrues_on_opened_page() {
  info(
    "TelemetryFeed should accrue active time on a page opened from the " +
      "newtab and record it when the page goes away"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(instance, browser);

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  let dwell = readOpenedPageDwell("story_organic");
  Assert.ok(dwell, "newtab.opened_page_dwell_time was recorded");
  Assert.equal(dwell.count, 1, "Exactly one sample per opened page");
  Assert.equal(
    dwell.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "Credited up to the last notification that said the user was there"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_labels() {
  info(
    "TelemetryFeed should label an opened page by what was clicked to open it"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);

  const allLabels = [
    "topsite_organic",
    "topsite_sponsored",
    "story_organic",
    "story_sponsored",
  ];

  let now = 0;
  let expected = new Map(allLabels.map(l => [l, 0]));
  for (const label of allLabels) {
    let browser = makeFakeBrowser();
    setTime(now);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser, { dwell_label: label });

    now += USER_INTERACTION_INTERVAL_MS;
    setTime(now);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    now += USER_INTERACTION_INTERVAL_MS;
    setTime(now);
    browser.isConnected = false;
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    expected.set(label, 1);
    for (const other of allLabels) {
      Assert.equal(
        readOpenedPageDwell(other)?.count ?? 0,
        expected.get(other),
        `After a ${label} open, ${other} has the samples it should`
      );
    }
  }

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_ignores_untagged_opens() {
  info(
    "TelemetryFeed should not measure links that did not opt in with a " +
      "dwell_label, such as the widgets"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(instance, browser, { dwell_label: undefined });

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  Assert.equal(
    readOpenedPageDwell("story_organic"),
    null,
    "Nothing was measured for a link that did not opt in"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_waits_for_the_destination() {
    info(
      "TelemetryFeed should not accrue on an opened page while the tab is " +
        "still showing the newtab, which is what the user is looking at"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openLinkFrom(instance, browser);

    // A slow destination: the user keeps interacting, but nothing has landed.
    setTime(USER_INTERACTION_INTERVAL_MS);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 2);
    browser.isConnected = false;
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic"),
      null,
      "Nothing is credited for time spent waiting for the page"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(
  async function test_opened_page_dwell_time_ends_when_the_tab_moves_on() {
    info(
      "TelemetryFeed should stop attributing a page once its tab is showing " +
        "something else"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    setTime(USER_INTERACTION_INTERVAL_MS);
    browser.goTo("https://example.com/somewhere-else");
    instance.observe(null, USER_INTERACTION_ACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "The page is recorded once the tab has moved on"
    );

    setTime(USER_INTERACTION_INTERVAL_MS * 4);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 5);
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic").count,
      1,
      "Time on the page the user went to next is not credited to the newtab"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_does_not_see_a_redirect() {
  info(
    "TelemetryFeed should attribute the page an ad click lands on, not the " +
      "server-side redirect it passed through on the way, however slow the " +
      "bounce is. A 30x never becomes a document, so there is nothing to " +
      "attribute until the landing page arrives."
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser, { dwell_label: "story_sponsored" });

  // The ad server takes its time bouncing the user on to the advertiser. A
  // redirect never becomes a document of its own, so the tab is still showing
  // the newtab and these sweeps have nothing to attribute yet, no matter how
  // many of them the bounce spans.
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  Assert.equal(
    readOpenedPageDwell("story_sponsored"),
    null,
    "Nothing is attributed while the redirect is still in flight"
  );

  browser.goTo("https://advertiser.example/landing");
  setTime(USER_INTERACTION_INTERVAL_MS * 3);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 4);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 5);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  Assert.equal(
    readOpenedPageDwell("story_sponsored")?.count,
    1,
    "One sample, for the page the user actually read"
  );
  // The landing page arrives at two intervals and the user was last active at
  // four, and the location change starts the clock the moment it arrives.
  Assert.equal(
    readOpenedPageDwell("story_sponsored").sum,
    USER_INTERACTION_INTERVAL_MS * 2 * NS_PER_MS,
    "Only time on the landing page, and the slow redirect cut nothing short"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_survives_same_document_nav() {
    info(
      "TelemetryFeed should keep measuring a page whose address changes " +
        "without its document changing, such as a fragment link or a pushState"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    // The reader clicks a table-of-contents anchor. The address changes, the
    // document does not, so this is still the page the newtab sent them to.
    setTime(USER_INTERACTION_INTERVAL_MS);
    browser.goToInDocument("https://example.com/story#comments");
    instance.observe(null, USER_INTERACTION_ACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic"),
      null,
      "Moving around within the page does not end the measurement"
    );

    setTime(USER_INTERACTION_INTERVAL_MS * 3);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 4);
    browser.isConnected = false;
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * 3 * NS_PER_MS,
      "The whole visit is credited, not just the part before the anchor"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(
  async function test_opened_page_dwell_time_shares_a_browser_with_session() {
    info(
      "TelemetryFeed should never credit a newtab session and the page it " +
        "opened at the same time. This is the ordinary path, because a story " +
        "clicked without a modifier replaces the newtab in its own tab, so " +
        "both dwell targets hang off one <browser>."
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    // One tab, so whichever of the two the tab is currently showing is the one
    // in front of the user. This is what the real isDwellTargetInForeground
    // works out from the DOM, where a newtab session stops qualifying as soon
    // as its <browser> is showing a web page instead.
    instance.isDwellTargetInForeground.callsFake(target => {
      const showingNewtab =
        browser.browsingContext.currentWindowGlobal.documentURI.scheme ===
        "about";
      return target.label ? !showingNewtab : showingNewtab;
    });

    instance.handleNewTabInit({
      meta: { fromTarget: "port1" },
      data: { url: "about:newtab", browser },
    });
    instance.sessions.get("port1").perf.visibility_event_rcvd_ts = 1;

    // The user reads the newtab for an interval, then clicks a story.
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS);
    openLinkFrom(instance, browser);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic"),
      null,
      "The story accrues nothing while the newtab is still the page on screen"
    );

    // The story lands in the same tab, which ends the newtab's turn.
    browser.goTo("https://example.com/story");
    setTime(USER_INTERACTION_INTERVAL_MS * 2);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 3);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);

    let newtabDwell;
    GleanPings.newtab.testBeforeNextSubmit(() => {
      newtabDwell = Glean.newtab.dwellTime.testGetValue("newtab");
    });
    await instance.endSession("port1");

    setTime(USER_INTERACTION_INTERVAL_MS * 4);
    browser.isConnected = false;
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    // The story lands one interval in, and the location change hands the time
    // over at that moment rather than at the next notification.
    Assert.equal(
      newtabDwell?.sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "The newtab is credited only up to the story replacing it"
    );
    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * 2 * NS_PER_MS,
      "The story is credited only from the point it replaced the newtab"
    );
    Assert.equal(
      newtabDwell.sum + readOpenedPageDwell("story_organic").sum,
      USER_INTERACTION_INTERVAL_MS * 3 * NS_PER_MS,
      "Between them they account for the active time exactly once, so no " +
        "interval was counted twice or dropped"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_ignores_a_failed_load() {
  info(
    "TelemetryFeed should not treat an error page as the page the newtab " +
      "sent the user to"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(
    instance,
    browser,
    {},
    "about:neterror?u=https%3A//example.com/story"
  );

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  Assert.equal(
    readOpenedPageDwell("story_organic"),
    null,
    "Time staring at an error page is not time spent reading the story"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_ends_when_the_user_goes_back() {
    info(
      "TelemetryFeed should stop attributing a page when the tab goes back to " +
        "a newtab, rather than crediting the newtab to the story"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    setTime(USER_INTERACTION_INTERVAL_MS);
    browser.goTo("about:newtab");
    instance.observe(null, USER_INTERACTION_ACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "The story was recorded when the tab went back to a newtab"
    );

    setTime(USER_INTERACTION_INTERVAL_MS * 4);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 5);
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic").sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "Time on the newtab itself is not added to the story"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(
  async function test_opened_page_dwell_time_ends_when_a_newtab_opens_here() {
    info(
      "TelemetryFeed should stop attributing a page as soon as a newtab " +
        "session starts in the same <browser>, whatever route got it there"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    // The tab still reports the story, so only the newtab session gives it away.
    setTime(USER_INTERACTION_INTERVAL_MS);
    instance.handleNewTabInit({
      meta: { fromTarget: "port1" },
      data: { url: "about:newtab", browser },
    });

    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "The story was recorded when the newtab took over the tab"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_only_credits_foreground() {
  info(
    "TelemetryFeed should credit only the opened page the user is actually " +
      "looking at"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let foreground = makeFakeBrowser();
  let background = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  instance.isDwellTargetInForeground.callsFake(
    target => target.browserRef?.deref() === foreground
  );
  openAndLand(instance, foreground, { dwell_label: "topsite_organic" });
  openAndLand(instance, background, { dwell_label: "story_organic" });

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  foreground.isConnected = false;
  background.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  Assert.equal(
    readOpenedPageDwell("topsite_organic").sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "The page in front of the user was credited"
  );
  Assert.equal(
    readOpenedPageDwell("story_organic"),
    null,
    "The page open in a background tab was not"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_not_in_the_newtab_ping() {
  info(
    "TelemetryFeed should keep an opened page's dwell out of the newtab " +
      "ping, and a newtab ping submission should not clear it"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(instance, browser);

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  let session = instance.addSession("port1");
  session.perf.visibility_event_rcvd_ts = 1;
  let inNewtabPing;
  GleanPings.newtab.testBeforeNextSubmit(() => {
    inNewtabPing =
      Glean.newtab.openedPageDwellTime.story_organic.testGetValue("newtab");
  });
  await instance.endSession("port1");

  Assert.equal(inNewtabPing, null, "The newtab ping carried no sample");
  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "The sample waits for the metrics ping, and the newtab submit left it alone"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_records_with_the_newtab_ping_off() {
    info(
      "TelemetryFeed should record an opened page's dwell when the newtab " +
        "ping is disabled, since the sample no longer rides that ping"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    Services.prefs.setBoolPref(PREF_NEWTAB_PING_ENABLED, false);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    setTime(USER_INTERACTION_INTERVAL_MS);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 2);
    browser.isConnected = false;
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "The pref that gates the newtab ping does not gate this metric"
    );

    Services.prefs.clearUserPref(PREF_NEWTAB_PING_ENABLED);
    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_rejects_an_unknown_label() {
  info(
    "TelemetryFeed should ignore a link whose dwell_label is not one the " +
      "metric declares, since the label is chosen in content"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(instance, browser, { dwell_label: "story_bogus" });

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  for (const label of [
    "topsite_organic",
    "topsite_sponsored",
    "story_organic",
    "story_sponsored",
  ]) {
    Assert.equal(
      readOpenedPageDwell(label),
      null,
      `An unknown label recorded nothing under ${label}`
    );
  }
  // A labeled metric accepts any label string and files an undeclared one
  // under __other__, so checking only the four declared labels would pass
  // whether or not the label was validated.
  Assert.equal(
    readOpenedPageDwell("__other__"),
    null,
    "And nothing under __other__ either, so the label really was rejected"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_not_recorded_when_telemetry_disabled() {
    info(
      "TelemetryFeed should not record an opened page's dwell when newtab " +
        "telemetry is off"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    Services.prefs.setBoolPref(PREF_TELEMETRY, false);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    setTime(USER_INTERACTION_INTERVAL_MS);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 2);
    browser.isConnected = false;
    instance.observe(null, USER_INTERACTION_INACTIVE, null);

    Assert.equal(
      readOpenedPageDwell("story_organic"),
      null,
      "Nothing is left in ping-lifetime storage for the metrics ping to pick up"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_finalized_at_uninit() {
  info(
    "TelemetryFeed should record what an opened page accrued when the feed " +
      "goes away, rather than dropping it"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(instance, browser);

  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.uninit();

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "The page the user was reading at shutdown was credited"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_reopening_the_same_browser() {
    info(
      "TelemetryFeed should finalize a page rather than lose it when the same " +
        "<browser> opens another link"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openAndLand(instance, browser);

    setTime(USER_INTERACTION_INTERVAL_MS);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 2);
    instance.observe(null, USER_INTERACTION_INACTIVE, null);
    openLinkFrom(instance, browser, { dwell_label: "topsite_organic" });

    Assert.equal(
      readOpenedPageDwell("story_organic")?.sum,
      USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
      "The first page was recorded when the second one took its place"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_caps_tracked_pages() {
  info(
    "TelemetryFeed should finalize the oldest opened page rather than track " +
      "an unbounded number of them"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let oldest = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openAndLand(instance, oldest);
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  // Open exactly enough pages that the first one has to make room.
  for (let i = 0; i < 100; i++) {
    openLinkFrom(instance, makeFakeBrowser(), {
      dwell_label: "topsite_organic",
    });
  }

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    USER_INTERACTION_INTERVAL_MS * NS_PER_MS,
    "The oldest page was recorded, not dropped, to make room"
  );

  teardownDwellTest(sandbox);
});

/**
 * Sets up a TelemetryFeed with a newtab session reachable from a fake source
 * browser, as the navigation target notification would hand us.
 *
 * @param {object} state Store state the feed should resolve tiles against.
 * @returns {object} The feed, a notify helper, the session id, and cleanup.
 */
function withNavigationTargetFeed(state) {
  const PORT_ID = "port-decaf";
  const SESSION_ID = "decafc0ffee";
  let sourceTabBrowser = {};
  let instance = new TelemetryFeed();

  instance.store = {
    getState: () => ({ Prefs: { values: {} }, ...state }),
  };
  instance.sessions.set(PORT_ID, { session_id: SESSION_ID });
  AboutNewTabParent.loadedTabs.set(sourceTabBrowser, { portID: PORT_ID });

  let notify = (url, { background = false, foregroundTab = false } = {}) => {
    let gBrowser = {};
    const listenable = {
      addEventListener() {},
      removeEventListener() {},
    };
    let createdTabBrowser = {
      permanentKey: {},
      isConnected: true,
      documentGlobal: { gBrowser, ...listenable },
    };
    gBrowser.selectedBrowser = background ? {} : createdTabBrowser;
    gBrowser.getTabForBrowser = browser =>
      browser === createdTabBrowser
        ? { openerTab: { linkedBrowser: sourceTabBrowser }, ...listenable }
        : null;
    instance.handleCreatedNavigationTarget({
      url,
      // A foreground tab is already selected when Firefox reads the source.
      sourceTabBrowser: foregroundTab ? createdTabBrowser : sourceTabBrowser,
      createdTabBrowser,
    });
  };

  let intend = (url, userEvent) =>
    instance.handleBrowserLinkOpenIntent({
      data: { url, userEvent },
      meta: { fromTarget: PORT_ID },
    });

  return {
    instance,
    notify,
    intend,
    sessionId: SESSION_ID,
    cleanup: () => AboutNewTabParent.loadedTabs.delete(sourceTabBrowser),
  };
}

add_task(
  async function test_handleCreatedNavigationTarget_records_topsite_click() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should record a " +
        "topsites.click for a tile Firefox opened in a new tab"
    );

    Services.fog.testResetFOG();
    let { notify, sessionId, cleanup } = withNavigationTargetFeed({
      TopSites: {
        rows: [
          { url: "https://other.example/" },
          { url: "https://example.com/", isPinned: true },
        ],
      },
    });

    notify("https://example.com/", { background: true });

    let clicks = Glean.topsites.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: sessionId,
      is_sponsored: String(false),
      position: String(1),
      is_pinned: String(true),
      event_source: "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK",
      background: String(true),
    });

    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_records_foreground_tab() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should find newtab through " +
        "the opener when the new tab was selected before the notification"
    );

    Services.fog.testResetFOG();
    let { notify, sessionId, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [{ url: "https://example.com/" }] },
    });

    notify("https://example.com/", { foregroundTab: true });

    let clicks = Glean.topsites.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.equal(clicks[0].extra.newtab_visit_id, sessionId);
    Assert.equal(clicks[0].extra.background, String(false));

    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_records_sponsored_topsite_without_callback() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should record a click for " +
        "a sponsored top site without sending an ad callback"
    );

    Services.fog.testResetFOG();
    let sandbox = sinon.createSandbox();
    let { instance, notify, sessionId, cleanup } = withNavigationTargetFeed({
      TopSites: {
        rows: [
          {
            url: "https://example.com/",
            label: "Example",
            sponsored_position: 1,
            sponsored_tile_id: 1,
            sponsored_click_url: "https://ads.example/click",
          },
        ],
      },
    });
    sandbox.stub(instance, "canSendUnifiedAdsTilesCallbacks").get(() => true);
    sandbox.stub(instance, "sendUnifiedAdsCallbackEvent");
    sandbox.stub(instance, "sendMacCallbackEvent");

    notify("https://example.com/");

    let clicks = Glean.topsites.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      advertiser_name: "example",
      newtab_visit_id: sessionId,
      is_sponsored: String(true),
      position: String(0),
      event_source: "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK",
      background: String(false),
    });
    Assert.ok(
      instance.sendUnifiedAdsCallbackEvent.notCalled &&
        instance.sendMacCallbackEvent.notCalled,
      "No ad callback was sent"
    );

    sandbox.restore();
    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_ignores_unknown_url() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should not record when the " +
        "url does not match a tile"
    );

    Services.fog.testResetFOG();
    let { notify, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [{ url: "https://example.com/" }] },
      DiscoveryStream: { feeds: { data: {} } },
    });

    notify("https://unrelated.example/");

    Assert.equal(
      Glean.topsites.click.testGetValue(),
      null,
      "No click recorded for a url that is not on the page"
    );
    Assert.equal(
      Glean.pocket.click.testGetValue(),
      null,
      "No story click recorded either"
    );

    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_ignores_other_browsers() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should ignore links opened " +
        "from a page that is not newtab"
    );

    Services.fog.testResetFOG();
    let { instance, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [{ url: "https://example.com/" }] },
    });

    instance.handleCreatedNavigationTarget({
      url: "https://example.com/",
      sourceTabBrowser: {},
      createdTabBrowser: {},
    });

    Assert.equal(
      Glean.topsites.click.testGetValue(),
      null,
      "No click recorded for a browser that is not a loaded newtab"
    );

    cleanup();
  }
);

const STORY_URL = "https://example.com/story";
const STORY_CLICK = {
  event: "CLICK",
  source: "CARDGRID",
  action_position: 9,
  value: {
    event_source: "card",
    card_type: "organic",
    corpus_item_id: "corpus-1",
    received_rank: 3,
    topic: "entertainment",
  },
};

add_task(
  async function test_handleCreatedNavigationTarget_records_pocket_click() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should record a " +
        "pocket.click with the payload the story sent before the open"
    );

    Services.fog.testResetFOG();
    let { notify, intend, sessionId, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [] },
    });

    intend(STORY_URL, STORY_CLICK);
    notify(STORY_URL);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.equal(
      clicks[0].extra.event_source,
      "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK"
    );
    Assert.equal(clicks[0].extra.background, String(false));
    Assert.equal(clicks[0].extra.newtab_visit_id, sessionId);
    Assert.equal(
      clicks[0].extra.position,
      String(9),
      "Position is the one the story reported, as for a plain click"
    );

    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_records_story_once() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should use a story's " +
        "payload for one open only"
    );

    Services.fog.testResetFOG();
    let { notify, intend, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [] },
    });

    intend(STORY_URL, STORY_CLICK);
    notify(STORY_URL);
    notify(STORY_URL);

    Assert.equal(Glean.pocket.click.testGetValue().length, 1);

    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_ignores_story_without_intent() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should not guess a story's " +
        "payload when the story did not send one"
    );

    Services.fog.testResetFOG();
    let { notify, intend, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [] },
    });

    intend("https://example.com/other", STORY_CLICK);
    notify(STORY_URL);

    Assert.equal(Glean.pocket.click.testGetValue(), null);

    cleanup();
  }
);

add_task(async function test_handleCreatedNavigationTarget_uses_latest_story() {
  info(
    "TelemetryFeed.handleCreatedNavigationTarget should only use the payload " +
      "of the story most recently right-clicked or middle-clicked"
  );

  Services.fog.testResetFOG();
  let { notify, intend, cleanup } = withNavigationTargetFeed({
    TopSites: { rows: [] },
  });

  intend(STORY_URL, STORY_CLICK);
  intend("https://example.com/other", STORY_CLICK);
  notify(STORY_URL);

  Assert.equal(
    Glean.pocket.click.testGetValue(),
    null,
    "An older story's payload was replaced, so nothing is recorded"
  );

  cleanup();
});

add_task(
  async function test_handleCreatedNavigationTarget_records_sponsored_story_without_callback() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should record a click for " +
        "a sponsored story without sending an ad callback"
    );

    Services.fog.testResetFOG();
    let sandbox = sinon.createSandbox();
    let { instance, notify, intend, cleanup } = withNavigationTargetFeed({
      TopSites: { rows: [] },
    });
    sandbox.stub(instance, "canSendUnifiedAdsSpocCallbacks").get(() => true);
    sandbox.stub(instance, "sendUnifiedAdsCallbackEvent");
    sandbox.stub(instance, "sendMacCallbackEvent");

    intend(STORY_URL, {
      ...STORY_CLICK,
      value: {
        event_source: "card",
        card_type: "spoc",
        shim: "https://ads.example/click",
      },
    });
    notify(STORY_URL);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.equal(clicks[0].extra.is_sponsored, String(true));
    Assert.equal(
      clicks[0].extra.event_source,
      "BROWSER_CONTEXT_MENU_OR_MIDDLE_CLICK"
    );
    Assert.ok(
      instance.sendUnifiedAdsCallbackEvent.notCalled &&
        instance.sendMacCallbackEvent.notCalled,
      "No ad callback was sent"
    );

    sandbox.restore();
    cleanup();
  }
);

add_task(
  async function test_handleCreatedNavigationTarget_records_newtab_content_click() {
    info(
      "TelemetryFeed.handleCreatedNavigationTarget should also record the " +
        "click on the newtab-content ping, without event_source or background"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);
    Services.fog.testResetFOG();
    let sandbox = sinon.createSandbox();

    let { instance, notify, intend, sessionId, cleanup } =
      withNavigationTargetFeed({ TopSites: { rows: [] } });
    // recordOrQueueEvent buffers until the session goes private, so events
    // reach the newtab-content ping only after the transition.
    instance.transitionToPrivateSession();
    sandbox.spy(instance.newtabContentPing, "sanitizeEventData");
    sandbox.spy(instance.newtabContentPing, "recordEvent");

    intend(STORY_URL, STORY_CLICK);
    notify(STORY_URL, { background: true });

    Assert.ok(
      instance.newtabContentPing.recordEvent.calledOnceWith(
        "click",
        sinon.match({
          newtab_visit_id: sessionId,
          is_sponsored: false,
          position: 9,
          corpus_item_id: "corpus-1",
          topic: "entertainment",
        })
      ),
      "Recorded the story on the newtab-content ping"
    );
    Assert.ok(
      !Object.hasOwn(
        instance.newtabContentPing.recordEvent.firstCall.args[1],
        "background"
      ),
      "background never reaches the content ping"
    );
    let sanitized =
      instance.newtabContentPing.sanitizeEventData.firstCall.returnValue;
    Assert.ok(
      !Object.hasOwn(sanitized, "event_source"),
      "event_source is stripped from the content ping"
    );

    sandbox.restore();
    cleanup();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(async function test_handleUserEvent_topsite_open_private_window() {
  info(
    "TelemetryFeed.handleUserEvent should record a topsites.click when a " +
      "top site is opened in a new or private window from newtab's menu"
  );

  Services.fog.testResetFOG();
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  const SESSION_ID = "decafc0ffee";
  instance.store = {
    getState: () => ({
      TopSites: {
        rows: [
          { url: "https://ad.example/", label: "Ad", sponsored_position: 1 },
          { isPinned: true },
        ],
      },
    }),
  };
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  instance.handleUserEvent(
    actionCreators.UserEvent({
      event: "OPEN_PRIVATE_WINDOW",
      source: "TOP_SITES",
      action_position: 1,
      value: { card_type: "pinned" },
    })
  );
  instance.handleUserEvent(
    actionCreators.UserEvent({
      event: "OPEN_PRIVATE_WINDOW",
      source: "TOP_SITES",
      action_position: 0,
      value: { card_type: "spoc" },
    })
  );

  instance.handleUserEvent(
    actionCreators.UserEvent({
      event: "OPEN_NEW_WINDOW",
      source: "TOP_SITES",
      action_position: 1,
      value: { card_type: "pinned" },
    })
  );

  let clicks = Glean.topsites.click.testGetValue();
  Assert.equal(
    clicks.length,
    3,
    "Recorded the organic and sponsored private window opens and the new window open"
  );
  Assert.equal(
    clicks[2].extra.position,
    String(1),
    "Recorded the new window open from the tile's menu"
  );
  Assert.equal(
    clicks[1].extra.is_sponsored,
    String(true),
    "Recorded the sponsored tile as sponsored"
  );
  Assert.deepEqual(clicks[0].extra, {
    newtab_visit_id: SESSION_ID,
    is_sponsored: String(false),
    position: String(1),
    is_pinned: String(true),
    event_source: "CONTEXT_MENU",
  });

  sandbox.restore();
});

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Set up an instance for the per-article dwell time: a stand-in for the
 * content ping's buffer so we can see exactly what the click row ends up
 * carrying, a session for the click path, a known substitute for the
 * randomized payload, and no dwell time noise. The prefs are cleared by
 * teardownDwellTest.
 *
 * @param {object} sandbox a sinon sandbox
 * @param {object} instance a TelemetryFeed from setupDwellFeed
 * @returns {Array} the rows handed to the content ping, in the order recorded
 */
function setupContentDwell(sandbox, instance) {
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_DWELL_TIME_ENABLED, true);
  sandbox.stub(instance.sessions, "get").returns({ session_id: "sess1" });
  sandbox.stub(instance, "randomizeOrganicContentEvent").returns({
    corpus_item_id: "randomized-id",
    topic: "randomized-topic",
  });
  sandbox.stub(instance, "dwellTimeNoise").returns(0);

  // The buffer the real ping keeps, reduced to what the dwell path touches.
  // clickRowsFrom reads each tracked row the way the ping does at submission.
  const rows = [];
  const findRow = token =>
    token ? rows.find(row => row.dwellToken === token) : undefined;
  sandbox
    .stub(instance.newtabContentPing, "recordEvent")
    .callsFake((name, data, dwellToken = null) => {
      rows.push({ name, data, dwellToken });
    });
  sandbox
    .stub(instance.newtabContentPing, "recordDwellTime")
    .callsFake((token, dwellMs) => {
      const row = findRow(token);
      if (!row) {
        return false;
      }
      if (dwellMs === null) {
        delete row.data.dwell_time;
      } else {
        row.data.dwell_time = dwellMs;
      }
      row.readDwellTime = null;
      return true;
    });
  sandbox
    .stub(instance.newtabContentPing, "trackDwellTime")
    .callsFake((token, readDwellMs) => {
      const row = findRow(token);
      if (!row) {
        return false;
      }
      row.readDwellTime = readDwellMs;
      return true;
    });
  return rows;
}

/**
 * The click rows the content ping was given, in the order recorded, as they
 * would be sent if the ping went out now.
 *
 * @param {Array} rows the array from setupContentDwell
 * @returns {Array} the payload of each click row
 */
function clickRowsFrom(rows) {
  return rows
    .filter(row => row.name === "click")
    .map(row => {
      if (row.readDwellTime) {
        const dwellMs = row.readDwellTime();
        if (dwellMs === null) {
          delete row.data.dwell_time;
        } else {
          row.data.dwell_time = dwellMs;
        }
      }
      return row.data;
    });
}

/**
 * The dwell time on every click row that carries one.
 *
 * @param {Array} rows the array from setupContentDwell
 * @returns {Array} one entry per click row that has a dwell_time
 */
function dwellTimesFrom(rows) {
  return clickRowsFrom(rows)
    .filter(data => data.dwell_time !== undefined)
    .map(data => data.dwell_time);
}

/**
 * The click event that reaches TelemetryFeed just after SafeAnchor's
 * OPEN_LINK, carrying the story identity the dwell measurement lacks.
 *
 * @param {object} instance a TelemetryFeed
 * @param {string} portId the newtab that sent it
 * @param {number} [dwellId] the id SafeAnchor put on both actions
 * @param {string} [cardType] "organic" or "spoc"
 */
function clickStoryFrom(instance, portId, dwellId, cardType = "organic") {
  instance.handleDiscoveryStreamUserEvent({
    type: actionTypes.DISCOVERY_STREAM_USER_EVENT,
    meta: { fromTarget: portId },
    data: {
      event: "CLICK",
      source: "CARDGRID",
      action_position: 0,
      value: {
        card_type: cardType,
        corpus_item_id: "true-id",
        scheduled_corpus_item_id: "true-scheduled-id",
        tile_id: 1234,
        topic: "true-topic",
        ...(dwellId ? { dwell_id: dwellId } : {}),
      },
    },
  });
}

/**
 * A click on a story and the destination landing: the two actions the content
 * process sends, then the sweep that notices the page has loaded.
 *
 * @param {object} instance a TelemetryFeed
 * @param {object} browser the <browser> that receives the load
 * @param {object} [opts] dwellId, portId, cardType, dwellLabel, isSponsored
 */
function openStoryAndRead(instance, browser, opts = {}) {
  const {
    dwellId = 1,
    portId = "port1",
    cardType = "organic",
    isSponsored = false,
    dwellLabel = isSponsored ? "story_sponsored" : "story_organic",
  } = opts;
  openLinkFrom(instance, browser, {
    dwell_label: dwellLabel,
    dwell_id: dwellId,
    port_id: portId,
  });
  clickStoryFrom(instance, portId, dwellId, cardType);
  browser.goTo("https://example.com/story");
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
}

/**
 * The user reads for a while, then closes the tab, which the feed notices on
 * its next sweep.
 *
 * @param {object} instance a TelemetryFeed
 * @param {object} browser the <browser> showing the story
 * @param {Function} setTime the clock helper from setupDwellFeed
 * @param {number} readForMs how long the user read before leaving
 */
function finishReading(instance, browser, setTime, readForMs) {
  setTime(readForMs);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);
}

/**
 * Draw many samples and summarize them.
 *
 * @param {Function} draw returns one sample
 * @returns {object} the samples, the share that are negative, and the mean of
 *  their absolute values
 */
function summarizeDraws(draw) {
  const samples = Array.from({ length: 4000 }, draw);
  return {
    samples,
    negativeShare: samples.filter(x => x < 0).length / samples.length,
    meanAbs: samples.reduce((sum, x) => sum + Math.abs(x), 0) / samples.length,
  };
}

add_task(async function test_laplaceNoise() {
  info(
    "laplaceNoise should draw from a Laplace distribution centered at 0, " +
      "whose mean absolute value is its scale"
  );
  for (const scale of [0.05, 1]) {
    const { samples, negativeShare, meanAbs } = summarizeDraws(() =>
      laplaceNoise(scale)
    );
    Assert.ok(
      samples.every(Number.isFinite),
      `Every draw at scale ${scale} is finite`
    );
    Assert.greater(
      negativeShare,
      0.45,
      `About half the draws at scale ${scale} are negative`
    );
    Assert.less(
      negativeShare,
      0.55,
      `About half the draws at scale ${scale} are negative`
    );
    Assert.less(
      Math.abs(meanAbs - scale),
      scale * 0.1,
      `The mean absolute draw at scale ${scale} is near the scale`
    );
  }
});

/**
 * How many console.error calls since the last clearEvents() mention the
 * dwell time noise scale.
 *
 * @returns {number}
 */
function countNoiseScaleErrors() {
  return Cc["@mozilla.org/consoleAPI-storage;1"]
    .getService(Ci.nsIConsoleAPIStorage)
    .getEvents()
    .filter(
      event =>
        event.level === "error" &&
        event.arguments.some(arg => String(arg).includes("noise scale"))
    ).length;
}

add_task(async function test_privatePingDwellTimeNoiseScale() {
  info(
    "TelemetryFeed should read the dwell time noise scale from its pref, and " +
      "fall back to 0.05 for anything that is not a positive number so the " +
      "noise can never be turned off"
  );
  const consoleStorage = Cc["@mozilla.org/consoleAPI-storage;1"].getService(
    Ci.nsIConsoleAPIStorage
  );
  consoleStorage.clearEvents();
  let instance = new TelemetryFeed();

  Assert.equal(
    instance.privatePingDwellTimeNoiseScale,
    0.05,
    "The default is 0.05"
  );
  Services.prefs.setStringPref(PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE, "0.2");
  Assert.equal(
    instance.privatePingDwellTimeNoiseScale,
    0.2,
    "A configured scale is used"
  );
  Assert.equal(countNoiseScaleErrors(), 0, "A usable scale logs no error");

  for (const value of ["0", "-0.1", "abc", "", "Infinity"]) {
    consoleStorage.clearEvents();
    Services.prefs.setStringPref(
      PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE,
      value
    );
    Assert.equal(
      instance.privatePingDwellTimeNoiseScale,
      0.05,
      `"${value}" falls back to 0.05`
    );
    Assert.equal(countNoiseScaleErrors(), 1, `"${value}" logs an error`);
  }

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE);
});

add_task(async function test_dwellTimeNoise_uses_the_configured_scale() {
  info(
    "TelemetryFeed.dwellTimeNoise should draw Laplace noise at the configured " +
      "scale"
  );
  let instance = new TelemetryFeed();
  Services.prefs.setStringPref(PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE, "0.5");
  const { meanAbs } = summarizeDraws(() => instance.dwellTimeNoise());
  Assert.less(
    Math.abs(meanAbs - 0.5),
    0.05,
    "The mean absolute draw is near the configured 0.5"
  );
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE);
});

add_task(async function test_content_dwell_lands_on_the_clicked_story() {
  info(
    "TelemetryFeed should report the dwell time on the newtab-content click " +
      "row itself, rather than on a second row of its own"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

  let clicks = clickRowsFrom(rows);
  Assert.equal(clicks.length, 1, "One click row for the one story");
  // Deep-equal rather than spot checks, so that a field added here later has
  // to be justified rather than slipping through.
  Assert.deepEqual(
    clicks[0],
    {
      corpus_item_id: "randomized-id",
      topic: "randomized-topic",
      dwell_time: 10000,
    },
    "The randomized identity the click released, plus a dwell time"
  );
  Assert.equal(
    rows.length,
    1,
    "And no second row carrying the story identity again"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_reports_a_bounce() {
  info(
    "TelemetryFeed should report a story the user bounced off before a " +
      "single sampling interval, because that is the clearest signal the " +
      "recommendation was not worth the click"
  );
  let sandbox = sinon.createSandbox();
  let { instance } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  // Straight back out, with no interval having elapsed.
  browser.goTo("about:newtab");
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [0],
    "Reported as 0 rather than left off the row"
  );
  Assert.equal(
    readOpenedPageDwell("story_organic"),
    null,
    "While the opened-page distribution still ignores it, as before"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_closed_unseen_reports_nothing() {
  info(
    "TelemetryFeed should leave the dwell time off a story opened in a " +
      "background tab and closed without ever being looked at, the same as " +
      "one still unseen when the ping goes out, so 0 only ever means a bounce"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();
  instance.isDwellTargetInForeground.callsFake(
    target => target.browserRef?.deref() !== browser
  );

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  browser.closeTab();

  Assert.equal(clickRowsFrom(rows).length, 1, "The click was still recorded");
  Assert.deepEqual(dwellTimesFrom(rows), [], "With no dwell time on it");

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_idle_glance_still_reports() {
  info(
    "TelemetryFeed should count a story the user had in front of them as " +
      "seen even if they never interacted with it, so it reports 0 rather " +
      "than nothing once it is left open in the background"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();
  let inFront = true;
  instance.isDwellTargetInForeground.callsFake(() => inFront);

  // No USER_INTERACTION_ACTIVE: the browser had been idle before the click.
  openLinkFrom(instance, browser, {
    dwell_label: "story_organic",
    dwell_id: 1,
    port_id: "port1",
  });
  clickStoryFrom(instance, "port1", 1);
  browser.goTo("https://example.com/story");
  // Then the user moves to another tab, leaving the story open.
  setTime(1000);
  inFront = false;

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [0],
    "Seen without any interaction, so 0 rather than left off"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_switched_to_and_closed_reports() {
  info(
    "TelemetryFeed should count a background story the user switched to and " +
      "closed before the next activity notification as seen"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();
  let inBackground = true;
  instance.isDwellTargetInForeground.callsFake(
    target => !inBackground || target.browserRef?.deref() !== browser
  );

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  // Switched to, then closed, with no sweep in between.
  inBackground = false;
  setTime(USER_INTERACTION_INTERVAL_MS * 2 + 1000);
  browser.closeTab();

  Assert.deepEqual(dwellTimesFrom(rows), [0], "Reported as a bounce");

  teardownDwellTest(sandbox);
});

/**
 * Have Firefox open a link from a newtab itself, the way a middle-click or its
 * own context menu does: newtab only learns of it from the navigation target
 * notification, handed the source and created browsers.
 *
 * @param {object} instance a TelemetryFeed from setupDwellFeed
 * @param {object} createdTabBrowser the browser the link opens in
 * @param {string} url the link
 * @param {object} [storyClick] the click payload the story sent beforehand,
 *  or none for a top site
 */
function openFromBrowser(instance, createdTabBrowser, url, storyClick = null) {
  const sourceTabBrowser = {};
  AboutNewTabParent.loadedTabs.set(sourceTabBrowser, { portID: "port1" });
  instance.sessions.set("port1", { session_id: "sess1" });
  if (storyClick) {
    instance.handleBrowserLinkOpenIntent({
      data: { url, userEvent: storyClick },
      meta: { fromTarget: "port1" },
    });
  }
  instance.handleCreatedNavigationTarget({
    url,
    sourceTabBrowser,
    createdTabBrowser,
  });
  AboutNewTabParent.loadedTabs.delete(sourceTabBrowser);
}

const BROWSER_OPENED_STORY_CLICK = {
  event: "CLICK",
  source: "CARDGRID",
  action_position: 0,
  value: { card_type: "organic", corpus_item_id: "true-id" },
};

add_task(async function test_content_dwell_pairs_a_browser_opened_story() {
  info(
    "TelemetryFeed should measure and pair a story Firefox opened itself, " +
      "on middle-click or from its context menu, like one newtab opened"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  instance.store = {
    getState: () => ({ Prefs: { values: {} }, TopSites: { rows: [] } }),
  };
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openFromBrowser(
    instance,
    browser,
    "https://example.com/story",
    BROWSER_OPENED_STORY_CLICK
  );
  browser.goTo("https://example.com/story");
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [10000],
    "The story's reading time landed on its click row"
  );
  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    USER_INTERACTION_INTERVAL_MS * 2 * NS_PER_MS,
    "And the page was timed for the opened-page distribution"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_opened_page_dwell_time_times_a_browser_opened_top_site() {
    info(
      "TelemetryFeed should time a top site Firefox opened itself, labelled " +
        "the same way as one the tile opened"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    sandbox.stub(instance, "handleTopSitesOrganicImpressionStats");
    instance.store = {
      getState: () => ({
        Prefs: { values: {} },
        TopSites: { rows: [{ url: "https://example.com/" }] },
      }),
    };
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openFromBrowser(instance, browser, "https://example.com/");
    browser.goTo("https://example.com/");
    setTime(3000);
    browser.closeTab();

    Assert.equal(
      readOpenedPageDwell("topsite_organic")?.sum,
      3000 * NS_PER_MS,
      "Timed under topsite_organic"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(
  async function test_content_dwell_skips_a_browser_opened_private_page() {
    info(
      "TelemetryFeed should not measure a story Firefox opened into a private " +
        "window, the same as PlacesFeed for links newtab opens"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let rows = setupContentDwell(sandbox, instance);
    instance.store = {
      getState: () => ({ Prefs: { values: {} }, TopSites: { rows: [] } }),
    };
    let browser = makeFakeBrowser();
    browser.browsingContext.usePrivateBrowsing = true;

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openFromBrowser(
      instance,
      browser,
      "https://example.com/story",
      BROWSER_OPENED_STORY_CLICK
    );
    browser.goTo("https://example.com/story");
    finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

    Assert.equal(clickRowsFrom(rows).length, 1, "The click was still recorded");
    Assert.deepEqual(dwellTimesFrom(rows), [], "With no dwell time on it");
    Assert.equal(
      readOpenedPageDwell("story_organic"),
      null,
      "And the private page was not timed"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_content_dwell_pairs_a_menu_new_window() {
  info(
    "TelemetryFeed should pair a story opened with Open in a New Window from " +
      "its menu, whose click event says OPEN_NEW_WINDOW rather than CLICK"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser, {
    dwell_label: "story_organic",
    dwell_id: 1,
    port_id: "port1",
  });
  instance.handleDiscoveryStreamUserEvent({
    type: actionTypes.DISCOVERY_STREAM_USER_EVENT,
    meta: { fromTarget: "port1" },
    data: {
      event: "OPEN_NEW_WINDOW",
      source: "CARDGRID",
      action_position: 0,
      value: {
        card_type: "organic",
        corpus_item_id: "true-id",
        event_source: "CONTEXT_MENU",
        dwell_id: 1,
      },
    },
  });
  browser.goTo("https://example.com/story");
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [10000],
    "The window's reading time landed on its click row"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_counts_a_quick_close() {
  info(
    "TelemetryFeed should report a story whose tab the user closed before " +
      "the next activity notification, rather than lose it"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser, {
    dwell_label: "story_organic",
    dwell_id: 1,
    port_id: "port1",
  });
  clickStoryFrom(instance, "port1", 1);
  setTime(1000);
  browser.goTo("https://example.com/story");
  setTime(3000);
  browser.closeTab();

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [2000],
    "The two seconds the story was open reached the row"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_noise_and_limits() {
  info(
    "TelemetryFeed should multiply the measured reading time by one draw of " +
      "1 + noise, round it to a whole millisecond, and keep it between 0 " +
      "and 60 seconds"
  );
  // Read for this long, with this noise, report this.
  const cases = [
    [10000, 0, 10000],
    [10000, 0.1, 11000],
    [10000, -0.1, 9000],
    [10000, 0.00006, 10001],
    [10000, -1.5, 0],
    [59000, 0.1, 60000],
    [600000, 0, 60000],
  ];
  for (const [readForMs, noise, expected] of cases) {
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let rows = setupContentDwell(sandbox, instance);
    instance.dwellTimeNoise.returns(noise);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openStoryAndRead(instance, browser);
    finishReading(instance, browser, setTime, readForMs);

    Assert.deepEqual(
      dwellTimesFrom(rows),
      [expected],
      `${readForMs}ms of reading with noise ${noise} reports ${expected}ms`
    );
    Assert.equal(
      instance.dwellTimeNoise.callCount,
      1,
      "One noise draw for the one reported dwell time"
    );
    teardownDwellTest(sandbox);
  }
});

add_task(async function test_content_dwell_still_reading_at_submission() {
  info(
    "TelemetryFeed should report what a user who is still reading when the " +
      "ping goes out has read so far, read at submission rather than when " +
      "the destination loaded"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  // No finishReading: the user is still on the page.
  openStoryAndRead(instance, browser);
  setTime(12000);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [12000],
    "Twelve seconds of reading so far reports twelve seconds"
  );

  setTime(25000);
  Assert.deepEqual(
    dwellTimesFrom(rows),
    [25000],
    "The clock still running counts, up to the moment the ping goes out"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_never_viewed_reports_nothing() {
  info(
    "TelemetryFeed should leave the dwell time off a story opened in a " +
      "background tab and not looked at before the ping goes out, so 0 only " +
      "ever means the user bounced"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();
  let inBackground = true;
  instance.isDwellTargetInForeground.callsFake(
    target => !inBackground || target.browserRef?.deref() !== browser
  );

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  setTime(USER_INTERACTION_INTERVAL_MS * 4);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  Assert.equal(clickRowsFrom(rows).length, 1, "The click was still recorded");
  Assert.deepEqual(dwellTimesFrom(rows), [], "With no dwell time on it");

  // The user gets to it before the ping goes out.
  inBackground = false;
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 4 + 6000);
  Assert.deepEqual(
    dwellTimesFrom(rows),
    [6000],
    "Once read, it reports what has been read so far"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_needs_the_destination_to_load() {
  info(
    "TelemetryFeed should leave the dwell time off a click whose destination " +
      "never loaded, so absence means the click could not be matched to a " +
      "page rather than that the page was read for no time at all"
  );
  let sandbox = sinon.createSandbox();
  let { instance } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser, {
    dwell_label: "story_organic",
    dwell_id: 1,
    port_id: "port1",
  });
  clickStoryFrom(instance, "port1", 1);
  // The load fails, so the tab shows an error page rather than the story.
  browser.goTo("about:neterror");
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  browser.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  Assert.equal(clickRowsFrom(rows).length, 1, "The click was still recorded");
  Assert.deepEqual(dwellTimesFrom(rows), [], "With no dwell time on it");

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_needs_the_feature_pref() {
  info(
    "TelemetryFeed should leave the dwell time off every click while the " +
      "feature pref is off, so the submission delay can be widened in an " +
      "experiment before any dwell time is collected"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_DWELL_TIME_ENABLED, false);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

  Assert.equal(clickRowsFrom(rows).length, 1, "The click was still recorded");
  Assert.deepEqual(dwellTimesFrom(rows), [], "With no dwell time on it");

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_ignores_unmatched_clicks() {
  info(
    "TelemetryFeed should not let a click with no matching token adopt " +
      "another page's measurement, which is what a context-menu open sends"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser, { dwell_id: 1, port_id: "port1" });
  clickStoryFrom(instance, "port1", undefined);
  browser.goTo("https://example.com/story");
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [],
    "Nothing was attributed to the wrong story"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_ignores_a_click_that_arrives_late() {
  info(
    "TelemetryFeed should ignore a click whose page has already been " +
      "finalized, rather than pairing with an entry that is no longer tracked"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser, { dwell_id: 1, port_id: "port1" });
  browser.goTo("https://example.com/story");
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS);

  // The entry is gone by the time the click turns up.
  clickStoryFrom(instance, "port1", 1);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [],
    "No dwell time, and nothing thrown pairing with a deleted entry"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_content_dwell_pairs_a_link_open_that_arrives_late() {
    info(
      "TelemetryFeed should pair a click with a link open that arrives after " +
        "it. A link opened in a new window is only reported once the window " +
        "has started."
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let rows = setupContentDwell(sandbox, instance);
    let browser = makeFakeBrowser("about:blank");

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    clickStoryFrom(instance, "port1", 1);
    openLinkFrom(instance, browser, { dwell_id: 1, port_id: "port1" });
    browser.goTo("https://example.com/story");
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

    Assert.deepEqual(
      dwellTimesFrom(rows),
      [10000],
      "The late link open still put its dwell time on the click"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_content_dwell_keeps_two_clicks_apart() {
  info(
    "TelemetryFeed should match each click to its own page, so two stories " +
      "opened in quick succession cannot swap dwell times"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let first = makeFakeBrowser();
  let second = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, first, { dwell_id: 1, port_id: "port1" });
  openLinkFrom(instance, second, { dwell_id: 2, port_id: "port1" });
  // The clicks arrive out of order, so only the token can pair them up.
  instance.randomizeOrganicContentEvent.returns({ corpus_item_id: "story-2" });
  clickStoryFrom(instance, "port1", 2);
  instance.randomizeOrganicContentEvent.returns({ corpus_item_id: "story-1" });
  clickStoryFrom(instance, "port1", 1);

  first.goTo("https://example.com/one");
  second.goTo("https://example.com/two");
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  // The second story is closed after one interval of reading.
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  second.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  // The first is read for another interval after the user comes back to it,
  // so the two stories end on different dwell times.
  setTime(USER_INTERACTION_INTERVAL_MS * 9);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(USER_INTERACTION_INTERVAL_MS * 10);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  first.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  let byId = Object.fromEntries(
    clickRowsFrom(rows).map(data => [data.corpus_item_id, data.dwell_time])
  );
  Assert.deepEqual(
    byId,
    { "story-1": 10000, "story-2": 5000 },
    "Each story kept the dwell time of the page it opened"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_follows_the_randomized_row() {
  info(
    "TelemetryFeed should write the dwell time onto whatever row the " +
      "randomizer produced. randomizeOrganicContentEvent rewrites a fraction " +
      "of organic clicks to a different story, so the corpus_item_id on the " +
      "row is not the article's and cannot be what pairs them."
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  // What the randomizer does to the fraction it rewrites: a different story
  // than the one the click named.
  instance.randomizeOrganicContentEvent.returns({
    corpus_item_id: "some-other-story",
  });
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser);
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS * 2);

  Assert.deepEqual(
    clickRowsFrom(rows),
    [{ corpus_item_id: "some-other-story", dwell_time: 10000 }],
    "The dwell time landed on the row the randomizer produced"
  );

  teardownDwellTest(sandbox);
});

add_task(
  async function test_content_dwell_leaves_the_newtab_ping_click_alone() {
    info(
      "TelemetryFeed should never put a dwell time on a click that is still " +
        "queued for the newtab ping. randomizeOrganicContentEvent usually " +
        "returns the object pocket.click records, and pocket.click has no " +
        "dwell_time key, so Glean would drop the whole event."
    );
    let sandbox = sinon.createSandbox();
    let { instance } = setupDwellFeed(sandbox);
    let rows = setupContentDwell(sandbox, instance);
    instance.randomizeOrganicContentEvent.restore();
    instance.gleanSessionType = GleanSessionType.NormalGleanSession;
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openStoryAndRead(instance, browser);
    instance.transitionToPrivateSession();

    const clicks = Glean.pocket.click.testGetValue("newtab");
    Assert.equal(clicks?.length, 1, "pocket.click was recorded, not rejected");
    Assert.ok(!("dwell_time" in clicks[0].extra), "And carries no dwell time");
    Assert.deepEqual(
      dwellTimesFrom(rows),
      [],
      "The queued click reached the content ping without a dwell time"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_content_dwell_skips_sponsored_and_topsites() {
  info(
    "TelemetryFeed should never put a dwell time on a sponsored story or a " +
      "topsite. randomizeOrganicContentEvent passes sponsored items through " +
      "untouched, so this guard is what stops a spoc's true id being joined " +
      "to how long it was read."
  );
  let sandbox = sinon.createSandbox();
  let { instance } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  let spoc = makeFakeBrowser();
  let topsite = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, spoc, {
    dwellId: 1,
    isSponsored: true,
    cardType: "spoc",
  });
  openStoryAndRead(instance, topsite, {
    dwellId: 2,
    dwellLabel: "topsite_organic",
  });

  spoc.isConnected = false;
  topsite.isConnected = false;
  instance.observe(null, USER_INTERACTION_INACTIVE, null);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [],
    "Neither row carries a dwell time"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_needs_both_flags_to_say_organic() {
  info(
    "TelemetryFeed should refuse a sponsored click even when the link open " +
      "that started the measurement called itself organic. DSCard derives " +
      "the two flags in separate expressions, and a spoc arrives here with " +
      "its true corpus_item_id, so agreeing is what keeps that id unjoined."
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);
  // What the real randomizeOrganicContentEvent does with a spoc: nothing.
  instance.randomizeOrganicContentEvent.callsFake(item => item);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, browser, { isSponsored: false, cardType: "spoc" });
  finishReading(instance, browser, setTime, USER_INTERACTION_INTERVAL_MS);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [],
    "The click disagreeing with the link open is enough to refuse it"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_content_dwell_respects_the_private_ping_pref() {
  info(
    "TelemetryFeed should check the private ping pref at both ends: off at " +
      "click time means no dwell time is ever paired, and off by the time " +
      "the user stops reading means an opt-out mid-read is honoured"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let rows = setupContentDwell(sandbox, instance);

  // Off for the whole visit: nothing is paired in the first place.
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);
  let optedOut = makeFakeBrowser();
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, optedOut, { dwellId: 1 });
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);
  finishReading(instance, optedOut, setTime, USER_INTERACTION_INTERVAL_MS);

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [],
    "Turning it back on cannot resurrect a pairing that never happened"
  );

  // On at click time, off before the user leaves.
  let optsOutLater = makeFakeBrowser();
  setTime(USER_INTERACTION_INTERVAL_MS * 2);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openStoryAndRead(instance, optsOutLater, { dwellId: 2 });
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);
  finishReading(
    instance,
    optsOutLater,
    setTime,
    USER_INTERACTION_INTERVAL_MS * 4
  );

  Assert.deepEqual(
    dwellTimesFrom(rows),
    [],
    "The opt-out took the dwell time back off the row"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_counts_a_quick_close() {
  info(
    "TelemetryFeed should record a visit that ends before the next activity " +
      "notification. The page landing and the tab closing are both noticed " +
      "as they happen, so a fast bounce is measured instead of lost."
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  setTime(1000);
  browser.goTo("https://example.com/story");
  setTime(3000);
  browser.closeTab();

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    2000 * NS_PER_MS,
    "The two seconds between landing and closing were recorded"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_counts_a_quick_back() {
  info(
    "TelemetryFeed should end the visit the moment the tab goes back to the " +
      "newtab, rather than at the next activity notification"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  setTime(1000);
  browser.goTo("https://example.com/story");
  setTime(3000);
  browser.goTo("about:newtab");
  setTime(USER_INTERACTION_INTERVAL_MS);
  instance.observe(null, USER_INTERACTION_ACTIVE, null);

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    2000 * NS_PER_MS,
    "Only the time before going back was recorded"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_ends_on_a_new_address() {
  info(
    "TelemetryFeed should end the visit when the tab loads a document at a " +
      "different address, at that moment"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  setTime(1000);
  browser.goTo("https://example.com/story");
  setTime(4000);
  browser.goTo("https://example.com/another-story");

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    3000 * NS_PER_MS,
    "The visit ended when the next page arrived"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_survives_a_replacement() {
  info(
    "TelemetryFeed should keep measuring when the page replaces its own " +
      "document at the same address as it starts up, or is reloaded. Some " +
      "sites do this on a first visit, and treating it as leaving would " +
      "record a fraction of a second for the whole visit."
  );
  for (const listeners of [true, false]) {
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser("about:newtab", { listeners });

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openLinkFrom(instance, browser);
    browser.goTo("https://shop.example/?ref=ad");
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(300);
    browser.replaceDocument();
    setTime(USER_INTERACTION_INTERVAL_MS);
    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    setTime(USER_INTERACTION_INTERVAL_MS * 2);
    browser.goTo("https://shop.example/basket");
    instance.observe(null, USER_INTERACTION_ACTIVE, null);

    Assert.deepEqual(
      [
        readOpenedPageDwell("story_organic")?.count,
        readOpenedPageDwell("story_organic")?.sum,
      ],
      [1, USER_INTERACTION_INTERVAL_MS * 2 * NS_PER_MS],
      `One sample for the whole visit (listeners ${listeners ? "on" : "off"})`
    );
    teardownDwellTest(sandbox);
  }
});

add_task(
  async function test_opened_page_dwell_time_follows_same_document_moves() {
    info(
      "TelemetryFeed should follow the address through same-document " +
        "navigation, so a later reload of the new address is still the same " +
        "visit"
    );
    let sandbox = sinon.createSandbox();
    let { instance, setTime } = setupDwellFeed(sandbox);
    let browser = makeFakeBrowser();

    instance.observe(null, USER_INTERACTION_ACTIVE, null);
    openLinkFrom(instance, browser);
    browser.goTo("https://example.com/story");
    setTime(1000);
    browser.goToInDocument("https://example.com/story/page-2");
    setTime(2000);
    browser.replaceDocument();
    setTime(3000);
    browser.closeTab();

    Assert.deepEqual(
      [
        readOpenedPageDwell("story_organic")?.count,
        readOpenedPageDwell("story_organic")?.sum,
      ],
      [1, 3000 * NS_PER_MS],
      "One sample across the pushState and the reload"
    );

    teardownDwellTest(sandbox);
  }
);

add_task(async function test_opened_page_dwell_time_ignores_an_error_page() {
  info(
    "TelemetryFeed should not start measuring when the load fails. The " +
      "location change for an error page carries the address that failed, " +
      "so it has to be recognised from its flags."
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  browser.failLoad("https://example.com/story");
  setTime(3000);
  browser.closeTab();

  Assert.equal(
    readOpenedPageDwell("story_organic"),
    null,
    "Nothing recorded for a page that never loaded"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_page_already_loaded() {
  info(
    "TelemetryFeed should start measuring at once when the destination has " +
      "already loaded by the time the link open is reported, as for a link " +
      "opened in a new window"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser("https://example.com/story");

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  setTime(1000);
  openLinkFrom(instance, browser);
  setTime(3000);
  browser.closeTab();

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    2000 * NS_PER_MS,
    "Measured from the link open, with no activity notification needed"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_removes_its_listeners() {
  info(
    "TelemetryFeed should take its progress, tab-close and window-close " +
      "listeners off a page once the page is finalized, so nothing is left " +
      "behind on the tab or its window"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  const sizes = () => [
    browser.progressListeners.size,
    browser.tabListeners.size,
    browser.windowListeners.size,
  ];
  Assert.deepEqual(
    sizes(),
    [1, 1, 1],
    "Every listener is attached while the page is tracked"
  );

  browser.goTo("https://example.com/story");
  setTime(1000);
  browser.goTo("about:newtab");
  Assert.deepEqual(sizes(), [0, 0, 0], "All are removed when it is finalized");

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_ends_with_its_window() {
  info(
    "TelemetryFeed should end a visit when the window showing it closes, " +
      "which fires no TabClose, rather than wait for the next activity " +
      "notification"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  setTime(1000);
  browser.goTo("https://example.com/story");
  setTime(3000);
  browser.closeWindow();

  Assert.equal(
    readOpenedPageDwell("story_organic")?.sum,
    2000 * NS_PER_MS,
    "The visit ended when the window closed"
  );

  teardownDwellTest(sandbox);
});

add_task(async function test_opened_page_dwell_time_ignores_frames() {
  info(
    "TelemetryFeed should ignore location changes inside the page's frames, " +
      "such as an embedded ad or video loading, which do not mean the user " +
      "left"
  );
  let sandbox = sinon.createSandbox();
  let { instance, setTime } = setupDwellFeed(sandbox);
  let browser = makeFakeBrowser();

  instance.observe(null, USER_INTERACTION_ACTIVE, null);
  openLinkFrom(instance, browser);
  browser.goTo("https://example.com/story");
  setTime(1000);
  browser.goToInFrame("https://ads.example/frame");
  setTime(3000);
  browser.closeTab();

  Assert.deepEqual(
    [
      readOpenedPageDwell("story_organic")?.count,
      readOpenedPageDwell("story_organic")?.sum,
    ],
    [1, 3000 * NS_PER_MS],
    "One sample, unbroken by the frame"
  );

  teardownDwellTest(sandbox);
});

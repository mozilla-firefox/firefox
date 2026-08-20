/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  GleanSessionType: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  NewTabContentPing: "resource://newtab/lib/NewTabContentPing.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  TelemetryFeed: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
});

const FAKE_UUID = "{foo-123-foo}";
const PREF_IMPRESSION_ID = "browser.newtabpage.activity-stream.impressionId";
const PREF_TELEMETRY = "browser.newtabpage.activity-stream.telemetry";
const PREF_PRIVATE_PING_ENABLED =
  "browser.newtabpage.activity-stream.telemetry.privatePing.enabled";
const PREF_IS_MERINO_FEED_EXPERIMENT =
  "browser.newtabpage.activity-stream.discoverystream.merino-feed-experiment";
const PREF_ENDPOINTS =
  "browser.newtabpage.activity-stream.discoverystream.endpoints";
const PREF_UNIFIED_ADS_TILES_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.tiles.enabled";
const PREF_UNIFIED_ADS_SPOCS_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.spocs.enabled";

add_setup(async function setup() {
  do_get_profile();
  // FOG needs to be initialized in order for data to flow.
  Services.fog.initializeFOG();

  await TelemetryController.testReset();

  updateAppInfo({
    name: "XPCShell",
    ID: "xpcshell@tests.mozilla.org",
    version: "122",
    platformVersion: "122",
  });

  let sandbox = sinon.createSandbox();
  sandbox.stub(ContextId, "requestSynchronously").returns(FAKE_UUID);

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function test_construction() {
  let testInstance = new TelemetryFeed();
  Assert.ok(
    testInstance,
    "Should have been able to create an instance of TelemetryFeed."
  );
  Assert.ok(
    testInstance._impressionId,
    "Should create impression id if none exists"
  );
});

add_task(async function test_load_impressionId() {
  info(
    "Constructing a TelemetryFeed should use a saved impression ID if one exists."
  );
  const FAKE_IMPRESSION_ID = "{some-fake-impression-ID}";
  const IMPRESSION_PREF = "browser.newtabpage.activity-stream.impressionId";
  Services.prefs.setCharPref(IMPRESSION_PREF, FAKE_IMPRESSION_ID);
  Assert.equal(new TelemetryFeed()._impressionId, FAKE_IMPRESSION_ID);
  Services.prefs.clearUserPref(IMPRESSION_PREF);
});

add_task(async function test_init() {
  info(
    "init should make this.browserOpenNewtabStart() observe browser-open-newtab-start"
  );
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(TelemetryFeed.prototype, "getOrCreateImpressionId")
    .returns(FAKE_UUID);

  let instance = new TelemetryFeed();
  sandbox.stub(instance, "browserOpenNewtabStart");
  instance.init();

  Services.obs.notifyObservers(null, "browser-open-newtab-start");
  Assert.ok(
    instance.browserOpenNewtabStart.calledOnce,
    "browserOpenNewtabStart called once."
  );

  info("init should create impression id if none exists");
  Assert.equal(instance._impressionId, FAKE_UUID);

  instance.uninit();
  sandbox.restore();
});

add_task(async function test_saved_impression_id() {
  const FAKE_IMPRESSION_ID = "fakeImpressionId";
  Services.prefs.setCharPref(PREF_IMPRESSION_ID, FAKE_IMPRESSION_ID);
  Assert.equal(new TelemetryFeed()._impressionId, FAKE_IMPRESSION_ID);
  Services.prefs.clearUserPref(PREF_IMPRESSION_ID);
});

add_task(async function test_telemetry_prefs() {
  info("Telemetry pref changes from false to true");
  Services.prefs.setBoolPref(PREF_TELEMETRY, false);
  let instance = new TelemetryFeed();
  Assert.ok(!instance.telemetryEnabled, "Telemetry disabled");

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  Assert.ok(instance.telemetryEnabled, "Telemetry enabled");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
});

add_task(async function test_deletionRequest_scalars() {
  info("TelemetryFeed.init should set two scalars for deletion-request");

  Services.telemetry.clearScalars();
  let instance = new TelemetryFeed();
  instance.init();

  let snapshot = Services.telemetry.getSnapshotForScalars(
    "deletion-request",
    false
  ).parent;
  TelemetryTestUtils.assertScalar(
    snapshot,
    "deletion.request.impression_id",
    instance._impressionId
  );

  // We'll only set the context_id in the deletion request if rotation is
  // disabled.
  if (!ContextId.rotationEnabled) {
    TelemetryTestUtils.assertScalar(
      snapshot,
      "deletion.request.context_id",
      FAKE_UUID
    );
  }

  instance.uninit();
});

add_task(async function test_metrics_on_initialization() {
  info("TelemetryFeed.init should record initial metrics from newtab prefs");
  Services.fog.testResetFOG();
  const ENABLED_SETTING = true;
  const TOP_SITES_ROWS = 3;
  const BLOCKED_SPONSORS = ["mozilla"];

  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.feeds.topsites",
    ENABLED_SETTING
  );
  Services.prefs.setIntPref(
    "browser.newtabpage.activity-stream.topSitesRows",
    TOP_SITES_ROWS
  );
  Services.prefs.setCharPref(
    "browser.topsites.blockedSponsors",
    JSON.stringify(BLOCKED_SPONSORS)
  );

  let instance = new TelemetryFeed();
  instance.init();

  Assert.equal(Glean.topsites.enabled.testGetValue(), ENABLED_SETTING);
  Assert.equal(Glean.topsites.rows.testGetValue(), TOP_SITES_ROWS);
  Assert.deepEqual(
    Glean.newtab.blockedSponsors.testGetValue(),
    BLOCKED_SPONSORS
  );

  instance.uninit();

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.feeds.topsites"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.topSitesRows"
  );
  Services.prefs.clearUserPref("browser.topsites.blockedSponsors");
});

add_task(async function test_metrics_with_bad_json() {
  info(
    "TelemetryFeed.init should not record blocked sponsor metrics when " +
      "bad json string is passed"
  );
  Services.fog.testResetFOG();
  Services.prefs.setCharPref("browser.topsites.blockedSponsors", "BAD[JSON]");

  let instance = new TelemetryFeed();
  instance.init();

  Assert.equal(Glean.newtab.blockedSponsors.testGetValue(), null);

  instance.uninit();

  Services.prefs.clearUserPref("browser.topsites.blockedSponsors");
});

add_task(async function test_metrics_on_pref_changes() {
  info("TelemetryFeed.init should record new metrics for newtab pref changes");
  const INITIAL_TOP_SITES_ROWS = 3;
  const INITIAL_BLOCKED_SPONSORS = [];
  Services.fog.testResetFOG();
  Services.prefs.setIntPref(
    "browser.newtabpage.activity-stream.topSitesRows",
    INITIAL_TOP_SITES_ROWS
  );
  Services.prefs.setCharPref(
    "browser.topsites.blockedSponsors",
    JSON.stringify(INITIAL_BLOCKED_SPONSORS)
  );

  let instance = new TelemetryFeed();
  instance.init();

  Assert.equal(Glean.topsites.rows.testGetValue(), INITIAL_TOP_SITES_ROWS);
  Assert.deepEqual(
    Glean.newtab.blockedSponsors.testGetValue(),
    INITIAL_BLOCKED_SPONSORS
  );

  const NEXT_TOP_SITES_ROWS = 2;
  const NEXT_BLOCKED_SPONSORS = ["mozilla"];

  Services.prefs.setIntPref(
    "browser.newtabpage.activity-stream.topSitesRows",
    NEXT_TOP_SITES_ROWS
  );

  Services.prefs.setStringPref(
    "browser.topsites.blockedSponsors",
    JSON.stringify(NEXT_BLOCKED_SPONSORS)
  );

  Assert.equal(Glean.topsites.rows.testGetValue(), NEXT_TOP_SITES_ROWS);
  Assert.deepEqual(
    Glean.newtab.blockedSponsors.testGetValue(),
    NEXT_BLOCKED_SPONSORS
  );

  instance.uninit();

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.topSitesRows"
  );
  Services.prefs.clearUserPref("browser.topsites.blockedSponsors");
});

add_task(async function test_events_on_pref_changes() {
  info("TelemetryFeed.init should record events for some newtab pref changes");
  // We only record events for browser.newtabpage.activity-stream.feeds.topsites and
  // browser.newtabpage.activity-stream.showSponsoredTopSites being changed.
  const INITIAL_TOPSITES_ENABLED = false;
  const INITIAL_SHOW_SPONSORED_TOP_SITES = true;
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.feeds.topsites",
    INITIAL_TOPSITES_ENABLED
  );
  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.showSponsoredTopSites",
    INITIAL_SHOW_SPONSORED_TOP_SITES
  );

  let instance = new TelemetryFeed();
  instance.init();

  const NEXT_TOPSITES_ENABLED = true;
  const NEXT_SHOW_SPONSORED_TOP_SITES = false;

  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.feeds.topsites",
    NEXT_TOPSITES_ENABLED
  );
  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.showSponsoredTopSites",
    NEXT_SHOW_SPONSORED_TOP_SITES
  );

  let prefChangeEvents = Glean.topsites.prefChanged.testGetValue();
  Assert.deepEqual(prefChangeEvents[0].extra, {
    pref_name: "browser.newtabpage.activity-stream.feeds.topsites",
    new_value: String(NEXT_TOPSITES_ENABLED),
  });
  Assert.deepEqual(prefChangeEvents[1].extra, {
    pref_name: "browser.newtabpage.activity-stream.showSponsoredTopSites",
    new_value: String(NEXT_SHOW_SPONSORED_TOP_SITES),
  });

  instance.uninit();

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.feeds.topsites"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.showSponsoredTopSites"
  );
});

add_task(async function test_topsites_change_display_event() {
  info(
    "TelemetryFeed.handleSetPref should record a topsites.change_display " +
      "event when the topSitesRows pref is set from the customize menu"
  );
  Services.fog.testResetFOG();

  const PORT_ID = "port123";
  const ROWS = 3;

  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.handleSetPref({
    meta: { fromTarget: PORT_ID },
    data: { name: "topSitesRows", value: ROWS },
  });

  let events = Glean.topsites.changeDisplay.testGetValue();
  Assert.equal(events.length, 1, "One change_display event was recorded");
  Assert.deepEqual(events[0].extra, {
    rows: String(ROWS),
    newtab_visit_id: session.session_id,
  });
});

add_task(async function test_topsites_change_display_event_no_session() {
  info(
    "TelemetryFeed.handleSetPref should not record a topsites.change_display " +
      "event when the sending port has no session"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();

  instance.handleSetPref({
    meta: { fromTarget: "port-with-no-session" },
    data: { name: "topSitesRows", value: 2 },
  });

  Assert.equal(
    Glean.topsites.changeDisplay.testGetValue(),
    null,
    "No change_display event was recorded"
  );
});

add_task(async function test_browserOpenNewtabStart() {
  info(
    "TelemetryFeed.browserOpenNewtabStart should call " +
      "ChromeUtils.addProfilerMarker with browser-open-newtab-start"
  );

  let instance = new TelemetryFeed();

  let entries = 10000;
  let interval = 1;
  let threads = ["GeckoMain"];
  let features = [];
  await Services.profiler.StartProfiler(entries, interval, features, threads);
  instance.browserOpenNewtabStart();

  let profileArrayBuffer =
    await Services.profiler.getProfileDataAsArrayBuffer();
  await Services.profiler.StopProfiler();

  let profileUint8Array = new Uint8Array(profileArrayBuffer);
  let textDecoder = new TextDecoder("utf-8", { fatal: true });
  let profileString = textDecoder.decode(profileUint8Array);
  let profile = JSON.parse(profileString);
  Assert.ok(profile.threads);
  Assert.equal(profile.threads.length, 1);

  // The Text marker's "name" field is a unique string, so the payload holds
  // an index into the thread's string table rather than the string itself.
  let [thread] = profile.threads;
  let foundMarker = thread.markers.data.find(marker => {
    return thread.stringTable[marker[5]?.name] === "browser-open-newtab-start";
  });

  Assert.ok(foundMarker, "Found the browser-open-newtab-start marker");
});

add_task(async function test_addSession_and_get_session() {
  info("TelemetryFeed.addSession should add a session and return it");
  let instance = new TelemetryFeed();
  let session = instance.addSession("foo");

  Assert.equal(instance.sessions.get("foo"), session);

  info("TelemetryFeed.addSession should set a session_id");
  Assert.ok(session.session_id, "Should have a session_id set");
});

add_task(async function test_addSession_url_param() {
  info("TelemetryFeed.addSession should set the page if a url param is given");
  let instance = new TelemetryFeed();
  let session = instance.addSession("foo", "about:monkeys");
  Assert.equal(session.page, "about:monkeys");

  info(
    "TelemetryFeed.assSession should set the page prop to 'unknown' " +
      "if no URL param given"
  );
  session = instance.addSession("test2");
  Assert.equal(session.page, "unknown");
});

add_task(async function test_addSession_perf_properties() {
  info(
    "TelemetryFeed.addSession should set the perf type when lacking " +
      "timestamp"
  );
  let instance = new TelemetryFeed();
  let session = instance.addSession("foo");
  Assert.equal(session.perf.load_trigger_type, "unexpected");

  info(
    "TelemetryFeed.addSession should set load_trigger_type to " +
      "first_window_opened on the first about:home seen"
  );
  session = instance.addSession("test2", "about:home");
  Assert.equal(session.perf.load_trigger_type, "first_window_opened");

  info(
    "TelemetryFeed.addSession should set load_trigger_ts to the " +
      "value of the process start timestamp"
  );
  Assert.equal(
    session.perf.load_trigger_ts,
    Services.startup.getStartupInfo().process.getTime(),
    "Should have set a timestamp to be the process start time"
  );

  info(
    "TelemetryFeed.addSession should NOT set load_trigger_type to " +
      "first_window_opened on the second about:home seen"
  );
  let session2 = instance.addSession("test2", "about:home");
  Assert.notEqual(session2.perf.load_trigger_type, "first_window_opened");
});

add_task(async function test_addSession_valid_ping_topsites_stats_perf() {
  info(
    "TelemetryFeed.addSession should create a valid session ping " +
      "with the topsites stats perf"
  );
  let instance = new TelemetryFeed();
  // Add a session
  const PORT_ID = "foo";
  instance.addSession(PORT_ID, "about:home");

  const SCREENSHOT_WITH_ICON = 2;
  const TOPSITES_PINNED = 3;
  const TOPSITES_SEARCH_SHORTCUTS = 2;

  instance.saveSessionPerfData("foo", {
    topsites_icon_stats: {
      custom_screenshot: 0,
      screenshot_with_icon: SCREENSHOT_WITH_ICON,
      screenshot: 1,
      tippytop: 2,
      rich_icon: 1,
      no_image: 0,
    },
    topsites_pinned: TOPSITES_PINNED,
    topsites_search_shortcuts: TOPSITES_SEARCH_SHORTCUTS,
  });

  Assert.equal(
    instance.sessions.get("foo").perf.topsites_icon_stats.screenshot_with_icon,
    SCREENSHOT_WITH_ICON
  );
  Assert.equal(
    instance.sessions.get("foo").perf.topsites_pinned,
    TOPSITES_PINNED
  );
  Assert.equal(
    instance.sessions.get("foo").perf.topsites_search_shortcuts,
    TOPSITES_SEARCH_SHORTCUTS
  );
});

add_task(async function test_endSession_no_throw_on_bad_session() {
  info(
    "TelemetryFeed.endSession should not throw if there is no " +
      "session for a given port ID"
  );
  let instance = new TelemetryFeed();
  try {
    instance.endSession("doesn't exist");
    Assert.ok(true, "Did not throw.");
  } catch (e) {
    Assert.ok(false, "Should not have thrown.");
  }
});

add_task(async function test_endSession_session_duration() {
  info(
    "TelemetryFeed.endSession should add a session_duration integer " +
      "if there is a visibility_event_rcvd_ts"
  );
  let instance = new TelemetryFeed();
  let session = instance.addSession("foo");
  session.perf.visibility_event_rcvd_ts = 444.4732;
  instance.endSession("foo");

  Assert.ok(
    Number.isInteger(session.session_duration),
    "session_duration should be an integer"
  );
});

add_task(async function test_endSession_no_ping_on_no_visibility_event() {
  info(
    "TelemetryFeed.endSession shouldn't send session ping if there's " +
      "no visibility_event_rcvd_ts"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  let instance = new TelemetryFeed();

  let sandbox = sinon.createSandbox();
  sandbox.stub(instance, "configureContentPing");

  let submittedReasons = [];
  GleanPings.newtab.testBeforeNextSubmit(reason => {
    submittedReasons.push(reason);
  });

  instance.addSession("foo");

  Services.telemetry.clearEvents();
  await instance.endSession("foo");
  TelemetryTestUtils.assertNumberOfEvents(0);

  Assert.deepEqual(
    submittedReasons,
    [],
    "A newtab that was never shown should not submit a newtab ping."
  );
  Assert.equal(
    Glean.newtab.closed.testGetValue(),
    null,
    "A newtab that was never shown should not record newtab.closed."
  );

  info("TelemetryFeed.endSession should remove the session from .sessions");
  Assert.ok(!instance.sessions.has("foo"));

  Services.prefs.clearUserPref(PREF_TELEMETRY);

  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(async function test_endSession_never_shown_keeps_buffered_events() {
  info(
    "TelemetryFeed.endSession shouldn't drain the shared event buffer for a " +
      "newtab that was never shown"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");
  instance.gleanSessionType = GleanSessionType.NormalGleanSession;

  let recorded = false;
  instance.recordOrQueueEvent("impression", {}, "other-session", () => {
    recorded = true;
  });
  Assert.ok(!recorded, "The event should be queued rather than recorded.");

  instance.addSession("preloaded");
  await instance.endSession("preloaded");

  Assert.ok(
    !recorded,
    "A newtab that was never shown should leave queued events alone."
  );
  Assert.ok(!instance.sessions.has("preloaded"));

  instance.uninit();
  Assert.ok(recorded, "The queued event should survive until uninit.");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(async function test_uninit_flushes_buffered_events() {
  info(
    "TelemetryFeed.uninit should flush events queued in a normal Glean " +
      "session rather than dropping them"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");
  instance.gleanSessionType = GleanSessionType.NormalGleanSession;

  let recorded = false;
  instance.recordOrQueueEvent("impression", {}, "orphaned-session", () => {
    recorded = true;
  });

  let pingSubmitted = new Promise(resolve => {
    GleanPings.newtab.testBeforeNextSubmit(reason => {
      Assert.equal(reason, "newtab_session_end");
      resolve();
    });
  });

  instance.uninit();
  await pingSubmitted;

  Assert.ok(recorded, "The queued event should be recorded at uninit.");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(async function test_uninit_no_ping_when_buffer_empty() {
  info("TelemetryFeed.uninit shouldn't submit an empty newtab ping");
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");

  let submittedReasons = [];
  GleanPings.newtab.testBeforeNextSubmit(reason => {
    submittedReasons.push(reason);
  });

  instance.uninit();

  Assert.deepEqual(
    submittedReasons,
    [],
    "uninit with nothing buffered should not submit a ping."
  );

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(async function test_endSession_send_ping() {
  info(
    "TelemetryFeed.endSession should call createSessionSendEvent with the " +
      "session if visibilty_event_rcvd_ts was set"
  );
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  let instance = new TelemetryFeed();

  let sandbox = sinon.createSandbox();
  sandbox.stub(instance, "configureContentPing");

  let session = instance.addSession("foo");

  session.perf.visibility_event_rcvd_ts = 444.4732;
  instance.endSession("foo");

  info("TelemetryFeed.endSession should remove the session from .sessions");
  Assert.ok(!instance.sessions.has("foo"));

  Services.prefs.clearUserPref(PREF_TELEMETRY);

  sandbox.restore();
});

add_task(async function test_setLoadTriggerInfo() {
  info(
    "TelemetryFeed.setLoadTriggerInfo should call saveSessionPerfData " +
      "w/load_trigger_{ts,type} data"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "saveSessionPerfData");

  instance.browserOpenNewtabStart();
  instance.addSession("port123");
  instance.setLoadTriggerInfo("port123");

  Assert.ok(
    instance.saveSessionPerfData.calledWith(
      "port123",
      sinon.match({
        load_trigger_type: "menu_plus_or_keyboard",
        load_trigger_ts: sinon.match.number,
      })
    ),
    "TelemetryFeed.saveSessionPerfData was called with the right arguments"
  );

  sandbox.restore();
});

add_task(async function test_setLoadTriggerInfo_no_saveSessionPerfData() {
  info(
    "TelemetryFeed.setLoadTriggerInfo should not call saveSessionPerfData " +
      "when getting mark throws"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "saveSessionPerfData");

  instance.addSession("port123");
  instance.setLoadTriggerInfo("port123");

  Assert.ok(
    instance.saveSessionPerfData.notCalled,
    "TelemetryFeed.saveSessionPerfData was not called"
  );

  sandbox.restore();
});

add_task(async function test_saveSessionPerfData_updates_session_with_data() {
  info(
    "TelemetryFeed.saveSessionPerfData should update the given session " +
      "with the given data"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  instance.addSession("port123");
  Assert.equal(instance.sessions.get("port123").fake_ts, undefined);
  let data = { fake_ts: 456, other_fake_ts: 789 };
  instance.saveSessionPerfData("port123", data);

  let sessionPerfData = instance.sessions.get("port123").perf;
  Assert.equal(sessionPerfData.fake_ts, 456);
  Assert.equal(sessionPerfData.other_fake_ts, 789);

  sandbox.restore();
});

add_task(async function test_saveSessionPerfData_calls_setLoadTriggerInfo() {
  info(
    "TelemetryFeed.saveSessionPerfData should call setLoadTriggerInfo if " +
      "data has visibility_event_rcvd_ts"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  sandbox.stub(instance, "setLoadTriggerInfo");
  instance.addSession("port123");
  let data = { visibility_event_rcvd_ts: 444455 };

  instance.saveSessionPerfData("port123", data);

  Assert.ok(
    instance.setLoadTriggerInfo.calledOnce,
    "TelemetryFeed.setLoadTriggerInfo was called once"
  );
  Assert.ok(instance.setLoadTriggerInfo.calledWithExactly("port123"));

  Assert.equal(
    instance.sessions.get("port123").perf.visibility_event_rcvd_ts,
    444455
  );

  sandbox.restore();
});

add_task(
  async function test_saveSessionPerfData_does_not_call_setLoadTriggerInfo() {
    info(
      "TelemetryFeed.saveSessionPerfData shouldn't call setLoadTriggerInfo if " +
        "data has no visibility_event_rcvd_ts"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();

    sandbox.stub(instance, "setLoadTriggerInfo");
    instance.addSession("port123");
    instance.saveSessionPerfData("port123", { monkeys_ts: 444455 });

    Assert.ok(
      instance.setLoadTriggerInfo.notCalled,
      "TelemetryFeed.setLoadTriggerInfo was not called"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_saveSessionPerfData_does_not_call_setLoadTriggerInfo_about_home() {
    info(
      "TelemetryFeed.saveSessionPerfData should not call setLoadTriggerInfo when " +
        "url is about:home"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();

    sandbox.stub(instance, "setLoadTriggerInfo");
    instance.addSession("port123", "about:home");
    let data = { visibility_event_rcvd_ts: 444455 };
    instance.saveSessionPerfData("port123", data);

    Assert.ok(
      instance.setLoadTriggerInfo.notCalled,
      "TelemetryFeed.setLoadTriggerInfo was not called"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_saveSessionPerfData_calls_maybeRecordTopsitesPainted() {
    info(
      "TelemetryFeed.saveSessionPerfData should call maybeRecordTopsitesPainted " +
        "when url is about:home and topsites_first_painted_ts is given"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();

    const TOPSITES_FIRST_PAINTED_TS = 44455;
    let data = { topsites_first_painted_ts: TOPSITES_FIRST_PAINTED_TS };

    sandbox.stub(AboutNewTab, "maybeRecordTopsitesPainted");
    instance.addSession("port123", "about:home");
    instance.saveSessionPerfData("port123", data);

    Assert.ok(
      AboutNewTab.maybeRecordTopsitesPainted.calledOnce,
      "AboutNewTab.maybeRecordTopsitesPainted called once"
    );
    Assert.ok(
      AboutNewTab.maybeRecordTopsitesPainted.calledWith(
        TOPSITES_FIRST_PAINTED_TS
      )
    );
    sandbox.restore();
  }
);

add_task(
  async function test_saveSessionPerfData_records_Glean_newtab_opened_event() {
    info(
      "TelemetryFeed.saveSessionPerfData should record a Glean newtab.opened event " +
        "with the correct visit_id when visibility event received"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    const PAGE = "about:newtab";
    let session = { page: PAGE, perf: {}, session_id: SESSION_ID };
    let data = { visibility_event_rcvd_ts: 444455 };

    sandbox.stub(instance.sessions, "get").returns(session);
    instance.saveSessionPerfData("port123", data);

    let newtabOpenedEvents = Glean.newtab.opened.testGetValue();
    Assert.deepEqual(newtabOpenedEvents[0].extra, {
      newtab_visit_id: SESSION_ID,
      source: PAGE,
    });

    sandbox.restore();
  }
);

add_task(async function test_uninit_deregisters_observer() {
  info(
    "TelemetryFeed.uninit should make this.browserOpenNewtabStart() stop " +
      "observing browser-open-newtab-start"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  let countObservers = () => {
    return [...Services.obs.enumerateObservers("browser-open-newtab-start")]
      .length;
  };

  const ORIGINAL_COUNT = countObservers();
  instance.init();
  Assert.equal(countObservers(), ORIGINAL_COUNT + 1, "Observer was added");

  instance.uninit();
  Assert.equal(countObservers(), ORIGINAL_COUNT, "Observer was removed");

  sandbox.restore();
});

add_task(async function test_onAction_basic_actions() {
  let browser = Services.appShell
    .createWindowlessBrowser(false)
    .document.createElement("browser");

  let testOnAction = (setupFn, action, checkFn) => {
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    setupFn(sandbox, instance);

    instance.onAction(action);
    checkFn(instance);
    sandbox.restore();
  };

  info("TelemetryFeed.onAction should call .init() on an INIT action");
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "init");
      sandbox.stub(instance, "sendPageTakeoverData");
    },
    { type: actionTypes.INIT },
    instance => {
      Assert.ok(instance.init.calledOnce, "TelemetryFeed.init called once");
      Assert.ok(
        instance.sendPageTakeoverData.calledOnce,
        "TelemetryFeed.sendPageTakeoverData called once"
      );
    }
  );

  info("TelemetryFeed.onAction should call .uninit() on an UNINIT action");
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "uninit");
    },
    { type: actionTypes.UNINIT },
    instance => {
      Assert.ok(instance.uninit.calledOnce, "TelemetryFeed.uninit called once");
    }
  );

  info(
    "TelemetryFeed.onAction should call .handleNewTabInit on a " +
      "NEW_TAB_INIT action"
  );
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "handleNewTabInit");
    },
    actionCreators.AlsoToMain({
      type: actionTypes.NEW_TAB_INIT,
      data: { url: "about:newtab", browser },
    }),
    instance => {
      Assert.ok(
        instance.handleNewTabInit.calledOnce,
        "TelemetryFeed.handleNewTabInit called once"
      );
    }
  );

  info(
    "TelemetryFeed.onAction should call .addSession() on a " +
      "NEW_TAB_INIT action"
  );
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "addSession").returns({ perf: {} });
      sandbox.stub(instance, "setLoadTriggerInfo");
    },
    actionCreators.AlsoToMain(
      {
        type: actionTypes.NEW_TAB_INIT,
        data: { url: "about:monkeys", browser },
      },
      "port123"
    ),
    instance => {
      Assert.ok(
        instance.addSession.calledOnce,
        "TelemetryFeed.addSession called once"
      );
      Assert.ok(instance.addSession.calledWith("port123", "about:monkeys"));
    }
  );

  info(
    "TelemetryFeed.onAction should call .endSession() on a " +
      "NEW_TAB_UNLOAD action"
  );
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "endSession");
    },
    actionCreators.AlsoToMain({ type: actionTypes.NEW_TAB_UNLOAD }, "port123"),
    instance => {
      Assert.ok(
        instance.endSession.calledOnce,
        "TelemetryFeed.endSession called once"
      );
      Assert.ok(instance.endSession.calledWith("port123"));
    }
  );

  info(
    "TelemetryFeed.onAction should call .saveSessionPerfData " +
      "on SAVE_SESSION_PERF_DATA"
  );
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "saveSessionPerfData");
    },
    actionCreators.AlsoToMain(
      { type: actionTypes.SAVE_SESSION_PERF_DATA, data: { some_ts: 10 } },
      "port123"
    ),
    instance => {
      Assert.ok(
        instance.saveSessionPerfData.calledOnce,
        "TelemetryFeed.saveSessionPerfData called once"
      );
      Assert.ok(
        instance.saveSessionPerfData.calledWith("port123", { some_ts: 10 })
      );
    }
  );
});

add_task(
  async function test_onAction_calls_handleDiscoveryStreamImpressionStats_ds() {
    info(
      "TelemetryFeed.onAction should call " +
        ".handleDiscoveryStreamImpressionStats on a " +
        "DISCOVERY_STREAM_IMPRESSION_STATS action"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();

    let session = {};
    sandbox.stub(instance.sessions, "get").returns(session);
    let data = { source: "foo", tiles: [{ id: 1 }] };
    let action = { type: actionTypes.DISCOVERY_STREAM_IMPRESSION_STATS, data };
    sandbox.spy(instance, "handleDiscoveryStreamImpressionStats");

    instance.onAction(actionCreators.AlsoToMain(action, "port123"));

    Assert.ok(
      instance.handleDiscoveryStreamImpressionStats.calledWith("port123", data)
    );

    sandbox.restore();
  }
);

add_task(
  async function test_onAction_calls_handleTopSitesSponsoredImpressionStats() {
    info(
      "TelemetryFeed.onAction should call " +
        ".handleTopSitesSponsoredImpressionStats on a " +
        "TOP_SITES_SPONSORED_IMPRESSION_STATS action"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();

    let session = {};
    sandbox.stub(instance.sessions, "get").returns(session);
    let data = { type: "impression", tile_id: 42, position: 1 };
    let action = {
      type: actionTypes.TOP_SITES_SPONSORED_IMPRESSION_STATS,
      data,
    };
    sandbox.spy(instance, "handleTopSitesSponsoredImpressionStats");

    instance.onAction(actionCreators.AlsoToMain(action));

    Assert.ok(
      instance.handleTopSitesSponsoredImpressionStats.calledOnce,
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats called once"
    );
    Assert.deepEqual(
      instance.handleTopSitesSponsoredImpressionStats.firstCall.args[0].data,
      data
    );

    sandbox.restore();
  }
);

add_task(async function test_onAction_calls_handleAboutSponsoredTopSites() {
  info(
    "TelemetryFeed.onAction should call " +
      ".handleAboutSponsoredTopSites on a " +
      "ABOUT_SPONSORED_TOP_SITES action"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  let data = { position: 0, advertiser_name: "moo", tile_id: 42 };
  let action = { type: actionTypes.ABOUT_SPONSORED_TOP_SITES, data };
  sandbox.spy(instance, "handleAboutSponsoredTopSites");

  instance.onAction(actionCreators.AlsoToMain(action));

  Assert.ok(
    instance.handleAboutSponsoredTopSites.calledOnce,
    "TelemetryFeed.handleAboutSponsoredTopSites called once"
  );

  sandbox.restore();
});

add_task(async function test_onAction_calls_handleBlockUrl() {
  info(
    "TelemetryFeed.onAction should call #handleBlockUrl on a BLOCK_URL action"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  let data = { position: 0, advertiser_name: "moo", tile_id: 42 };
  let action = { type: actionTypes.BLOCK_URL, data };
  sandbox.spy(instance, "handleBlockUrl");

  instance.onAction(actionCreators.AlsoToMain(action));

  Assert.ok(
    instance.handleBlockUrl.calledOnce,
    "TelemetryFeed.handleBlockUrl called once"
  );

  sandbox.restore();
});

add_task(
  async function test_onAction_calls_handleTopSitesOrganicImpressionStats() {
    info(
      "TelemetryFeed.onAction should call .handleTopSitesOrganicImpressionStats " +
        "on a TOP_SITES_ORGANIC_IMPRESSION_STATS action"
    );
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();

    let session = {};
    sandbox.stub(instance.sessions, "get").returns(session);

    let data = { type: "impression", position: 1 };
    let action = { type: actionTypes.TOP_SITES_ORGANIC_IMPRESSION_STATS, data };
    sandbox.spy(instance, "handleTopSitesOrganicImpressionStats");

    instance.onAction(actionCreators.AlsoToMain(action));

    Assert.ok(
      instance.handleTopSitesOrganicImpressionStats.calledOnce,
      "TelemetryFeed.handleTopSitesOrganicImpressionStats called once"
    );
    Assert.deepEqual(
      instance.handleTopSitesOrganicImpressionStats.firstCall.args[0].data,
      data
    );

    sandbox.restore();
  }
);

add_task(async function test_handleNewTabInit_sets_preloaded_session() {
  info(
    "TelemetryFeed.handleNewTabInit should set the session as preloaded " +
      "if the browser is preloaded"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  let session = { perf: {} };
  let preloadedBrowser = {
    getAttribute() {
      return "preloaded";
    },
  };
  sandbox.stub(instance, "addSession").returns(session);

  instance.onAction(
    actionCreators.AlsoToMain({
      type: actionTypes.NEW_TAB_INIT,
      data: { url: "about:newtab", browser: preloadedBrowser },
    })
  );

  Assert.ok(session.perf.is_preloaded, "is_preloaded property was set");

  sandbox.restore();
});

add_task(async function test_handleNewTabInit_sets_nonpreloaded_session() {
  info(
    "TelemetryFeed.handleNewTabInit should set the session as non-preloaded " +
      "if the browser is non-preloaded"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  let session = { perf: {} };
  let preloadedBrowser = {
    getAttribute() {
      return "";
    },
  };
  sandbox.stub(instance, "addSession").returns(session);

  instance.onAction(
    actionCreators.AlsoToMain({
      type: actionTypes.NEW_TAB_INIT,
      data: { url: "about:newtab", browser: preloadedBrowser },
    })
  );

  Assert.ok(!session.perf.is_preloaded, "is_preloaded property is not true");

  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_homepage_category() {
  info(
    "TelemetryFeed.sendPageTakeoverData should call " +
      "handleASRouterUserEvent"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  sandbox.stub(HomePage, "get").returns("https://searchprovider.com");
  sandbox.stub(instance, "configureContentPing");
  instance._classifySite = () => Promise.resolve("other");

  await instance.sendPageTakeoverData();
  Assert.equal(Glean.newtab.homepageCategory.testGetValue(), "other");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_newtab_category_custom() {
  info(
    "TelemetryFeed.sendPageTakeoverData should send correct newtab " +
      "category for about:newtab set to custom URL"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  sandbox.stub(AboutNewTab, "newTabURLOverridden").get(() => true);
  sandbox
    .stub(AboutNewTab, "newTabURL")
    .get(() => "https://searchprovider.com");
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  sandbox.stub(instance, "configureContentPing");
  instance._classifySite = () => Promise.resolve("other");

  await instance.sendPageTakeoverData();
  Assert.equal(Glean.newtab.newtabCategory.testGetValue(), "other");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_newtab_category_custom() {
  info(
    "TelemetryFeed.sendPageTakeoverData should not set home|newtab " +
      "category if neither about:{home,newtab} are set to custom URL"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  sandbox.stub(instance, "configureContentPing");
  instance._classifySite = () => Promise.resolve("other");

  await instance.sendPageTakeoverData();
  Assert.equal(Glean.newtab.newtabCategory.testGetValue(), "enabled");
  Assert.equal(Glean.newtab.homepageCategory.testGetValue(), "enabled");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_newtab_category_extension() {
  info(
    "TelemetryFeed.sendPageTakeoverData should set correct home|newtab " +
      "category when changed by extension"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const ID = "{abc-foo-bar}";
  sandbox.stub(ExtensionSettingsStore, "getSetting").returns({ id: ID });

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  sandbox.stub(instance, "configureContentPing");
  instance._classifySite = () => Promise.resolve("other");

  await instance.sendPageTakeoverData();
  Assert.equal(Glean.newtab.newtabCategory.testGetValue(), "extension");
  Assert.equal(Glean.newtab.homepageCategory.testGetValue(), "extension");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_newtab_disabled() {
  info(
    "TelemetryFeed.sendPageTakeoverData instruments when newtab is disabled"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  Services.prefs.setBoolPref("browser.newtabpage.enabled", false);
  sandbox.stub(instance, "configureContentPing");
  instance._classifySite = () => Promise.resolve("other");

  await instance.sendPageTakeoverData();
  Assert.equal(Glean.newtab.newtabCategory.testGetValue(), "disabled");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  Services.prefs.clearUserPref("browser.newtabpage.enabled");
  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_homepage_disabled() {
  info(
    "TelemetryFeed.sendPageTakeoverData instruments when homepage is disabled"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  sandbox.stub(HomePage, "overridden").get(() => true);
  sandbox.stub(instance, "configureContentPing");

  await instance.sendPageTakeoverData();
  Assert.equal(Glean.newtab.homepageCategory.testGetValue(), "disabled");

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_sendPageTakeoverData_newtab_ping() {
  info("TelemetryFeed.sendPageTakeoverData should send a 'newtab' ping");
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  sandbox.stub(instance, "configureContentPing");

  let pingSubmitted = new Promise(resolve => {
    GleanPings.newtab.testBeforeNextSubmit(reason => {
      Assert.equal(reason, "component_init");
      resolve();
    });
  });

  await instance.sendPageTakeoverData();
  await pingSubmitted;

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(
  async function test_handleDiscoveryStreamImpressionStats_records_adsClient_impression_if_enabled() {
    info(
      "TelemetryFeed.handleDiscoveryStreamImpressionStats should record adsClient impression " +
        "when enabled"
    );
    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");
    Services.prefs.setBoolPref(PREF_UNIFIED_ADS_SPOCS_ENABLED, true);

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { recordImpression: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();
    sandbox.replaceGetter(
      instance,
      "SHOW_SPONSORED_STORIES_ENABLED",
      sinon.fake.returns(true)
    );

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    sandbox
      .stub(instance.sessions, "get")
      .returns({ session_id: "decafc0ffee" });

    instance.handleDiscoveryStreamImpressionStats(42, {
      tiles: [
        {
          card_type: "spoc",
          pos: 42,
          shim: "https://test.reporting.net/",
        },
      ],
    });

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.recordImpression.calledOnce);

    Services.prefs.clearUserPref(PREF_UNIFIED_ADS_SPOCS_ENABLED);
    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamImpressionStats_should_throw() {
    info(
      "TelemetryFeed.handleDiscoveryStreamImpressionStats should throw " +
        "for a missing session"
    );

    let instance = new TelemetryFeed();
    try {
      instance.handleDiscoveryStreamImpressionStats("a_missing_port", {});
      Assert.ok(false, "Should not have reached here.");
    } catch (e) {
      Assert.ok(true, "Should have thrown for a missing session.");
    }
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_add_keyed_scalar() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should add to " +
        "keyed scalar on an impression event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.telemetry.clearScalars();

    let data = {
      type: "impression",
      tile_id: 42,
      source: "newtab",
      position: 0,
      reporting_url: "https://test.reporting.net/",
    };
    await instance.handleTopSitesSponsoredImpressionStats({ data });
    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "contextual.services.topsites.impression",
      "newtab_1",
      1
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_add_keyed_scalar_click() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should add to " +
        "keyed scalar on a click event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.telemetry.clearScalars();

    let data = {
      type: "click",
      tile_id: 42,
      source: "newtab",
      position: 0,
      reporting_url: "https://test.reporting.net/",
    };
    await instance.handleTopSitesSponsoredImpressionStats({ data });
    TelemetryTestUtils.assertKeyedScalar(
      TelemetryTestUtils.getProcessScalars("parent", true, true),
      "contextual.services.topsites.click",
      "newtab_1",
      1
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_record_glean_impression() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should record a " +
        "Glean topsites.impression event on an impression event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      type: "impression",
      tile_id: 42,
      source: "newtab",
      position: 1,
      reporting_url: "https://test.reporting.net/",
      advertiser: "adnoid ads",
    };
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    await instance.handleTopSitesSponsoredImpressionStats({ data });
    let impressions = Glean.topsites.impression.testGetValue();
    Assert.equal(impressions.length, 1, "Should have recorded 1 impression");

    Assert.deepEqual(impressions[0].extra, {
      advertiser_name: "adnoid ads",
      tile_id: data.tile_id,
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(true),
      position: String(1),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_record_glean_click() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should record " +
        "a Glean topsites.click event on a click event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      type: "click",
      advertiser: "test advertiser",
      tile_id: 42,
      source: "newtab",
      position: 0,
      reporting_url: "https://test.reporting.net/",
    };
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    await instance.handleTopSitesSponsoredImpressionStats({ data });
    let clicks = Glean.topsites.click.testGetValue();
    Assert.equal(clicks.length, 1, "Should have recorded 1 click");

    Assert.deepEqual(clicks[0].extra, {
      advertiser_name: "test advertiser",
      tile_id: data.tile_id,
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(true),
      position: String(0),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_no_submit_unknown_pingType() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should not " +
        "submit on unknown pingTypes"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = { type: "unknown_type" };

    await instance.handleTopSitesSponsoredImpressionStats({ data });
    let impressions = Glean.topsites.impression.testGetValue();
    Assert.ok(!impressions, "Should not have recorded any impressions");

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_record_adsClient_impression_if_enabled() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should record an " +
        "adsClient impression event on an impression event if enabled"
    );

    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");
    Services.prefs.setBoolPref(PREF_UNIFIED_ADS_TILES_ENABLED, true);

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { recordImpression: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();
    sandbox.replaceGetter(
      instance,
      "SHOW_SPONSORED_TOPSITES_ENABLED",
      sinon.fake.returns(true)
    );

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    await instance.handleTopSitesSponsoredImpressionStats({
      data: {
        type: "impression",
        tile_id: 42,
        source: "newtab",
        position: 1,
        reporting_url: "https://test.reporting.net/",
        advertiser: "adnoid ads",
      },
    });

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.recordImpression.calledOnce);

    Services.prefs.clearUserPref(PREF_UNIFIED_ADS_TILES_ENABLED);
    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_record_adsClient_click_if_enabled() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should record an " +
        "adsClient impresclicksion event on an click event if enabled"
    );

    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");
    Services.prefs.setBoolPref(PREF_UNIFIED_ADS_TILES_ENABLED, true);

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { recordClick: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();
    sandbox.replaceGetter(
      instance,
      "SHOW_SPONSORED_TOPSITES_ENABLED",
      sinon.fake.returns(true)
    );

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    await instance.handleTopSitesSponsoredImpressionStats({
      data: {
        type: "click",
        tile_id: 42,
        source: "newtab",
        position: 1,
        reporting_url: "https://test.reporting.net/",
        advertiser: "adnoid ads",
      },
    });

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.recordClick.calledOnce);

    Services.prefs.clearUserPref(PREF_UNIFIED_ADS_TILES_ENABLED);
    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesOrganicImpressionStats_record_glean_topsites_impression() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should record a " +
        "Glean topsites.impression event on an impression event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      type: "impression",
      source: "newtab",
      position: 0,
      isPinned: false,
      smartScores: { moo: 1 },
      smartWeights: { moo: 0 },
    };
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesOrganicImpressionStats({ data });
    let impressions = Glean.topsites.impression.testGetValue();
    Assert.equal(impressions.length, 1, "Recorded 1 impression");

    Assert.deepEqual(impressions[0].extra, {
      is_pinned: String(false),
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(0),
      smart_scores: JSON.stringify({ moo: 1 }),
      smart_weights: JSON.stringify({ moo: 0 }),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesOrganicImpressionStats_record_glean_topsites_click() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should record a " +
        "Glean topsites.click event on a click event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      type: "click",
      source: "newtab",
      position: 0,
      isPinned: false,
      smartScores: { moo: 1 },
      smartWeights: { moo: 0 },
    };
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesOrganicImpressionStats({ data });
    let clicks = Glean.topsites.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");

    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(0),
      is_pinned: String(false),
      smart_scores: JSON.stringify({ moo: 1 }),
      smart_weights: JSON.stringify({ moo: 0 }),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesOrganicImpressionStats_no_recording() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should not " +
        "record events on an unknown session"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    sandbox.stub(instance.sessions, "get").returns(false);

    await instance.handleTopSitesOrganicImpressionStats({});
    Assert.ok(!Glean.topsites.click.testGetValue(), "Click was not recorded");
    Assert.ok(
      !Glean.topsites.impression.testGetValue(),
      "Impression was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesOrganicImpressionStats_no_recording_with_session() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should not record " +
        "events on an unknown impressionStats action"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesOrganicImpressionStats({ type: "unknown" });
    Assert.ok(!Glean.topsites.click.testGetValue(), "Click was not recorded");
    Assert.ok(
      !Glean.topsites.impression.testGetValue(),
      "Impression was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_no_recording_with_session() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "action with no `data`"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let action = actionCreators.DiscoveryStreamUserEvent();
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    Assert.ok(
      !Glean.pocket.topicClick.testGetValue(),
      "Pocket topicClick was not recorded"
    );
    Assert.ok(
      !Glean.pocket.click.testGetValue(),
      "Pocket click was not recorded"
    );
    Assert.ok(
      !Glean.pocket.save.testGetValue(),
      "Pocket save was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_click_with_no_value() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "CLICK data with no value"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "POPULAR_TOPICS",
    });
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    let topicClicks = Glean.pocket.topicClick.testGetValue();
    Assert.equal(topicClicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(topicClicks[0].extra, {
      newtab_visit_id: SESSION_ID,
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_non_popular_click_with_no_value() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "non-POPULAR_TOPICS CLICK data with no value"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "not-POPULAR_TOPICS",
    });
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    Assert.ok(
      !Glean.pocket.topicClick.testGetValue(),
      "Pocket topicClick was not recorded"
    );
    Assert.ok(
      !Glean.pocket.click.testGetValue(),
      "Pocket click was not recorded"
    );
    Assert.ok(
      !Glean.pocket.save.testGetValue(),
      "Pocket save was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_non_popular_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "CLICK data with non-POPULAR_TOPICS source"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const TOPIC = "atopic";
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "not-POPULAR_TOPICS",
      value: {
        card_type: "topics_widget",
        topic: TOPIC,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    let topicClicks = Glean.pocket.topicClick.testGetValue();
    Assert.equal(topicClicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(topicClicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      topic: TOPIC,
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_without_card_type() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent doesn't instrument " +
        "a CLICK without a card_type"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "not-POPULAR_TOPICS",
      value: {
        card_type: "not spoc, organic, or topics_widget",
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    Assert.ok(
      !Glean.pocket.topicClick.testGetValue(),
      "Pocket topicClick was not recorded"
    );
    Assert.ok(
      !Glean.pocket.click.testGetValue(),
      "Pocket click was not recorded"
    );
    Assert.ok(
      !Glean.pocket.save.testGetValue(),
      "Pocket save was not recorded"
    );

    sandbox.restore();
  }
);

add_task(async function test_handleDiscoveryStreamUserEvent_popular_click() {
  info(
    "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a popular " +
      "topic click"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  const TOPIC = "entertainment";
  let action = actionCreators.DiscoveryStreamUserEvent({
    event: "CLICK",
    source: "POPULAR_TOPICS",
    value: {
      card_type: "topics_widget",
      topic: TOPIC,
    },
  });

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  instance.handleDiscoveryStreamUserEvent(action);
  let topicClicks = Glean.pocket.topicClick.testGetValue();
  Assert.equal(topicClicks.length, 1, "Recorded 1 click");
  Assert.deepEqual(topicClicks[0].extra, {
    newtab_visit_id: SESSION_ID,
    topic: TOPIC,
  });

  sandbox.restore();
});

add_task(async function test_handleDiscoveryStreamUserEvent_tooltip_click() {
  info(
    "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a " +
      "tooltip click"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  const feature = "FEATURE_HIGHLIGHT_DEFAULT";
  let action = actionCreators.DiscoveryStreamUserEvent({
    event: "CLICK",
    source: "FEATURE_HIGHLIGHT",
    value: {
      feature,
    },
  });

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  instance.handleDiscoveryStreamUserEvent(action);
  let tooltipClicks = Glean.newtab.tooltipClick.testGetValue();
  Assert.equal(tooltipClicks.length, 1, "Recorded 1 click");
  Assert.deepEqual(tooltipClicks[0].extra, {
    newtab_visit_id: SESSION_ID,
    feature,
  });

  sandbox.restore();
});

add_task(
  async function test_handleDiscoveryStreamUserEvent_organic_top_stories_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments an organic " +
        "top stories click"
    );
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        corpus_item_id: "decaf-beef",
        scheduled_corpus_item_id: "dead-beef",
        tile_id: 314623757745896,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(ACTION_POSITION),
      content_redacted: String(true),
    });

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_private_ping_with_redactions_organic_top_stories_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments an organic " +
        "top stories click with private ping fully enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        corpus_item_id: "decaf-beef",
        scheduled_corpus_item_id: "dead-beef",
        tile_id: 314623757745896,
        content_redacted: true,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    sandbox.spy(instance.newtabContentPing, "recordEvent");

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();

    Assert.equal(clicks.length, 1, "Recorded 1 content click");
    Assert.equal(clicks.length, 1, "Recorded 1 private click");
    Assert.deepEqual(clicks[0].extra, {
      content_redacted: String(true),
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(ACTION_POSITION),
    });

    Assert.ok(
      instance.newtabContentPing.recordEvent.calledWith(
        "click",
        sinon.match({
          newtab_visit_id: SESSION_ID,
          is_sponsored: false,
          position: ACTION_POSITION,
          corpus_item_id: "decaf-beef",
          scheduled_corpus_item_id: "dead-beef",
          tile_id: 314623757745896,
        })
      ),
      "NewTabContentPing passed the expected arguments."
    );

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_sponsored_top_stories_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a sponsored " +
        "top stories click"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    const SHIM = "Y29uc2lkZXIgeW91ciBjdXJpb3NpdHkgcmV3YXJkZWQ=";
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "spoc",
        tile_id: 448685088,
        shim: SHIM,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(true),
      position: String(ACTION_POSITION),
      tile_id: String(448685088),
      content_redacted: String(true),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_record_adsClient_click_if_enabled() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent records an adsClient click " +
        "if enabled"
    );

    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");
    Services.prefs.setBoolPref(PREF_UNIFIED_ADS_SPOCS_ENABLED, true);

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { recordClick: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();
    sandbox.replaceGetter(
      instance,
      "SHOW_SPONSORED_STORIES_ENABLED",
      sinon.fake.returns(true)
    );

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    sandbox
      .stub(instance.sessions, "get")
      .returns({ session_id: "decafc0ffee" });

    instance.handleDiscoveryStreamUserEvent(
      actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        action_position: 42,
        value: {
          card_type: "spoc",
          shim: "https://test.reporting.net/",
        },
      })
    );

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.recordClick.calledOnce);

    Services.prefs.clearUserPref(PREF_UNIFIED_ADS_SPOCS_ENABLED);
    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleReportAdUserEvent_reports_adsClient_ad_if_enabled() {
    info(
      "TelemetryFeed.handleDiscoveryStreamImpressionStats should report add to adsClient " +
        "when enabled"
    );

    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { reportAd: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    await instance.handleReportAdUserEvent({
      type: actionTypes.REPORT_AD_SUBMIT,
      data: {
        placement_id: "place",
        position: 42,
        report_reason: "not_interested",
        reporting_url: "https://test.reporting.net/",
      },
    });

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.reportAd.calledOnce);

    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_sponsored_top_stories_click_tile_id_redacted() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent redacts the tile_id from " +
        "the newtab ping for a sponsored top stories click when the " +
        "redactTileIdForSponsored trainhop config is enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            trainhopConfig: {
              newtabPing: { redactTileIdForSponsored: true },
            },
          },
        },
      }),
    };
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    const TILE_ID = 448685088;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "spoc",
        tile_id: TILE_ID,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    sandbox.spy(instance.newtabContentPing, "recordEvent");

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(
      clicks[0].extra,
      {
        newtab_visit_id: SESSION_ID,
        is_sponsored: String(true),
        position: String(ACTION_POSITION),
        content_redacted: String(true),
      },
      "The tile_id should have been redacted from the newtab ping."
    );

    Assert.ok(
      instance.newtabContentPing.recordEvent.calledWith(
        "click",
        sinon.match({
          is_sponsored: true,
          position: ACTION_POSITION,
          tile_id: TILE_ID,
        })
      ),
      "Redacting the newtab ping should not have mutated the event data handed " +
        "to the newtab-content ping, which does its own sanitization."
    );

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_handleAboutSponsoredTopSites_record_showPrivacyClick() {
    info(
      "TelemetryFeed.handleAboutSponsoredTopSites should record a Glean " +
        "topsites.showPrivacyClick event on action"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      position: 42,
      advertiser_name: "mozilla",
      tile_id: 4567,
    };

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleAboutSponsoredTopSites({ data });

    let clicks = Glean.topsites.showPrivacyClick.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      advertiser_name: data.advertiser_name,
      tile_id: String(data.tile_id),
      newtab_visit_id: SESSION_ID,
      position: String(data.position),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleAboutSponsoredTopSites_no_record_showPrivacyClick() {
    info(
      "TelemetryFeed.handleAboutSponsoredTopSites should not record a Glean " +
        "topsites.showPrivacyClick event if there's no session"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      position: 42,
      advertiser_name: "mozilla",
      tile_id: 4567,
    };

    sandbox.stub(instance.sessions, "get").returns(null);

    instance.handleAboutSponsoredTopSites({ data });

    let clicks = Glean.topsites.showPrivacyClick.testGetValue();
    Assert.ok(!clicks, "Did not record any clicks");

    sandbox.restore();
  }
);

add_task(async function test_handleBlockUrl_no_record_dismisses() {
  info(
    "TelemetryFeed.handleBlockUrl shouldn't record events for pocket " +
      "cards' dismisses"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  let data = [
    {
      // Shouldn't record anything for this one
      is_pocket_card: true,
      position: 43,
      tile_id: undefined,
    },
  ];

  await instance.handleBlockUrl({ data });

  Assert.ok(
    !Glean.topsites.dismiss.testGetValue(),
    "Should not record a dismiss for Pocket cards"
  );

  sandbox.restore();
});

add_task(async function test_handleBlockUrl_record_dismiss_on_action() {
  info(
    "TelemetryFeed.handleBlockUrl should record a topsites.dismiss event " +
      "on action"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  let data = [
    {
      is_pocket_card: false,
      position: 42,
      advertiser_name: "mozilla",
      tile_id: 4567,
      isSponsoredTopSite: 1, // for some reason this is an int.
    },
  ];

  await instance.handleBlockUrl({ data, source: "TOP_SITES" });

  let dismisses = Glean.topsites.dismiss.testGetValue();
  Assert.equal(dismisses.length, 1, "Should have recorded 1 dismiss");
  Assert.deepEqual(dismisses[0].extra, {
    advertiser_name: data[0].advertiser_name,
    tile_id: String(data[0].tile_id),
    newtab_visit_id: SESSION_ID,
    is_sponsored: String(!!data[0].isSponsoredTopSite),
    position: String(data[0].position),
  });

  sandbox.restore();
});

add_task(
  async function test_handleBlockUrl_record_dismiss_on_nonsponsored_action() {
    info(
      "TelemetryFeed.handleBlockUrl should record a Glean topsites.dismiss " +
        "event on action on non-sponsored topsite"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let data = [
      {
        is_pocket_card: false,
        position: 42,
        tile_id: undefined,
      },
    ];

    await instance.handleBlockUrl({ data, source: "TOP_SITES" });

    let dismisses = Glean.topsites.dismiss.testGetValue();
    Assert.equal(dismisses.length, 1, "Should have recorded 1 dismiss");
    Assert.deepEqual(dismisses[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(data[0].position),
    });

    sandbox.restore();
  }
);

add_task(async function test_handleBlockUrl_no_record_dismiss_on_no_session() {
  info(
    "TelemetryFeed.handleBlockUrl should not record a Glean " +
      "topsites.dismiss event if there's no session"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  sandbox.stub(instance.sessions, "get").returns(null);

  let data = {};

  await instance.handleBlockUrl({ data });

  Assert.ok(
    !Glean.topsites.dismiss.testGetValue(),
    "Should not have recorded a dismiss"
  );

  sandbox.restore();
});

add_task(function test_randomizeOrganicContentEvent() {
  info(
    "TelemetryFeed._randomizeOrganicContentEvent should return true or false" +
      " based on the given probability"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  const computeRec = id => ({
    corpus_item_id: `item-${id}`,
    topic: "a",
    is_sponsored: false,
    section_id: "section",
    section_position: 3,
  });
  const allRecs = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(computeRec);
  sandbox.stub(instance, "getRecommendationCount").returns(allRecs.length);
  sandbox.stub(instance, "getAllRecommendations").returns(allRecs);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  let decideStub = sandbox.stub(NewTabContentPing, "decideWithProbability");
  decideStub.returns(true);
  let result = instance.randomizeOrganicContentEvent(allRecs[0]);
  Assert.equal(result, allRecs[0]);
  Assert.ok(decideStub.calledOnce, "decideWithProbability was called once");
  const [probUsed] = decideStub.firstCall.args;
  Assert.greater(probUsed, 0.9); // Epsilon 30 is very high probability
  Assert.less(probUsed, 1.01);

  // Run again - randomization kicks in
  decideStub.returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(3);
  result = instance.randomizeOrganicContentEvent(allRecs[0]);
  Assert.equal(probUsed, decideStub.lastCall.args[0]);
  Assert.deepEqual(result, allRecs[3]);

  sandbox.restore();
});

add_task(async function test_handleSpocPlaceholderDuration_records_metric() {
  info(
    "TelemetryFeed.handleSpocPlaceholderDuration should record the " +
      "spoc_placeholder_duration metric"
  );

  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const DURATION_MS = 150;
  let action = {
    type: actionTypes.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
    data: { duration: DURATION_MS },
  };

  instance.handleSpocPlaceholderDuration(action);

  let recordedDuration = Glean.pocket.spocPlaceholderDuration.testGetValue();
  Assert.ok(recordedDuration, "Metric should be recorded");
  Assert.equal(recordedDuration.count, 1, "Should have 1 sample");
  Assert.greaterOrEqual(
    recordedDuration.sum,
    DURATION_MS,
    "Duration should be at least the input value"
  );
});

add_task(async function test_handleSpocPlaceholderDuration_ignores_negative() {
  info(
    "TelemetryFeed.handleSpocPlaceholderDuration should ignore negative durations"
  );

  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  let action = {
    type: actionTypes.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
    data: { duration: -1 },
  };

  instance.handleSpocPlaceholderDuration(action);

  let recordedDuration = Glean.pocket.spocPlaceholderDuration.testGetValue();
  Assert.equal(
    recordedDuration,
    null,
    "Metric should not be recorded for negative duration"
  );
});

add_task(async function test_handleSpocPlaceholderDuration_ignores_undefined() {
  info(
    "TelemetryFeed.handleSpocPlaceholderDuration should ignore undefined durations"
  );

  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  let action = {
    type: actionTypes.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
    data: {},
  };

  instance.handleSpocPlaceholderDuration(action);

  let recordedDuration = Glean.pocket.spocPlaceholderDuration.testGetValue();
  Assert.equal(
    recordedDuration,
    null,
    "Metric should not be recorded for undefined duration"
  );
});
/**
 * Helper to create a mock store with trainhopConfig
 */
function createMockStore(
  trainhopConfig = {},
  civState = { initialized: false }
) {
  return {
    getState: () => ({
      Prefs: {
        values: {
          trainhopConfig,
        },
      },
      InferredPersonalization: civState,
    }),
  };
}

add_task(async function test_initializePrivacySession_defaults_to_private() {
  info(
    "initializeGleanSession should default to PrivateGleanSession " +
      "when privatePingEnabled is false"
  );

  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);

  let instance = new TelemetryFeed();
  instance.store = createMockStore({
    Prefs: {
      values: {
        trainhopConfig: { newtabPrivatePing: { clickOnly: true } },
      },
    },
  });

  instance.initializeGleanSession();

  Assert.equal(
    instance.gleanSessionType,
    "private",
    "Should be PrivateGleanSession when privatePingEnabled is false"
  );

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
});

add_task(
  async function test_initializePrivacySession_defaults_to_private_no_clickOnly() {
    info(
      "initializeGleanSession should default to PrivateGleanSession " +
        "when clickOnly is false"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let instance = new TelemetryFeed();
    instance.store = createMockStore({
      Prefs: {
        values: {
          trainhopConfig: { newtabPrivatePing: { clickOnly: false } },
        },
      },
    });

    instance.initializeGleanSession();

    Assert.equal(
      instance.gleanSessionType,
      "private",
      "Should be PrivateGleanSession when clickOnly is false"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_initializePrivacySession_defaults_to_private_sov_enabled() {
    info(
      "initializeGleanSession should default to PrivateGleanSession when SOV is enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            trainhopConfig: {
              sov: { enabled: true },
              newtabPrivatePing: { clickOnly: true },
            },
          },
        },
      }),
    };

    instance.initializeGleanSession();

    Assert.equal(
      instance.gleanSessionType,
      "private",
      "Should be PrivateGleanSession when SOV is enabled"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(async function test_hasRecordedClicksInCIV_with_click_count() {
  info("hasRecordedClicksInCIV should return true when click count > 0");

  let instance = new TelemetryFeed();
  instance.store = createMockStore(
    {},
    {
      initialized: true,
      inferredInterests: { clicks: 5 },
    }
  );

  Assert.equal(
    instance.hasRecordedClicksInCIV(),
    true,
    "Should return true when clicks > 0"
  );
});

add_task(async function test_recordOrQueueEvent_queues_in_normal_session() {
  info("recordOrQueueEvent should queue events in NormalContentSession mode");

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "normal";
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

  let callbackCalled = false;
  let callback = () => {
    callbackCalled = true;
  };

  let recordEventStub = sandbox.stub(instance.newtabContentPing, "recordEvent");

  instance.recordOrQueueEvent(
    "testEvent",
    { test: "data" },
    "session1",
    callback
  );

  Assert.ok(
    !recordEventStub.called,
    "newtabContentPing.recordEvent should NOT be called"
  );
  Assert.ok(!callbackCalled, "callback should NOT be called");

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  sandbox.restore();
});

add_task(async function test_recordOrQueueEvent_immediate_in_private_session() {
  info(
    "recordOrQueueEvent should send events immediately in PrivateContentSession mode"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "private";
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

  let callbackCalled = false;
  let callback = () => {
    callbackCalled = true;
  };

  let recordEventStub = sandbox.stub(instance.newtabContentPing, "recordEvent");

  const eventData = { test: "data" };
  instance.recordOrQueueEvent("testEvent", eventData, "session1", callback);

  Assert.ok(callbackCalled, "callback should be called immediately");
  Assert.ok(
    recordEventStub.calledOnce,
    "newtabContentPing.recordEvent should be called once"
  );
  Assert.ok(
    recordEventStub.calledWith("testEvent", eventData),
    "recordEvent should be called with correct arguments"
  );

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  sandbox.restore();
});

add_task(
  async function test_recordOrQueueEvent_no_private_ping_when_disabled() {
    info(
      "recordOrQueueEvent should not send to newtab-content ping when privatePingEnabled is false"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.gleanSessionType = "private";
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);

    let callbackCalled = false;
    let callback = () => {
      callbackCalled = true;
    };

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    instance.recordOrQueueEvent(
      "testEvent",
      { test: "data" },
      "session1",
      callback
    );

    Assert.ok(callbackCalled, "callback should still be called");
    Assert.ok(
      !recordEventStub.called,
      "newtabContentPing.recordEvent should NOT be called when pref is false"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(async function test_endSession_leaves_other_sessions_events_queued() {
  info(
    "endSession should drain only its own session's queued events, leaving " +
      "events belonging to other open sessions in the buffer"
  );
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "normal";

  let endingSession = instance.addSession("ending-port");
  let otherSession = instance.addSession("other-port");
  endingSession.perf.visibility_event_rcvd_ts = 1000;
  otherSession.perf.visibility_event_rcvd_ts = 1000;

  let endingRecorded = false;
  let otherRecorded = false;
  instance.recordOrQueueEvent(
    "impression",
    {},
    endingSession.session_id,
    () => {
      endingRecorded = true;
    }
  );
  instance.recordOrQueueEvent("impression", {}, otherSession.session_id, () => {
    otherRecorded = true;
  });

  await instance.endSession("ending-port");

  Assert.ok(endingRecorded, "The ending session's event should be recorded.");
  Assert.ok(
    !otherRecorded,
    "The still-open session's event should stay queued."
  );

  // The survivor is still drainable by its own session.
  await instance.endSession("other-port");
  Assert.ok(
    otherRecorded,
    "The other session's event should be recorded when that session ends."
  );

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(async function test_endSession_never_shown_discards_own_events() {
  info(
    "endSession should discard a never-shown session's queued events without " +
      "recording them, and leave other sessions' events alone"
  );
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "normal";

  let neverShown = instance.addSession("preloaded-port");
  let visible = instance.addSession("visible-port");
  visible.perf.visibility_event_rcvd_ts = 1000;

  let neverShownRecorded = false;
  let visibleRecorded = false;
  instance.recordOrQueueEvent("impression", {}, neverShown.session_id, () => {
    neverShownRecorded = true;
  });
  instance.recordOrQueueEvent("impression", {}, visible.session_id, () => {
    visibleRecorded = true;
  });

  await instance.endSession("preloaded-port");

  Assert.ok(
    !neverShownRecorded,
    "A never-shown session's own events must not be recorded."
  );
  Assert.ok(
    !visibleRecorded,
    "A never-shown session must not drain the visible session's events."
  );

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_frecency_boosted_queued() {
    info(
      "Frecency-boosted topsite events should be queued, not sent immediately"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    instance.gleanSessionType = "normal";
    instance.store = createMockStore({
      sov: { enabled: true },
      newtabPrivatePing: { clickOnly: true },
    });

    sandbox.stub(instance.sessions, "get").returns({ session_id: FAKE_UUID });

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    // Send an impression (should be queued)
    let impressionData = {
      type: "impression",
      tile_id: 42,
      source: "newtab",
      position: 1,
      advertiser: "test advertiser",
      visible_topsites: 8,
      frecency_boosted: true,
    };
    await instance.handleTopSitesSponsoredImpressionStats({
      data: impressionData,
    });

    Assert.equal(
      recordEventStub.callCount,
      0,
      "Impression should be queued, not sent to newtab-content"
    );

    // Send a click (should also be queued, not trigger transition)
    let clickData = {
      type: "click",
      tile_id: 42,
      source: "newtab",
      position: 1,
      advertiser: "test advertiser",
      visible_topsites: 8,
      frecency_boosted: true,
    };
    await instance.handleTopSitesSponsoredImpressionStats({ data: clickData });

    Assert.equal(
      instance.gleanSessionType,
      "normal",
      "Topsite clicks should NOT transition to private session"
    );
    Assert.equal(
      recordEventStub.callCount,
      0,
      "Click should also be queued, not sent immediately"
    );

    // Manually transition and verify events are flushed
    instance.transitionToPrivateSession();

    Assert.equal(
      recordEventStub.callCount,
      2,
      "Both impression and click should be sent after transition"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_non_frecency_boosted_content_ping() {
    info(
      "Non-frecency-boosted sponsored topsites in SOV mode should go to newtab-content ping"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    instance.gleanSessionType = "private";
    instance.store = createMockStore({
      sov: { enabled: true },
      newtabPrivatePing: { clickOnly: true },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    let impressionData = {
      type: "impression",
      tile_id: 42,
      source: "newtab",
      position: 1,
      advertiser: "test advertiser",
      visible_topsites: 8,
      frecency_boosted: false,
    };
    await instance.handleTopSitesSponsoredImpressionStats({
      data: impressionData,
    });

    Assert.equal(
      recordEventStub.callCount,
      1,
      "Non-frecency-boosted impression should go to newtab-content ping in private session"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(
  async function test_configureContentPing_includes_experiment_when_merino_feed_experiment() {
    info(
      "configureContentPing should include experiment name and branch " +
        "when merino-feed-experiment pref is truthy"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: { values: { trainhopConfig: {} } },
        DiscoveryStream: { sectionPersonalization: {} },
        InferredPersonalization: { initialized: false },
      }),
    };

    Services.prefs.setBoolPref(PREF_IS_MERINO_FEED_EXPERIMENT, true);

    sandbox
      .stub(NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
      .returns({ slug: "test-experiment", branch: "treatment" });
    let scheduleStub = sandbox.stub(
      instance.newtabContentPing,
      "scheduleSubmission"
    );
    sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

    await instance.configureContentPing();

    Assert.ok(scheduleStub.calledOnce, "scheduleSubmission called once");
    let [metrics] = scheduleStub.firstCall.args;
    Assert.equal(
      metrics.experimentName,
      "test-experiment",
      "experimentName should be the Nimbus slug"
    );
    Assert.equal(
      metrics.experimentBranch,
      "treatment",
      "experimentBranch should be the Nimbus branch"
    );

    Services.prefs.clearUserPref(PREF_IS_MERINO_FEED_EXPERIMENT);
    sandbox.restore();
  }
);

add_task(
  async function test_configureContentPing_omits_experiment_when_not_merino_feed_experiment() {
    info(
      "configureContentPing should not include experiment name and branch " +
        "when merino-feed-experiment pref is falsy"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: { values: { trainhopConfig: {} } },
        DiscoveryStream: { sectionPersonalization: {} },
        InferredPersonalization: { initialized: false },
      }),
    };

    Services.prefs.setBoolPref(PREF_IS_MERINO_FEED_EXPERIMENT, false);

    sandbox
      .stub(NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
      .returns({ slug: "test-experiment", branch: "treatment" });
    let scheduleStub = sandbox.stub(
      instance.newtabContentPing,
      "scheduleSubmission"
    );
    sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

    await instance.configureContentPing();

    Assert.ok(scheduleStub.calledOnce, "scheduleSubmission called once");
    let [metrics] = scheduleStub.firstCall.args;
    Assert.equal(
      metrics.experimentName,
      "",
      "experimentName should not be the Nimbus slug when pref is falsy"
    );
    Assert.equal(
      metrics.experimentBranch,
      "",
      "experimentBranch should not be the Nimbus branch when pref is falsy"
    );

    Services.prefs.clearUserPref(PREF_IS_MERINO_FEED_EXPERIMENT);
    sandbox.restore();
  }
);

add_task(
  async function test_configureContentPing_empty_experiment_when_merino_but_no_metadata() {
    info(
      "configureContentPing should set empty experiment name and branch " +
        "when merino-feed-experiment is truthy but no enrollment metadata"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: { values: { trainhopConfig: {} } },
        DiscoveryStream: { sectionPersonalization: {} },
        InferredPersonalization: { initialized: false },
      }),
    };

    Services.prefs.setBoolPref(PREF_IS_MERINO_FEED_EXPERIMENT, true);

    sandbox
      .stub(NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
      .returns(null);
    let scheduleStub = sandbox.stub(
      instance.newtabContentPing,
      "scheduleSubmission"
    );
    sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

    await instance.configureContentPing();

    Assert.ok(scheduleStub.calledOnce, "scheduleSubmission called once");
    let [metrics] = scheduleStub.firstCall.args;
    Assert.equal(
      metrics.experimentName,
      "",
      "experimentName should be empty when no enrollment metadata"
    );
    Assert.equal(
      metrics.experimentBranch,
      "",
      "experimentBranch should be empty when no enrollment metadata"
    );

    Services.prefs.clearUserPref(PREF_IS_MERINO_FEED_EXPERIMENT);
    sandbox.restore();
  }
);

add_task(async function test_recordEnabledWidgets_partial() {
  info(
    "recordEnabledWidgets should set widgetsEnabledList with only enabled widget names"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.enabled": true,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": true,
          "widgets.focusTimer.enabled": false,
          "widgets.system.focusTimer.enabled": true,
          "widgets.weather.enabled": true,
          "widgets.system.weather.enabled": true,
          "widgets.sportsWidget.enabled": false,
          "widgets.system.sportsWidget.enabled": true,
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists", "weather"],
    "widgetsEnabledList should contain only enabled widget names"
  );
});

add_task(async function test_recordEnabledWidgets_excludes_no_history() {
  info(
    "recordEnabledWidgets should drop the privacy widget when the profile records no history"
  );
  Services.fog.testResetFOG();

  const values = {
    "widgets.enabled": true,
    "widgets.privacy.enabled": true,
    "widgets.system.privacy.enabled": true,
    "widgets.lists.enabled": true,
    "widgets.system.lists.enabled": true,
  };

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({ Prefs: { values } }),
  };

  instance.recordEnabledWidgets();
  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue().sort(),
    ["lists", "privacy"],
    "privacy is listed while the profile records history"
  );

  // Turning history off flips enablement without any widget toggle, so the
  // PREF_CHANGED for the derived value has to re-record the list (Bug 2063207).
  values.recordsHistory = false;
  instance.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "recordsHistory", value: false },
  });

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists"],
    "privacy drops out once the profile records no history"
  );
});

add_task(async function test_recordEnabledWidgets_trainhop() {
  info(
    "recordEnabledWidgets should count a widget enabled via trainhopConfig when the system pref is off"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.enabled": true,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": false,
          trainhopConfig: { widgets: { listsEnabled: true } },
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists"],
    "widgetsEnabledList should include widgets gated on by trainhopConfig"
  );
});

add_task(async function test_recordEnabledWidgets_container_disabled() {
  info(
    "recordEnabledWidgets should return an empty list when the widgets container is off"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.enabled": false,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": true,
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    [],
    "widgetsEnabledList should be empty when widgets.enabled is false"
  );
});

add_task(async function test_recordEnabledWidgets_none_enabled() {
  info(
    "recordEnabledWidgets should set an empty list when no widgets are enabled"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.lists.enabled": false,
          "widgets.focusTimer.enabled": false,
          "widgets.weather.enabled": false,
          "widgets.sportsWidget.enabled": false,
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    [],
    "widgetsEnabledList should be empty when no widgets are enabled"
  );
});

add_task(async function test_recordPageLayoutVariant_default() {
  info("recordPageLayoutVariant should report the default layout name");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = { getState: () => ({ Prefs: { values: {} } }) };

  instance.recordPageLayoutVariant();

  Assert.equal(
    Glean.newtab.pageLayoutVariant.testGetValue(),
    "nova-full-width",
    "pageLayoutVariant should be the default name rather than an empty string"
  );
});

add_task(async function test_recordPageLayoutVariant_pref() {
  info("recordPageLayoutVariant should report the variant set by pref");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: { values: { "pageLayouts.variant": "side-by-side-content-lead" } },
    }),
  };

  instance.recordPageLayoutVariant();

  Assert.equal(
    Glean.newtab.pageLayoutVariant.testGetValue(),
    "side-by-side-content-lead",
    "pageLayoutVariant should be the pref value"
  );
});

add_task(async function test_recordPageLayoutVariant_trainhop() {
  info(
    "recordPageLayoutVariant should report a variant that only trainhopConfig carries"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "pageLayouts.variant": "nova-full-width",
          trainhopConfig: {
            pageLayouts: { variant: "side-by-side-widgets-lead" },
          },
        },
      },
    }),
  };

  instance.recordPageLayoutVariant();

  Assert.equal(
    Glean.newtab.pageLayoutVariant.testGetValue(),
    "side-by-side-widgets-lead",
    "pageLayoutVariant should follow trainhopConfig over the pref"
  );
});

add_task(async function test_onAction_NEW_TAB_SCROLL_records_scroll_metric() {
  Services.fog.testResetFOG();
  let instance = new TelemetryFeed();
  instance.onAction({ type: actionTypes.NEW_TAB_SCROLL });
  Assert.equal(
    Glean.newtab.scroll.testGetValue(),
    true,
    "newtab.scroll should be true after NEW_TAB_SCROLL action"
  );
});

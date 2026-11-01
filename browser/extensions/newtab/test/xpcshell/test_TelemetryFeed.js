/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AboutNewTabParent: "resource:///actors/AboutNewTabParent.sys.mjs",
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  GleanSessionType: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  laplaceNoise: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  NewTabContentPing: "resource://newtab/lib/NewTabContentPing.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  TelemetryFeed: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  WIDGET_REGISTRY: "resource://newtab/common/WidgetsRegistry.mjs",
});

const FAKE_UUID = "{foo-123-foo}";
const PREF_IMPRESSION_ID = "browser.newtabpage.activity-stream.impressionId";
const PREF_TELEMETRY = "browser.newtabpage.activity-stream.telemetry";
const PREF_NEWTAB_PING_ENABLED = "browser.newtabpage.ping.enabled";
const PREF_PRIVATE_PING_ENABLED =
  "browser.newtabpage.activity-stream.telemetry.privatePing.enabled";
const PREF_PRIVATE_PING_DWELL_TIME_ENABLED =
  "browser.newtabpage.activity-stream.telemetry.privatePing.dwellTime.enabled";
const PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE =
  "browser.newtabpage.activity-stream.telemetry.privatePing.dwellTime.noiseScale";
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

add_task(async function test_wallpaper_saved_click_event() {
  info(
    "TelemetryFeed should record a wallpaper_saved_click event that can tell " +
      "saved images apart, which wallpaper_click cannot"
  );
  Services.fog.testResetFOG();

  const PORT_ID = "port-saved-click";
  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.handleWallpaperUserEvent({
    type: actionTypes.WALLPAPER_SAVED_APPLIED,
    meta: { fromTarget: PORT_ID },
    data: { saved_wallpaper_count: 4 },
  });

  const events = Glean.newtab.wallpaperSavedClick.testGetValue();
  Assert.equal(events.length, 1, "One wallpaper_saved_click event");
  Assert.deepEqual(events[0].extra, {
    saved_wallpaper_count: "4",
    newtab_visit_id: session.session_id,
  });

  Assert.equal(
    Glean.newtab.wallpaperClick.testGetValue(),
    null,
    "It is its own event, so nothing was added to one that cannot train-hop"
  );
});

add_task(async function test_wallpaper_saved_click_not_recorded_elsewhere() {
  info(
    "TelemetryFeed should not record wallpaper_saved_click for a wallpaper " +
      "picked outside Your images"
  );
  Services.fog.testResetFOG();

  const PORT_ID = "port-builtin-click";
  let instance = new TelemetryFeed();
  instance.addSession(PORT_ID);

  instance.handleWallpaperUserEvent({
    type: actionTypes.WALLPAPER_CLICK,
    meta: { fromTarget: PORT_ID },
    data: {
      selected_wallpaper: "celestial-1",
      had_previous_wallpaper: false,
      had_uploaded_previously: false,
    },
  });

  Assert.equal(
    Glean.newtab.wallpaperSavedClick.testGetValue(),
    null,
    "Nothing recorded for a shipped wallpaper"
  );
  Assert.equal(
    Glean.newtab.wallpaperClick.testGetValue().length,
    1,
    "The existing event still fires"
  );
});

add_task(async function test_wallpaper_saved_remove_event() {
  info("TelemetryFeed should record removing a saved wallpaper");
  Services.fog.testResetFOG();

  const PORT_ID = "port-saved-remove";
  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.onAction({
    type: actionTypes.WALLPAPER_SAVED_REMOVED,
    meta: { fromTarget: PORT_ID },
    data: { saved_wallpaper_count: 2, was_applied: true },
  });

  const events = Glean.newtab.wallpaperSavedRemove.testGetValue();
  Assert.equal(events.length, 1, "One wallpaper_saved_remove event");
  Assert.deepEqual(
    events[0].extra,
    {
      saved_wallpaper_count: "2",
      was_applied: "true",
      newtab_visit_id: session.session_id,
    },
    "The counts are recorded and the filename is not"
  );
});

add_task(async function test_no_saved_wallpaper_events_without_the_parent() {
  info(
    "Asking to apply or remove records nothing: only the parent knows whether " +
      "it happened"
  );
  Services.fog.testResetFOG();

  const PORT_ID = "port-requested-only";
  let instance = new TelemetryFeed();
  instance.addSession(PORT_ID);

  instance.handleWallpaperUserEvent({
    type: actionTypes.WALLPAPER_CLICK,
    meta: { fromTarget: PORT_ID },
    data: {
      selected_wallpaper: "custom",
      had_previous_wallpaper: true,
      had_uploaded_previously: true,
    },
  });
  instance.onAction({
    type: actionTypes.WALLPAPER_REMOVE_UPLOAD,
    meta: { fromTarget: PORT_ID },
    data: { filename: "v1-custom-dark-center-abc" },
  });

  Assert.equal(
    Glean.newtab.wallpaperSavedClick.testGetValue(),
    null,
    "Nothing is recorded for a request to apply"
  );
  Assert.equal(
    Glean.newtab.wallpaperSavedRemove.testGetValue(),
    null,
    "Nor for a request to remove"
  );
  Assert.equal(
    Glean.newtab.wallpaperClick.testGetValue().length,
    1,
    "The existing click event is untouched"
  );
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

add_task(async function test_customize_panel_open_event() {
  info("handleUserEvent records newtab.customize_panel_open with the visit id");
  Services.fog.testResetFOG();
  const PORT_ID = "port123";
  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.handleUserEvent({
    meta: { fromTarget: PORT_ID },
    data: { event: "SHOW_PERSONALIZE" },
  });

  let events = Glean.newtab.customizePanelOpen.testGetValue();
  Assert.equal(events.length, 1, "One customize_panel_open event");
  Assert.deepEqual(events[0].extra, { newtab_visit_id: session.session_id });
});

add_task(async function test_customize_panel_subpanel_open_event() {
  info(
    "handleUserEvent records newtab.customize_panel_subpanel_open with the panel"
  );
  Services.fog.testResetFOG();
  const PORT_ID = "port123";
  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.handleUserEvent({
    meta: { fromTarget: PORT_ID },
    data: { event: "SHOW_PERSONALIZE_SUBPANEL", source: "themes_management" },
  });

  let events = Glean.newtab.customizePanelSubpanelOpen.testGetValue();
  Assert.equal(events.length, 1, "One customize_panel_subpanel_open event");
  Assert.deepEqual(events[0].extra, {
    newtab_visit_id: session.session_id,
    panel: "themes_management",
  });
});

add_task(async function test_explore_more_themes_click_event() {
  info("handleUserEvent records newtab.appearance_explore_more_themes_click");
  Services.fog.testResetFOG();
  const PORT_ID = "port123";
  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.handleUserEvent({
    meta: { fromTarget: PORT_ID },
    data: { event: "EXPLORE_MORE_THEMES_CLICK" },
  });

  let events = Glean.newtab.appearanceExploreMoreThemesClick.testGetValue();
  Assert.equal(events.length, 1, "One explore_more_themes_click event");
  Assert.deepEqual(events[0].extra, { newtab_visit_id: session.session_id });
});

add_task(async function test_customize_panel_events_need_a_session() {
  info("None of the customize panel events record without a session");
  Services.fog.testResetFOG();
  let instance = new TelemetryFeed();
  const meta = { fromTarget: "port-with-no-session" };

  instance.handleUserEvent({ meta, data: { event: "SHOW_PERSONALIZE" } });
  instance.handleUserEvent({
    meta,
    data: { event: "SHOW_PERSONALIZE_SUBPANEL", source: "themes_management" },
  });
  instance.handleUserEvent({
    meta,
    data: { event: "EXPLORE_MORE_THEMES_CLICK" },
  });

  Assert.equal(Glean.newtab.customizePanelOpen.testGetValue(), null);
  Assert.equal(Glean.newtab.customizePanelSubpanelOpen.testGetValue(), null);
  Assert.equal(
    Glean.newtab.appearanceExploreMoreThemesClick.testGetValue(),
    null
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

add_task(async function test_sendPageTakeoverData_no_custom_url() {
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
  async function test_handleDiscoveryStreamImpressionStats_is_ad_eligible_position() {
    info(
      "TelemetryFeed.handleDiscoveryStreamImpressionStats should record " +
        "is_ad_eligible_position on the pocket.impression event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamImpressionStats("port123", {
      tiles: [
        { id: 1, pos: 0, is_ad_eligible_position: true },
        // selectLayoutRender only flags eligible positions, so an unflagged
        // card is what a non-eligible one actually looks like here.
        { id: 2, pos: 1 },
      ],
    });

    let impressions = Glean.pocket.impression.testGetValue();
    Assert.equal(impressions.length, 2, "Should have recorded 2 impressions");
    Assert.equal(
      impressions[0].extra.is_ad_eligible_position,
      String(true),
      "An ad-eligible position should be flagged"
    );
    Assert.equal(
      impressions[1].extra.is_ad_eligible_position,
      undefined,
      "An unflagged card should omit the key"
    );

    sandbox.restore();
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
      advertiser_name: "adnoid ads",
    };
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    await instance.handleTopSitesSponsoredImpressionStats({ data });
    let impressions = Glean.topsites.impression.testGetValue();
    Assert.equal(impressions.length, 1, "Should have recorded 1 impression");

    Assert.deepEqual(impressions[0].extra, {
      advertiser_name: "adnoid ads",
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
      advertiser_name: "test advertiser",
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
        advertiser_name: "adnoid ads",
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
        advertiser_name: "adnoid ads",
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
      smart_scores: { moo: 1 },
      smart_weights: { moo: 0 },
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
  async function test_handleTopSitesOrganicImpressionStats_is_ad_eligible_position() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should report " +
        "an organic tile sitting in an ad-eligible position"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesOrganicImpressionStats({
      data: {
        type: "impression",
        source: "newtab",
        position: 0,
        is_ad_eligible_position: true,
      },
    });
    // TopSitesFeed only flags eligible positions, so an unflagged tile is what
    // a non-eligible one actually looks like here.
    await instance.handleTopSitesOrganicImpressionStats({
      data: { type: "impression", source: "newtab", position: 1 },
    });

    let impressions = Glean.topsites.impression.testGetValue();
    Assert.equal(impressions.length, 2, "Recorded 2 impressions");
    Assert.equal(
      impressions[0].extra.is_ad_eligible_position,
      String(true),
      "An ad-eligible position should be flagged"
    );
    Assert.equal(
      impressions[1].extra.is_ad_eligible_position,
      undefined,
      "An unflagged tile should omit the key"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_is_ad_eligible_position() {
    info(
      "TelemetryFeed.handleTopSitesSponsoredImpressionStats should report " +
        "is_ad_eligible_position on the topsites.impression event"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesSponsoredImpressionStats({
      data: {
        type: "impression",
        tile_id: 42,
        source: "newtab",
        position: 1,
        advertiser_name: "adnoid ads",
        is_ad_eligible_position: true,
      },
    });

    let impressions = Glean.topsites.impression.testGetValue();
    Assert.equal(impressions.length, 1, "Recorded 1 impression");
    Assert.equal(
      impressions[0].extra.is_ad_eligible_position,
      String(true),
      "A sponsored tile in an ad-eligible position should be flagged"
    );

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
      smart_scores: { moo: 1 },
      smart_weights: { moo: 0 },
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

add_task(async function test_handleInlineSelectionUserEvent_topic_position() {
  info(
    "TelemetryFeed.handleInlineSelectionUserEvent should record the " +
      "topic_position that InterestPicker sends"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  sandbox.stub(instance.sessions, "get").returns({ session_id: "decafc0ffee" });

  instance.handleInlineSelectionUserEvent({
    type: "INLINE_SELECTION_CLICK",
    data: {
      topic: "sports",
      section_position: 2,
      topic_position: 3,
      is_followed: true,
    },
  });

  const [event] = Glean.newtab.inlineSelectionClick.testGetValue();
  Assert.equal(event.extra.topic_position, "3", "topic_position is recorded");

  sandbox.restore();
});

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
        variant_id: 5050,
        source_section_id: "sourced-section",
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
          variant_id: 5050,
          source_section_id: "sourced-section",
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
      "TelemetryFeed.handleDiscoveryStreamUserEvent never sends a tile_id to " +
        "either the newtab ping or the newtab-content ping for a sponsored " +
        "top stories click"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
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
      "A tile_id should not have been included with the newtab ping."
    );

    Assert.ok(
      instance.newtabContentPing.recordEvent.calledOnce,
      "recordEvent was called"
    );
    const newtabContentClickArgs =
      instance.newtabContentPing.recordEvent.getCall(0).args;
    Assert.equal(
      newtabContentClickArgs[0],
      "click",
      "Called with the click event type"
    );
    Assert.equal(
      newtabContentClickArgs[1].tile_id,
      undefined,
      "tile_id was not included."
    );

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

/**
 * Every sponsored top sites code path that could put a tile_id on the newtab
 * ping. These topsites metrics no longer declare a tile_id extra key, so none
 * of these paths should ever record one.
 */
const SPONSORED_TOPSITES_TILE_ID_PATHS = [
  {
    name: "topsites.impression",
    metric: () => Glean.topsites.impression,
    drive: instance =>
      instance.handleTopSitesSponsoredImpressionStats({
        data: {
          type: "impression",
          tile_id: 4567,
          source: "newtab",
          position: 1,
          advertiser_name: "adnoid ads",
        },
      }),
  },
  {
    name: "topsites.click",
    metric: () => Glean.topsites.click,
    drive: instance =>
      instance.handleTopSitesSponsoredImpressionStats({
        data: {
          type: "click",
          tile_id: 4567,
          source: "newtab",
          position: 0,
          advertiser_name: "adnoid ads",
        },
      }),
  },
  {
    name: "topsites.dismiss",
    metric: () => Glean.topsites.dismiss,
    drive: instance =>
      instance.handleBlockUrl({
        source: "TOP_SITES",
        data: [
          {
            is_pocket_card: false,
            position: 42,
            advertiser_name: "mozilla",
            tile_id: 4567,
            isSponsoredTopSite: 1,
          },
        ],
      }),
  },
  {
    name: "topsites.showPrivacyClick",
    metric: () => Glean.topsites.showPrivacyClick,
    drive: instance =>
      instance.handleAboutSponsoredTopSites({
        data: {
          position: 42,
          advertiser_name: "mozilla",
          tile_id: 4567,
        },
      }),
  },
];

add_task(async function test_sponsored_topsites_tile_id_never_recorded() {
  info(
    "No sponsored top sites event on the newtab ping should ever carry a " +
      "tile_id, since the topsites metrics no longer declare it"
  );

  for (const { name, metric, drive } of SPONSORED_TOPSITES_TILE_ID_PATHS) {
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    sandbox
      .stub(instance.sessions, "get")
      .returns({ session_id: "decafc0ffee" });

    await drive(instance);

    let events = metric().testGetValue();
    Assert.equal(events.length, 1, `Recorded 1 ${name} event`);
    Assert.ok(
      !("tile_id" in events[0].extra),
      `${name} should not carry a tile_id`
    );

    sandbox.restore();
  }
});

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
    variant_id: 0,
    source_section_id: "src-section",
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

add_task(function test_randomizeOrganicContentEvent_tracks_layout_name() {
  info(
    "randomizeOrganicContentEvent should set layout_name to the swapped " +
      "section's layout"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  const item = {
    corpus_item_id: "orig",
    topic: "a",
    is_sponsored: false,
    section: "orig-section",
    section_position: 0,
    layout_name: "orig-layout",
    variant_id: 0,
    source_section_id: "orig-source",
  };
  const randomItem = {
    corpus_item_id: "swapped",
    topic: "b",
    is_sponsored: false,
    section: "swapped-section",
    variant_id: 5050,
    source_section_id: "swapped-source",
  };
  sandbox.stub(instance, "getRecommendationCount").returns(10);
  sandbox.stub(instance, "getAllRecommendations").returns([randomItem]);
  sandbox
    .stub(instance, "getAllSections")
    .returns([
      { sectionKey: "swapped-section", layout: { name: "swapped-layout" } },
    ]);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  sandbox.stub(NewTabContentPing, "decideWithProbability").returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(0);

  const result = instance.randomizeOrganicContentEvent(item);

  Assert.equal(result.section, "swapped-section", "section is swapped");
  Assert.equal(
    result.layout_name,
    "swapped-layout",
    "layout_name tracks the swapped section"
  );
  Assert.equal(
    result.variant_id,
    5050,
    "variant_id tracks the swapped section"
  );
  Assert.equal(
    result.source_section_id,
    "swapped-source",
    "source_section_id tracks the swapped section"
  );

  sandbox.restore();
});

add_task(async function test_randomizeOrganicContentEvent_section_position() {
  info(
    "randomizeOrganicContentEvent should only swap in items from rendered " +
      "sections and report the rendered position of the swapped section"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  const PORT_ID = "port123";
  const session = instance.addSession(PORT_ID);

  await instance.onAction({
    type: actionTypes.CARD_SECTIONS_ORDER,
    meta: { fromTarget: PORT_ID },
    data: { sections: ["orig-section", "swapped-section"] },
  });

  const item = {
    corpus_item_id: "orig",
    section: "orig-section",
    section_position: 0,
  };
  sandbox.stub(instance, "getRecommendationCount").returns(10);
  sandbox
    .stub(instance, "getAllRecommendations")
    .returns([
      { corpus_item_id: "hidden", section: "hidden" },
      { corpus_item_id: "unsectioned" },
      { corpus_item_id: "swapped", section: "swapped-section" },
    ]);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  sandbox.stub(NewTabContentPing, "decideWithProbability").returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(0);

  const result = instance.randomizeOrganicContentEvent(item, session);

  Assert.equal(
    result.corpus_item_id,
    "swapped",
    "Only items from rendered sections are candidates"
  );
  Assert.equal(
    result.section_position,
    1,
    "section_position is the swapped section's rendered position"
  );

  sandbox.restore();
});

add_task(function test_randomizeOrganicContentEvent_variant_id_popular_today() {
  info(
    "randomizeOrganicContentEvent should keep the original variant_id when the " +
      "section stays Popular Today, since the swapped item's variant would be " +
      "an impossible section/variant pair"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  const item = {
    corpus_item_id: "orig",
    topic: "a",
    is_sponsored: false,
    section: "top_stories_section",
    variant_id: 0,
    source_section_id: "orig-origin",
  };
  const randomItem = {
    corpus_item_id: "swapped",
    topic: "b",
    is_sponsored: false,
    section: "sports",
    variant_id: 5050,
  };
  sandbox.stub(instance, "getRecommendationCount").returns(10);
  sandbox.stub(instance, "getAllRecommendations").returns([randomItem]);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  sandbox.stub(NewTabContentPing, "decideWithProbability").returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(0);

  const result = instance.randomizeOrganicContentEvent(item);

  Assert.equal(
    result.section,
    "top_stories_section",
    "section stays Popular Today"
  );
  Assert.equal(
    result.variant_id,
    0,
    "variant_id stays the Popular Today variant, not the swapped item's"
  );
  Assert.equal(result.corpus_item_id, "swapped", "content is still swapped");

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

add_task(
  async function test_initializePrivacySession_defaults_to_private_sponsored_scoring() {
    info(
      "initializeGleanSession should default to PrivateGleanSession when sponsored top site scoring is enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            adsBackendConfig: { sponsored_top_site_scoring: true },
            trainhopConfig: { newtabPrivatePing: { clickOnly: true } },
          },
        },
      }),
    };

    instance.initializeGleanSession();

    Assert.equal(
      instance.gleanSessionType,
      "private",
      "Should be PrivateGleanSession when sponsored top site scoring is enabled"
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
      advertiser_name: "test advertiser",
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
      advertiser_name: "test advertiser",
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
      advertiser_name: "test advertiser",
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
  async function test_handleTopSitesSponsoredImpressionStats_sponsored_scoring_content_ping() {
    info(
      "Sponsored topsites events with score-based selection enabled should go to the newtab-content ping"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    instance.gleanSessionType = "private";
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            adsBackendConfig: { sponsored_top_site_scoring: true },
            trainhopConfig: { newtabPrivatePing: { clickOnly: true } },
          },
        },
      }),
    };

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    for (const type of ["impression", "click"]) {
      await instance.handleTopSitesSponsoredImpressionStats({
        data: {
          type,
          tile_id: 42,
          source: "newtab",
          position: 1,
          advertiser_name: "test advertiser",
          visible_topsites: 8,
        },
      });
    }

    Assert.deepEqual(
      recordEventStub.getCalls().map(call => call.args[0]),
      ["topSitesImpression", "topSitesClick"],
      "The impression and click should go to the newtab-content ping"
    );
    Assert.equal(
      Glean.topsites.impression.testGetValue(),
      null,
      "No topsites.impression event should be recorded in the newtab ping"
    );
    Assert.equal(
      Glean.topsites.click.testGetValue(),
      null,
      "No topsites.click event should be recorded in the newtab ping"
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

add_task(async function test_configureContentPing_surfaceId_gates_utcOffset() {
  info(
    "configureContentPing should only send surfaceId and utcOffset when a " +
      "surfaceId is known"
  );

  const SURFACE_ID_PREF =
    "browser.newtabpage.activity-stream.telemetry.surfaceId";
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          trainhopConfig: {},
          inferredPersonalizationConfig: { normalized_time_zone_offset: true },
        },
      },
      DiscoveryStream: { sectionPersonalization: {} },
      InferredPersonalization: { initialized: false },
    }),
  };
  let scheduleStub = sandbox.stub(
    instance.newtabContentPing,
    "scheduleSubmission"
  );
  sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
  sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
  sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

  Services.prefs.clearUserPref(SURFACE_ID_PREF);
  await instance.configureContentPing();
  let [metrics] = scheduleStub.lastCall.args;
  Assert.ok(!("surfaceId" in metrics), "No surfaceId without the pref");
  Assert.ok(!("utcOffset" in metrics), "No utcOffset without a surfaceId");

  Services.prefs.setStringPref(SURFACE_ID_PREF, "NEW_TAB_EN_US");
  await instance.configureContentPing();
  [metrics] = scheduleStub.lastCall.args;
  Assert.equal(metrics.surfaceId, "NEW_TAB_EN_US", "surfaceId is sent");
  Assert.equal(typeof metrics.utcOffset, "number", "utcOffset is sent");

  Services.prefs.clearUserPref(SURFACE_ID_PREF);
  sandbox.restore();
});

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

add_task(async function test_recordEnabledWidgets_extension_widgets() {
  info(
    "recordEnabledWidgets should list an extension widget only while ExtensionWidgets has its extension"
  );
  Services.fog.testResetFOG();

  const extensionWidget = {
    id: "extensionWidget",
    telemetryName: "extension_widget",
    enabledPref: "widgets.extensionWidget.enabled",
    systemEnabledPref: "widgets.system.extensionWidget.enabled",
    extensionID: "fixture-widget@mozilla.org",
  };
  WIDGET_REGISTRY.push(extensionWidget);

  const state = {
    Prefs: {
      values: {
        "widgets.enabled": true,
        "widgets.lists.enabled": true,
        "widgets.system.lists.enabled": true,
        "widgets.extensionWidget.enabled": true,
        "widgets.system.extensionWidget.enabled": true,
      },
    },
    ExtensionWidgets: {},
  };

  let instance = new TelemetryFeed();
  instance.store = { getState: () => state };

  try {
    instance.recordEnabledWidgets();
    Assert.deepEqual(
      Glean.newtab.widgetsEnabledList.testGetValue(),
      ["lists"],
      "extension widget without its extension left out"
    );

    state.ExtensionWidgets = {
      [extensionWidget.extensionID]: { src: "moz-extension://uuid/w.html" },
    };
    await instance.onAction({
      type: actionTypes.WIDGETS_EXTENSIONS_UPDATE,
      data: state.ExtensionWidgets,
    });

    Assert.deepEqual(
      Glean.newtab.widgetsEnabledList.testGetValue(),
      ["lists", "extension_widget"],
      "extension widget listed"
    );
  } finally {
    WIDGET_REGISTRY.splice(WIDGET_REGISTRY.indexOf(extensionWidget), 1);
  }
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
          region: "US",
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

add_task(async function test_recordEnabledWidgets_region_resolves() {
  info(
    "recordEnabledWidgets should re-record when the region resolves and reveals a trainhop widget"
  );
  Services.fog.testResetFOG();

  const values = {
    "widgets.enabled": true,
    "widgets.lists.enabled": true,
    "widgets.system.lists.enabled": false,
    trainhopConfig: { widgets: { listsEnabled: true } },
  };

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({ Prefs: { values } }),
  };

  instance.recordEnabledWidgets();
  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    [],
    "a trainhop-revealed widget is not listed while the region is unresolved"
  );

  values.region = "US";
  instance.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "region", value: "US" },
  });

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists"],
    "lists is listed once the region resolves"
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

add_task(async function test_handleSpacesUserEvent() {
  info("handleSpacesUserEvent should record a spaces_switch event");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  const session = instance.addSession("foo", "about:newtab");

  instance.handleSpacesUserEvent({
    data: { space: "widgets", previous_space: "stories", method: "tab" },
    meta: { fromTarget: "foo" },
  });

  const events = Glean.newtab.spacesSwitch.testGetValue();
  Assert.equal(events.length, 1, "one spaces_switch event should be recorded");
  Assert.deepEqual(
    events[0].extra,
    {
      newtab_visit_id: session.session_id,
      space: "widgets",
      previous_space: "stories",
      method: "tab",
    },
    "spaces_switch should carry the visit id and the switch it describes"
  );
});

add_task(async function test_recordHomeWidgets() {
  info(
    "recordHomeWidgets should list the widgets Home shows, only where users pin widgets to Home"
  );
  Services.fog.testResetFOG();

  const spaces = {
    order: ["home", "widgets"],
    default: "home",
    spaces: {
      home: { label: "Home", widgets: ["lists"] },
      widgets: { label: "Widgets" },
    },
  };
  const values = {
    "pageLayouts.variant": "spaces-thematic-v1",
    trainhopConfig: { spaces },
    "widgets.system.enabled": true,
    "widgets.enabled": true,
    "widgets.home": '["focusTimer","lists","clocks","stocks"]',
  };
  for (const id of ["lists", "focusTimer", "clocks", "stocks"]) {
    values[`widgets.system.${id}.enabled`] = true;
    values[`widgets.${id}.enabled`] = true;
  }
  values["widgets.focusTimer.enabled"] = false;

  let instance = new TelemetryFeed();
  instance.store = { getState: () => ({ Prefs: { values } }) };

  instance.recordHomeWidgets();
  Assert.equal(
    Glean.newtab.widgetsHomeList.testGetValue(),
    null,
    "widgetsHomeList is not set without a widget library"
  );

  values.trainhopConfig = { spaces: { ...spaces, widgetLibrary: "widgets" } };
  instance.recordEnabledWidgets();
  Assert.deepEqual(
    Glean.newtab.widgetsHomeList.testGetValue().sort(),
    ["clocks", "lists", "stocks"],
    "widgetsHomeList lists every pinned widget that is turned on, with no limit"
  );

  values["widgets.home"] = "[]";
  instance.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "widgets.home", value: "[]" },
  });
  Assert.deepEqual(
    Glean.newtab.widgetsHomeList.testGetValue(),
    [],
    "widgetsHomeList is re-recorded as empty when Home is emptied"
  );
});

add_task(async function test_widget_events_carry_widget_location() {
  info(
    "widgets_impression and widgets_user_event should pass widget_location through only when it is set"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  const session = instance.addSession("foo", "about:newtab");
  const meta = { fromTarget: "foo" };

  instance.handleUnifiedWidgetImpression({
    data: {
      widget_name: "lists",
      widget_size: "medium",
      widget_location: "home_space",
    },
    meta,
  });
  instance.handleUnifiedWidgetUserEvent({
    data: {
      widget_name: "lists",
      widget_source: "widget",
      user_action: "list_create",
      widget_location: "sidebar",
    },
    meta,
  });
  instance.handleUnifiedWidgetImpression({
    data: { widget_name: "clocks", widget_size: "medium" },
    meta,
  });

  const impressions = Glean.newtab.widgetsImpression.testGetValue();
  Assert.deepEqual(
    impressions.map(e => e.extra),
    [
      {
        newtab_visit_id: session.session_id,
        widget_name: "lists",
        widget_size: "medium",
        widget_location: "home_space",
      },
      {
        newtab_visit_id: session.session_id,
        widget_name: "clocks",
        widget_size: "medium",
      },
    ],
    "widget_location is recorded when given and left out otherwise"
  );
  Assert.equal(
    Glean.newtab.widgetsUserEvent.testGetValue()[0].extra.widget_location,
    "sidebar",
    "widgets_user_event carries widget_location"
  );
});

add_task(async function test_handleSpacesUserEvent_no_session() {
  info("handleSpacesUserEvent should record nothing without a session");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();

  instance.handleSpacesUserEvent({
    data: { space: "widgets", previous_space: "stories", method: "tab" },
    meta: { fromTarget: "missing" },
  });

  Assert.equal(
    Glean.newtab.spacesSwitch.testGetValue(),
    null,
    "no event should be recorded for an unknown port"
  );
});

add_task(async function test_handleUnifiedWidget_widget_variant() {
  info("The four unified widget handlers should pass widget_variant through");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  const session = instance.addSession("foo", "about:newtab");
  const meta = { fromTarget: "foo" };
  const base = { widget_name: "privacy", widget_size: "medium" };
  const withVariant = { ...base, widget_variant: "no_trackers_blocked" };

  instance.handleUnifiedWidgetImpression({ data: withVariant, meta });
  instance.handleUnifiedWidgetUserEvent({
    data: { ...withVariant, user_action: "learn_more" },
    meta,
  });
  instance.handleUnifiedWidgetEnabled({
    data: { ...withVariant, enabled: false },
    meta,
  });
  instance.handleUnifiedWidgetError({
    data: { ...withVariant, error_type: "load_error" },
    meta,
  });
  instance.handleUnifiedWidgetImpression({
    data: { widget_name: "lists", widget_size: "large" },
    meta,
  });

  for (const metric of [
    "widgetsImpression",
    "widgetsUserEvent",
    "widgetsEnabled",
    "widgetsError",
  ]) {
    const [event] = Glean.newtab[metric].testGetValue();
    Assert.equal(
      event.extra.widget_variant,
      "no_trackers_blocked",
      `${metric} should carry widget_variant`
    );
    Assert.equal(event.extra.newtab_visit_id, session.session_id);
  }
  Assert.deepEqual(
    Glean.newtab.widgetsImpression.testGetValue()[1].extra,
    {
      newtab_visit_id: session.session_id,
      widget_name: "lists",
      widget_size: "large",
    },
    "a widget that sends no variant should record no widget_variant"
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

add_task(async function test_onAction_NEW_TAB_SCROLL_tracks_max_threshold() {
  info(
    "TelemetryFeed should track the deepest scroll threshold passed in a session"
  );
  let instance = new TelemetryFeed();
  instance.addSession("foo");

  const scrollAction = threshold => ({
    type: actionTypes.NEW_TAB_SCROLL,
    data: { threshold },
    meta: { fromTarget: "foo" },
  });

  instance.onAction(scrollAction(50));
  Assert.equal(instance.sessions.get("foo").max_scroll_threshold, 50);

  instance.onAction(scrollAction(250));
  Assert.equal(instance.sessions.get("foo").max_scroll_threshold, 250);

  info("A shallower threshold should not lower the recorded one");
  instance.onAction(scrollAction(100));
  Assert.equal(instance.sessions.get("foo").max_scroll_threshold, 250);
});

add_task(async function test_onAction_NEW_TAB_SCROLL_no_throw_on_bad_session() {
  info("TelemetryFeed should ignore a scroll for an unknown session");
  let instance = new TelemetryFeed();
  try {
    instance.onAction({
      type: actionTypes.NEW_TAB_SCROLL,
      data: { threshold: 50 },
      meta: { fromTarget: "doesn't exist" },
    });
    Assert.ok(true, "Did not throw.");
  } catch (e) {
    Assert.ok(false, "Should not have thrown.");
  }
});

add_task(async function test_endSession_records_scroll_metrics() {
  info(
    "TelemetryFeed.endSession should record a scroll metric for every " +
      "threshold, true only for those the session passed"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");

  const session = instance.addSession("foo");
  session.perf.visibility_event_rcvd_ts = 1000;
  instance.onAction({
    type: actionTypes.NEW_TAB_SCROLL,
    data: { threshold: 100 },
    meta: { fromTarget: "foo" },
  });

  // Ping lifetime metrics are cleared on submission, so assert before it.
  let pingSubmitted = new Promise(resolve => {
    GleanPings.newtab.testBeforeNextSubmit(() => {
      Assert.equal(Glean.newtab.scroll.testGetValue(), true);
      Assert.equal(Glean.newtab.scroll100.testGetValue(), true);
      Assert.equal(Glean.newtab.scroll250.testGetValue(), false);
      resolve();
    });
  });

  await instance.endSession("foo");
  await pingSubmitted;

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_endSession_records_false_scroll_metrics() {
  info(
    "TelemetryFeed.endSession should record false for every scroll metric " +
      "when the session was never scrolled"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");

  const session = instance.addSession("foo");
  session.perf.visibility_event_rcvd_ts = 1000;

  let pingSubmitted = new Promise(resolve => {
    GleanPings.newtab.testBeforeNextSubmit(() => {
      Assert.equal(Glean.newtab.scroll.testGetValue(), false);
      Assert.equal(Glean.newtab.scroll100.testGetValue(), false);
      Assert.equal(Glean.newtab.scroll250.testGetValue(), false);
      resolve();
    });
  });

  await instance.endSession("foo");
  await pingSubmitted;

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

const USER_INTERACTION_ACTIVE = "user-interaction-active-non-synthesized";
const USER_INTERACTION_INACTIVE = "user-interaction-inactive-non-synthesized";
// EventStateManager's default notification cadence. The feed takes its cutoff
// from the notifications themselves, so this only paces the fake ones.
const USER_INTERACTION_INTERVAL_MS = 5000;
// testGetValue().sum is in nanoseconds, whatever the time_unit.
const NS_PER_MS = 1000000;

/**
 * Build a TelemetryFeed with the clock and the foreground check under the
 * test's control, and telemetry on so the newtab ping is submitted.
 *
 * @param {object} sandbox a sinon sandbox
 * @returns {object} the instance and a setTime(ms) helper
 */
function setupDwellFeed(sandbox) {
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");
  sandbox.stub(instance, "isDwellTargetInForeground").returns(true);
  let clock = sandbox.stub(instance, "now").returns(0);

  return { instance, setTime: ms => clock.returns(ms) };
}

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
 * Undo setupDwellFeed.
 *
 * @param {object} sandbox the sinon sandbox it was given
 */
function teardownDwellTest(sandbox) {
  sandbox.restore();
  Services.prefs.clearUserPref(PREF_TELEMETRY);
  // Owned here rather than by each test, so setupContentDwell setting them
  // cannot leak into whatever runs next.
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_DWELL_TIME_ENABLED);
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE);
  Services.fog.testResetFOG();
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

/**
 * A URI the way the dwell code reads one off a <browser>.
 *
 * @param {string} spec
 * @returns {object}
 */
function fakeURI(spec) {
  return {
    spec,
    specIgnoringRef: spec.split("#")[0],
    scheme: spec.split(":")[0],
  };
}

// Window globals are identified by a process-wide id in the real thing, so
// every document a test creates gets a distinct one.
let gNextInnerWindowId = 1;

/**
 * A stand-in for a <browser> in a tab, with the surface the dwell code
 * touches. Like the real thing, it tells its progress listeners about each
 * top-level location change as it happens, and its tab fires TabClose.
 *
 * - goTo() loads a new document at a new address.
 * - goToInDocument() is same-document navigation, which keeps the window
 *   global it already had.
 * - goToInFrame() is a frame inside the page loading something.
 * - replaceDocument() loads a new document at the same address, the way a
 *   reload or a page that replaces itself does.
 * - failLoad() shows an error page, which keeps the failed address.
 * - closeTab() closes the tab.
 * - closeWindow() closes the tab's window, which fires no TabClose.
 *
 * @param {string} [spec] where the tab starts out
 * @param {object} [options]
 * @param {boolean} [options.listeners] false for a browser whose progress
 *  listeners never fire, so only the activity notifications see changes
 * @returns {object} the fake browser
 */
function makeFakeBrowser(spec = "about:newtab", { listeners = true } = {}) {
  const progressListeners = new Set();
  const tabListeners = new Set();
  const windowListeners = new Set();
  const tab = {
    addEventListener(type, listener) {
      if (type === "TabClose") {
        tabListeners.add(listener);
      }
    },
    removeEventListener(type, listener) {
      if (type === "TabClose") {
        tabListeners.delete(listener);
      }
    },
  };
  const browser = {
    permanentKey: {},
    isConnected: true,
    progressListeners,
    tabListeners,
    windowListeners,
    browsingContext: {
      currentWindowGlobal: {
        documentURI: fakeURI(spec),
        innerWindowId: gNextInnerWindowId++,
      },
    },
    documentGlobal: {
      gBrowser: { getTabForBrowser: () => tab },
      addEventListener(type, listener) {
        if (type === "unload") {
          windowListeners.add(listener);
        }
      },
      removeEventListener(type, listener) {
        if (type === "unload") {
          windowListeners.delete(listener);
        }
      },
    },
    getAttribute() {
      return "";
    },
    addProgressListener(listener) {
      progressListeners.add(listener);
    },
    removeProgressListener(listener) {
      progressListeners.delete(listener);
    },
    notifyLocationChange(uri, flags = 0) {
      if (!listeners) {
        return;
      }
      for (const listener of [...progressListeners]) {
        listener.onLocationChange({ isTopLevel: true }, null, uri, flags);
      }
    },
    goTo(newSpec) {
      this.browsingContext.currentWindowGlobal = {
        documentURI: fakeURI(newSpec),
        innerWindowId: gNextInnerWindowId++,
      };
      this.notifyLocationChange(fakeURI(newSpec));
    },
    goToInDocument(newSpec) {
      this.browsingContext.currentWindowGlobal.documentURI = fakeURI(newSpec);
      this.notifyLocationChange(
        fakeURI(newSpec),
        Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT
      );
    },
    goToInFrame(frameSpec) {
      for (const listener of [...progressListeners]) {
        listener.onLocationChange(
          { isTopLevel: false },
          null,
          fakeURI(frameSpec),
          0
        );
      }
    },
    replaceDocument() {
      this.goTo(this.browsingContext.currentWindowGlobal.documentURI.spec);
    },
    failLoad(failedSpec) {
      this.browsingContext.currentWindowGlobal = {
        documentURI: fakeURI("about:neterror?e=connectionFailure"),
        innerWindowId: gNextInnerWindowId++,
      };
      this.notifyLocationChange(
        fakeURI(failedSpec),
        Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE
      );
    },
    closeTab() {
      for (const listener of [...tabListeners]) {
        listener();
      }
      this.isConnected = false;
    },
    closeWindow() {
      for (const listener of [...windowListeners]) {
        listener();
      }
      this.isConnected = false;
    },
  };
  return browser;
}

/**
 * Open a link from the newtab, the way PlacesFeed reports it.
 *
 * @param {object} instance a TelemetryFeed
 * @param {object} browser the <browser> that receives the load
 * @param {object} [data] the dwell_label the link opted in with
 */
function openLinkFrom(instance, browser, data = {}) {
  instance.handleDwellLinkOpened({
    data: { browser, dwell_label: "story_organic", ...data },
  });
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

/**
 * The newtab.opened_page_dwell_time distribution for one label, or null if
 * none.
 *
 * @param {string} label one of the four labels the metric declares
 * @returns {object|null}
 */
function readOpenedPageDwell(label) {
  return Glean.newtab.openedPageDwellTime[label].testGetValue("metrics");
}

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

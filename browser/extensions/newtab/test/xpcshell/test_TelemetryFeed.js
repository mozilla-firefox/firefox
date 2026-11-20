/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

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
    data: {
      event: "SHOW_PERSONALIZE",
      source: "settings",
      subpanel: "widgets_management",
    },
  });

  let events = Glean.newtab.customizePanelOpen.testGetValue();
  Assert.equal(events.length, 1, "One customize_panel_open event");
  Assert.deepEqual(events[0].extra, {
    newtab_visit_id: session.session_id,
    source: "settings",
    subpanel: "widgets_management",
  });
});

add_task(async function test_customize_panel_open_event_without_subpanel() {
  info("handleUserEvent leaves subpanel out when the action has none");
  Services.fog.testResetFOG();
  const PORT_ID = "port123";
  let instance = new TelemetryFeed();
  let session = instance.addSession(PORT_ID);

  instance.handleUserEvent({
    meta: { fromTarget: PORT_ID },
    data: { event: "SHOW_PERSONALIZE", source: "customize_button" },
  });

  let events = Glean.newtab.customizePanelOpen.testGetValue();
  Assert.equal(events.length, 1, "One customize_panel_open event");
  Assert.deepEqual(events[0].extra, {
    newtab_visit_id: session.session_id,
    source: "customize_button",
  });
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

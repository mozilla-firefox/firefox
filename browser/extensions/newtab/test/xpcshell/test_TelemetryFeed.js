/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  JsonSchemaValidator:
    "resource://gre/modules/components-utils/JsonSchemaValidator.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  TelemetryFeed: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  USER_PREFS_ENCODING: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  UTEventReporting: "resource://newtab/lib/UTEventReporting.sys.mjs",
});

const FAKE_UUID = "{foo-123-foo}";
const PREF_IMPRESSION_ID = "browser.newtabpage.activity-stream.impressionId";
const PREF_TELEMETRY = "browser.newtabpage.activity-stream.telemetry";
const PREF_EVENT_TELEMETRY =
  "browser.newtabpage.activity-stream.telemetry.ut.events";

let BasePingSchemaPromise;
let SessionPingSchemaPromise;
let UserEventPingSchemaPromise;

function assertPingMatchesSchema(pingKind, ping, schema) {
  // Unlike the validator from JsonSchema.sys.mjs, JsonSchemaValidator
  // lets us opt-in to having "undefined" properties, which are then
  // ignored. This is fine because the ping is sent as a JSON string
  // over an XHR, and undefined properties are culled as part of the
  // JSON encoding process.
  let result = JsonSchemaValidator.validate(ping, schema, {
    allowExplicitUndefinedProperties: true,
  });

  if (!result.valid) {
    info(`${pingKind} failed to validate against the schema: ${result.error}`);
  }

  Assert.ok(result.valid, `${pingKind} is valid against the schema.`);
}

async function assertSessionPingValid(ping) {
  let schema = await SessionPingSchemaPromise;
  assertPingMatchesSchema("SessionPing", ping, schema);
}

async function assertBasePingValid(ping) {
  let schema = await BasePingSchemaPromise;
  assertPingMatchesSchema("BasePing", ping, schema);
}

async function assertUserEventPingValid(ping) {
  let schema = await UserEventPingSchemaPromise;
  assertPingMatchesSchema("UserEventPing", ping, schema);
}

add_setup(async function setup() {
  BasePingSchemaPromise = IOUtils.readJSON(
    do_get_file("../schemas/base_ping.schema.json").path
  );

  SessionPingSchemaPromise = IOUtils.readJSON(
    do_get_file("../schemas/session_ping.schema.json").path
  );

  UserEventPingSchemaPromise = IOUtils.readJSON(
    do_get_file("../schemas/user_event_ping.schema.json").path
  );

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

  Services.prefs.setCharPref(
    "browser.contextual-services.contextId",
    FAKE_UUID
  );
});

add_task(async function test_construction() {
  let testInstance = new TelemetryFeed();
  Assert.ok(
    testInstance,
    "Should have been able to create an instance of TelemetryFeed."
  );
  Assert.ok(
    testInstance.utEvents instanceof UTEventReporting,
    "Should add .utEvents, a UTEventReporting instance."
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

  info("Event telemetry pref changes from false to true");

  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, false);
  Assert.ok(!instance.eventTelemetryEnabled, "Event telemetry disabled");

  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, true);
  Assert.ok(instance.eventTelemetryEnabled, "Event telemetry enabled");

  Services.prefs.clearUserPref(PREF_EVENT_TELEMETRY);
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
  TelemetryTestUtils.assertScalar(
    snapshot,
    "deletion.request.context_id",
    FAKE_UUID
  );
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

  let foundMarker = profile.threads[0].markers.data.find(marker => {
    return marker[5]?.name === "browser-open-newtab-start";
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

add_task(async function test_addSession_valid_ping_on_first_abouthome() {
  info(
    "TelemetryFeed.addSession should create a valid session ping " +
      "on the first about:home seen"
  );
  let instance = new TelemetryFeed();
  // Add a session
  const PORT_ID = "foo";
  let session = instance.addSession(PORT_ID, "about:home");

  // Create a ping referencing the session
  let ping = instance.createSessionEndEvent(session);
  await assertSessionPingValid(ping);
});

add_task(async function test_addSession_valid_ping_data_late_by_ms() {
  info(
    "TelemetryFeed.addSession should create a valid session ping " +
      "with the data_late_by_ms perf"
  );
  let instance = new TelemetryFeed();
  // Add a session
  const PORT_ID = "foo";
  let session = instance.addSession(PORT_ID, "about:home");

  const TOPSITES_LATE_BY_MS = 10;
  const HIGHLIGHTS_LATE_BY_MS = 20;
  instance.saveSessionPerfData("foo", {
    topsites_data_late_by_ms: TOPSITES_LATE_BY_MS,
  });
  instance.saveSessionPerfData("foo", {
    highlights_data_late_by_ms: HIGHLIGHTS_LATE_BY_MS,
  });

  // Create a ping referencing the session
  let ping = instance.createSessionEndEvent(session);
  await assertSessionPingValid(ping);
  Assert.equal(session.perf.topsites_data_late_by_ms, TOPSITES_LATE_BY_MS);
  Assert.equal(session.perf.highlights_data_late_by_ms, HIGHLIGHTS_LATE_BY_MS);
});

add_task(async function test_addSession_valid_ping_topsites_stats_perf() {
  info(
    "TelemetryFeed.addSession should create a valid session ping " +
      "with the topsites stats perf"
  );
  let instance = new TelemetryFeed();
  // Add a session
  const PORT_ID = "foo";
  let session = instance.addSession(PORT_ID, "about:home");

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

  // Create a ping referencing the session
  let ping = instance.createSessionEndEvent(session);
  await assertSessionPingValid(ping);
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
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, true);
  let instance = new TelemetryFeed();
  instance.addSession("foo");

  Services.telemetry.clearEvents();
  instance.endSession("foo");
  TelemetryTestUtils.assertNumberOfEvents(0);

  info("TelemetryFeed.endSession should remove the session from .sessions");
  Assert.ok(!instance.sessions.has("foo"));

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  Services.prefs.clearUserPref(PREF_EVENT_TELEMETRY);
});

add_task(async function test_endSession_send_ping() {
  info(
    "TelemetryFeed.endSession should call createSessionSendEvent with the " +
      "session if visibilty_event_rcvd_ts was set"
  );
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, true);
  let instance = new TelemetryFeed();

  let sandbox = sinon.createSandbox();
  sandbox.stub(instance, "createSessionEndEvent");
  sandbox.stub(instance.utEvents, "sendSessionEndEvent");

  let session = instance.addSession("foo");

  session.perf.visibility_event_rcvd_ts = 444.4732;
  instance.endSession("foo");

  Assert.ok(instance.createSessionEndEvent.calledWith(session));
  let sessionEndEvent = instance.createSessionEndEvent.firstCall.returnValue;
  Assert.ok(instance.utEvents.sendSessionEndEvent.calledWith(sessionEndEvent));

  info("TelemetryFeed.endSession should remove the session from .sessions");
  Assert.ok(!instance.sessions.has("foo"));

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  Services.prefs.clearUserPref(PREF_EVENT_TELEMETRY);

  sandbox.restore();
});

add_task(async function test_createPing_valid_base_if_no_portID() {
  info(
    "TelemetryFeed.createPing should create a valid base ping " +
      "without a session if no portID is supplied"
  );
  let instance = new TelemetryFeed();
  let ping = await instance.createPing();
  await assertBasePingValid(ping);
  Assert.ok(!ping.session_id);
  Assert.ok(!ping.page);
});

add_task(async function test_createPing_valid_base_if_portID() {
  info(
    "TelemetryFeed.createPing should create a valid base ping " +
      "with session info if a portID is supplied"
  );
  // Add a session
  const PORT_ID = "foo";
  let instance = new TelemetryFeed();
  instance.addSession(PORT_ID, "about:home");
  let sessionID = instance.sessions.get(PORT_ID).session_id;

  // Create a ping referencing the session
  let ping = await instance.createPing(PORT_ID);
  await assertBasePingValid(ping);

  // Make sure we added the right session-related stuff to the ping
  Assert.equal(ping.session_id, sessionID);
  Assert.equal(ping.page, "about:home");
});

add_task(async function test_createPing_no_session_yet_portID() {
  info(
    "TelemetryFeed.createPing should create an 'unexpected' base ping " +
      "if no session yet portID is supplied"
  );
  let instance = new TelemetryFeed();
  let ping = await instance.createPing("foo");
  await assertBasePingValid(ping);

  Assert.equal(ping.page, "unknown");
  Assert.equal(
    instance.sessions.get("foo").perf.load_trigger_type,
    "unexpected"
  );
});

add_task(async function test_createPing_includes_userPrefs() {
  info("TelemetryFeed.createPing should create a base ping with user_prefs");
  let expectedUserPrefs = 0;

  for (let pref of Object.keys(USER_PREFS_ENCODING)) {
    Services.prefs.setBoolPref(
      `browser.newtabpage.activity-stream.${pref}`,
      true
    );
    expectedUserPrefs |= USER_PREFS_ENCODING[pref];
  }

  let instance = new TelemetryFeed();
  let ping = await instance.createPing("foo");
  await assertBasePingValid(ping);
  Assert.equal(ping.user_prefs, expectedUserPrefs);

  for (const pref of Object.keys(USER_PREFS_ENCODING)) {
    Services.prefs.clearUserPref(`browser.newtabpage.activity-stream.${pref}`);
  }
});

add_task(async function test_createUserEvent_is_valid() {
  info(
    "TelemetryFeed.createUserEvent should create a valid user event ping " +
      "with the right session_id"
  );
  const PORT_ID = "foo";

  let instance = new TelemetryFeed();
  let data = { source: "TOP_SITES", event: "CLICK" };
  let action = actionCreators.AlsoToMain(
    actionCreators.UserEvent(data),
    PORT_ID
  );
  let session = instance.addSession(PORT_ID);

  let ping = await instance.createUserEvent(action);

  // Is it valid?
  await assertUserEventPingValid(ping);
  // Does it have the right session_id?
  Assert.equal(ping.session_id, session.session_id);
});

add_task(async function test_createSessionEndEvent_is_valid() {
  info(
    "TelemetryFeed.createSessionEndEvent should create a valid session ping"
  );
  const FAKE_DURATION = 12345;
  let instance = new TelemetryFeed();
  let ping = await instance.createSessionEndEvent({
    session_id: FAKE_UUID,
    page: "about:newtab",
    session_duration: FAKE_DURATION,
    perf: {
      load_trigger_ts: 10,
      load_trigger_type: "menu_plus_or_keyboard",
      visibility_event_rcvd_ts: 20,
      is_preloaded: true,
    },
  });

  // Is it valid?
  await assertSessionPingValid(ping);
  Assert.equal(ping.session_id, FAKE_UUID);
  Assert.equal(ping.page, "about:newtab");
  Assert.equal(ping.session_duration, FAKE_DURATION);
});

add_task(async function test_createSessionEndEvent_with_unexpected_is_valid() {
  info(
    "TelemetryFeed.createSessionEndEvent should create a valid 'unexpected' " +
      "session ping"
  );
  const FAKE_DURATION = 12345;
  const FAKE_TRIGGER_TYPE = "unexpected";

  let instance = new TelemetryFeed();
  let ping = await instance.createSessionEndEvent({
    session_id: FAKE_UUID,
    page: "about:newtab",
    session_duration: FAKE_DURATION,
    perf: {
      load_trigger_type: FAKE_TRIGGER_TYPE,
      is_preloaded: true,
    },
  });

  // Is it valid?
  await assertSessionPingValid(ping);
  Assert.equal(ping.session_id, FAKE_UUID);
  Assert.equal(ping.page, "about:newtab");
  Assert.equal(ping.session_duration, FAKE_DURATION);
  Assert.equal(ping.perf.load_trigger_type, FAKE_TRIGGER_TYPE);
});

add_task(async function test_sendUTEvent_call_right_function() {
  info("TelemetryFeed.sendUTEvent should call the UT event function passed in");
  let sandbox = sinon.createSandbox();

  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, true);

  let event = {};
  let instance = new TelemetryFeed();
  sandbox.stub(instance.utEvents, "sendUserEvent");
  instance.addSession("foo");

  await instance.sendUTEvent(event, instance.utEvents.sendUserEvent);
  Assert.ok(instance.utEvents.sendUserEvent.calledWith(event));

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  Services.prefs.clearUserPref(PREF_EVENT_TELEMETRY);
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

  info(
    "TelemetryFeed.onAction should send an event on a TELEMETRY_USER_EVENT " +
      "action"
  );
  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, true);
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "createUserEvent");
      sandbox.stub(instance.utEvents, "sendUserEvent");
    },
    { type: actionTypes.TELEMETRY_USER_EVENT },
    instance => {
      Assert.ok(
        instance.createUserEvent.calledOnce,
        "TelemetryFeed.createUserEvent called once"
      );
      Assert.ok(
        instance.createUserEvent.calledWith({
          type: actionTypes.TELEMETRY_USER_EVENT,
        })
      );
      Assert.ok(
        instance.utEvents.sendUserEvent.calledOnce,
        "TelemetryFeed.utEvents.sendUserEvent called once"
      );
      Assert.ok(
        instance.utEvents.sendUserEvent.calledWith(
          instance.createUserEvent.returnValue
        )
      );
    }
  );
  Services.prefs.clearUserPref(PREF_EVENT_TELEMETRY);
  Services.prefs.clearUserPref(PREF_TELEMETRY);

  info(
    "TelemetryFeed.onAction should send an event on a " +
      "DISCOVERY_STREAM_USER_EVENT action"
  );
  Services.prefs.setBoolPref(PREF_EVENT_TELEMETRY, true);
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  testOnAction(
    (sandbox, instance) => {
      sandbox.stub(instance, "createUserEvent");
      sandbox.stub(instance.utEvents, "sendUserEvent");
    },
    { type: actionTypes.DISCOVERY_STREAM_USER_EVENT },
    instance => {
      Assert.ok(
        instance.createUserEvent.calledOnce,
        "TelemetryFeed.createUserEvent called once"
      );
      Assert.ok(
        instance.createUserEvent.calledWith({
          type: actionTypes.DISCOVERY_STREAM_USER_EVENT,
          data: {
            value: {
              pocket_logged_in_status: Glean.pocket.isSignedIn.testGetValue(),
            },
          },
        })
      );
      Assert.ok(
        instance.utEvents.sendUserEvent.calledOnce,
        "TelemetryFeed.utEvents.sendUserEvent called once"
      );
      Assert.ok(
        instance.utEvents.sendUserEvent.calledWith(
          instance.createUserEvent.returnValue
        )
      );
    }
  );
  Services.prefs.clearUserPref(PREF_EVENT_TELEMETRY);
  Services.prefs.clearUserPref(PREF_TELEMETRY);
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
  async function test_handleDiscoveryStreamImpressionStats_instrument_pocket_impressions() {
    info(
      "TelemetryFeed.handleDiscoveryStreamImpressionStats should throw " +
        "for a missing session"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "1337cafe";
    const POS_1 = 1;
    const POS_2 = 4;
    const SHIM = "Y29uc2lkZXIgeW91ciBjdXJpb3NpdHkgcmV3YXJkZWQ=";
    const FETCH_TIMESTAMP = new Date("March 22, 2024 10:15:20");
    const NEWTAB_CREATION_TIMESTAMP = new Date("March 23, 2024 11:10:30");
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let pingSubmitted = new Promise(resolve => {
      GleanPings.spoc.testBeforeNextSubmit(reason => {
        Assert.equal(reason, "impression");
        let pocketImpressions = Glean.pocket.impression.testGetValue();
        Assert.equal(pocketImpressions.length, 2);
        Assert.deepEqual(pocketImpressions[0].extra, {
          newtab_visit_id: SESSION_ID,
          is_sponsored: String(false),
          position: String(POS_1),
          recommendation_id: "decaf-c0ff33",
          tile_id: String(1),
        });
        Assert.deepEqual(pocketImpressions[1].extra, {
          newtab_visit_id: SESSION_ID,
          is_sponsored: String(true),
          position: String(POS_2),
          tile_id: String(2),
        });
        Assert.equal(Glean.pocket.shim.testGetValue(), SHIM);
        Assert.deepEqual(
          Glean.pocket.fetchTimestamp.testGetValue(),
          FETCH_TIMESTAMP
        );
        Assert.deepEqual(
          Glean.pocket.newtabCreationTimestamp.testGetValue(),
          NEWTAB_CREATION_TIMESTAMP
        );

        resolve();
      });
    });

    instance.handleDiscoveryStreamImpressionStats("_", {
      source: "foo",
      tiles: [
        {
          id: 1,
          pos: POS_1,
          type: "organic",
          recommendation_id: "decaf-c0ff33",
        },
        {
          id: 2,
          pos: POS_2,
          type: "spoc",
          recommendation_id: undefined,
          shim: SHIM,
          fetchTimestamp: FETCH_TIMESTAMP.valueOf(),
        },
      ],
      window_inner_width: 1000,
      window_inner_height: 900,
      firstVisibleTimestamp: NEWTAB_CREATION_TIMESTAMP.valueOf(),
    });

    await pingSubmitted;

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
    };
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesOrganicImpressionStats({ data });
    let impressions = Glean.topsites.impression.testGetValue();
    Assert.equal(impressions.length, 1, "Recorded 1 impression");

    Assert.deepEqual(impressions[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(0),
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
  const feature = "SPONSORED_CONTENT_INFO";
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

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        recommendation_id: "decaf-c0ff33",
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
      position: ACTION_POSITION,
      recommendation_id: "decaf-c0ff33",
      tile_id: String(314623757745896),
    });

    Assert.ok(
      !Glean.pocket.shim.testGetValue(),
      "Pocket shim was not recorded"
    );

    sandbox.restore();
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
    const FETCH_TIMESTAMP = new Date("March 22, 2024 10:15:20");
    const NEWTAB_CREATION_TIMESTAMP = new Date("March 23, 2024 11:10:30");
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "spoc",
        recommendation_id: undefined,
        tile_id: 448685088,
        shim: SHIM,
        fetchTimestamp: FETCH_TIMESTAMP.valueOf(),
        firstVisibleTimestamp: NEWTAB_CREATION_TIMESTAMP.valueOf(),
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let pingSubmitted = new Promise(resolve => {
      GleanPings.spoc.testBeforeNextSubmit(reason => {
        Assert.equal(reason, "click");
        Assert.deepEqual(
          Glean.pocket.fetchTimestamp.testGetValue(),
          FETCH_TIMESTAMP
        );
        Assert.deepEqual(
          Glean.pocket.newtabCreationTimestamp.testGetValue(),
          NEWTAB_CREATION_TIMESTAMP
        );
        resolve();
      });
    });

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(true),
      position: ACTION_POSITION,
      tile_id: String(448685088),
    });

    await pingSubmitted;

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_organic_top_stories_save() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a save of an " +
        "organic top story"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "SAVE_TO_POCKET",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        recommendation_id: "decaf-c0ff33",
        tile_id: 314623757745896,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let saves = Glean.pocket.save.testGetValue();
    Assert.equal(saves.length, 1, "Recorded 1 save");
    Assert.deepEqual(saves[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: ACTION_POSITION,
      recommendation_id: "decaf-c0ff33",
      tile_id: String(314623757745896),
    });
    Assert.ok(
      !Glean.pocket.shim.testGetValue(),
      "Pocket shim was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_sponsored_top_stories_save() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a save of a " +
        "sponsored top story"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    const SHIM = "Y29uc2lkZXIgeW91ciBjdXJpb3NpdHkgcmV3YXJkZWQ=";
    const FETCH_TIMESTAMP = new Date("March 22, 2024 10:15:20");
    const NEWTAB_CREATION_TIMESTAMP = new Date("March 23, 2024 11:10:30");
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "SAVE_TO_POCKET",
      action_position: ACTION_POSITION,
      value: {
        card_type: "spoc",
        recommendation_id: undefined,
        tile_id: 448685088,
        shim: SHIM,
        fetchTimestamp: FETCH_TIMESTAMP.valueOf(),
        newtabCreationTimestamp: NEWTAB_CREATION_TIMESTAMP.valueOf(),
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    let pingSubmitted = new Promise(resolve => {
      GleanPings.spoc.testBeforeNextSubmit(reason => {
        Assert.equal(reason, "save");
        Assert.equal(
          Glean.pocket.shim.testGetValue(),
          SHIM,
          "Pocket shim was recorded"
        );
        Assert.deepEqual(
          Glean.pocket.fetchTimestamp.testGetValue(),
          FETCH_TIMESTAMP
        );
        Assert.deepEqual(
          Glean.pocket.newtabCreationTimestamp.testGetValue(),
          NEWTAB_CREATION_TIMESTAMP
        );

        resolve();
      });
    });

    instance.handleDiscoveryStreamUserEvent(action);

    let saves = Glean.pocket.save.testGetValue();
    Assert.equal(saves.length, 1, "Recorded 1 save");
    Assert.deepEqual(saves[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(true),
      position: ACTION_POSITION,
      tile_id: String(448685088),
    });

    await pingSubmitted;

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_sponsored_top_stories_save_no_value() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a save of a " +
        "sponsored top story, without `value`"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "SAVE_TO_POCKET",
      action_position: ACTION_POSITION,
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let saves = Glean.pocket.save.testGetValue();
    Assert.equal(saves.length, 1, "Recorded 1 save");
    Assert.deepEqual(saves[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: ACTION_POSITION,
    });
    Assert.ok(
      !Glean.pocket.shim.testGetValue(),
      "Pocket shim was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_thumbs_down_event() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a thumbs down" +
        " event of an organic story"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "POCKET_THUMBS_DOWN",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        recommendation_id: "decaf-c0ff33",
        tile_id: 314623757745896,
        thumbs_down: true,
        thumbs_up: false,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let thumbVotes = Glean.pocket.thumbVotingInteraction.testGetValue();
    Assert.equal(thumbVotes.length, 1, "Recorded 1 thumbs down");
    Assert.deepEqual(thumbVotes[0].extra, {
      newtab_visit_id: SESSION_ID,
      recommendation_id: "decaf-c0ff33",
      tile_id: String(314623757745896),
      thumbs_down: String(true),
      thumbs_up: String(false),
    });

    sandbox.restore();
  }
);

add_task(async function test_handleDiscoveryStreamUserEvent_thumbs_up_event() {
  info(
    "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a thumbs up" +
      " event of an organic story"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  const ACTION_POSITION = 42;
  let action = actionCreators.DiscoveryStreamUserEvent({
    event: "POCKET_THUMBS_DOWN",
    action_position: ACTION_POSITION,
    value: {
      card_type: "organic",
      recommendation_id: "decaf-c0ff33",
      tile_id: 314623757745896,
      thumbs_down: false,
      thumbs_up: true,
    },
  });

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  instance.handleDiscoveryStreamUserEvent(action);

  let thumbVotes = Glean.pocket.thumbVotingInteraction.testGetValue();
  Assert.equal(thumbVotes.length, 1, "Recorded 1 thumbs down");
  Assert.deepEqual(thumbVotes[0].extra, {
    newtab_visit_id: SESSION_ID,
    recommendation_id: "decaf-c0ff33",
    tile_id: String(314623757745896),
    thumbs_down: String(false),
    thumbs_up: String(true),
  });

  sandbox.restore();
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

  await instance.handleBlockUrl({ data });

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

    await instance.handleBlockUrl({ data });

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

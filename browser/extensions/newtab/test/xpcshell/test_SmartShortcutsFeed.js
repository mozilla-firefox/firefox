/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionTypes: "resource://newtab/common/Actions.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  SmartShortcutsFeed: "resource://newtab/lib/SmartShortcutsFeed.sys.mjs",
});

const PREF_SYSTEM_SHORTCUTS_PERSONALIZATION =
  "discoverystream.shortcuts.personalization.enabled";
const PREF_SYSTEM_SHORTCUTS_LOG = "discoverystream.shortcuts.force_log.enabled";

function makeStore(values) {
  return {
    getState() {
      return this.state;
    },
    state: {
      Prefs: {
        values,
      },
    },
  };
}

add_task(async function test_construction() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: false,
  });

  info("SmartShortcutsFeed constructor should create initial values");

  Assert.ok(feed, "Could construct a SmartShortcutsFeed");
  Assert.ok(!feed.loaded, "SmartShortcutsFeed is not loaded");
  Assert.ok(!feed.isEnabled());
});

add_task(async function test_onAction_INIT() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
  });

  info("SmartShortcutsFeed.onAction INIT should set loaded");

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.ok(feed.loaded);
});

add_task(async function test_isEnabled() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
  });

  info("SmartShortcutsFeed should be enabled");
  Assert.ok(feed.isEnabled());
});

add_task(async function test_isEnabled_pref_fallback_when_trainhop_missing() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
  });

  Assert.ok(
    feed.isEnabled(),
    "local pref enables feed without trainhop config"
  );
});

add_task(async function test_isEnabled_pref_fallback_when_trainhop_partial() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
    trainhopConfig: { smartShortcuts: {} },
  });

  Assert.ok(
    feed.isEnabled(),
    "partial trainhop config falls back to local pref"
  );
});

add_task(async function test_onAction_INIT_remote_false_disables_loaded() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
    trainhopConfig: { smartShortcuts: { enabled: false } },
  });

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.ok(!feed.loaded, "explicit remote false disables normal feed loading");
});

add_task(async function test_force_log_keeps_feed_enabled_when_remote_false() {
  let feed = new SmartShortcutsFeed();

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
    trainhopConfig: {
      smartShortcuts: { enabled: false, force_log: true },
    },
  });

  Assert.ok(feed.isEnabled(), "force_log overrides remote false for logging");

  await feed.onAction({
    type: actionTypes.INIT,
  });

  Assert.ok(feed.loaded, "force_log keeps the feed loaded");

  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
    [PREF_SYSTEM_SHORTCUTS_LOG]: true,
    trainhopConfig: { smartShortcuts: { enabled: false } },
  });

  Assert.ok(
    feed.isEnabled(),
    "local force_log pref also keeps logging enabled"
  );
});

function countInteractions() {
  return PlacesTestUtils.getDatabaseValue(
    "moz_newtab_shortcuts_interaction",
    "COUNT(*)"
  );
}

add_task(async function test_interactions_written_in_one_flush() {
  await PlacesUtils.history.clear();
  await PlacesTestUtils.addVisits("https://example.com/");
  const guid = await PlacesTestUtils.getDatabaseValue("moz_places", "guid", {
    url: "https://example.com/",
  });

  let feed = new SmartShortcutsFeed();
  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
  });
  await feed.onAction({ type: actionTypes.INIT });

  for (const [type, isPinned, position] of [
    ["impression", false, 0],
    ["impression", true, 1],
    ["click", false, 2],
  ]) {
    await feed.onAction({
      type: actionTypes.TOP_SITES_ORGANIC_IMPRESSION_STATS,
      data: { type, guid, position, isPinned },
    });
  }

  Assert.ok(feed.flushTask.isArmed, "Flush is scheduled, not run");
  Assert.equal(feed.pendingInteractions.length, 3, "Interactions are queued");

  const firstTask = feed.flushTask;
  await feed.onAction({ type: actionTypes.UNINIT });

  Assert.equal(await countInteractions(), 3, "UNINIT writes all interactions");
  const db = await PlacesUtils.promiseDBConnection();
  const stored = await db.execute(
    `SELECT event_type, pinned, tile_position
     FROM moz_newtab_shortcuts_interaction ORDER BY id`
  );
  Assert.deepEqual(
    stored.map(row => [
      row.getResultByName("event_type"),
      row.getResultByName("pinned"),
      row.getResultByName("tile_position"),
    ]),
    [
      [0, 0, 0],
      [0, 1, 1],
      [1, 0, 2],
    ],
    "Each interaction is stored with its own values"
  );
  Assert.equal(feed.pendingInteractions.length, 0, "Queue is emptied");

  await feed.onAction({ type: actionTypes.INIT });
  await feed.onAction({
    type: actionTypes.TOP_SITES_ORGANIC_IMPRESSION_STATS,
    data: { type: "impression", guid, position: 0, isPinned: false },
  });
  Assert.notEqual(feed.flushTask, firstTask, "A new task is created");
  Assert.ok(feed.flushTask.isArmed, "The new task is scheduled");

  await feed.onAction({ type: actionTypes.UNINIT });
  Assert.equal(await countInteractions(), 4, "The new task writes too");

  await PlacesUtils.history.clear();
});

add_task(async function test_idle_daily_observer() {
  let feed = new SmartShortcutsFeed();
  feed.store = makeStore({
    [PREF_SYSTEM_SHORTCUTS_PERSONALIZATION]: true,
  });

  const countObservers = () =>
    [...Services.obs.enumerateObservers("idle-daily")].length;
  const before = countObservers();

  await feed.onAction({ type: actionTypes.INIT });
  Assert.equal(countObservers(), before + 1, "Observes idle-daily after INIT");

  await feed.onAction({ type: actionTypes.UNINIT });
  Assert.equal(countObservers(), before, "Stops observing on UNINIT");
});

add_task(async function test_idle_daily_prunes_old_interactions() {
  await PlacesUtils.history.clear();
  await PlacesTestUtils.addVisits("https://example.com/");
  const placeId = await PlacesTestUtils.getDatabaseValue("moz_places", "id", {
    url: "https://example.com/",
  });

  const DAY_MS = 24 * 60 * 60 * 1000;
  const now = Date.now();
  await PlacesUtils.withConnectionWrapper("test", async db => {
    for (const age of [1, 63]) {
      await db.execute(
        `INSERT INTO moz_newtab_shortcuts_interaction
           (place_id, event_type, timestamp_s, pinned, tile_position)
         VALUES (:placeId, 0, :timestamp_s, 0, 0)`,
        { placeId, timestamp_s: Math.round((now - age * DAY_MS) / 1000) }
      );
    }
  });

  let feed = new SmartShortcutsFeed();
  await feed.pruneInteractions();

  Assert.equal(
    await countInteractions(),
    1,
    "Removes interactions past the lookback window"
  );

  await PlacesUtils.history.clear();
});

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * A per-device client creates meta/global, then a legacy client joins. The
 * creator must publish its engines so the legacy client doesn't treat them
 * as disabled remotely.
 */

var phases = { phase1: "profile1", phase2: "profile2" };

const PER_DEVICE = {
  name: "services.sync.perDeviceEngineChoices",
  value: true,
};
const DEFAULT_ENGINES = ["bookmarks", "history", "passwords", "prefs", "tabs"];
const DEFAULT_ENGINES_ON = DEFAULT_ENGINES.map(engine => ({
  name: `services.sync.engine.${engine}`,
  value: true,
}));

var bookmarks_initial = {
  menu: [{ uri: "http://www.mozilla.com", title: "Mozilla" }],
};

// Per-device client. TPS signs in before the first action, but doesn't sync
// until [Sync], so the pref is on when meta/global is created. The first sync
// only syncs tabs, as opening synced tabs right after sign-in would, but
// meta/global must still list every enabled engine.
Phase("phase1", [
  [Prefs.set, [PER_DEVICE]],
  [Bookmarks.add, bookmarks_initial],
  [Sync, null, ["tabs"]],
  [Server.verify, { engines: [...DEFAULT_ENGINES, "clients"] }],
  [Sync],
  [
    Server.verify,
    {
      engines: [...DEFAULT_ENGINES, "clients"],
      notDeclined: DEFAULT_ENGINES,
      syncIDs: ["bookmarks"],
      matchesLocal: ["bookmarks"],
    },
  ],
]);

// Legacy client. Engines missing from meta/global would be disabled once it
// counts both clients; a second sync makes sure that has happened.
Phase("phase2", [
  [Sync],
  [Sync],
  [Prefs.verify, DEFAULT_ENGINES_ON],
  [Bookmarks.verify, bookmarks_initial],
  [
    Server.verify,
    {
      engines: DEFAULT_ENGINES,
      matchesLocal: ["bookmarks"],
      registryMatchesGlobal: ["bookmarks"],
    },
  ],
]);

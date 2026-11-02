/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * A legacy client declines and re-enables bookmarks. The per-device client
 * keeps syncing from meta/syncids, and the legacy client adopts that
 * generation on re-enable instead of resetting everyone.
 */

var phases = {
  phase1: "profile1",
  phase2: "profile2",
  phase3: "profile1",
  phase4: "profile2",
  phase5: "profile1",
  phase6: "profile2",
};

const PER_DEVICE = {
  name: "services.sync.perDeviceEngineChoices",
  value: true,
};
const BOOKMARKS_OFF = { name: "services.sync.engine.bookmarks", value: false };
const BOOKMARKS_ON = { name: "services.sync.engine.bookmarks", value: true };

var bookmarks_initial = {
  menu: [{ uri: "http://www.mozilla.com", title: "Mozilla" }],
};
var bookmarks_extra = {
  menu: [{ uri: "http://www.firefox.com", title: "Firefox" }],
};

// Legacy client.
Phase("phase1", [[Bookmarks.add, bookmarks_initial], [Sync]]);

// Per-device client.
Phase("phase2", [
  [Prefs.set, [PER_DEVICE]],
  [Sync],
  [Bookmarks.verify, bookmarks_initial],
  [Server.verify, { syncIDs: ["bookmarks"], matchesLocal: ["bookmarks"] }],
]);

Phase("phase3", [
  [Sync],
  [Prefs.set, [BOOKMARKS_OFF]],
  [Sync],
  [
    Server.verify,
    {
      notEngines: ["bookmarks"],
      declined: ["bookmarks"],
      syncIDs: ["bookmarks"],
    },
  ],
]);

// The legacy decline deleted the server's bookmarks while per-collection
// DELETE still works, and this client doesn't reset, so the server tree is
// incomplete here. Bookmark actions re-enable validation, so skip it last.
// TODO: Stop skipping validation once the server makes per-collection DELETE
// a no-op.
Phase("phase4", [
  [Sync],
  [Prefs.verify, [BOOKMARKS_ON]],
  [Bookmarks.add, bookmarks_extra],
  [Sync],
  [
    Server.verify,
    {
      notEngines: ["bookmarks"],
      declined: ["bookmarks"],
      matchesLocal: ["bookmarks"],
    },
  ],
  [Bookmarks.skipValidation],
]);

Phase("phase5", [
  [Prefs.set, [BOOKMARKS_ON]],
  [Sync],
  [
    Server.verify,
    {
      engines: ["bookmarks"],
      notDeclined: ["bookmarks"],
      matchesLocal: ["bookmarks"],
    },
  ],
  [Bookmarks.verify, bookmarks_initial],
  [Bookmarks.verify, bookmarks_extra],
]);

Phase("phase6", [
  [Sync],
  [Server.verify, { matchesLocal: ["bookmarks"] }],
  [Bookmarks.verify, bookmarks_initial],
  [Bookmarks.verify, bookmarks_extra],
]);

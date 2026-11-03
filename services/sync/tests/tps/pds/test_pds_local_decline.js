/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * A per-device client disables and re-enables bookmarks without affecting a
 * legacy client or the server's data.
 */

var phases = {
  phase1: "profile1",
  phase2: "profile2",
  phase3: "profile1",
  phase4: "profile2",
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
  [Prefs.set, [BOOKMARKS_OFF]],
  [Sync],
  [
    Server.verify,
    {
      engines: ["bookmarks"],
      notDeclined: ["bookmarks"],
      nonEmpty: ["bookmarks"],
    },
  ],
]);

Phase("phase3", [
  [Sync],
  [Prefs.verify, [BOOKMARKS_ON]],
  [Bookmarks.add, bookmarks_extra],
  [Sync],
]);

Phase("phase4", [
  [Sync],
  [Bookmarks.verifyNot, bookmarks_extra],
  [Prefs.set, [BOOKMARKS_ON]],
  [Sync],
  [Bookmarks.verify, bookmarks_initial],
  [Bookmarks.verify, bookmarks_extra],
  [Server.verify, { engines: ["bookmarks"], matchesLocal: ["bookmarks"] }],
]);

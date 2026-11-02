/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * A per-device client signs in to an account a legacy client already uses,
 * inheriting its choices without changing them.
 */

var phases = { phase1: "profile1", phase2: "profile2", phase3: "profile1" };

const PER_DEVICE = {
  name: "services.sync.perDeviceEngineChoices",
  value: true,
};
const PASSWORDS_OFF = { name: "services.sync.engine.passwords", value: false };
const BOOKMARKS_ON = { name: "services.sync.engine.bookmarks", value: true };

var bookmarks_initial = {
  menu: [{ uri: "http://www.mozilla.com", title: "Mozilla" }],
};
var bookmarks_after = {
  menu: [
    { uri: "http://www.mozilla.com", title: "Mozilla" },
    { uri: "http://www.firefox.com", title: "Firefox" },
  ],
};

// Legacy client. A new meta/global doesn't reconcile declined, so decline
// after the first sync.
Phase("phase1", [
  [Bookmarks.add, bookmarks_initial],
  [Sync],
  [Prefs.set, [PASSWORDS_OFF]],
  [Sync],
  [Server.verify, { engines: ["bookmarks"], declined: ["passwords"] }],
]);

// Per-device client. Inheriting PASSWORDS_OFF relies on the FxA sign-in
// sending no Choose What to Sync selection, which defers to the account.
Phase("phase2", [
  [Prefs.set, [PER_DEVICE]],
  [Sync],
  [Prefs.verify, [PASSWORDS_OFF, BOOKMARKS_ON]],
  [Bookmarks.verify, bookmarks_initial],
  [
    Server.verify,
    {
      engines: ["bookmarks"],
      notEngines: ["passwords"],
      declined: ["passwords"],
      syncIDs: ["bookmarks"],
      matchesLocal: ["bookmarks"],
    },
  ],
  [
    Bookmarks.add,
    { menu: [{ uri: "http://www.firefox.com", title: "Firefox" }] },
  ],
  [Sync],
]);

Phase("phase3", [
  [Sync],
  [Bookmarks.verify, bookmarks_after],
  [Prefs.verify, [PASSWORDS_OFF, BOOKMARKS_ON]],
  [
    Server.verify,
    {
      declined: ["passwords"],
      matchesLocal: ["bookmarks"],
      registryMatchesGlobal: ["bookmarks"],
    },
  ],
]);

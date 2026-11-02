"use strict";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);
const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { ContentClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ContentClassifierTestUtils.sys.mjs"
);

const TEST_DOMAIN = "https://example.net/";
const TEST_BLOCKED_3RD_PARTY_DOMAIN = "https://example.org/";
const TEST_ANNOTATED_3RD_PARTY_DOMAIN = "https://example.com/";
const TEST_PATH = "browser/toolkit/components/content-classifier/test/browser/";
const TEST_TOP_PAGE = TEST_DOMAIN + TEST_PATH + "page.html";
const BLOCK_LIST_URL = "https://example.net/" + TEST_PATH + "block_list.txt";
const ANNOTATE_LIST_URL =
  "https://example.net/" + TEST_PATH + "annotate_list.txt";

const COLLECTION_NAME = "content-classifier-lists";

// Must match NS_CONTENT_CLASSIFIER_FILTER_LISTS_LOADED_TOPIC in
// nsIContentClassifierService.idl.
const LISTS_LOADED_TOPIC = "test-content-classifier-filter-lists-loaded";

async function loadThirdPartyImage(browser, domain) {
  let imageURL =
    domain +
    "browser/toolkit/components/antitracking/test/browser/raptor.jpg?" +
    Math.random();
  return SpecialPowers.spawn(browser, [imageURL], async url => {
    let img = new content.Image();
    img.src = url;
    return new content.Promise(resolve => {
      img.onload = () => resolve(true);
      img.onerror = () => resolve(false);
    });
  });
}

async function assertImageBlocked(browser, domain, msg) {
  let loaded = await loadThirdPartyImage(browser, domain);
  ok(!loaded, msg);
}

async function assertImageLoaded(browser, domain, msg) {
  let loaded = await loadThirdPartyImage(browser, domain);
  ok(loaded, msg);
}

async function loadThirdPartyScript(browser, domain) {
  let scriptURL = domain + TEST_PATH + "empty_script.js?" + Math.random();
  return SpecialPowers.spawn(browser, [scriptURL], async url => {
    let script = content.document.createElement("script");
    script.src = url;
    return new content.Promise(resolve => {
      script.onload = () => resolve(true);
      script.onerror = () => resolve(false);
      content.document.body.appendChild(script);
    });
  });
}

async function assertScriptBlocked(browser, domain, msg) {
  let loaded = await loadThirdPartyScript(browser, domain);
  ok(!loaded, msg);
}

async function assertScriptLoaded(browser, domain, msg) {
  let loaded = await loadThirdPartyScript(browser, domain);
  ok(loaded, msg);
}

// content: string-encoded filter list content (may include CRLF etc.).
async function makeListRecordFromContent(id, name, content) {
  return ContentClassifierTestUtils.makeListRecord(id, name, content);
}

async function makeListRecord(id, name, rules) {
  return makeListRecordFromContent(id, name, rules.join("\n") + "\n");
}

// Populate the RS database with one or more lists. Each entry is
// { id, name, rules } or { id, name, content } (raw string content).
async function populateMultipleRS(db, lists) {
  return ContentClassifierTestUtils.publishLists(lists);
}

async function populateRS(db, id, name, rules) {
  let [record] = await populateMultipleRS(db, [{ id, name, rules }]);
  return record;
}

async function waitForListsSettled(options = {}) {
  return ContentClassifierTestUtils.waitForListsSettled({
    minNotifies: 0,
    ...options,
  });
}

// Emit an RS sync and wait for the resulting rebuild to settle. A sync is a
// single synchronous change, so it yields exactly one notification carrying
// the final state (see waitForListsSettled): wait for that one and stop.
async function syncAndWaitForLists(client, records) {
  return ContentClassifierTestUtils.syncLists(records);
}

// Get the RS client and register cleanup to clear its DB and disable
// the feature after the current test task finishes (pass OR fail,
// including throws). In browser-chrome tests, registerCleanupFunction
// runs after the current task and before the next. Disabling the
// feature here ensures a thrown task cannot leave the C++ service
// with live engines / an initialized RS client that the next task
// would pick up.
function getRSClient() {
  let client = RemoteSettings(COLLECTION_NAME);
  registerCleanupFunction(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["privacy.trackingprotection.content.protection.enabled", false],
        ["privacy.trackingprotection.content.annotation.enabled", false],
        ["privacy.trackingprotection.content.protection.engines", ""],
        ["privacy.trackingprotection.content.protection.engines.pbmode", ""],
        ["privacy.trackingprotection.content.annotation.engines", ""],
        ["privacy.trackingprotection.content.annotation.engines.pbmode", ""],
      ],
    });
    await client.db.clear();
  });
  return client;
}

async function openTestTab() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_TOP_PAGE
  );
  registerCleanupFunction(() => {
    if (tab && tab.parentNode) {
      BrowserTestUtils.removeTab(tab);
    }
  });
  return tab;
}

async function openPrivateTab() {
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    TEST_TOP_PAGE
  );
  registerCleanupFunction(async () => {
    await BrowserTestUtils.closeWindow(win);
  });
  return tab;
}

// Set the four engines prefs in one call. Empty string disables that
// slot. protectionEnabled / annotationEnabled default to true iff the
// corresponding non-PBM or PBM engines string is non-empty; pass an
// explicit boolean to override (e.g. to set engines while leaving the
// feature disabled).
async function pushEnginePrefs({
  protection = "",
  pbmProtection = "",
  annotation = "",
  pbmAnnotation = "",
  protectionEnabled,
  annotationEnabled,
} = {}) {
  if (protectionEnabled === undefined) {
    protectionEnabled = Boolean(protection || pbmProtection);
  }
  if (annotationEnabled === undefined) {
    annotationEnabled = Boolean(annotation || pbmAnnotation);
  }
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.content.testing", true],
      [
        "privacy.trackingprotection.content.protection.enabled",
        protectionEnabled,
      ],
      ["privacy.trackingprotection.content.protection.engines", protection],
      [
        "privacy.trackingprotection.content.protection.engines.pbmode",
        pbmProtection,
      ],
      ["privacy.trackingprotection.content.protection.test_list_urls", ""],
      [
        "privacy.trackingprotection.content.annotation.enabled",
        annotationEnabled,
      ],
      ["privacy.trackingprotection.content.annotation.engines", annotation],
      [
        "privacy.trackingprotection.content.annotation.engines.pbmode",
        pbmAnnotation,
      ],
      ["privacy.trackingprotection.content.annotation.test_list_urls", ""],
    ],
  });
  // No wait here: these prefs are one synchronous batch, so generation
  // suppression collapses them to a single (final-state) rebuild whose
  // notification - if it even fires before the caller's syncAndWaitForLists -
  // already carries the full pref snapshot. Every caller that then asserts
  // engine behavior goes through syncAndWaitForLists, which owns the wait;
  // the lone exception leaves the feature disabled (no rebuild to wait for).
}

// Inspect a content-blocking log dump and assert that `origin` has an
// entry whose state-bit equals `state`. Uses Assert.ok so a missing
// origin or missing entry STOPS the surrounding task instead of
// silently falling through.
function assertLogHasState(log, origin, state, msg) {
  Assert.ok(log[origin], `content blocking log has entry for ${origin}`);
  let entry = log[origin].find(e => e[0] === state);
  Assert.ok(entry, msg);
  return entry;
}

// Asymmetric with assertLogHasState: uses ok() (records, doesn't throw)
// so a completely missing origin counts as "lacks state" rather than
// aborting the task.
function assertLogLacksState(log, origin, state, msg) {
  if (!log[origin]) {
    ok(true, msg + " (no log entry for origin)");
    return;
  }
  let entry = log[origin].find(e => e[0] === state);
  ok(!entry, msg);
}

// Convenience: fetch the content-blocking log for `browser`, slice the
// trailing slash off `domain`, and assert (or assert-absence of) `state`.
async function assertHasBlockingState(browser, domain, state, msg) {
  let log = JSON.parse(await browser.getContentBlockingLog());
  let origin = domain.replace(/\/$/, "");
  return assertLogHasState(log, origin, state, msg);
}

async function assertLacksBlockingState(browser, domain, state, msg) {
  let log = JSON.parse(await browser.getContentBlockingLog());
  let origin = domain.replace(/\/$/, "");
  assertLogLacksState(log, origin, state, msg);
}

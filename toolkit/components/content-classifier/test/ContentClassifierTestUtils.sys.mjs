/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { RemoteSettings } from "resource://services-settings/remote-settings.sys.mjs";
import { clearTimeout, setTimeout } from "resource://gre/modules/Timer.sys.mjs";

const COLLECTION_NAME = "content-classifier-lists";

// Must match NS_CONTENT_CLASSIFIER_FILTER_LISTS_LOADED_TOPIC in
// nsIContentClassifierService.idl.
const LISTS_LOADED_TOPIC = "test-content-classifier-filter-lists-loaded";

const TESTING_PREF = "privacy.trackingprotection.content.testing";
const USE_TEST_ENGINES_PREF =
  "privacy.trackingprotection.content.testing.use_test_trackers_engines";
const BLOCK_LIST_URLS_PREF =
  "privacy.trackingprotection.content.protection.test_list_urls";
const ANNOTATE_LIST_URLS_PREF =
  "privacy.trackingprotection.content.annotation.test_list_urls";
const PROTECTION_ENABLED_PREF =
  "privacy.trackingprotection.content.protection.enabled";
const ANNOTATION_ENABLED_PREF =
  "privacy.trackingprotection.content.annotation.enabled";

// Hosts transcribed from UrlClassifierTestUtils.addTestTrackers(). The tracking
// table and the tracking annotation table become the test_block and
// test_annotate lists, which the service uses in place of the trackers engine
// while USE_TEST_ENGINES_PREF is set. The other sets become the Remote Settings
// list of the content classifier engine covering the same feature; list ids
// are the ones the engines declare in kFeatures (ContentClassifierService.cpp).
const TRACKING_HOSTS = [
  "tracking.example.com",
  "itisatracker.org",
  "trackertest.org",
];

const TRACKING_ANNOTATION_HOSTS = [
  "tracking.example.org",
  "itisatracker.org",
  "trackertest.org",
  "another-tracking.example.net",
  "tlsresumptiontest.example.org",
  "consent-manager.example.org",
  "anti-fraud.example.org",
];

const SOCIAL_TRACKER_HOSTS = ["social-tracking.example.org"];

const EMAIL_TRACKER_HOSTS = ["email-tracking.example.org"];

function blockRule(host) {
  return `||${host}^`;
}

function exceptionRule(host) {
  return `@@||${host}^`;
}

function testTrackerLists() {
  return [
    {
      id: "mozilla-social",
      rules: SOCIAL_TRACKER_HOSTS.map(blockRule),
    },
    {
      id: "disconnect-email-base",
      rules: EMAIL_TRACKER_HOSTS.map(blockRule),
    },
  ];
}

// Engines whose list is served to the service through a test_list_urls pref
// rather than Remote Settings, by engine name.
const TEST_ENGINE_LIST_URLS_PREFS = {
  test_block: BLOCK_LIST_URLS_PREF,
  test_annotate: ANNOTATE_LIST_URLS_PREF,
};

// The list each other engine is built from, by engine name; see kFeatures in
// ContentClassifierService.cpp.
const ENGINE_LIST_IDS = {
  "social-trackers": "mozilla-social",
  fingerprinters: "disconnect-fingerprinters-base",
  cryptominers: "disconnect-cryptominer-base",
  "email-trackers": "disconnect-email-base",
  "harmful-addon": "mozilla-harmful-addon",
};

// What addTestTrackers() and addTestHosts() have asked for so far. Every
// publish writes the union, because publishLists() replaces the collection.
let gTrackersRequested = false;
let gHostsByEngine = new Map();

// Hosts test_block and test_annotate must not classify, published as
// exception rules on their lists (see setSkipHosts).
let gSkipHostsByEngine = new Map();

// Whether publishRequestedLists() has written lists that cleanupTestTrackers()
// still has to take down.
let gPublished = false;

// The cleanupTestTrackers() still in flight. Callers rarely await it, so the
// next publish, possibly from the next test file, waits for it instead.
let gPendingCleanup = null;

function requestedHosts(engine) {
  let hosts = new Set(gHostsByEngine.get(engine));
  if (gTrackersRequested) {
    if (engine == "test_block") {
      TRACKING_HOSTS.forEach(host => hosts.add(host));
    } else if (engine == "test_annotate") {
      TRACKING_ANNOTATION_HOSTS.forEach(host => hosts.add(host));
    }
  }
  return hosts;
}

function requestedLists() {
  let rulesById = new Map();
  if (gTrackersRequested) {
    for (let { id, rules } of testTrackerLists()) {
      rulesById.set(id, [...rules]);
    }
  }
  for (let [engine, hosts] of gHostsByEngine) {
    let id = ENGINE_LIST_IDS[engine];
    if (!id) {
      continue;
    }
    let rules = rulesById.get(id) ?? [];
    for (let host of hosts) {
      let rule = blockRule(host);
      if (!rules.includes(rule)) {
        rules.push(rule);
      }
    }
    rulesById.set(id, rules);
  }
  return Array.from(rulesById, ([id, rules]) => ({ id, rules }));
}

// A data: URL serving the requested list of a test engine, or "" if it has no
// rules.
function requestedListURL(engine) {
  let rules = Array.from(requestedHosts(engine), blockRule);
  if (!rules.length) {
    return "";
  }
  rules.push(
    ...Array.from(gSkipHostsByEngine.get(engine) ?? [], exceptionRule)
  );
  return "data:text/plain," + encodeURIComponent(`${rules.join("\n")}\n`);
}

// Instantiate the service so that it observes the prefs below. Getting it
// throws while the content classifier is disabled, but the instance stays.
function ensureService() {
  try {
    Cc["@mozilla.org/content-classifier-service;1"].getService(
      Ci.nsIContentClassifierService
    );
  } catch (e) {}
}

// Change a pref the service rebuilds its engines on, and wait for the rebuild.
// The service rebuilds on a test_list_urls change even while it is disabled,
// but on the other prefs only while it is active. One change at a time, so
// that every rebuild settles before the next one starts.
async function setServicePref(
  pref,
  value,
  { rebuildsWhileDisabled = false } = {}
) {
  let current =
    typeof value == "boolean"
      ? Services.prefs.getBoolPref(pref, false)
      : Services.prefs.getStringPref(pref, "");
  if (current === value) {
    return;
  }
  let settled =
    Services.prefs.getBoolPref(TESTING_PREF, false) &&
    (rebuildsWhileDisabled || ContentClassifierTestUtils.isActive)
      ? ContentClassifierTestUtils.waitForListsSettled({ minNotifies: 1 })
      : null;
  if (typeof value == "boolean") {
    Services.prefs.setBoolPref(pref, value);
  } else {
    Services.prefs.setStringPref(pref, value);
  }
  await settled;
}

async function publishTestEngineLists() {
  ensureService();
  for (let [engine, pref] of Object.entries(TEST_ENGINE_LIST_URLS_PREFS)) {
    await setServicePref(pref, requestedListURL(engine), {
      rebuildsWhileDisabled: true,
    });
  }
  await setServicePref(USE_TEST_ENGINES_PREF, true);
}

async function unpublishTestEngineLists() {
  ensureService();
  await setServicePref(USE_TEST_ENGINES_PREF, false);
  for (let pref of Object.values(TEST_ENGINE_LIST_URLS_PREFS)) {
    await setServicePref(pref, "", { rebuildsWhileDisabled: true });
    Services.prefs.clearUserPref(pref);
  }
  Services.prefs.clearUserPref(USE_TEST_ENGINES_PREF);
}

async function unpublishLists() {
  await unpublishTestEngineLists();
  let client = RemoteSettings(COLLECTION_NAME);
  let records = await client.db.list();
  await client.db.importChanges({}, Date.now(), [], { clear: true });
  if (records.length && ContentClassifierTestUtils.isActive) {
    await ContentClassifierTestUtils.syncLists([], { deleted: records });
  }
  Services.prefs.clearUserPref(TESTING_PREF);
}

function sha256Hex(bytes) {
  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );
  hasher.init(Ci.nsICryptoHash.SHA256);
  hasher.update(bytes, bytes.length);
  return Array.from(hasher.finish(false), c =>
    c.charCodeAt(0).toString(16).padStart(2, "0")
  ).join("");
}

export var ContentClassifierTestUtils = {
  COLLECTION_NAME,
  LISTS_LOADED_TOPIC,

  /**
   * Whether the content classifier will be consulted for channel
   * classification, i.e. whether publishing lists can have any effect and a
   * rebuild notification can be expected.
   */
  get isActive() {
    return (
      Services.prefs.getBoolPref(PROTECTION_ENABLED_PREF, false) ||
      Services.prefs.getBoolPref(ANNOTATION_ENABLED_PREF, false)
    );
  },

  /**
   * Build a RemoteSettings record plus attachment blob for one filter list.
   *
   * @param {string} id - Record id.
   * @param {string} name - The record's Name field, which is what
   *   ContentClassifierRemoteSettingsClient resolves engine list ids against.
   * @param {string} content - Raw filter list text.
   */
  async makeListRecord(id, name, content) {
    let bytes = new TextEncoder().encode(content);
    return {
      record: {
        id,
        Name: name,
        last_modified: Date.now(),
        attachment: {
          hash: sha256Hex(bytes),
          size: bytes.length,
          filename: `${name}.txt`,
          location: `main-workspace/${COLLECTION_NAME}/${id}.txt`,
          mimetype: "text/plain",
        },
      },
      blob: new Blob([bytes]),
    };
  },

  /**
   * Replace the content classifier RemoteSettings collection with the given
   * lists, without notifying the service.
   *
   * @param {Array} lists - Entries of { id, name, rules } or
   *   { id, name, content }. `name` defaults to `id`.
   * @returns {Array} The records written, for passing to syncLists().
   */
  async publishLists(lists) {
    let client = RemoteSettings(COLLECTION_NAME);
    let records = [];
    let attachments = [];
    for (let { id, name, rules, content } of lists) {
      let made = await this.makeListRecord(
        id,
        name ?? id,
        content ?? `${rules.join("\n")}\n`
      );
      records.push(made.record);
      attachments.push(made);
    }
    await client.db.importChanges({}, Date.now(), records, { clear: true });
    for (let { record, blob } of attachments) {
      await client.db.saveAttachment(record.id, { record, blob });
    }
    return records;
  },

  /**
   * Wait until the filter lists have settled after a change. The C++ service
   * rebuilds engines off the main thread and fires LISTS_LOADED_TOPIC once a
   * rebuild completes.
   *
   * Key invariant we rely on: every change bumps a global generation counter
   * synchronously, and a rebuild only fires the topic if its generation is
   * still the latest when it finishes. So a batch of changes applied
   * synchronously (all the prefs in one pushPrefEnv, or one RS sync) leaves
   * exactly one un-superseded rebuild, i.e. exactly one notification, and it
   * carries the final state. That lets callers who make a single such batch
   * wait for exactly one notification and stop - no quiet-window drain, which
   * is pure dead time and dominated test runtime.
   *
   * The minNotifies gate is also what makes this robust under MOZ_CHAOSMODE,
   * where a rebuild's notification can arrive long after its trigger: we never
   * settle until the rebuild has actually reported in.
   *
   * @param {object} [options] - Settling options.
   * @param {number} [options.minNotifies] - How many notifications to wait for
   *   before settling. A guaranteed-rebuild batch (an RS sync, a real
   *   engines-pref change) uses 1; best-effort drains, where zero rebuilds is a
   *   legitimate outcome, use 0.
   * @param {number} [options.quietMs] - 0 settles as soon as minNotifies is
   *   reached. A positive value instead waits for that long a gap with no
   *   further notification - only needed when a single call may legitimately
   *   produce several rebuilds whose order matters and must all be drained
   *   (see test_rs_back_to_back).
   * @param {number} [options.timeoutMs] - Deadlock backstop only: if an
   *   expected notification never arrives we resolve anyway rather than hang.
   *
   * The service only notifies while TESTING_PREF is set, so this also settles
   * as soon as TESTING_PREF is turned off, as the test harness does when it
   * resets the prefs a test left changed.
   */
  async waitForListsSettled({
    quietMs = 0,
    minNotifies = 1,
    timeoutMs = 30000,
  } = {}) {
    return new Promise(resolve => {
      let quietTimer;
      let deadlineTimer;
      let notifies = 0;

      let observer = () => {
        notifies++;
        maybeSettle();
      };

      let testingPrefObserver = () => {
        if (!Services.prefs.getBoolPref(TESTING_PREF, false)) {
          finish();
        }
      };

      function finish() {
        clearTimeout(quietTimer);
        clearTimeout(deadlineTimer);
        Services.obs.removeObserver(observer, LISTS_LOADED_TOPIC);
        Services.prefs.removeObserver(TESTING_PREF, testingPrefObserver);
        resolve();
      }

      function maybeSettle() {
        if (notifies < minNotifies) {
          return;
        }
        if (!quietMs) {
          finish();
          return;
        }
        clearTimeout(quietTimer);
        quietTimer = setTimeout(finish, quietMs);
      }

      Services.obs.addObserver(observer, LISTS_LOADED_TOPIC);
      Services.prefs.addObserver(TESTING_PREF, testingPrefObserver);
      maybeSettle();
      deadlineTimer = setTimeout(() => {
        if (notifies < minNotifies) {
          console.warn(
            `waitForListsSettled: timed out after ${timeoutMs}ms with ` +
              `${notifies}/${minNotifies} notification(s); resolving anyway`
          );
        }
        finish();
      }, timeoutMs);
    });
  },

  /**
   * Notify the service of already-published records and wait for the
   * resulting engine rebuild.
   *
   * @param {Array} records - Records as returned by publishLists().
   * @param {object} [options] - Sync options.
   * @param {Array} [options.deleted] - Records removed from the collection.
   */
  async syncLists(records, { deleted = [] } = {}) {
    let client = RemoteSettings(COLLECTION_NAME);
    let settled = Services.prefs.getBoolPref(TESTING_PREF, false)
      ? this.waitForListsSettled({ minNotifies: 1 })
      : null;
    await client.emit("sync", {
      data: { created: records, updated: [], deleted },
    });
    await settled;
  },

  /**
   * Publish content classifier lists equivalent to the Safe Browsing tables
   * UrlClassifierTestUtils.addTestTrackers() installs, so that tests asserting
   * on tracker blocking and annotation behave the same under either engine.
   * Hosts added through addTestHosts() stay published alongside.
   *
   * A no-op beyond populating the RemoteSettings database when the content
   * classifier is inactive, since nothing is listening for a rebuild.
   */
  async addTestTrackers() {
    gTrackersRequested = true;
    await this.publishRequestedLists();
  },

  /**
   * Publish block rules for the given hosts on the named engines' lists, on
   * top of anything addTestTrackers() published. This stands in for the
   * per-feature host prefs url-classifier reads. test_block and test_annotate
   * take url-classifier's tracking protection and tracking annotation hosts;
   * every other engine has one list serving both passes.
   *
   * @param {{[engine: string]: string[]}} hostsByEngine - Hosts keyed by
   *   engine name (see TEST_ENGINE_LIST_URLS_PREFS and ENGINE_LIST_IDS).
   */
  async addTestHosts(hostsByEngine) {
    for (let [engine, hosts] of Object.entries(hostsByEngine)) {
      if (
        !(engine in ENGINE_LIST_IDS) &&
        !(engine in TEST_ENGINE_LIST_URLS_PREFS)
      ) {
        throw new Error(`Unknown content classifier engine "${engine}"`);
      }
      let known = gHostsByEngine.get(engine) ?? new Set();
      hosts.forEach(host => known.add(host));
      gHostsByEngine.set(engine, known);
    }
    await this.publishRequestedLists();
  },

  /**
   * Hosts test_block and test_annotate must leave alone, standing in for
   * url-classifier's skip URL prefs for tracking protection and tracking
   * annotation. They become exception rules on those lists. Takes effect on
   * the next publish.
   *
   * @param {string[]} blockHosts - Hosts test_block skips.
   * @param {string[]} annotateHosts - Hosts test_annotate skips.
   */
  setSkipHosts(blockHosts, annotateHosts) {
    gSkipHostsByEngine = new Map([
      ["test_block", new Set(blockHosts)],
      ["test_annotate", new Set(annotateHosts)],
    ]);
  },

  async publishRequestedLists() {
    await gPendingCleanup;
    gPublished = true;
    Services.prefs.setBoolPref(TESTING_PREF, true);
    let records = await this.publishLists(requestedLists());
    if (this.isActive) {
      await this.syncLists(records);
    }
    await publishTestEngineLists();
  },

  /**
   * Drop everything addTestTrackers() and addTestHosts() published, and wait
   * for the service to remove the engines built from it.
   */
  cleanupTestTrackers() {
    gTrackersRequested = false;
    gHostsByEngine.clear();
    gSkipHostsByEngine.clear();
    if (gPublished) {
      gPublished = false;
      let previous = gPendingCleanup;
      let cleanup = (async () => {
        try {
          await previous;
          await unpublishLists();
        } finally {
          if (gPendingCleanup === cleanup) {
            gPendingCleanup = null;
          }
        }
      })();
      gPendingCleanup = cleanup;
    }
    return gPendingCleanup ?? Promise.resolve();
  },
};

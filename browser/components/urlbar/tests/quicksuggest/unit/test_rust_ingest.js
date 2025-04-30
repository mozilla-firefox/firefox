/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests ingest in the Rust backend.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  InterruptKind: "resource://gre/modules/RustSuggest.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  SuggestIngestionMetrics: "resource://gre/modules/RustSuggest.sys.mjs",
  SuggestionProvider: "resource://gre/modules/RustSuggest.sys.mjs",
});

// These consts are copied from the update timer manager test. See
// `initUpdateTimerManager()`.
const PREF_APP_UPDATE_TIMERMINIMUMDELAY = "app.update.timerMinimumDelay";
const PREF_APP_UPDATE_TIMERFIRSTINTERVAL = "app.update.timerFirstInterval";
const MAIN_TIMER_INTERVAL = 1000; // milliseconds
const CATEGORY_UPDATE_TIMER = "update-timer";

const REMOTE_SETTINGS_SUGGESTION = QuickSuggestTestUtils.ampRemoteSettings();

add_setup(async function () {
  initUpdateTimerManager();

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: [
      {
        type: "data",
        attachment: [REMOTE_SETTINGS_SUGGESTION],
      },
    ],
    prefs: [
      ["suggest.quicksuggest.sponsored", true],
      ["suggest.quicksuggest.nonsponsored", true],
    ],
  });
});

// The backend should ingest when it's disabled and then re-enabled.
add_task(async function disableEnable() {
  Assert.strictEqual(
    UrlbarPrefs.get("quicksuggest.rustEnabled"),
    true,
    "Sanity check: Rust pref is initially true"
  );
  Assert.strictEqual(
    QuickSuggest.rustBackend.isEnabled,
    true,
    "Sanity check: Rust backend is initially enabled"
  );

  let enabledTypes = QuickSuggest.rustBackend._test_enabledSuggestionTypes;
  Assert.greater(
    enabledTypes.length,
    0,
    "This test expects some Rust suggestion types to be enabled"
  );

  UrlbarPrefs.set("quicksuggest.rustEnabled", false);
  UrlbarPrefs.set("quicksuggest.rustEnabled", true);

  // `ingest()` must be stubbed only after re-enabling the backend since the
  // `SuggestStore` is recreated then.
  await withIngestStub(async ({ stub, rustBackend }) => {
    info("Awaiting ingest promise");
    await rustBackend.ingestPromise;

    checkIngestCounts({
      stub,
      expected: Object.fromEntries(
        enabledTypes.map(({ provider }) => [provider, 1])
      ),
    });
  });
});

// For a feature whose suggestion type provider has constraints, ingest should
// happen when the constraints change.
// TODO Bug 1961040: Convert ExposureSuggestions to DynamicSuggestions.
add_task({ skip_if: () => true }, async function providerConstraintsChanged() {
  // We'll use the Exposure feature since it has provider constraints. Make sure
  // it exists.
  let feature = QuickSuggest.getFeature("ExposureSuggestions");
  Assert.ok(
    !!feature,
    "This test expects the ExposureSuggestions feature to exist"
  );
  Assert.equal(
    feature.rustSuggestionType,
    "Exposure",
    "This test expects Exposure suggestions to exist"
  );

  let providersFilter = [SuggestionProvider.EXPOSURE];
  await withIngestStub(async ({ stub, rustBackend }) => {
    // Set the pref to a few non-empty string values. Each time, an exposure
    // ingest should be triggered.
    for (let type of ["aaa", "bbb", "aaa,bbb"]) {
      UrlbarPrefs.set("quicksuggest.exposureSuggestionTypes", type);
      info("Awaiting ingest promise after setting exposureSuggestionTypes");
      await rustBackend.ingestPromise;

      checkIngestCounts({
        stub,
        providersFilter,
        expected: {
          [SuggestionProvider.EXPOSURE]: 1,
        },
      });
    }

    // Set the pref to an empty string. The feature should become disabled and
    // it shouldn't trigger ingest since no exposure suggestions are enabled.
    UrlbarPrefs.set("quicksuggest.exposureSuggestionTypes", "");
    info(
      "Awaiting ingest promise after setting exposureSuggestionTypes to empty string"
    );
    await rustBackend.ingestPromise;

    Assert.ok(
      !feature.isEnabled,
      "Exposure feature should be disabled after setting exposureSuggestionTypes to empty string"
    );
    checkIngestCounts({
      stub,
      providersFilter,
      expected: {},
    });
  });

  UrlbarPrefs.clear("quicksuggest.exposureSuggestionTypes");
  await QuickSuggest.rustBackend.ingestPromise;
});

// Ingestion should be performed according to the defined interval.
add_task(async function interval() {
  // Re-enable the backend with a small ingest interval. A new ingest will
  // immediately start.
  let intervalSecs = 3;
  UrlbarPrefs.set("quicksuggest.rustIngestIntervalSeconds", intervalSecs);
  UrlbarPrefs.set("quicksuggest.rustEnabled", false);
  UrlbarPrefs.set("quicksuggest.rustEnabled", true);

  info("Awaiting initial ingest promise");
  let { ingestPromise } = QuickSuggest.rustBackend;
  await ingestPromise;

  let enabledTypes = QuickSuggest.rustBackend._test_enabledSuggestionTypes;
  Assert.greater(
    enabledTypes.length,
    0,
    "This test expects some Rust suggestion types to be enabled"
  );

  await withIngestStub(async ({ stub }) => {
    // Wait for a few ingests to happen due to the timer firing.
    for (let i = 0; i < 3; i++) {
      info(`Waiting ${intervalSecs}s for ingest to start at index ${i}`);
      ({ ingestPromise } = await waitForIngestStart(ingestPromise));
      info("Waiting for ingest to finish at index " + i);
      await ingestPromise;
      info("Ingest finished at index " + i);

      checkIngestCounts({
        stub,
        expected: Object.fromEntries(
          enabledTypes.map(({ provider }) => [provider, 1])
        ),
      });
    }
  });

  info("Disabling the backend");
  UrlbarPrefs.set("quicksuggest.rustEnabled", false);

  // At this point, ingests should stop with two caveats. (1) There may be one
  // ongoing ingest that started immediately after `ingestPromise` resolved in
  // the final iteration of the loop above. (2) The timer manager sometimes
  // fires our ingest timer even after it was unregistered by the backend (when
  // the backend was disabled), maybe because the interval is so small in this
  // test. These two things mean that up to two more ingests may finish now.
  // We'll simply wait for a few seconds up to two times until no new ingests
  // start.

  let waitSecs = 2 * intervalSecs;
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  let wait = () => new Promise(r => setTimeout(r, 1000 * waitSecs));

  let waitedAtEndOfLoop = false;
  for (let i = 0; i < 2; i++) {
    info(`Waiting ${waitSecs}s after disabling backend, i=${i}...`);
    await wait();

    let { ingestPromise: newIngestPromise } = QuickSuggest.rustBackend;
    if (ingestPromise == newIngestPromise) {
      info(`No new ingest started, i=${i}`);
      waitedAtEndOfLoop = true;
      break;
    }

    info(`New ingest started, now awaiting, i=${i}`);
    ingestPromise = newIngestPromise;
    await ingestPromise;
  }

  if (!waitedAtEndOfLoop) {
    info(`Waiting a final ${waitSecs}s...`);
    await wait();
  }

  // No new ingests should have started.
  Assert.equal(
    QuickSuggest.rustBackend.ingestPromise,
    ingestPromise,
    "No new ingest started after disabling the backend"
  );

  // Clean up for later tasks: Reset the interval and enable the backend again.
  UrlbarPrefs.clear("quicksuggest.rustIngestIntervalSeconds");
  UrlbarPrefs.set("quicksuggest.rustEnabled", true);

  info("Awaiting cleanup ingest promise");
  await QuickSuggest.rustBackend.ingestPromise;
  info("Done awaiting cleanup ingest promise");
});

// `SuggestStore.interrupt()` should be called on shutdown.
add_task(async function shutdown() {
  let sandbox = sinon.createSandbox();
  let spy = sandbox.spy(QuickSuggest.rustBackend._test_store, "interrupt");

  Services.prefs.setBoolPref("toolkit.asyncshutdown.testing", true);
  AsyncShutdown.profileChangeTeardown._trigger();

  let calls = spy.getCalls();
  Assert.equal(
    calls.length,
    1,
    "store.interrupt() should have been called once on simulated shutdown"
  );
  Assert.deepEqual(
    calls[0].args,
    [InterruptKind.READ_WRITE],
    "store.interrupt() should have been called with InterruptKind.READ_WRITE"
  );
  Assert.ok(
    InterruptKind.READ_WRITE,
    "Sanity check: InterruptKind.READ_WRITE is defined"
  );

  Services.prefs.clearUserPref("toolkit.asyncshutdown.testing");
  sandbox.restore();
});

/**
 * Stubs `SuggestStore.ingest()` and calls your callback.
 *
 * @param {Function} callback
 *   Callback
 */
async function withIngestStub(callback) {
  let sandbox = sinon.createSandbox();
  let { rustBackend } = QuickSuggest;
  let stub = sandbox.stub(rustBackend._test_store, "ingest");

  // `ingest()` returns a `SuggestIngestionMetrics` object.
  stub.returns(
    new SuggestIngestionMetrics({ ingestionTimes: [], downloadTimes: [] })
  );

  await callback({ stub, rustBackend });
  sandbox.restore();
}

/**
 * Gets `ingest()` call counts per Rust suggestion provider. Also resets the
 * call counts before returning.
 *
 * @param {stub} stub
 *   Sinon `ingest()` stub.
 * @param {Array} providersFilter
 *   Array of provider integers to filter in. If null, ingest counts from all
 *   providers will be returned.
 * @returns {object}
 *   An plain JS object that maps provider integers to ingest counts.
 */
function getIngestCounts(stub, providersFilter = null) {
  let countsByProvider = {};
  for (let call of stub.getCalls()) {
    let ingestConstraints = call.args[0];
    for (let p of ingestConstraints.providers) {
      if (!providersFilter || providersFilter.includes(p)) {
        if (!countsByProvider.hasOwnProperty(p)) {
          countsByProvider[p] = 0;
        }
        countsByProvider[p]++;
      }
    }
  }

  info("Got ingest counts: " + JSON.stringify(countsByProvider));

  stub.resetHistory();
  return countsByProvider;
}

function checkIngestCounts({ stub, providersFilter, expected }) {
  Assert.deepEqual(
    getIngestCounts(stub, providersFilter),
    expected,
    "Actual ingest counts should match expected counts"
  );
}

async function waitForIngestStart(oldIngestPromise) {
  let newIngestPromise;
  await TestUtils.waitForCondition(() => {
    let { ingestPromise } = QuickSuggest.rustBackend;
    if (
      (oldIngestPromise && ingestPromise != oldIngestPromise) ||
      (!oldIngestPromise && ingestPromise)
    ) {
      newIngestPromise = ingestPromise;
      return true;
    }
    return false;
  }, "Waiting for a new ingest to start");

  Assert.equal(
    QuickSuggest.rustBackend.ingestPromise,
    newIngestPromise,
    "Sanity check: ingestPromise hasn't changed since waitForCondition returned"
  );

  // A bare promise can't be returned because it will cause the awaiting caller
  // to await that promise! We're simply trying to return the promise, which the
  // caller can later await.
  return { ingestPromise: newIngestPromise };
}

/**
 * Sets up the update timer manager for testing: makes it fire more often,
 * removes all existing timers, and initializes it for testing. The body of this
 * function is copied from:
 * https://searchfox.org/mozilla-central/source/toolkit/components/timermanager/tests/unit/consumerNotifications.js
 */
function initUpdateTimerManager() {
  // Set the timer to fire every second
  Services.prefs.setIntPref(
    PREF_APP_UPDATE_TIMERMINIMUMDELAY,
    MAIN_TIMER_INTERVAL / 1000
  );
  Services.prefs.setIntPref(
    PREF_APP_UPDATE_TIMERFIRSTINTERVAL,
    MAIN_TIMER_INTERVAL
  );

  // Remove existing update timers to prevent them from being notified
  for (let { data: entry } of Services.catMan.enumerateCategory(
    CATEGORY_UPDATE_TIMER
  )) {
    Services.catMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, entry, false);
  }

  Cc["@mozilla.org/updates/timer-manager;1"]
    .getService(Ci.nsIUpdateTimerManager)
    .QueryInterface(Ci.nsIObserver)
    .observe(null, "utm-test-init", "");
}

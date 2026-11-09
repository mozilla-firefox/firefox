"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const ALL_CHANNELS = Ci.nsITelemetry.DATASET_ALL_CHANNELS;

const PAGELOAD_BASE = "https://example.com/browser/dom/base/test/";
const SLOW_DOCUMENT_URL = PAGELOAD_BASE + "file_slow_document.sjs";

// Untouched by every other task here, so its connection is established after
// the address space override below is in place. Reusing a socket opened
// earlier would carry the security info from that earlier handshake.
const DOMAIN_EVENT_URL = "https://example.org/browser/dom/base/test/empty.html";

// Which of page_load and page_load_domain a load produces is a random sample,
// and they carry different extra keys, so force the former for the whole file.
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.pageload_event.testing.always_send_normal", true]],
  });
});

// The pageload ping is submitted every ten events, which takes them out of the
// store, and where that falls depends on everything loaded earlier in the
// session. Keep what each submission takes, so that a task sees all of its own
// events regardless.
let gSubmittedPageloadEvents = [];

function keepSubmittedPageloadEvents() {
  GleanPings.pageload.testBeforeNextSubmit(() => {
    gSubmittedPageloadEvents.push(
      ...(Glean.perf.pageLoad.testGetValue() ?? [])
    );
    // Only once this submission is done with its callback.
    Promise.resolve().then(keepSubmittedPageloadEvents);
  });
}

async function resetPageloadTelemetry() {
  // Drain anything a previous task left pending in a content process, so the
  // reset actually clears it.
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  gSubmittedPageloadEvents = [];
  keepSubmittedPageloadEvents();
}

function collectedPageloadEvents() {
  return [
    ...gSubmittedPageloadEvents,
    ...(Glean.perf.pageLoad.testGetValue() ?? []),
  ];
}

// The content process sends the event and the parent process records it an IPC
// hop later, so it is not visible the moment a navigation resolves.
async function waitForPageloadEvents(count) {
  await TestUtils.waitForCondition(
    () => collectedPageloadEvents().length >= count,
    `Waiting for ${count} pageload event(s).`
  );
  return collectedPageloadEvents();
}

// A load that runs entirely in the background is still reported, but is flagged
// so that it can be told apart, and is left out of the timing histograms.
add_task(async function test_background_load_is_reported_and_flagged() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }

  let first = PAGELOAD_BASE + "empty.html";
  let second = PAGELOAD_BASE + "dummy.html";

  // The first document has to reach the point it is reported from, which is
  // its destruction. Left in the bfcache it would outlive the assertion below.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionhistory.max_total_viewers", 0]],
  });

  await resetPageloadTelemetry();

  // Load and hide the document in a tab that is never selected. Clearing
  // docShellIsActive on the selected tab instead is not reliable, since the tab
  // switcher sets it back.
  let tab = BrowserTestUtils.addTab(gBrowser, first);
  let browser = tab.linkedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, first);

  BrowserTestUtils.startLoadingURIString(browser, second);
  await BrowserTestUtils.browserLoaded(browser, false, second);

  let events = await waitForPageloadEvents(1);
  Assert.equal(
    events[0].extra.loaded_in_foreground,
    "false",
    "The background load was reported, flagged as not foreground."
  );

  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.performancePageload.loadTime.testGetValue(),
    null,
    "The background load was left out of the load time histogram."
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// A navigation can start before its docshell is foregrounded - on Android the
// embedder marks the session active after kicking off the load. Such a load
// still runs in the foreground, so it must not be excluded from pageload
// telemetry.
add_task(async function test_navigation_started_while_backgrounded() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    waitForLoad: true,
  });
  let browser = tab.linkedBrowser;

  await resetPageloadTelemetry();
  Assert.equal(
    Glean.performancePageload.loadTime.testGetValue(),
    null,
    "No load time samples before the test load."
  );

  // Start the navigation while the docshell is inactive.
  browser.docShellIsActive = false;
  let loaded = BrowserTestUtils.browserLoaded(
    browser,
    false,
    SLOW_DOCUMENT_URL
  );
  BrowserTestUtils.startLoadingURIString(browser, SLOW_DOCUMENT_URL);

  // Foreground it again. The document is served after a delay, so this happens
  // well before the load event fires.
  browser.docShellIsActive = true;
  await loaded;
  await Services.fog.testFlushAllChildren();

  Assert.notEqual(
    Glean.performancePageload.loadTime.testGetValue(),
    null,
    "A load that started while backgrounded and was foregrounded before the " +
      "load event should still be measured."
  );

  BrowserTestUtils.startLoadingURIString(browser, PAGELOAD_BASE + "empty.html");
  await BrowserTestUtils.browserLoaded(
    browser,
    false,
    PAGELOAD_BASE + "empty.html"
  );

  let events = await waitForPageloadEvents(1);
  Assert.equal(
    events[0].extra.loaded_in_foreground,
    "true",
    "It counts as a foreground load, since it was foreground at the load event."
  );

  BrowserTestUtils.removeTab(tab);
});

// The flag rides on page_load_domain too, which SendAsPageLoadDomainEvent
// assembles by hand rather than through the metric macro, so it can be dropped
// there without anything else noticing.
//
// Reaching that path takes more than the pref. MaybeSetPublicRegistrableDomain
// wants a chain rooted in a built-in certificate and a host in the public
// address space, and mochitest has neither: its authority is trusted only
// through the profile's database, and every host is reached through a proxy
// on 127.0.0.1. Both have test overrides, but the built-in root one is
// debug-only, as is always_send_domain itself, so this cannot run on opt.
//
// The transport is not observable from here. Automation sets
// telemetry.fog.test.localhost_port, and the uploader fakes success before it
// ever decides whether to wrap the ping in OHTTP.
add_task(async function test_domain_event_carries_foreground_flag() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }
  if (!AppConstants.DEBUG) {
    Assert.ok(true, "The prefs this needs only exist in debug.");
    return;
  }

  // Any mochitest host will do to find the root, since one authority issues
  // all of them. It must not be the host measured below, whose handshake has
  // to happen with the override already in place.
  let seed = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: PAGELOAD_BASE + "empty.html",
  });
  let chain = seed.linkedBrowser.securityUI.secInfo.succeededCertChain;

  await resetPageloadTelemetry();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.pageload_event.testing.always_send_domain", true],
      [
        "security.test.built_in_root_hash",
        chain[chain.length - 1].sha256Fingerprint,
      ],
      // A built-in root is held to Certificate Transparency, which the test
      // certificates cannot meet, so their chains would be rejected outright.
      ["security.pki.certificate_transparency.mode", 0],
      ["network.lna.enabled", true],
      // Every https host is reached through the harness's ssltunnel proxy,
      // listening on mozprofile's default https port rather than on 443.
      ["network.lna.address_space.public.override", "127.0.0.1:4443"],
    ],
  });

  // The prefs above turn every later load into a domain event, and an open tab
  // derails the tasks after this one, so undo both however this ends.
  let tab;
  try {
    tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: DOMAIN_EVENT_URL,
    });

    // A domain event submits its own ping as soon as it is recorded, and
    // submitting clears the store, so read it from the submission rather than
    // racing it.
    await GleanPings.pageloadBaseDomain.testSubmission(
      reason => {
        Assert.equal(reason, "pageload");
        let events = Glean.perf.pageLoadDomain.testGetValue();
        Assert.equal(
          events.length,
          1,
          "Domain events are sent one to a ping, for privacy."
        );
        Assert.equal(
          events[0].extra.domain,
          "example.org",
          "The domain event carries the etld+1."
        );
        Assert.equal(
          events[0].extra.loaded_in_foreground,
          "true",
          "It carries the foreground flag too."
        );
      },
      async () => {
        await BrowserTestUtils.removeTab(tab);
      },
      // Not a duration to wait for, but a deadline: the promise rejects once it
      // passes. Closing the tab only starts the work - the event crosses to the
      // parent, a history query resolves is_first_daily_load, and only then is
      // the ping submitted, on the idle queue. Generous, since this runs on
      // debug builds alone and only costs time when it is already failing.
      30000
    );
  } finally {
    if (tab && gBrowser.tabs.includes(tab)) {
      BrowserTestUtils.removeTab(tab);
    }
    BrowserTestUtils.removeTab(seed);
    await SpecialPowers.popPrefEnv();
  }
});

// A document is hidden every time it is navigated away from, including after
// being restored from the bfcache, but it must only be reported once.
add_task(async function test_repeated_page_hides_report_once() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }

  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionhistory.max_total_viewers", 10]],
  });

  let first = PAGELOAD_BASE + "empty.html";
  let second = PAGELOAD_BASE + "dummy.html";
  let third = PAGELOAD_BASE + "empty.html?third";
  let fourth = PAGELOAD_BASE + "empty.html?fourth";

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    waitForLoad: true,
  });
  let browser = tab.linkedBrowser;

  await resetPageloadTelemetry();

  BrowserTestUtils.startLoadingURIString(browser, first);
  await BrowserTestUtils.browserLoaded(browser, false, first);

  // Hiding the first document reports it.
  BrowserTestUtils.startLoadingURIString(browser, second);
  await BrowserTestUtils.browserLoaded(browser, false, second);
  await waitForPageloadEvents(1);

  // Going back restores it from the bfcache rather than loading it again, and
  // hides the second document, which reports.
  let restored = BrowserTestUtils.waitForContentEvent(
    browser,
    "pageshow",
    true,
    event => event.persisted
  );
  browser.goBack();
  await restored;
  await waitForPageloadEvents(2);

  // Hide the restored document a second time, then hide the third document so
  // that there is something to wait for. The restored document is hidden first,
  // so by the time the third has reported, a second report of the first would
  // already have arrived.
  BrowserTestUtils.startLoadingURIString(browser, third);
  await BrowserTestUtils.browserLoaded(browser, false, third);
  BrowserTestUtils.startLoadingURIString(browser, fourth);
  await BrowserTestUtils.browserLoaded(browser, false, fourth);

  // A second report of the first document would have been sent before the third
  // document's, and pageload events are ordered on PContent, so a round trip to
  // the content process leaves nothing in flight to arrive later.
  await waitForPageloadEvents(3);
  await SpecialPowers.spawn(browser, [], () => true);

  Assert.equal(
    collectedPageloadEvents().length,
    3,
    "The second page hide of the first document did not report it again."
  );

  // Closing the tab hides the fourth document, which reports, and destroys it
  // along with those left in the bfcache, all of which already have.
  BrowserTestUtils.removeTab(tab);
  await waitForPageloadEvents(4);
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    collectedPageloadEvents().length,
    4,
    "Destroying documents that were already hidden did not report them again."
  );

  await SpecialPowers.popPrefEnv();
});

const REDIRECT_BEFORE_LOAD_URL =
  PAGELOAD_BASE + "file_redirect_before_load.html";

// The flag describes the load, so foregrounding a tab after a background load
// has completed must not turn it into a foreground load.
add_task(async function test_background_load_foregrounded_before_hide() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }

  let background = PAGELOAD_BASE + "empty.html";
  let foreground = PAGELOAD_BASE + "dummy.html";

  await resetPageloadTelemetry();

  // Load a document entirely in the background, in a tab that is not selected,
  // so its load event fires while the tab is hidden.
  let tab = BrowserTestUtils.addTab(gBrowser, background);
  let browser = tab.linkedBrowser;
  await BrowserTestUtils.browserLoaded(browser, false, background);

  // Select the tab and navigate, hiding the background document. Collecting it
  // at that point would describe it as a foreground load.
  await BrowserTestUtils.switchTab(gBrowser, tab);
  BrowserTestUtils.startLoadingURIString(browser, foreground);
  await BrowserTestUtils.browserLoaded(browser, false, foreground);

  let events = await waitForPageloadEvents(1);
  Assert.equal(
    events[0].extra.loaded_in_foreground,
    "false",
    "The background load is still flagged as not foreground."
  );

  BrowserTestUtils.removeTab(tab);
});

// A document that navigates itself away before its load event fires has no load
// time, but its navigation, network and paint timings are still valid, so it
// should be reported rather than dropped.
add_task(async function test_navigation_before_load_event() {
  if (Services.prefs.getBoolPref("telemetry.fog.artifact_build", false)) {
    Assert.ok(true, "Test skipped in artifact builds. See bug 1836686.");
    return;
  }

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    waitForLoad: true,
  });
  let browser = tab.linkedBrowser;

  await resetPageloadTelemetry();

  let replaced = BrowserTestUtils.browserLoaded(browser, false, url =>
    url.endsWith("empty.html")
  );
  BrowserTestUtils.startLoadingURIString(browser, REDIRECT_BEFORE_LOAD_URL);
  await replaced;

  // The replaced document reports when it is hidden, which happens before the
  // replacement finishes loading. The replacement has not been hidden yet.
  await waitForPageloadEvents(1);
  let incomplete = collectedPageloadEvents().filter(
    event => event.extra.load_time === undefined
  );
  Assert.equal(
    incomplete.length,
    1,
    "The document replaced before its load event should be reported, without " +
      "a load time, since its load event never fired."
  );
  Assert.ok(
    "response_time" in incomplete[0].extra,
    "It should still carry the response timing it did have."
  );
  Assert.equal(
    incomplete[0].extra.lcp_time,
    undefined,
    "It should carry no LCP."
  );
  Assert.ok(
    !("loaded_in_foreground" in incomplete[0].extra),
    "It carries no foreground flag, since that is judged at the load event."
  );

  BrowserTestUtils.removeTab(tab);

  // This is the only task that produces an event with no load_time. Clear it,
  // rather than leaving later tasks to cope with it.
  await resetPageloadTelemetry();
});

add_task(async function () {
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    waitForLoad: true,
  });

  let browser = tab.linkedBrowser;

  // Reset event counts.
  Services.telemetry.clearEvents();
  TelemetryTestUtils.assertNumberOfEvents(0);

  // Check for pageload ping and pageload event
  await GleanPings.pageload.testSubmission(
    reason => {
      Assert.equal(reason, "threshold");
      let record = Glean.perf.pageLoad.testGetValue();

      // Ensure the events in the pageload ping are reasonable.
      record.forEach(entry => {
        Assert.equal(entry.name, "page_load");
        Assert.greater(parseInt(entry.extra.load_time), 0);
        Assert.ok(
          "is_active_client" in entry.extra,
          "Active client field should be recorded."
        );
      });
    },
    async () => {
      // Perform page load 30 times to trigger the ping being sent
      for (let i = 0; i < 30; i++) {
        BrowserTestUtils.startLoadingURIString(browser, "https://example.com");
        await BrowserTestUtils.browserLoaded(browser);
      }
      BrowserTestUtils.removeTab(tab);
    },
    // The ping itself is submitted via idle dispatch, so we need to specify a
    // timeout.
    1000
  );
});

/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Bug 2063091: the CTA only helps on a page the user is looking at. A
// dnsNotFound error in a frame gets the standard page and records
// not_top_level, decided before we read the URL, so it never gets a URL reason.

const CTA_PREF = "browser.netError.searchCTA.enabled";
// A keyword-rich path, so this URL would reach keywords_found at top level,
// which is what makes the frame checks below mean something. _telemetry.js
// pins that top-level behaviour with the same path.
const FAILED_URL =
  "https://www.wildernessgear-frame-cta.com/best-hiking-boots/reviews";

add_setup(async function () {
  stubSearchCTASupportedEngine();
  await SearchTestUtils.installSearchExtension(
    {
      name: "MozSearchCTAFrame",
      search_url: "https://example.com/",
      search_url_get_params: "q={searchTerms}",
    },
    { setAsDefault: true }
  );
  await SpecialPowers.pushPrefEnv({
    set: [
      [CTA_PREF, true],
      // Treat the connectivity reading as always fresh so the bug 2055712 guard
      // is a no-op and the decision is deterministic.
      ["browser.netError.searchCTA.connectivityFreshnessMs", 2147483647],
    ],
  });
});

const reason = label =>
  Glean.securityUiNeterror.searchCtaReason[label].testGetValue();
const action = label =>
  Glean.securityUiNeterror.searchCtaAction[label].testGetValue();
const exitEvents = () =>
  Glean.securityUiNeterror.searchCtaExit.testGetValue() ?? [];

// Tab closes in this and other searchCTA files can leave exit events buffered
// in content. Ship those to the parent, then clear, so each task starts from a
// known-empty state rather than inheriting its predecessor's.
async function drainAndReset() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
}

/**
 * The parent records its decision before content resolves, so a settled card
 * means the counters are already in. The flush then makes content-side metrics
 * readable here too.
 *
 * @param {MozBrowser|BrowsingContext} target The error page's browser, or the
 *   BrowsingContext of the frame showing it.
 */
async function waitForCtaResolved(target) {
  await waitForSettledNetErrorCard(target);
  await Services.fog.testFlushAllChildren();
}

/**
 * Fire pagehide by navigating the error page away, then flush while its process
 * is still alive: a closing tab's process drops out of the list
 * testFlushAllChildren() walks (bug 1843178).
 *
 * @param {MozBrowser} browser The browser hosting the error page.
 * @param {BrowsingContext} [target] The frame to navigate, when the error page
 *   is embedded. Defaults to the top-level document.
 */
async function navigateAwayAndFlush(browser, target = browser) {
  const loaded = BrowserTestUtils.browserLoaded(
    browser,
    target !== browser,
    url => url === "about:blank"
  );
  await SpecialPowers.spawn(target, [], () => {
    content.location = "about:blank";
  });
  await loaded;
  await Services.fog.testFlushAllChildren();
}

add_task(async function test_frameGetsTheStandardPage() {
  await drainAndReset();
  const { tab, frame } = await loadDnsNotFoundFrame(FAILED_URL);
  await waitForCtaResolved(frame);

  await SpecialPowers.spawn(frame, [], async () => {
    const card =
      content.document.querySelector("net-error-card").wrappedJSObject;

    // Confirm which document this is before concluding anything from something
    // being absent from it.
    is(card.resolvedErrorId, "dnsNotFound", "The frame shows dnsNotFound");
    Assert.notStrictEqual(
      content.parent,
      content,
      "The error document is embedded"
    );

    ok(
      card.isSearchCTAEligible(),
      "The load is otherwise eligible, so framing is the only suppressor"
    );
    ok(!card.shouldShowSearchCTA(), "The CTA layout is suppressed in a frame");
    ok(!card.searchCTAButton, "No Search button in a frame");
    ok(
      !card.shadowRoot.querySelector(
        '[data-l10n-id="neterror-search-cta-title"]'
      ),
      "No CTA heading in a frame"
    );

    // The fallback has to be the ordinary dnsNotFound page, not an empty card.
    ok(
      card.shadowRoot.querySelector(".custom-net-error-card"),
      "The standard dnsNotFound card renders instead"
    );
    ok(card.tryAgainButton, "The standard Try Again button is present");
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_frameRecordsNotTopLevel() {
  await drainAndReset();
  const { tab, frame } = await loadDnsNotFoundFrame(FAILED_URL);
  await waitForCtaResolved(frame);

  is(reason("not_top_level"), 1, "The frame is recorded as not_top_level");
  // The action count is what proves the frame actually reached the parent, so
  // the null assertions below describe a decision that happened rather than
  // telemetry that never ran at all.
  is(
    action(SEARCH_CTA_ACTIONS.NONE),
    1,
    "The frame counts as an action=none decision"
  );
  is(
    reason("keywords_found"),
    null,
    "The gate short-circuits before keyword extraction"
  );
  is(reason("no_path"), null, "No URL-shape reason for a frame");
  is(reason("no_meaningful_keywords"), null, "No URL-shape reason for a frame");
  is(
    Glean.securityUiNeterror.searchCtaShown.testGetValue(),
    null,
    "Nothing is counted as shown for a frame"
  );
  is(
    Glean.securityUiNeterror.searchCtaReason.__other__.testGetValue(),
    null,
    "No unexpected reason label"
  );
  is(
    Glean.securityUiNeterror.searchCtaAction.__other__.testGetValue(),
    null,
    "No unexpected action label"
  );

  BrowserTestUtils.removeTab(tab);
});

// A frame is out of scope for the exit outcome too, since nobody chose to
// leave it. The negative check only means something next to the positive one.
// If pagehide never fired, "no event" would pass for the wrong reason.
add_task(async function test_frameRecordsNoExitOutcome() {
  await drainAndReset();
  const embedded = await loadDnsNotFoundFrame(FAILED_URL);
  await waitForCtaResolved(embedded.frame);
  await navigateAwayAndFlush(embedded.browser, embedded.frame);

  is(
    exitEvents().length,
    0,
    "Leaving an embedded error page records no exit outcome"
  );
  BrowserTestUtils.removeTab(embedded.tab);

  await drainAndReset();
  const { tab, browser } = await loadDnsNotFoundPage(FAILED_URL);
  await waitForCtaResolved(browser);
  await navigateAwayAndFlush(browser);

  const events = exitEvents();
  is(events.length, 1, "The same teardown at top level does record an exit");
  is(
    events[0].extra.reason,
    "navigated-or-closed",
    "Recorded as navigated-or-closed"
  );
  BrowserTestUtils.removeTab(tab);
});

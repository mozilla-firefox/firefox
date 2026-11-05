/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const CTA_PREF = "browser.netError.searchCTA.enabled";
// Treat the connectivity reading as always fresh so the bug 2055712 guard is a
// no-op here and these tests don't depend on real captive-portal state. The
// freshness window is an int pref compared against the reading's age in
// milliseconds, so int32 max is the largest value that can be stored and is
// effectively "never stale".
const FRESHNESS_PREF = "browser.netError.searchCTA.connectivityFreshnessMs";
const ALWAYS_FRESH = 2147483647;
// A .com host (not a reserved TLD, so it passes host-viability from bug
// 2055651) with a www subdomain, to verify the query falls back to the
// registrable domain (the subdomain is dropped).
const FAILED_HOST = "www.doesnotexist-searchcta.com";
const REGISTRABLE_DOMAIN = "doesnotexist-searchcta.com";
const SEARCH_URL = `https://example.com/?q=${REGISTRABLE_DOMAIN}`;
const BAD_CERT = "https://expired.example.com/";
// A subdomain of the same registrable domain, for the failed load in
// test_introNamesFailedSubdomain.
const SUBDOMAIN_HOST = `bogus.${REGISTRABLE_DOMAIN}`;

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gDNSOverride",
  "@mozilla.org/network/native-dns-override;1",
  Ci.nsINativeDNSResolverOverride
);

add_setup(async function () {
  stubSearchCTASupportedEngine();
  pinSearchCTADecisionDeadline();
  // A deterministic default engine so the search submission URL is known.
  await SearchTestUtils.installSearchExtension(
    {
      name: "MozSearchCTA",
      search_url: "https://example.com/",
      search_url_get_params: "q={searchTerms}",
    },
    { setAsDefault: true }
  );
});

/**
 * Load a synthetic online dnsNotFound page for FAILED_HOST with the search CTA
 * pref set, run a task against its browser, then close the tab and restore the
 * pref. The pref is pushed before the load because the card reads it during
 * page setup.
 *
 * @param {boolean} enabled Value for browser.netError.searchCTA.enabled.
 * @param {Function} taskFn Async callback receiving the error page's browser.
 */
async function withDnsNotFoundPage(enabled, taskFn) {
  await SpecialPowers.pushPrefEnv({
    set: [
      [CTA_PREF, enabled],
      [FRESHNESS_PREF, ALWAYS_FRESH],
    ],
  });
  const { tab, browser } = await loadNetErrorPage("dnsNotFound", FAILED_HOST);
  try {
    await taskFn(browser);
  } finally {
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
}

add_task(async function test_ctaRendersWhenEnabled() {
  await withDnsNotFoundPage(true, async browser => {
    await waitForSettledNetErrorCard(browser);
    await SpecialPowers.spawn(
      browser,
      [REGISTRABLE_DOMAIN],
      async registrableDomain => {
        const doc = content.document;
        const card = doc.querySelector("net-error-card").wrappedJSObject;

        is(
          card.errorTitle.getAttribute("data-l10n-id"),
          "neterror-search-cta-title",
          "Search CTA heading is shown"
        );

        ok(card.reloadButton, "Reload button is present");

        const searchButton = card.searchCTAButton;
        ok(searchButton, "Search button renders once the parent responds");
        // moz-button exposes the new-tab hint as the internal button's title,
        // which is also its accessible description. Compare against the value
        // Fluent resolves for .tooltiptext, so a break in the .tooltiptext ->
        // title plumbing still fails here without pinning the English string.
        const [buttonMessage] = await doc.l10n.formatMessages([
          { id: "neterror-search-cta-search-button" },
        ]);
        const expectedTitle = buttonMessage.attributes.find(
          attribute => attribute.name === "tooltiptext"
        ).value;
        const innerButton =
          searchButton.shadowRoot.querySelector("#main-button");
        is(
          innerButton.getAttribute("title"),
          expectedTitle,
          "Search button is described for the new-tab action"
        );
        is(
          searchButton.shadowRoot.querySelector("img").getAttribute("src"),
          "chrome://global/skin/icons/search-glass.svg",
          "Search button uses the unbranded fallback icon"
        );

        // The intro shows the same display host as every other error page
        // string, rather than a separately derived one (bug 2067835).
        is(
          card.errorIntro.getAttribute("data-l10n-args"),
          JSON.stringify({ hostname: card.hostname }),
          "Intro is given the page's display host"
        );

        const emphasizedHost = await ContentTaskUtils.waitForCondition(
          () => card.errorIntro.querySelector("strong"),
          "Fluent's DOM overlay renders the emphasized host"
        );
        is(
          emphasizedHost.textContent,
          card.hostname,
          "Only the host is emphasized, not the whole sentence"
        );

        const hint = card.shadowRoot.querySelector(
          '[data-l10n-id="neterror-search-cta-hint-search-query"]'
        );
        ok(hint, "The search hint names a query rather than generic wording");
        is(
          hint.getAttribute("data-l10n-args"),
          JSON.stringify({ query: registrableDomain }),
          "The hint names the exact query Search will submit"
        );
        const emphasized = await ContentTaskUtils.waitForCondition(
          () => hint.querySelector("strong"),
          "Fluent's DOM overlay renders the emphasized query"
        );
        ok(
          emphasized.textContent.includes(registrableDomain),
          `The emphasized text contains the query (got "${emphasized.textContent}")`
        );

        is(
          card.tryAgainButton,
          null,
          "The original Try Again button is replaced by Reload"
        );

        // Reload stands in for a Try Again button that has an accesskey, so
        // both CTA buttons need their own rather than silently dropping it.
        // moz-button maps accessKey onto its internal button, so assert there
        // to cover the whole Fluent -> host -> inner button path. The letters
        // are localizable, so only require that they exist and do not collide.
        const accessKeyOf = button =>
          button.shadowRoot
            .querySelector("#main-button")
            .getAttribute("accesskey");
        const searchKey = accessKeyOf(searchButton);
        const reloadKey = accessKeyOf(card.reloadButton);
        ok(searchKey, "Search button has an accesskey");
        ok(reloadKey, "Reload button has an accesskey");
        isnot(searchKey, reloadKey, "The two accesskeys do not collide");

        is(
          card.shadowRoot
            .querySelector(".img-container img")
            .getAttribute("src"),
          "resource://content-accessible/illustrations/security-error.svg",
          "The CTA page shows the security illustration"
        );
      }
    );
  });
});

// The page originally showed placeholder wording while the parent was still
// deciding, then swap in the real content a frame later, which made the UI
// flicker (bug 2067882).
add_task(async function test_nothingRendersBeforeTheDecision() {
  const sandbox = sinon.createSandbox();
  let releaseDecision;
  const decisionHeld = new Promise(resolve => {
    releaseDecision = resolve;
  });
  const realDecide = NetErrorParent.prototype.decideSearchCTA;
  sandbox
    .stub(NetErrorParent.prototype, "decideSearchCTA")
    .callsFake(async function (failedURL) {
      await decisionHeld;
      return realDecide.call(this, failedURL);
    });
  await SpecialPowers.pushPrefEnv({
    set: [
      [CTA_PREF, true],
      [FRESHNESS_PREF, ALWAYS_FRESH],
    ],
  });

  try {
    await BrowserTestUtils.withNewTab("about:blank", async browser => {
      const url = `about:neterror?e=dnsNotFound&u=http%3A%2F%2F${encodeURIComponent(
        FAILED_HOST
      )}%2F`;
      SpecialPowers.spawn(browser, [url], errorUrl => {
        content.location = errorUrl;
      });
      await TestUtils.waitForCondition(
        () => browser.currentURI.spec.startsWith("about:neterror"),
        "The error document became current"
      );

      await SpecialPowers.spawn(browser, [], async () => {
        const card = await ContentTaskUtils.waitForCondition(
          () =>
            content.document.querySelector("net-error-card")?.wrappedJSObject,
          "The net-error-card is created"
        );
        ok(!card.searchCTAResolved, "The parent has not answered yet");
        ok(
          !card.hasUpdated,
          "The card has not rendered while the answer is out"
        );
        is(
          card.shadowRoot.childElementCount,
          0,
          "Nothing at all is rendered before the answer arrives"
        );
      });

      releaseDecision();
      await waitForSettledNetErrorCard(browser);

      await SpecialPowers.spawn(browser, [REGISTRABLE_DOMAIN], async query => {
        const card =
          content.document.querySelector("net-error-card").wrappedJSObject;
        ok(card.hasUpdated, "The card renders once the answer is in");
        ok(card.searchCTAButton, "The render it does has the Search button");
        const hint = card.shadowRoot.querySelector(
          '[data-l10n-id="neterror-search-cta-hint-search-query"]'
        );
        ok(hint, "The render it does names the query");
        is(
          hint.getAttribute("data-l10n-args"),
          JSON.stringify({ query }),
          "The named query is the one Search will submit"
        );
      });
    });
  } finally {
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
  }
});

// The intro has to name the host that actually failed, subdomain included, so
// "bogus.example.com" is never reported as "example.com" (bug 2067835).
add_task(async function test_introNamesFailedSubdomain() {
  lazy.gDNSOverride.addIPOverride(SUBDOMAIN_HOST, "N/A");
  await SpecialPowers.pushPrefEnv({
    set: [
      [CTA_PREF, true],
      [FRESHNESS_PREF, ALWAYS_FRESH],
      // Keep the dnsNotFound "did you mean www.<host>?" suggestion from firing
      // a second DNS lookup that no override covers.
      ["browser.fixup.alternate.enabled", false],
    ],
  });

  const tab = BrowserTestUtils.addTab(gBrowser, `https://${SUBDOMAIN_HOST}/`);
  gBrowser.selectedTab = tab;
  const browser = tab.linkedBrowser;
  try {
    await BrowserTestUtils.waitForErrorPage(browser);
    await waitForSettledNetErrorCard(browser);
    await SpecialPowers.spawn(browser, [SUBDOMAIN_HOST], async host => {
      const card =
        content.document.querySelector("net-error-card").wrappedJSObject;
      is(
        card.errorIntro.getAttribute("data-l10n-id"),
        "neterror-search-cta-intro2",
        "The failed load lands on the search CTA intro"
      );
      const emphasized = await ContentTaskUtils.waitForCondition(
        () => card.errorIntro.querySelector("strong"),
        "Fluent's DOM overlay renders the emphasized host"
      );
      is(
        emphasized.textContent,
        host,
        "The intro names the host that failed, subdomain included"
      );
    });
  } finally {
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
    lazy.gDNSOverride.clearHostOverride(SUBDOMAIN_HOST);
  }
});

add_task(async function test_searchClickOpensNewTab() {
  await withDnsNotFoundPage(true, async browser => {
    const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, null, true);

    await waitForSettledNetErrorCard(browser, {
      clickQuery: "searchCTAButton",
    });

    const searchTab = await newTabPromise;
    is(
      searchTab.linkedBrowser.currentURI.spec,
      SEARCH_URL,
      "Search opens a new tab with the default-engine query for the domain"
    );
    is(gBrowser.selectedTab, searchTab, "The search tab is brought forward");
    ok(
      browser.currentURI.spec.startsWith("about:neterror"),
      "The failed error page stays open behind it"
    );
    BrowserTestUtils.removeTab(searchTab);
  });
});

// With no Search button to name a query, the hint falls back to generic
// wording rather than naming a query the user cannot act on.
add_task(async function test_genericHintWhenNoSearchButton() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(NetErrorParent.prototype, "getSearchCTAInfo").resolves({
    action: SEARCH_CTA_ACTIONS.NONE,
    query: "",
    reason: SEARCH_CTA_REASONS.HOST_UNUSABLE,
    hasEngine: false,
  });
  try {
    await withDnsNotFoundPage(true, async browser => {
      await waitForSettledNetErrorCard(browser);
      await SpecialPowers.spawn(browser, [], async () => {
        const card =
          content.document.querySelector("net-error-card").wrappedJSObject;
        ok(
          card.reloadButton,
          "The redesigned page still renders with Reload only"
        );
        is(card.searchCTAButton, null, "No Search button to name a query");
        ok(
          card.shadowRoot.querySelector(
            '[data-l10n-id="neterror-search-cta-hint-search"]'
          ),
          "The hint uses generic wording"
        );
        is(
          card.shadowRoot.querySelector(
            '[data-l10n-id="neterror-search-cta-hint-search-query"]'
          ),
          null,
          "No query is named when none will be searched"
        );
      });
    });
  } finally {
    sandbox.restore();
  }
});

// The CTA layout puts Search first, so initial focus belongs there rather than
// on Reload, which would leave keyboard users tabbing backwards.
add_task(async function test_searchButtonTakesInitialFocus() {
  await withDnsNotFoundPage(true, async browser => {
    await waitForSettledNetErrorCard(browser);
    await SpecialPowers.spawn(browser, [], async () => {
      const card =
        content.document.querySelector("net-error-card").wrappedJSObject;
      // focusPrimaryButton() runs on its own async path after the render, so
      // the focus move can still land after the card has settled.
      ok(
        await ContentTaskUtils.waitForCondition(
          () => card.shadowRoot.activeElement === card.searchCTAButton
        ),
        "The Search button takes initial focus, not Reload"
      );
    });
  });
});

// Reload stands in for Try Again on this page, so the existing retry signal
// must keep firing or it looks like retries stopped when the CTA shipped.
add_task(async function test_reloadRecordsTryAgainTelemetry() {
  Services.fog.testResetFOG();
  await withDnsNotFoundPage(true, async browser => {
    await waitForSettledNetErrorCard(browser, { clickQuery: "reloadButton" });
    await Services.fog.testFlushAllChildren();
    const events = Glean.securityUiNeterror.clickTryAgainButton.testGetValue();
    is(events?.length, 1, "Reload records one click_try_again_button event");
  });
});

// Same reasoning for the learn-more link. Without its telemetry attributes it
// still renders and still navigates, so the only symptom is the event quietly
// going missing on every page that shows the CTA (bug 2064931).
add_task(async function test_learnMoreRecordsClickTelemetry() {
  Services.fog.testResetFOG();
  await withDnsNotFoundPage(true, async browser => {
    const newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
    await waitForSettledNetErrorCard(browser, { clickQuery: "learnMoreLink" });
    const learnMoreTab = await newTabPromise;
    await Services.fog.testFlushAllChildren();

    const events = Glean.securityUiNeterror.clickLearnMoreLink.testGetValue();
    is(events?.length, 1, "Learn more records one click_learn_more_link event");
    BrowserTestUtils.removeTab(learnMoreTab);
  });
});

// net-error-card.mjs reads the CTA pref at module scope and is shared with
// about:certerror, which has its own RemotePageAccessManager allowlist. If the
// pref is missing there the read throws, aboutNetError.mjs fails to evaluate,
// and every cert error page renders blank regardless of the pref's value.
add_task(async function test_certErrorPageRendersWithCtaEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [CTA_PREF, true],
      ["security.certerrors.felt-privacy-v1", true],
    ],
  });
  const tab = await openErrorPage(BAD_CERT);
  try {
    await waitForSettledNetErrorCard(tab.linkedBrowser);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const card =
        content.document.querySelector("net-error-card").wrappedJSObject;
      ok(card.errorTitle, "The cert error page has a heading");
      is(card.searchCTAButton, null, "No Search button on a cert error page");
      is(card.reloadButton, null, "No Reload button on a cert error page");
    });
  } finally {
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_noCtaWhenDisabled() {
  await withDnsNotFoundPage(false, async browser => {
    await waitForSettledNetErrorCard(browser);
    await SpecialPowers.spawn(browser, [], async () => {
      const card =
        content.document.querySelector("net-error-card").wrappedJSObject;

      ok(
        card.tryAgainButton,
        "The standard dnsNotFound page is shown with the pref off"
      );
      is(card.searchCTAButton, null, "No Search button with the pref off");
      is(card.reloadButton, null, "No Reload button with the pref off");
    });
  });
});

add_task(async function test_ctaButtonAccessKeys() {
  await withDnsNotFoundPage(true, async browser => {
    await waitForSettledNetErrorCard(browser);
    await SpecialPowers.spawn(
      browser,
      [getAccessKeyModifiers()],
      async mods => {
        const card =
          content.document.querySelector("net-error-card").wrappedJSObject;
        const searchButton = card.searchCTAButton;
        const reloadButton = card.reloadButton;

        await ContentTaskUtils.waitForCondition(
          () => searchButton.accessKey && reloadButton.accessKey,
          "Waiting for accesskeys to be set by Fluent"
        );

        is(searchButton.accessKey, "c", "Search button has accesskey 'c'");
        is(reloadButton.accessKey, "R", "Reload button has accesskey 'R'");
        isnot(
          searchButton.accessKey,
          reloadButton.accessKey,
          "The two CTA buttons take different access keys"
        );

        // Listen on the card's shadow root, above the button, so the capture
        // phase reaches this before the template's own @click and the search
        // never actually runs. The capture flag has to be the boolean form.
        let clickedId = null;
        const onClick = e => {
          e.stopPropagation();
          clickedId = e.target.id;
        };
        card.shadowRoot.addEventListener("click", onClick, true);

        EventUtils.synthesizeKey("s", mods, content);
        is(
          clickedId,
          null,
          "Access key S no longer activates the Search button"
        );

        clickedId = null;
        EventUtils.synthesizeKey("c", mods, content);
        is(
          clickedId,
          "searchCTAButton",
          "Access key c activated the Search button"
        );

        card.shadowRoot.removeEventListener("click", onClick, true);
      }
    );
  });
});

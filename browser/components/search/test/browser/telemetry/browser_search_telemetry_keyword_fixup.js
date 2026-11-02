/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that searches from docshell keyword fixup record the partner code and
 * telemetry id used in the search submission.
 */

"use strict";

const SEARCH_URL =
  "https://example.org/browser/browser/components/search/test/browser/telemetry/searchTelemetry.html";

add_setup(async function () {
  let partnerDetails;
  if (SearchUtils.configV3FeatureGate) {
    partnerDetails = {
      partner: {
        default: { code: "defaultcode" },
        urlbar: { code: "urlbarcode", telemetrySuffix: "urlbarsx" },
      },
    };
  } else {
    partnerDetails = { partnerCode: "defaultcode" };
  }
  await SearchTestUtils.updateRemoteSettingsConfig([
    {
      identifier: "engine",
      base: {
        urls: {
          search: {
            base: SEARCH_URL,
            params: [{ name: "pc", value: "{partnerCode}" }],
            searchTermParamName: "s",
          },
        },
        ...partnerDetails,
      },
    },
  ]);
  await SearchService.setDefault(
    SearchService.getEngineById("engine"),
    SearchService.CHANGE_REASON.UNKNOWN
  );

  registerCleanupFunction(resetTelemetry);
});

/**
 * Performs a search that is handled by keyword fixup, and checks the partner
 * code used for the search and its telemetry.
 *
 * @param {(browser: MozBrowser) => void} searchFn
 *   Starts the search in the given browser.
 */
async function checkPartnerCode(searchFn) {
  resetTelemetry();

  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    let loaded = BrowserTestUtils.browserLoaded(browser, false, url =>
      url.startsWith(SEARCH_URL)
    );
    searchFn(browser);
    await loaded;

    // Per-SAP partner codes are only supported by search-config-v3.
    let { partnerCode, telemetrySuffix } = SearchUtils.configV3FeatureGate
      ? { partnerCode: "urlbarcode", telemetrySuffix: "-urlbarsx" }
      : { partnerCode: "defaultcode", telemetrySuffix: "" };
    Assert.equal(
      URL.fromURI(browser.currentURI).searchParams.get("pc"),
      partnerCode,
      "Should have loaded the search with the expected partner code"
    );
    await SearchUITestUtils.assertSAPTelemetry({
      engineId: "engine",
      engineName: "engine",
      partnerCode,
      source: "urlbar",
      telemetrySuffix,
      count: 1,
    });
  });
}

add_task(async function test_fixup_on_load() {
  await checkPartnerCode(browser => {
    browser.fixupAndLoadURIString("foo bar", {
      flags: Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  });
});

add_task(async function test_fixup_on_unknown_host() {
  // Make the urlbar treat a single word as a host, so that the docshell
  // fixes it up to a search when the host fails to resolve.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.fixup.dns_first_for_single_words", true]],
  });
  await checkPartnerCode(() => {
    gURLBar.value = "idontreallyexistonthisnetwork";
    gURLBar.handleCommand();
  });
  await SpecialPowers.popPrefEnv();
});

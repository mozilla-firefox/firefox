/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SiteDataTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SiteDataTestUtils.sys.mjs"
);

const uuidGenerator = Services.uuid;

const ORIGIN_A = "https://example.net";
const ORIGIN_B = "https://example.org";

const PREFLIGHT_URL_PATH =
  "/browser/toolkit/components/cleardata/tests/browser/file_cors_preflight.sjs";

const PREFLIGHT_URL_A = ORIGIN_A + PREFLIGHT_URL_PATH;
const PREFLIGHT_URL_B = ORIGIN_B + PREFLIGHT_URL_PATH;

function testPreflightCached(browser, url, token, isCached) {
  return SpecialPowers.spawn(
    browser,
    [url, token, isCached],
    async (url, token, isCached) => {
      let response = await content.fetch(
        new content.Request(`${url}?token=${token}`, {
          mode: "cors",
          method: "GET",
          headers: [["x-test-header", "check"]],
        })
      );

      let expected = isCached ? "0" : "1";
      is(
        await response.text(),
        expected,
        `Preflight cache for ${url} ${isCached ? "HIT" : "MISS"}.`
      );
    }
  );
}

async function testDeleteAll(
  clearDataFlag,
  { deleteBy = "all", hasUserInput = false } = {}
) {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    let token = uuidGenerator.generateUUID().toString();

    // Populate the preflight cache.
    await testPreflightCached(browser, PREFLIGHT_URL_A, token, false);
    await testPreflightCached(browser, PREFLIGHT_URL_B, token, false);
    // Cache should be populated.
    await testPreflightCached(browser, PREFLIGHT_URL_A, token, true);
    await testPreflightCached(browser, PREFLIGHT_URL_B, token, true);

    await new Promise(resolve => {
      if (deleteBy == "principal") {
        Services.clearData.deleteDataFromPrincipal(
          browser.contentPrincipal,
          hasUserInput,
          clearDataFlag,
          value => {
            Assert.equal(value, 0);
            resolve();
          }
        );
      } else if (deleteBy == "baseDomain") {
        Services.clearData.deleteDataFromSite(
          browser.contentPrincipal.baseDomain,
          {},
          hasUserInput,
          clearDataFlag,
          value => {
            Assert.equal(value, 0);
            resolve();
          }
        );
      } else {
        Services.clearData.deleteData(clearDataFlag, value => {
          Assert.equal(value, 0);
          resolve();
        });
      }
    });

    // The preflight cache cleaner cannot delete by principal or baseDomain
    // (Bug 1727141). If this method is called, it will check whether the used
    // requested the clearing. If the user requested clearing, it will
    // over-clear (clear all data). If the request didn't come from the user,
    // for example from the PurgeTrackerService, it will not clear anything to
    // avoid clearing storage unrelated to the baseDomain or principal.
    let clearedAll = deleteBy == "all" || hasUserInput;

    // Cache should be cleared.
    await testPreflightCached(browser, PREFLIGHT_URL_A, token, !clearedAll);
    await testPreflightCached(browser, PREFLIGHT_URL_B, token, !clearedAll);
  });

  await SiteDataTestUtils.clear();
}

add_task(async function test_deleteAll() {
  // The cleaner should be called when we target all cleaners, all cache
  // cleaners, or just the preflight cache.
  let { CLEAR_ALL, CLEAR_ALL_CACHES, CLEAR_PREFLIGHT_CACHE } =
    Ci.nsIClearDataService;

  for (let flag of [CLEAR_ALL, CLEAR_ALL_CACHES, CLEAR_PREFLIGHT_CACHE]) {
    await testDeleteAll(flag);
  }
});

add_task(async function test_deleteByPrincipal() {
  // The cleaner should be called when we target all cleaners, all cache
  // cleaners, or just the preflight cache.
  let { CLEAR_ALL, CLEAR_ALL_CACHES, CLEAR_PREFLIGHT_CACHE } =
    Ci.nsIClearDataService;

  for (let flag of [CLEAR_ALL, CLEAR_ALL_CACHES, CLEAR_PREFLIGHT_CACHE]) {
    for (let hasUserInput of [true, false]) {
      await testDeleteAll(flag, { deleteBy: "principal", hasUserInput });
    }
  }
});

add_task(async function test_deletePrivateBrowsingCache() {
  async function deletePrivateBrowsingCache(token) {
    const browser = await BrowserTestUtils.openNewBrowserWindow({
      private: true,
    });

    const tab = (browser.gBrowser.selectedTab = BrowserTestUtils.addTab(
      browser.gBrowser,
      "https://example.com"
    ));
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

    // Populate the preflight cache and make sure it isn't populated right now
    await testPreflightCached(tab.linkedBrowser, PREFLIGHT_URL_A, token, false);
    await testPreflightCached(tab.linkedBrowser, PREFLIGHT_URL_B, token, false);
    // Cache should be populated.
    await testPreflightCached(tab.linkedBrowser, PREFLIGHT_URL_A, token, true);
    await testPreflightCached(tab.linkedBrowser, PREFLIGHT_URL_B, token, true);

    await browser.close();
  }

  // Disable https_first mode to not upgrade the connection of the main page
  // and get "Blocked loading mixed active content" for the CORS request
  // making this test case fail. Another solution would be to change all URLs
  // to https.
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_first_pbm", false]],
  });

  let token = uuidGenerator.generateUUID().toString();

  // Make sure the CORS preflight cache is cleared between two private
  // browsing sessions. Calling this function twice to see if the cache isn't
  // populated anymore after the first call.
  await deletePrivateBrowsingCache(token);
  await deletePrivateBrowsingCache(token);

  await SpecialPowers.clearUserPref("dom.security.https_first_pbm");
});

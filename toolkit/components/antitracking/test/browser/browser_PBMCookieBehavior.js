"use strict";

// 36 pref combinations, each needing a fresh document in both a regular and a
// private window. Code coverage builds need the extra headroom.
requestLongerTimeout(8);

const COOKIE_BEHAVIORS = [
  Ci.nsICookieService.BEHAVIOR_ACCEPT,
  Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN,
  Ci.nsICookieService.BEHAVIOR_REJECT,
  Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN,
  Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
  Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN,
];

async function verifyCookieBehavior(browser, expected, label) {
  await SpecialPowers.spawn(
    browser,
    [{ expected, label, page: TEST_3RD_PARTY_PAGE }],
    async obj => {
      is(
        content.document.cookieJarSettings.cookieBehavior,
        obj.expected,
        `The tab in the ${obj.label} window has the expected CookieBehavior.`
      );

      // Create an 3rd party iframe and check the cookieBehavior.
      let ifr = content.document.createElement("iframe");
      const loading = ContentTaskUtils.waitForEvent(ifr, "load");
      ifr.src = obj.page;
      content.document.body.appendChild(ifr);
      await loading;

      await SpecialPowers.spawn(
        ifr.browsingContext,
        [{ expected: obj.expected, label: obj.label }],
        async inner => {
          is(
            content.document.cookieJarSettings.cookieBehavior,
            inner.expected,
            `The iframe in the ${inner.label} window has the expected CookieBehavior.`
          );
        }
      );
    }
  );
}

// A document takes its cookieBehavior from the prefs in effect when it is
// created, so every combination below needs a fresh load. Renavigating is much
// cheaper than opening and closing one tab per combination, which made this test
// exceed its timeout on slower configurations.
async function loadPage(browser, uri) {
  let loaded = BrowserTestUtils.browserLoaded(browser, { wantLoad: uri });
  BrowserTestUtils.startLoadingURIString(browser, uri);
  await loaded;
}

add_task(async function () {
  let pb_win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  // Reuse each window's existing tab rather than adding one, so that the test
  // neither opens nor closes a tab per combination.
  let browser = gBrowser.selectedBrowser;
  let pbBrowser = pb_win.gBrowser.selectedBrowser;

  for (let regularCookieBehavior of COOKIE_BEHAVIORS) {
    for (let PBMCookieBehavior of COOKIE_BEHAVIORS) {
      await SpecialPowers.flushPrefEnv();
      await SpecialPowers.pushPrefEnv({
        set: [
          ["network.cookie.cookieBehavior", regularCookieBehavior],
          ["network.cookie.cookieBehavior.pbmode", PBMCookieBehavior],
          ["dom.security.https_first_pbm", false],
        ],
      });

      info(
        ` Start testing with regular cookieBehavior(${regularCookieBehavior}) and PBM cookieBehavior(${PBMCookieBehavior})`
      );

      let expectPBMCookieBehavior = PBMCookieBehavior;

      // The private cookieBehavior will mirror the regular pref if the regular
      // pref has a user value and the private pref doesn't have a user pref.
      if (
        Services.prefs.prefHasUserValue("network.cookie.cookieBehavior") &&
        !Services.prefs.prefHasUserValue("network.cookie.cookieBehavior.pbmode")
      ) {
        expectPBMCookieBehavior = regularCookieBehavior;
      }

      await Promise.all([
        (async () => {
          await loadPage(browser, TEST_TOP_PAGE);
          await verifyCookieBehavior(browser, regularCookieBehavior, "regular");
        })(),
        (async () => {
          await loadPage(pbBrowser, TEST_TOP_PAGE);
          await verifyCookieBehavior(
            pbBrowser,
            expectPBMCookieBehavior,
            "private"
          );
        })(),
      ]);
    }
  }

  await loadPage(browser, "about:blank");
  await BrowserTestUtils.closeWindow(pb_win);
});

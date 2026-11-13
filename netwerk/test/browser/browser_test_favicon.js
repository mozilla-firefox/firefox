// Tests third party cookie blocking using a favicon loaded from a different
// domain. The cookie should be considered third party.
"use strict";
add_task(async function () {
  const iconUrl =
    "http://example.org/browser/netwerk/test/browser/damonbowling.jpg";
  const pageUrl =
    "http://example.com/browser/netwerk/test/browser/file_favicon.html";
  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.cookie.cookieBehavior", 1],
      ["dom.security.https_first", false],
    ],
  });

  let promise = TestUtils.topicObserved("cookie-rejected", subject => {
    let uri = subject.QueryInterface(Ci.nsIURI);
    return uri.spec == iconUrl;
  });

  // Kick off a page load that will load the favicon.
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, pageUrl);
  registerCleanupFunction(async function () {
    BrowserTestUtils.removeTab(tab);
    // FaviconLoader loads with LOAD_FROM_CACHE | VALIDATE_NEVER, so a later
    // run would get the icon from the cache and never process its Set-Cookie.
    Services.cache2.clear();
  });

  await promise;
  ok(true, "foreign favicon cookie was blocked");
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

registerCleanupFunction(() => {
  Services.cookies.removeAll();
  Services.prefs.clearUserPref("dom.security.https_first");
  Services.prefs.clearUserPref("network.cookie.blockFileScheme");
  Services.prefs.clearUserPref("network.cookie.cookieBehavior");
  Services.prefs.clearUserPref(
    "network.cookieJarSettings.unblocked_for_testing"
  );
});

add_task(async function test_file_url_external_domain_rejected() {
  Services.prefs.setBoolPref("dom.security.https_first", false);
  Services.prefs.setBoolPref("network.cookie.blockFileScheme", false);
  Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);
  Services.prefs.setBoolPref(
    "network.cookieJarSettings.unblocked_for_testing",
    true
  );
  Services.cookies.removeAll();

  // Simulate a cookie previously set by https://www.example.com.
  const exampleURI = Services.io.newURI("https://www.example.com/");
  const exampleChannel = NetUtil.newChannel({
    uri: exampleURI,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  });
  Services.cookies.setCookieStringFromHttp(
    exampleURI,
    "test=value; domain=.example.com; max-age=3600",
    exampleChannel
  );
  Assert.ok(
    Services.cookies.cookies.some(c => c.name === "test"),
    "HTTPS cookie added"
  );

  // Build the file:// URL for the test page.
  const testPath = getResolvedURI(gTestPath);
  const dir = getChromeDir(testPath);
  dir.append("file_cookie_domain.html");
  const fileURL = Services.io.newFileURI(dir).spec;

  const tab = BrowserTestUtils.addTab(gBrowser, fileURL);
  const browser = gBrowser.getBrowserForTab(tab);
  await BrowserTestUtils.browserLoaded(browser);

  const cookies = Services.cookies.cookies;

  // "test=evil; domain=.example.com" must have been rejected: no file:// cookie
  // named "test" should exist.  Without the fix it would be accepted, producing
  // a duplicate (name, host, path, OA) that corrupts the DB on the next write.
  const fileCookieWithExternalDomain = cookies.find(
    c => c.name === "test" && c.schemeMap === Ci.nsICookie.SCHEME_FILE
  );
  Assert.ok(
    !fileCookieWithExternalDomain,
    "file:// cookie with external domain was rejected"
  );

  // "local=1" (no explicit domain) must have been accepted.
  const localCookie = cookies.find(c => c.name === "local");
  Assert.ok(localCookie, "file:// cookie without explicit domain was accepted");
  Assert.equal(localCookie.schemeMap, Ci.nsICookie.SCHEME_FILE);

  // The original HTTPS cookie must still be intact.
  const httpsCookie = cookies.find(
    c => c.name === "test" && c.schemeMap === Ci.nsICookie.SCHEME_HTTPS
  );
  Assert.ok(httpsCookie, "HTTPS cookie is still present");

  BrowserTestUtils.removeTab(tab);
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

function getFileURL() {
  const dir = getChromeDir(getResolvedURI(gTestPath));
  dir.append("file_cookie_domain.html");
  return Services.io.newFileURI(dir).spec;
}

async function readDocumentCookie(url) {
  return BrowserTestUtils.withNewTab(url, browser =>
    SpecialPowers.spawn(browser, [], () => content.document.cookie)
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.cookie.cookieBehavior", 0],
      ["network.cookieJarSettings.unblocked_for_testing", true],
    ],
  });
});

registerCleanupFunction(() => {
  Services.cookies.removeAll();
});

add_task(async function test_file_url_cookies_blocked() {
  Services.cookies.removeAll();

  await SpecialPowers.pushPrefEnv({
    set: [["network.cookie.blockFileScheme", true]],
  });

  is(
    await readDocumentCookie(getFileURL()),
    "",
    "file:// document cannot set cookies"
  );
  ok(
    !Services.cookies.cookies.some(c => c.schemeMap & Ci.nsICookie.SCHEME_FILE),
    "No file:// cookie stored"
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_existing_file_url_cookies_hidden() {
  Services.cookies.removeAll();

  await SpecialPowers.pushPrefEnv({
    set: [["network.cookie.blockFileScheme", false]],
  });

  is(
    await readDocumentCookie(getFileURL()),
    "local=1",
    "file:// cookie set while the pref is off"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["network.cookie.blockFileScheme", true]],
  });

  is(
    await readDocumentCookie(getFileURL()),
    "",
    "Existing file:// cookie is not exposed when the pref is on"
  );
  ok(
    Services.cookies.cookies.some(
      c => c.name === "local" && c.schemeMap & Ci.nsICookie.SCHEME_FILE
    ),
    "Existing file:// cookie is kept in storage"
  );

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

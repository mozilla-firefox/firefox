"use strict";

// A process that only loaded http://example.com must not be able to read,
// write or delete Secure cookies through CookieStore. The secure context allowlist is added
// after the load, so the parent still records the cookie key as insecure while
// a new about:blank popup in that process becomes a secure context exposing
// cookieStore.
// The test depends on the fact that the about:blank popup runs in the same
// process as the opener and that the process is not allowed to read or write
// secure cookies.

const HTTP_URL =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.com/browser/netwerk/cookie/test/browser/file_empty.html";
// eslint-disable-next-line sdl/no-insecure-url
const HTTPS_URL = HTTP_URL.replace("http://", "https://");

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.cookieStore.enabled", true],
      ["dom.disable_open_during_load", false],
      ["dom.security.https_first", false],
    ],
  });

  Services.cookies.removeAll();
  Services.cookies.add(
    "example.com",
    "/",
    "secret",
    "secureValue",
    true /* secure */,
    false /* httpOnly */,
    true /* session */,
    Date.now() + 24 * 60 * 60 * 1000,
    {},
    Ci.nsICookie.SAMESITE_NONE,
    Ci.nsICookie.SCHEME_HTTPS
  );
  Services.cookies.add(
    "example.com",
    "/",
    "public",
    "insecureValue",
    false /* secure */,
    false /* httpOnly */,
    true /* session */,
    Date.now() + 24 * 60 * 60 * 1000,
    {},
    Ci.nsICookie.SAMESITE_LAX,
    Ci.nsICookie.SCHEME_HTTP
  );

  registerCleanupFunction(() => {
    Services.cookies.removeAll();
  });
});

function cookieExists(name) {
  return Services.cookies
    .getCookiesFromHost("example.com", {})
    .some(c => c.name == name);
}

add_task(async function test_http_process_cannot_access_secure_cookies() {
  ok(cookieExists("secret"), "Secure cookie was added");
  ok(cookieExists("public"), "Non-Secure cookie was added");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, HTTP_URL);

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    ok(!content.isSecureContext, "HTTP page is not a secure context");
    ok(
      !content.document.cookie.includes("secret"),
      "Secure cookie is not visible through document.cookie"
    );
  });

  await SpecialPowers.pushPrefEnv({
    set: [["dom.securecontext.allowlist", "example.com"]],
  });

  let popupPromise = BrowserTestUtils.waitForNewTab(gBrowser, "about:blank");
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    let win = content.open("about:blank");
    ok(win.isSecureContext, "Popup is a secure context");
    ok(win.cookieStore, "Popup exposes cookieStore");
    is(
      win.document.nodePrincipal.origin,
      // eslint-disable-next-line sdl/no-insecure-url
      "http://example.com",
      "Popup inherits the HTTP principal"
    );

    let cookie = await win.cookieStore.get("secret");
    is(cookie, null, "Secure cookie is not returned to an HTTP process");

    let all = await win.cookieStore.getAll();
    Assert.deepEqual(
      all.map(c => c.name),
      ["public"],
      "Only the non-Secure cookie is returned to an HTTP process"
    );

    await win.cookieStore.set("written", "value");
    await win.cookieStore.delete("secret");
  });

  ok(!cookieExists("written"), "HTTP process cannot set a Secure cookie");
  ok(cookieExists("secret"), "HTTP process cannot delete a Secure cookie");

  let popup = await popupPromise;

  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(popup);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_https_process_can_read_secure_cookie() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, HTTPS_URL);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    ok(content.isSecureContext, "HTTPS page is a secure context");
    let cookie = await content.cookieStore.get("secret");
    is(cookie?.value, "secureValue", "Secure cookie is returned over HTTPS");
  });

  BrowserTestUtils.removeTab(tab);
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const URL_EXAMPLE =
  "https://example.com/browser/netwerk/cookie/test/browser/file_empty.html";
const COOKIE_COUNT = 300;

registerCleanupFunction(() => {
  Services.cookies.removeAll();
});

add_task(async function test_large_child_cookie_list() {
  Services.cookies.removeAll();
  const maxCookiesPerHost = Services.prefs.getIntPref(
    "network.cookie.maxPerHost"
  );

  await BrowserTestUtils.withNewTab(URL_EXAMPLE, async browser => {
    await SpecialPowers.spawn(browser, [COOKIE_COUNT], cookieCount => {
      function cookiesByName() {
        return new Map(
          content.document.cookie
            .split("; ")
            .filter(Boolean)
            .map(cookie => {
              const equals = cookie.indexOf("=");
              return [cookie.slice(0, equals), cookie.slice(equals + 1)];
            })
        );
      }

      // Keep all writes in this task. The child updates its local cookie cache
      // synchronously, while the parent's quota-driven removals arrive later.
      for (let i = 0; i < cookieCount; ++i) {
        content.document.cookie = `child-large-${i}=v${i}; Secure; Path=/`;
      }

      let cookies = cookiesByName();
      is(
        cookies.size,
        cookieCount,
        "all child-side cookies are immediately visible"
      );
      is(
        cookies.get("child-large-299"),
        "v299",
        "lookup works after promotion"
      );

      content.document.cookie = "child-large-151=; Max-Age=0; Secure; Path=/";
      cookies = cookiesByName();
      is(cookies.size, cookieCount - 1, "deleting a cookie shrinks the list");
      ok(!cookies.has("child-large-151"), "deletion works after promotion");

      content.document.cookie = "child-large-150=updated; Secure; Path=/";
      cookies = cookiesByName();
      is(
        cookies.size,
        cookieCount - 1,
        "replacing a cookie keeps the list size"
      );
      is(
        cookies.get("child-large-150"),
        "updated",
        "replacement works after promotion"
      );
    });

    // The replacement is the final write, so observing it in the parent proves
    // the complete async write stream has been processed.
    await TestUtils.waitForCondition(() => {
      const cookies = Services.cookies.getCookiesFromHost("example.com", {});
      return cookies.some(
        cookie =>
          cookie.name === "child-large-150" && cookie.value === "updated"
      );
    }, "parent receives the replacement cookie");

    ok(
      !Services.cookies.cookieExists("example.com", "/", "child-large-151", {}),
      "parent receives the deletion"
    );
    ok(
      Services.cookies.cookieExists("example.com", "/", "child-large-299", {}),
      "newest initial cookie survives quota purge"
    );

    Assert.lessOrEqual(
      Services.cookies.getCookiesFromHost("example.com", {}).length,
      maxCookiesPerHost,
      "parent enforces the normal per-host cap"
    );

    // Yielding lets quota-removal IPC run in the content process. The list stays
    // in its promoted representation, so this exercises promoted-mode removals.
    await SpecialPowers.spawn(
      browser,
      [maxCookiesPerHost],
      async maxCookies => {
        await ContentTaskUtils.waitForCondition(() => {
          const value = content.document.cookie;
          return !value || value.split("; ").length <= maxCookies;
        }, "child receives the parent's quota removals");

        ok(
          content.document.cookie.includes("child-large-299=v299"),
          "newest cookie remains visible after quota purge"
        );
      }
    );
  });
});

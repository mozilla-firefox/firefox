/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

/* exported testCookies */
/* import-globals-from head.js */

async function testCookies(options) {
  // Changing the options object is a bit of a hack, but it allows us to easily
  // pass an expiration date to the background script.
  options.expiry = Date.now() / 1000 + 3600;

  async function background(backgroundOptions) {
    // Ask the parent scope to change some cookies we may or may not have
    // permission for.
    let awaitChanges = new Promise(resolve => {
      browser.test.onMessage.addListener(msg => {
        browser.test.assertEq("cookies-changed", msg, "browser.test.onMessage");
        resolve();
      });
    });

    let changed = [];
    browser.cookies.onChanged.addListener(event => {
      changed.push(`${event.cookie.name}:${event.cause}`);
    });
    browser.test.sendMessage("change-cookies");

    // Try to access some cookies in various ways.
    let { url, domain, secure } = backgroundOptions;

    let failures = 0;
    let tallyFailure = () => {
      failures++;
    };

    try {
      await awaitChanges;

      let cookie = await browser.cookies.get({ url, name: "foo" });
      browser.test.assertEq(
        backgroundOptions.shouldPass,
        cookie != null,
        "should pass == get cookie"
      );

      let cookies = await browser.cookies.getAll({ domain });
      if (backgroundOptions.shouldPass) {
        browser.test.assertEq(2, cookies.length, "expected number of cookies");
      } else {
        browser.test.assertEq(0, cookies.length, "expected number of cookies");
      }

      await Promise.all([
        browser.cookies
          .set({
            url,
            domain,
            secure,
            name: "foo",
            value: "baz",
            expirationDate: backgroundOptions.expiry,
          })
          .catch(tallyFailure),
        browser.cookies
          .set({
            url,
            domain,
            secure,
            name: "bar",
            value: "quux",
            expirationDate: backgroundOptions.expiry,
          })
          .catch(tallyFailure),
        browser.cookies.remove({ url, name: "deleted" }),
      ]);

      if (backgroundOptions.shouldPass) {
        // The order of eviction events isn't guaranteed, so just check that
        // it's there somewhere.
        let evicted = changed.indexOf("evicted:evicted");
        if (evicted < 0) {
          browser.test.fail("got no eviction event");
        } else {
          browser.test.succeed("got eviction event");
          changed.splice(evicted, 1);
        }

        browser.test.assertEq(
          "x:explicit,x:overwrite,x:explicit,x:explicit,foo:overwrite,foo:explicit,bar:explicit,deleted:explicit",
          changed.join(","),
          "expected changes"
        );
      } else {
        browser.test.assertEq("", changed.join(","), "expected no changes");
      }

      if (!(backgroundOptions.shouldPass || backgroundOptions.shouldWrite)) {
        browser.test.assertEq(2, failures, "Expected failures");
      } else {
        browser.test.assertEq(0, failures, "Expected no failures");
      }

      browser.test.notifyPass("cookie-permissions");
    } catch (error) {
      browser.test.fail(`Error: ${error} :: ${error.stack}`);
      browser.test.notifyFail("cookie-permissions");
    }
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: options.permissions,
    },

    background: `(${background})(${JSON.stringify(options)})`,
  });

  let stepOne = loadChromeScript(() => {
    const { addMessageListener, sendAsyncMessage } = this;
    addMessageListener("options", options => {
      let domain = options.domain.replace(/^\.?/, ".");
      // This will be evicted after we add a fourth cookie.
      Services.cookies.add(
        domain,
        "/",
        "evicted",
        "bar",
        options.secure,
        false,
        false,
        options.expiry,
        {},
        Ci.nsICookie.SAMESITE_NONE,
        options.url.startsWith("https")
          ? Ci.nsICookie.SCHEME_HTTPS
          : Ci.nsICookie.SCHEME_HTTP
      );
      // This will be modified by the background script
      Services.cookies.add(
        domain,
        "/",
        "foo",
        "bar",
        options.secure,
        false,
        false,
        options.expiry,
        {},
        Ci.nsICookie.SAMESITE_NONE,
        options.url.startsWith("https")
          ? Ci.nsICookie.SCHEME_HTTPS
          : Ci.nsICookie.SCHEME_HTTP
      );
      // This will be deleted by the background script.
      Services.cookies.add(
        domain,
        "/",
        "deleted",
        "bar",
        options.secure,
        false,
        false,
        options.expiry,
        {},
        Ci.nsICookie.SAMESITE_NONE,
        options.url.startsWith("https")
          ? Ci.nsICookie.SCHEME_HTTPS
          : Ci.nsICookie.SCHEME_HTTP
      );
      sendAsyncMessage("done");
    });
  });
  stepOne.sendAsyncMessage("options", options);
  await stepOne.promiseOneMessage("done");
  stepOne.destroy();

  await extension.startup();

  await extension.awaitMessage("change-cookies");

  let stepTwo = loadChromeScript(() => {
    const { addMessageListener, sendAsyncMessage } = this;
    addMessageListener("options", options => {
      let domain = options.domain.replace(/^\.?/, ".");

      // this is the fourth cookie, it triggers a purge
      Services.cookies.add(
        domain,
        "/",
        "x",
        "y",
        options.secure,
        false,
        false,
        options.expiry,
        {},
        Ci.nsICookie.SAMESITE_NONE,
        options.url.startsWith("https")
          ? Ci.nsICookie.SCHEME_HTTPS
          : Ci.nsICookie.SCHEME_HTTP
      );
      // overwrite the latest cookie
      Services.cookies.add(
        domain,
        "/",
        "x",
        "z",
        options.secure,
        false,
        false,
        options.expiry,
        {},
        Ci.nsICookie.SAMESITE_NONE,
        options.url.startsWith("https")
          ? Ci.nsICookie.SCHEME_HTTPS
          : Ci.nsICookie.SCHEME_HTTP
      );
      Services.cookies.remove(domain, "x", "/", {});
      sendAsyncMessage("done");
    });
  });
  stepTwo.sendAsyncMessage("options", options);
  await stepTwo.promiseOneMessage("done");
  stepTwo.destroy();

  extension.sendMessage("cookies-changed");

  await extension.awaitFinish("cookie-permissions");
  await extension.unload();

  let stepThree = loadChromeScript(() => {
    const { addMessageListener, sendAsyncMessage, assert } = this;
    let cookieSvc = Services.cookies;

    function getCookies(host) {
      let cookies = [];
      for (let cookie of cookieSvc.getCookiesFromHost(host, {})) {
        cookies.push(cookie);
      }
      return cookies.sort((a, b) => a.name.localeCompare(b.name));
    }

    addMessageListener("options", options => {
      let cookies = getCookies(options.domain);

      if (options.shouldPass) {
        assert.equal(cookies.length, 2, "expected two cookies for host");

        assert.equal(cookies[0].name, "bar", "correct cookie name");
        assert.equal(cookies[0].value, "quux", "correct cookie value");

        assert.equal(cookies[1].name, "foo", "correct cookie name");
        assert.equal(cookies[1].value, "baz", "correct cookie value");
      } else if (options.shouldWrite) {
        // Note: |shouldWrite| applies only when |shouldPass| is false.
        // This is necessary because, unfortunately, websites (and therefore web
        // extensions) are allowed to write some cookies which they're not allowed
        // to read.
        assert.equal(cookies.length, 3, "expected three cookies for host");

        assert.equal(cookies[0].name, "bar", "correct cookie name");
        assert.equal(cookies[0].value, "quux", "correct cookie value");

        assert.equal(cookies[1].name, "deleted", "correct cookie name");

        assert.equal(cookies[2].name, "foo", "correct cookie name");
        assert.equal(cookies[2].value, "baz", "correct cookie value");
      } else {
        assert.equal(cookies.length, 2, "expected two cookies for host");

        assert.equal(cookies[0].name, "deleted", "correct second cookie name");

        assert.equal(cookies[1].name, "foo", "correct cookie name");
        assert.equal(cookies[1].value, "bar", "correct cookie value");
      }

      for (let cookie of cookies) {
        cookieSvc.remove(cookie.host, cookie.name, "/", {});
      }
      // Make sure we don't silently poison subsequent tests if something goes wrong.
      assert.equal(getCookies(options.domain).length, 0, "cookies cleared");
      sendAsyncMessage("done");
    });
  });
  stepThree.sendAsyncMessage("options", options);
  await stepThree.promiseOneMessage("done");
  stepThree.destroy();
}

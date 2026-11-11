/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TippyTopProvider:
    "moz-src:///browser/components/topsites/TippyTopProvider.sys.mjs",
});

/**
 * Constructs an initialized TippyTopProvider with some prepared site icons
 * for testing.
 *
 * @param {SinonSandbox} sandbox
 *   The Sinon sandbox used to create the stubs and ensure cleanup.
 * @returns {Promise<TippyTopProvider>}
 */
async function getTippyTopProviderForTest(sandbox) {
  let instance = new TippyTopProvider();
  let fetchStub = sandbox.stub(instance, "fetch");
  fetchStub.resolves({
    ok: true,
    status: 200,
    json: () =>
      Promise.resolve([
        {
          domains: ["facebook.com"],
          image_url: "images/facebook-com.png",
          favicon_url: "images/facebook-com.png",
          background_color: "#3b5998",
        },
        {
          domains: ["gmail.com", "mail.google.com"],
          image_url: "images/gmail-com.png",
          favicon_url: "images/gmail-com.png",
          background_color: "#000000",
        },
      ]),
  });

  await instance.init();

  return instance;
}

add_task(async function test_provide_facebook_icon() {
  info("should provide an icon for facebook.com");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  let site = instance.processSite({ url: "https://facebook.com" });
  Assert.equal(
    site.tippyTopIcon,
    "chrome://activity-stream/content/data/content/tippytop/images/facebook-com.png"
  );
  Assert.equal(
    site.smallFavicon,
    "chrome://activity-stream/content/data/content/tippytop/images/facebook-com.png"
  );
  Assert.equal(site.backgroundColor, "#3b5998");

  sandbox.restore();
});

add_task(async function test_dont_provide_other_facebook_icon() {
  info("should not provide an icon for other.facebook.com");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  const site = instance.processSite({ url: "https://other.facebook.com" });
  Assert.equal(site.tippyTopIcon, undefined);

  sandbox.restore();
});

add_task(async function test_provide_other_facebook_icon_stripping() {
  info("should provide an icon for other.facebook.com with stripping");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  let site = instance.processSite({ url: "https://other.facebook.com" }, "*");
  Assert.equal(
    site.tippyTopIcon,
    "chrome://activity-stream/content/data/content/tippytop/images/facebook-com.png"
  );

  sandbox.restore();
});

add_task(async function test_provide_facebook_icon_foobar() {
  info("should provide an icon for facebook.com/foobar");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  let site = instance.processSite({ url: "https://facebook.com/foobar" });
  Assert.equal(
    site.tippyTopIcon,
    "chrome://activity-stream/content/data/content/tippytop/images/facebook-com.png"
  );
  Assert.equal(
    site.smallFavicon,
    "chrome://activity-stream/content/data/content/tippytop/images/facebook-com.png"
  );
  Assert.equal(site.backgroundColor, "#3b5998");

  sandbox.restore();
});

add_task(async function test_provide_gmail_icon() {
  info("should provide an icon for gmail.com");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  const site = instance.processSite({ url: "https://gmail.com" });
  Assert.equal(
    site.tippyTopIcon,
    "chrome://activity-stream/content/data/content/tippytop/images/gmail-com.png"
  );
  Assert.equal(
    site.smallFavicon,
    "chrome://activity-stream/content/data/content/tippytop/images/gmail-com.png"
  );
  Assert.equal(site.backgroundColor, "#000000");

  sandbox.restore();
});

add_task(async function test_provide_mail_google_icon() {
  info("should provide an icon for mail.google.com");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  const site = instance.processSite({ url: "https://mail.google.com" });
  Assert.equal(
    site.tippyTopIcon,
    "chrome://activity-stream/content/data/content/tippytop/images/gmail-com.png"
  );
  Assert.equal(
    site.smallFavicon,
    "chrome://activity-stream/content/data/content/tippytop/images/gmail-com.png"
  );
  Assert.equal(site.backgroundColor, "#000000");

  sandbox.restore();
});

add_task(async function test_garbage_urls() {
  info("should handle garbage URLs gracefully");

  let sandbox = sinon.createSandbox();
  let instance = await getTippyTopProviderForTest(sandbox);
  const site = instance.processSite({ url: "garbagejlfkdsa" });
  Assert.equal(site.tippyTopIcon, undefined);
  Assert.equal(site.backgroundColor, undefined);

  sandbox.restore();
});

add_task(async function test_failed_manifest_parse() {
  info("should handle error when fetching and parsing manifest");

  let sandbox = sinon.createSandbox();
  let instance = new TippyTopProvider();
  let fetchStub = sandbox.stub(instance, "fetch");
  fetchStub.rejects("whaaaa");

  await instance.init();
  let site = instance.processSite({ url: "https://facebook.com" });
  Assert.equal(site.tippyTopIcon, undefined);
  Assert.equal(site.backgroundColor, undefined);

  sandbox.restore();
});

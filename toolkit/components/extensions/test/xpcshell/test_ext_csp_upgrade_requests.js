"use strict";

const server = createHttpServer({ hosts: ["example.com"] });
server.registerPathHandler("/", (req, res) => {
  res.write("ok");
});

add_task(async function test_csp_upgrade() {
  async function background() {
    browser.webRequest.onBeforeRequest.addListener(
      details => {
        browser.test.assertEq(
          details.url,
          "https://example.com/",
          "request upgraded and sent"
        );
        browser.test.notifyPass();
        return { cancel: true };
      },
      {
        urls: ["https://example.com/*"],
      },
      ["blocking"]
    );

    await browser.test.assertRejects(
      fetch("http://example.com/"),
      "NetworkError when attempting to fetch resource.",
      "request was upgraded"
    );
  }
  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://example.com/*"],
      permissions: ["webRequest", "webRequestBlocking"],
    },
  });
  await extension.startup();
  await extension.awaitFinish();
  await extension.unload();
});

add_task(async function test_csp_noupgrade() {
  async function background() {
    let req = await fetch("http://example.com/");
    browser.test.assertEq(
      req.url,
      "http://example.com/",
      "request not upgraded"
    );
    browser.test.notifyPass();
  }
  let extension = ExtensionTestUtils.loadExtension({
    background,
    temporarilyInstalled: true,
    allowInsecureRequests: true,
    manifest: {
      manifest_version: 3,
      granted_host_permissions: true,
      host_permissions: ["*://example.com/*"],
    },
  });
  await extension.startup();
  await extension.awaitFinish();
  await extension.unload();
});

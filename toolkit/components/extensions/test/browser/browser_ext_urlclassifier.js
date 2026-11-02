/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

AddonTestUtils.initMochitest(this);

const server = AddonTestUtils.createHttpServer({
  hosts: ["expected.example.org", "extra.example.org"],
});
server.registerPathHandler("/", (req, res) => {
  info(`Test HTTP server for domain "${req.host}" got ${req.method} request\n`);
  res.setStatusLine(req.httpVersion, 200, "OK");
  res.write("OK");
});

add_task(async function test_extension_tab_create() {
  Services.fog.testResetFOG();

  // This exercises url-classifier's own harmful-addon feature, so keep it as
  // the channel classifier where the content classifier is the default.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.content.mirror.mode", 0],
      [
        "privacy.trackingprotection.urlclassifier.disable_for_channel_classifier",
        false,
      ],
      [
        "urlclassifier.features.harmfuladdon.blocklistHosts",
        "expected.example.org",
      ],
    ],
  });
  const id = "ext-create-tab@mochitest";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      version: "1.2.3",
      browser_specific_settings: {
        gecko: { id },
      },
      host_permissions: [
        "*://expected.example.org/*",
        "*://extra.example.org/*",
      ],
    },
    background() {
      const { browser } = this;
      let tab;
      browser.test.onMessage.addListener(async (msg, ...args) => {
        if (msg === "create-tab") {
          tab = await browser.tabs.create({
            url: "about:blank",
            active: true,
          });
        } else if (msg === "load-tab") {
          await browser.tabs.update(tab.id, { url: args[0] });
        } else {
          browser.test.fail(`Got unexpected test message ${msg}`);
        }
        browser.test.sendMessage(`${msg}:done`);
      });
    },
  });

  await extension.startup();

  extension.sendMessage("create-tab");
  await extension.awaitMessage("create-tab:done");

  const aboutBlockedLoaded = BrowserTestUtils.waitForContentEvent(
    gBrowser.selectedTab.linkedBrowser,
    "AboutBlockedLoaded",
    true,
    undefined,
    true
  );

  // eslint-disable-next-line sdl/no-insecure-url
  extension.sendMessage("load-tab", "http://expected.example.org");
  await extension.awaitMessage("load-tab:done");

  info("Wait for custom Glean ping submit");
  const gleanEvents = Glean.network.urlclassifierHarmfulAddonBlock
    .testGetValue()
    ?.map(evt => evt.extra);
  Assert.deepEqual(
    gleanEvents,
    [
      {
        addon_id: id,
        addon_version: "1.2.3",
        table: "harmfuladdon-blocklist-pref",
        etld: "example.org",
      },
    ],
    "Got the expected Glean events"
  );

  await aboutBlockedLoaded;

  Services.fog.testResetFOG();

  // eslint-disable-next-line sdl/no-insecure-url
  extension.sendMessage("load-tab", "http://extra.example.org");
  await extension.awaitMessage("load-tab:done");

  const newGleanEvents =
    Glean.network.urlclassifierHarmfulAddonBlock.testGetValue();
  Assert.deepEqual(newGleanEvents, null, "No glean event received");

  await extension.unload();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

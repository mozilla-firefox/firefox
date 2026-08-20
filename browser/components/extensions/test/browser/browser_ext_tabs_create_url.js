"use strict";

async function runWithDisabledPrivateBrowsing(callback) {
  const { EnterprisePolicyTesting, PoliciesPrefTracker } =
    ChromeUtils.importESModule(
      "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
    );

  PoliciesPrefTracker.start();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: { DisablePrivateBrowsing: true },
  });

  try {
    await callback();
  } finally {
    await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
    EnterprisePolicyTesting.resetRunOnceState();
    PoliciesPrefTracker.stop();
  }
}

add_task(async function test_urlbar_focus() {
  // Disable preloaded new tab because the urlbar is automatically focused when
  // a preloaded new tab is opened, while this test is supposed to test that the
  // implementation of tabs.create automatically focuses the urlbar of new tabs.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtab.preload", false]],
  });

  const extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.tabs.onUpdated.addListener(function onUpdated(_, info, tab) {
        if (info.status === "complete" && tab.url !== "about:blank") {
          browser.test.sendMessage("complete");
          browser.tabs.onUpdated.removeListener(onUpdated);
        }
      });
      browser.test.onMessage.addListener(async (cmd, ...args) => {
        const result = await browser.tabs[cmd](...args);
        browser.test.sendMessage("result", result);
      });
    },
  });

  await extension.startup();

  // Test content is focused after opening a regular url
  extension.sendMessage("create", { url: "https://example.com" });
  const [tab1] = await Promise.all([
    extension.awaitMessage("result"),
    extension.awaitMessage("complete"),
  ]);

  is(
    document.activeElement.tagName,
    "browser",
    "Content focused after opening a web page"
  );

  extension.sendMessage("remove", tab1.id);
  await extension.awaitMessage("result");

  // Test urlbar is focused after opening an empty tab
  extension.sendMessage("create", {});
  const tab2 = await extension.awaitMessage("result");

  const active = document.activeElement;
  info(
    `Active element: ${active.tagName}, id: ${active.id}, class: ${active.className}`
  );

  const parent = active.parentNode;
  info(
    `Parent element: ${parent.tagName}, id: ${parent.id}, class: ${parent.className}`
  );

  info(`After opening an empty tab, gURLBar.focused: ${gURLBar.focused}`);

  is(active.localName, "input", "Input element focused");
  ok(active.classList.contains("urlbar-input"), "Urlbar focused");

  extension.sendMessage("remove", tab2.id);
  await extension.awaitMessage("result");

  await extension.unload();
});

add_task(async function default_url() {
  const extension = ExtensionTestUtils.loadExtension({
    incognitoOverride: "spanning",
    manifest: {
      permissions: ["tabs"],
    },
    background() {
      function promiseNonBlankTab() {
        return new Promise(resolve => {
          browser.tabs.onUpdated.addListener(
            function listener(tabId, changeInfo, tab) {
              if (
                changeInfo.status === "complete" &&
                tab.url !== "about:blank"
              ) {
                browser.tabs.onUpdated.removeListener(listener);
                resolve(tab);
              }
            }
          );
        });
      }

      browser.test.onMessage.addListener(
        async (msg, { incognito, expectedNewWindowUrl, expectedNewTabUrl }) => {
          browser.test.assertEq(
            "start",
            msg,
            `Start test, incognito=${incognito}`
          );

          let tabPromise = promiseNonBlankTab();
          let win;
          try {
            win = await browser.windows.create({ incognito });
            browser.test.assertEq(
              1,
              win.tabs.length,
              "Expected one tab in the new window."
            );
          } catch (e) {
            browser.test.assertEq(
              expectedNewWindowUrl,
              e.message,
              "Expected error"
            );
            browser.test.sendMessage("done");
            return;
          }
          let tab = await tabPromise;
          browser.test.assertEq(
            expectedNewWindowUrl,
            tab.url,
            "Expected default URL of new window"
          );

          tabPromise = promiseNonBlankTab();
          await browser.tabs.create({ windowId: win.id });
          tab = await tabPromise;
          browser.test.assertEq(
            expectedNewTabUrl,
            tab.url,
            "Expected default URL of new tab"
          );

          await browser.windows.remove(win.id);
          browser.test.sendMessage("done");
        }
      );
    },
  });

  await extension.startup();

  extension.sendMessage("start", {
    incognito: false,
    expectedNewWindowUrl: "about:home",
    expectedNewTabUrl: "about:newtab",
  });
  await extension.awaitMessage("done");
  extension.sendMessage("start", {
    incognito: true,
    expectedNewWindowUrl: "about:privatebrowsing",
    expectedNewTabUrl: "about:privatebrowsing",
  });
  await extension.awaitMessage("done");

  info("Testing with multiple homepages.");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.startup.homepage", "about:robots|about:blank|about:home"]],
  });
  extension.sendMessage("start", {
    incognito: false,
    expectedNewWindowUrl: "about:robots",
    expectedNewTabUrl: "about:newtab",
  });
  await extension.awaitMessage("done");
  extension.sendMessage("start", {
    incognito: true,
    expectedNewWindowUrl: "about:privatebrowsing",
    expectedNewTabUrl: "about:privatebrowsing",
  });
  await extension.awaitMessage("done");
  await SpecialPowers.popPrefEnv();

  info("Testing with perma-private browsing mode.");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.autostart", true]],
  });
  extension.sendMessage("start", {
    incognito: false,
    expectedNewWindowUrl: "about:home",
    expectedNewTabUrl: "about:newtab",
  });
  await extension.awaitMessage("done");
  extension.sendMessage("start", {
    incognito: true,
    expectedNewWindowUrl: "about:home",
    expectedNewTabUrl: "about:newtab",
  });
  await extension.awaitMessage("done");
  await SpecialPowers.popPrefEnv();

  info("Testing with disabled private browsing mode.");
  await runWithDisabledPrivateBrowsing(async () => {
    extension.sendMessage("start", {
      incognito: false,
      expectedNewWindowUrl: "about:home",
      expectedNewTabUrl: "about:newtab",
    });
    await extension.awaitMessage("done");
    extension.sendMessage("start", {
      incognito: true,
      expectedNewWindowUrl:
        "`incognito` cannot be used if incognito mode is disabled",
    });
    await extension.awaitMessage("done");
  });

  await extension.unload();
});

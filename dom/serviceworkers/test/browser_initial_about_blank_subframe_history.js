"use strict";

// A controlled page embeds a cross-origin iframe. The iframe navigates itself
// to about:blank, its SH entry is an about:blank with cross-origin principal.
// When the embedder is reloaded, that iframe entry is restored onto the new
// docshell and treated as initial load. But the initial about:blank code
// expects to inherit a controller from the embedder into the client.
// See bug 2021375

const PARENT_ORIGIN = "https://example.com";
const SUBFRAME_ORIGIN = "https://test1.example.com";
const TEST_PATH = "/browser/dom/serviceworkers/test/";

const PARENT_URI = PARENT_ORIGIN + TEST_PATH + "subframe_container.html";
const SW_SCRIPT = PARENT_ORIGIN + TEST_PATH + "empty.js";
const SW_SCOPE = PARENT_ORIGIN + TEST_PATH;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.serviceWorkers.enabled", true],
      ["dom.serviceWorkers.testing.enabled", true],
    ],
  });
});

async function isControlled(browsingContextOrBrowser) {
  return SpecialPowers.spawn(browsingContextOrBrowser, [], () => {
    return !!content.navigator.serviceWorker.controller;
  });
}

function checkSubframeIsAboutBlank(wgp, when) {
  is(wgp.documentURI.spec, "about:blank", `Subframe on about:blank ${when}`);
  is(wgp.documentPrincipal.origin, SUBFRAME_ORIGIN, `Origin kept ${when}`);
}

add_task(async function testSubframeHistoryRestoreOntoInitialAboutBlank() {
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PARENT_URI);
  const browser = tab.linkedBrowser;

  info("Register a ServiceWorker for the top level page");
  await SpecialPowers.spawn(
    browser,
    [{ script: SW_SCRIPT, scope: SW_SCOPE }],
    async opts => {
      await content.wrappedJSObject.registerAndWaitForActive(
        opts.script,
        opts.scope
      );
    }
  );

  info("Reload so that the top level page becomes controlled");
  await BrowserTestUtils.reloadTab(tab);

  ok(await isControlled(browser), "Top page controlled");

  info("Navigate the subframe to about:blank from within the subframe");
  await SpecialPowers.spawn(browser, [], async () => {
    const ifr = content.document.getElementById("subframe");
    const loaded = ContentTaskUtils.waitForEvent(ifr, "load");
    ifr.contentWindow.postMessage("about:blank", "*");
    await loaded;
  });

  checkSubframeIsAboutBlank(
    browser.browsingContext.children[0].currentWindowGlobal,
    "before reload"
  );

  info("Reload the top level page, restoring the subframe's history entry");
  await BrowserTestUtils.reloadTab(tab);

  ok(!browser.isCrashed, "Content process didn't crash restoring subframe");

  const childBC = browser.browsingContext.children[0];
  checkSubframeIsAboutBlank(childBC.currentWindowGlobal, "after reload");
  ok(await isControlled(browser), "Top page still controlled");

  ok(
    !(await isControlled(childBC)),
    "Restored about:blank must not inherit cross-origin controller"
  );

  info("Clean up");
  await SpecialPowers.spawn(browser, [], async () => {
    await content.wrappedJSObject.unregisterAll();
  });
  BrowserTestUtils.removeTab(tab);
});

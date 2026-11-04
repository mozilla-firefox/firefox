/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint no-unused-vars: [2, {"vars": "local"}] */

"use strict";

// Load the shared-head file first.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/shared-head.js",
  this
);

// Cleanup preferences that may be set by tests when interacting with the UI
registerCleanupFunction(function () {
  Services.prefs.clearUserPref("devtools.application.selectedSidebar");
});

/**
 * Set all preferences needed to enable service worker debugging and testing.
 */
async function enableServiceWorkerDebugging() {
  // Enable service workers.
  await pushPref("dom.serviceWorkers.enabled", true);
  // Accept workers from mochitest's http.
  await pushPref("dom.serviceWorkers.testing.enabled", true);
  // Force single content process, see Bug 1231208 for the SW refactor that should enable
  // SW debugging in multi-e10s.
  await pushPref("dom.ipc.processCount", 1);

  // Disable randomly spawning processes during tests
  await pushPref("dom.ipc.processPrelaunch.enabled", false);

  // Wait for dom.ipc.processCount to be updated before releasing processes.
  Services.ppmm.releaseCachedProcesses();
}

async function enableApplicationPanel() {
  // FIXME bug 1575427 this rejection is very common.
  const { PromiseTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/PromiseTestUtils.sys.mjs"
  );
  PromiseTestUtils.allowMatchingRejectionsGlobally(
    /this._frontCreationListeners is null/
  );

  // Enable all preferences related to service worker debugging.
  await enableServiceWorkerDebugging();

  // Enable web manifest processing.
  Services.prefs.setBoolPref("dom.manifest.enabled", true);

  // Enable application panel in DevTools.
  await pushPref("devtools.application.enabled", true);
}

function getWorkerContainers(doc) {
  return doc.querySelectorAll(".js-sw-container");
}

async function openNewTabAndApplicationPanel(url) {
  const tab = await addTab(url);

  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "application",
  });
  const panel = toolbox.getCurrentPanel();
  const target = toolbox.target;
  const commands = toolbox.commands;
  return { panel, tab, target, toolbox, commands };
}

async function unregisterAllWorkers(client, doc) {
  // This method is declared in shared-head.js
  await unregisterAllServiceWorkers(client);

  info("Wait for service workers to disappear from the UI");
  await waitUntil(() => getWorkerContainers(doc).length === 0);
}

async function waitForWorkerRegistration(swTab) {
  info("Wait until the registration appears on the window");
  const swBrowser = swTab.linkedBrowser;
  await asyncWaitUntil(async () =>
    SpecialPowers.spawn(swBrowser, [], function () {
      return !!content.wrappedJSObject.getRegistration();
    })
  );
}

/**
 * Select a page by simulating a user click in the sidebar.
 *
 * @param {string} page The page we want to select (see `PAGE_TYPES`)
 */
async function selectPage(panel, page) {
  info(`Selecting application page: ${page}`);
  const doc = panel.panelWin.document;
  const navItem = doc.querySelector(`.js-sidebar-${page}`);
  // Force/wait for the element to be painted before clicking, to avoid a11y
  // checks issues (Bug 1946641).
  await waitFor(() => navItem.getBoundingClientRect().width > 0);
  navItem.click();
}

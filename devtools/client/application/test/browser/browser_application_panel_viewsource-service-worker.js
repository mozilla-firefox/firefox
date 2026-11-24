/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TAB_URL = URL_ROOT + "resources/service-workers/debug.html";
const SW_URL = URL_ROOT + "resources/service-workers/debug-sw.js";

add_task(async function () {
  await enableApplicationPanel();

  // disable service worker debugging
  await pushPref(
    "devtools.debugger.features.windowless-service-workers",
    false
  );

  const { panel, tab, commands } = await openNewTabAndApplicationPanel(TAB_URL);
  const doc = panel.panelWin.document;

  await selectPage(panel, "service-workers");

  info("Wait until the service worker appears in the application panel");
  await waitUntil(() => getWorkerContainers(doc).length === 1);

  const container = getWorkerContainers(doc)[0];
  info("Wait until the inspect link is displayed");
  await waitUntil(() => {
    return container.querySelector(".js-inspect-link");
  });

  info("Click on the inspect link and wait for a new view-source: tab open");
  // click on the link and wait for the new tab to open
  const onTabLoaded = BrowserTestUtils.waitForNewTab(
    gBrowser,
    `view-source:${SW_URL}`
  );
  const inspectLink = container.querySelector(".js-inspect-link");
  inspectLink.click();

  const sourceTab = await onTabLoaded;
  ok(sourceTab, "The service worker source was opened in a new tab");

  // clean up
  await unregisterAllWorkers(commands.client, doc);
  // close the tabs
  info("Closing the tabs.");
  await BrowserTestUtils.removeTab(sourceTab);
  await BrowserTestUtils.removeTab(tab);
});

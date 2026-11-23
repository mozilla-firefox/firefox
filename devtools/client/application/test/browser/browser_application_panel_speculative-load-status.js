/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const HTTPS_ROOT = URL_ROOT.replace("http:", "https:");
const SOURCE_URL = HTTPS_ROOT + "resources/service-workers/empty.html";
const STATUS_TARGET =
  HTTPS_ROOT + "resources/speculative-loads/prefetch-status.sjs";
const RUN = Date.now();

add_setup(async function () {
  await enableApplicationPanel();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.speculation_rules.enabled", true],
      ["dom.performance.deliverytype.enabled", false],
    ],
  });
});

async function waitForStatus(panel, status) {
  const doc = panel.panelWin.document;
  await waitFor(
    () =>
      doc.querySelector(".speculative-loads-status")?.dataset.prefetchStatus ===
      status
  );
  const node = doc.querySelector(".speculative-loads-status");
  is(
    doc.querySelector(".speculative-loads-pane .app-page__title").textContent,
    "Speculative load status for this page",
    "The existing section heading is preserved"
  );
  const expectedText = {
    prefetched: "Navigation used a prefetched document response",
    "not-prefetched": "Navigation did not use a prefetched document response",
    unknown: "Prefetch reuse status unavailable",
  };
  is(
    node.textContent,
    expectedText[status],
    "The status text matches the browser signal"
  );
}

async function prefetchTarget(browser, url) {
  const completed = TestUtils.topicObserved("http-on-stop-request", subject => {
    return subject.QueryInterface(Ci.nsIHttpChannel).URI.spec === url;
  });
  await SpecialPowers.spawn(browser, [url], target => {
    const rule = content.document.createElement("script");
    rule.type = "speculationrules";
    rule.textContent = JSON.stringify({
      prefetch: [{ source: "list", eagerness: "immediate", urls: [target] }],
    });
    content.document.head.appendChild(rule);
  });
  await completed;
}

async function assertChannelStatus(browser, expected) {
  const activated = await SpecialPowers.spawn(browser, [], () => {
    return docShell.currentDocumentChannel.loadInfo
      .activatedFromNavigationalPrefetch;
  });
  is(
    activated,
    expected,
    "The document channel has the expected activation flag"
  );
}

add_task(async function test_prefetch_activation_and_history() {
  let { panel, tab, toolbox } = await openNewTabAndApplicationPanel(SOURCE_URL);
  await selectPage(panel, "speculative-loads");
  await waitForStatus(panel, "not-prefetched");

  const target = `${STATUS_TARGET}?activation=${RUN}`;
  await prefetchTarget(tab.linkedBrowser, target);
  await waitForStatus(panel, "not-prefetched");
  await navigateTo(target);
  await assertChannelStatus(tab.linkedBrowser, true);
  await waitForStatus(panel, "prefetched");

  const deliveryType = await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    return (
      content.performance.getEntriesByType("navigation")[0].deliveryType ?? ""
    );
  });
  is(
    deliveryType,
    "",
    "The panel works without the web-exposed deliveryType signal"
  );

  info(
    "Opening DevTools after activation reads the existing document's status"
  );
  await toolbox.closeToolbox();
  toolbox = await gDevTools.showToolboxForTab(tab, { toolId: "application" });
  panel = toolbox.getCurrentPanel();
  await selectPage(panel, "speculative-loads");
  await waitForStatus(panel, "prefetched");

  const otherOrigin = SOURCE_URL.replace("example.com", "example.org");
  await SpecialPowers.spawn(tab.linkedBrowser, [otherOrigin], async url => {
    const iframe = content.document.createElement("iframe");
    const loaded = new Promise(resolve => {
      iframe.onload = resolve;
    });
    iframe.src = url;
    content.document.body.appendChild(iframe);
    await loaded;
  });
  await waitForStatus(panel, "prefetched");

  info("A normal navigation clears the previous document's positive status");
  await navigateTo(otherOrigin);
  await assertChannelStatus(tab.linkedBrowser, false);
  await waitForStatus(panel, "not-prefetched");

  info("Restoring the prefetched document restores its status");
  const backShown = BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "pageshow"
  );
  const switched = toolbox.commands.targetCommand.once("switched-target");
  gBrowser.goBack();
  await backShown;
  await switched;
  await assertChannelStatus(tab.linkedBrowser, true);
  await waitForStatus(panel, "prefetched");

  info("Reloading creates a normal navigation");
  await navigateTo(target);
  await assertChannelStatus(tab.linkedBrowser, false);
  await waitForStatus(panel, "not-prefetched");
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_cache_hit_is_not_prefetch_activation() {
  const { panel, tab } = await openNewTabAndApplicationPanel(SOURCE_URL);
  await selectPage(panel, "speculative-loads");
  const target = `${STATUS_TARGET}?ordinary-cache=${RUN}`;
  await SpecialPowers.spawn(tab.linkedBrowser, [target], async url => {
    const response = await content.fetch(url);
    await response.text();
  });
  await navigateTo(target);
  await assertChannelStatus(tab.linkedBrowser, false);
  await waitForStatus(panel, "not-prefetched");
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_no_store_prefetch_activation() {
  const { panel, tab } = await openNewTabAndApplicationPanel(SOURCE_URL);
  await selectPage(panel, "speculative-loads");
  const target = `${STATUS_TARGET}?no-store=${RUN}`;
  await prefetchTarget(tab.linkedBrowser, target);
  await navigateTo(target);
  await assertChannelStatus(tab.linkedBrowser, true);
  await waitForStatus(panel, "prefetched");
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_failed_prefetch_is_not_activation() {
  const { panel, tab } = await openNewTabAndApplicationPanel(SOURCE_URL);
  await selectPage(panel, "speculative-loads");
  const target = `${STATUS_TARGET}?failure=${RUN}`;
  await prefetchTarget(tab.linkedBrowser, target);
  await navigateTo(target);
  await assertChannelStatus(tab.linkedBrowser, false);
  await waitForStatus(panel, "not-prefetched");
  await BrowserTestUtils.removeTab(tab);
});

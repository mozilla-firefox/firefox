/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that an engine a page offers under a different title to the name in
 * its OpenSearch description is not offered once it is installed.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  OpenSearchManager:
    "moz-src:///browser/components/search/OpenSearchManager.sys.mjs",
});

const ROOT =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/";
const PAGE_URL = ROOT + "opensearch.html";
// opensearch.html offers this engine as "engine1", but its ShortName is "Foo".
const ENGINE_URL = ROOT + "testEngine.xml";

function offeredTitles(browser) {
  return OpenSearchManager.getEngines(browser).map(e => e.title);
}

async function openPage() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE_URL);
  await TestUtils.waitForCondition(
    () => offeredTitles(tab.linkedBrowser).includes("engine2"),
    "Waiting for the page's engines to be offered"
  );
  return tab;
}

add_task(async function test_installed_engine_is_hidden() {
  let tab = await openPage();
  Assert.ok(
    offeredTitles(tab.linkedBrowser).includes("engine1"),
    "The engine is offered before it is installed"
  );

  let engine = await SearchService.addOpenSearchEngine(ENGINE_URL);
  Assert.equal(engine.name, "Foo", "The engine is installed with its own name");
  Assert.ok(
    !offeredTitles(tab.linkedBrowser).includes("engine1"),
    "The engine is no longer offered once installed"
  );
  Assert.ok(
    offeredTitles(tab.linkedBrowser).includes("engine2"),
    "Other engines are still offered"
  );

  let secondTab = await openPage();
  Assert.ok(
    !offeredTitles(secondTab.linkedBrowser).includes("engine1"),
    "The installed engine is not offered by a newly loaded page"
  );

  await SearchService.removeEngine(engine);
  Assert.ok(
    offeredTitles(tab.linkedBrowser).includes("engine1"),
    "The engine is offered again once removed"
  );
  Assert.ok(
    offeredTitles(secondTab.linkedBrowser).includes("engine1"),
    "The engine is offered again in the newly loaded page"
  );

  BrowserTestUtils.removeTab(secondTab);
  BrowserTestUtils.removeTab(tab);
});

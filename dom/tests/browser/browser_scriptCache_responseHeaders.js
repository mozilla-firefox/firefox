const TEST_URL = "https://example.com/browser/dom/tests/browser/dummy.html";
const SCRIPT_NAME = "responseHeaders_server.sjs";
const TEST_SCRIPT_URL =
  "https://example.com/browser/dom/tests/browser/" + SCRIPT_NAME;

function getCounter(tab, query) {
  const browser = tab.linkedBrowser;
  const scriptPath = SCRIPT_NAME + query;
  return SpecialPowers.spawn(browser, [scriptPath], async scriptPath => {
    const { promise, resolve } = Promise.withResolvers();

    const script = content.document.createElement("script");
    script.src = scriptPath;
    script.addEventListener("load", resolve);
    content.document.body.appendChild(script);

    await promise;

    return parseInt(content.document.body.getAttribute("counter"));
  });
}

add_task(async function test_redirectCache() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.script_loader.experimental.navigation_cache", true]],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());

  const tests = [
    {
      query: "?vary",
      cachedCounter: false,
      log: ",vary,vary",
    },
    {
      query: "?normal",
      cachedCounter: true,
      log: ",normal",
    },
  ];

  for (const { query, cachedCounter, log } of tests) {
    ChromeUtils.clearResourceCache();
    Services.cache2.clear();

    const resetResponse = await fetch(TEST_SCRIPT_URL + "?reset");
    is(await resetResponse.text(), "reset", "Server state should be reset");

    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: TEST_URL,
    });

    is(
      await getCounter(tab, query),
      0,
      "counter should be 0 for the first load."
    );

    await BrowserTestUtils.reloadTab(tab);

    const counter = await getCounter(tab, query);
    if (cachedCounter) {
      is(counter, 0, "cache should be used for " + query);
    } else {
      is(counter, 1, "cache should not be used for " + query);
    }

    const logResponse = await fetch(TEST_SCRIPT_URL + "?log");
    is(await logResponse.text(), log, "Log should match");

    BrowserTestUtils.removeTab(tab);
  }
});

const TEST_URL = "https://example.com/browser/dom/tests/browser/dummy.html";
const SCRIPT_NAME = "redirect_server.sjs";
const TEST_SCRIPT_URL =
  "https://example.com/browser/dom/tests/browser/" + SCRIPT_NAME;

const { SpecialPowersForProcess } = ChromeUtils.importESModule(
  "resource://testing-common/SpecialPowersProcessActor.sys.mjs"
);

const scope = this;

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

async function reloadAndGetCounter(tab, query) {
  await BrowserTestUtils.reloadTab(tab);

  return getCounter(tab, query);
}

add_task(async function test_redirectCache() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.expose_test_interfaces", true],
      ["dom.script_loader.experimental.navigation_cache", true],
    ],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());

  const tests = [
    {
      query: "?redirect=cacheable&script=cacheable",
      cachedCounter: true,
      useInMemoryCache: false,
      log: [",redirect=cacheable&script=cacheable", ",script=cacheable"].join(
        ""
      ),
    },
    {
      query: "?redirect=cacheable&script=not-cacheable",
      cachedCounter: false,
      useInMemoryCache: false,
      log: [
        ",redirect=cacheable&script=not-cacheable",
        ",script=not-cacheable",
        ",script=not-cacheable",
      ].join(""),
    },
    {
      query: "?redirect=not-cacheable&script=cacheable",
      cachedCounter: true,
      useInMemoryCache: false,
      log: [
        ",redirect=not-cacheable&script=cacheable",
        ",script=cacheable",
        ",redirect=not-cacheable&script=cacheable",
      ].join(""),
    },
    {
      query: "?redirect=not-cacheable&script=not-cacheable",
      cachedCounter: false,
      useInMemoryCache: false,
      log: [
        ",redirect=not-cacheable&script=not-cacheable",
        ",script=not-cacheable",
        ",redirect=not-cacheable&script=not-cacheable",
        ",script=not-cacheable",
      ].join(""),
    },
    {
      // Test the request without redirect uses the cache.
      query: "?script=cacheable",
      cachedCounter: true,
      useInMemoryCache: true,
      log: [",script=cacheable"].join(""),
    },
  ];

  for (const { query, cachedCounter, useInMemoryCache, log } of tests) {
    ChromeUtils.clearResourceCache();
    Services.cache2.clear();

    const resetResponse = await fetch(TEST_SCRIPT_URL + "?reset");
    is(await resetResponse.text(), "reset", "Server state should be reset");

    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: TEST_URL,
    });

    const browser = tab.linkedBrowser;
    const proc = browser.browsingContext.currentWindowGlobal.domProcess;
    const processBoundSpecialPowers = new SpecialPowersForProcess(scope, proc);

    is(
      await getCounter(tab, query),
      0,
      "counter should be 0 for the first load."
    );

    const eventPromise = processBoundSpecialPowers.spawn([], async () => {
      const { promise, resolve } = Promise.withResolvers();

      const observer = function (subject, topic, data) {
        if (data.startsWith("event:load:")) {
          resolve(data.split("\n")[0]);
        }
      };

      Services.obs.addObserver(observer, "ScriptLoaderTest");

      const event = await promise;

      Services.obs.removeObserver(observer, "ScriptLoaderTest");

      return event;
    });

    const counter = await reloadAndGetCounter(tab, query);
    if (cachedCounter) {
      is(counter, 0, "cache should be used for " + query);
    } else {
      is(counter, 1, "cache should not be used for " + query);
    }

    const event = await eventPromise;

    if (useInMemoryCache) {
      is(event, "event:load:memorycache");
    } else {
      is(event, "event:load:source");
    }

    const logResponse = await fetch(TEST_SCRIPT_URL + "?log");
    is(await logResponse.text(), log, "Log should match");

    BrowserTestUtils.removeTab(tab);
  }
});

add_setup(function () {
  registerCleanupFunction(clearSiteTestData);
  // Clears the Alt-Svc mappings, which the test expects to start empty.
  registerCleanupFunction(() =>
    Services.obs.notifyObservers(null, "browser:purge-session-history")
  );
});

function altSvcCacheKeyIsolated(parsed) {
  return parsed.length > 5 && parsed[5] == "I";
}

function altSvcPartitionKey(key) {
  let parts = key.split(":");
  return parts[parts.length - 2];
}

const gHttpHandler = Cc["@mozilla.org/network/protocol;1?name=http"].getService(
  Ci.nsIHttpProtocolHandler
);

add_task(async function () {
  info("Starting tlsSessionTickets test");

  await SpecialPowers.flushPrefEnv();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.cache.disk.enable", false],
      ["browser.cache.memory.enable", false],
      ["network.cookie.cookieBehavior", Ci.nsICookieService.BEHAVIOR_ACCEPT],
      [
        "network.cookie.cookieBehavior.pbmode",
        Ci.nsICookieService.BEHAVIOR_ACCEPT,
      ],
      ["network.http.altsvc.proxy_checks", false],
      ["privacy.trackingprotection.enabled", false],
      ["privacy.trackingprotection.pbmode.enabled", false],
      ["privacy.trackingprotection.annotate_channels", false],
      ["privacy.partition.network_state.connection_with_proxy", true],
    ],
  });

  info("Creating a new tab");
  let tab = BrowserTestUtils.addTab(gBrowser, TEST_TOP_PAGE);
  gBrowser.selectedTab = tab;

  let browser = gBrowser.getBrowserForTab(tab);
  await BrowserTestUtils.browserLoaded(browser);

  const thirdPartyURL =
    "https://tlsresumptiontest.example.org/browser/toolkit/components/antitracking/test/browser/empty-altsvc.js";
  const partitionKey1 = "^partitionKey=%28http%2Cexample.net%29";
  const partitionKey2 = "^partitionKey=%28http%2Cmochi.test%29";

  function checkAltSvcCache(keys) {
    let arr = gHttpHandler.altSvcCacheKeys;
    is(
      arr.length,
      keys.length,
      "Found the expected number of items in the cache"
    );
    for (let i = 0; i < arr.length; ++i) {
      is(
        altSvcPartitionKey(arr[i]),
        keys[i],
        "Expected top window origin found in the Alt-Svc cache key"
      );
    }
  }

  checkAltSvcCache([]);

  info("Loading something in the tab");
  await SpecialPowers.spawn(browser, [{ thirdPartyURL }], async function (obj) {
    dump("AAA: " + content.window.location.href + "\n");
    let src = content.document.createElement("script");
    let p = new content.Promise(resolve => {
      src.onload = resolve;
    });
    content.document.body.appendChild(src);
    src.src = obj.thirdPartyURL;
    await p;
  });

  checkAltSvcCache([partitionKey1]);

  info("Creating a second tab");
  let tab2 = BrowserTestUtils.addTab(gBrowser, TEST_TOP_PAGE_6);
  gBrowser.selectedTab = tab2;

  let browser2 = gBrowser.getBrowserForTab(tab2);
  await BrowserTestUtils.browserLoaded(browser2);

  info("Loading something in the second tab");
  await SpecialPowers.spawn(
    browser2,
    [{ thirdPartyURL }],
    async function (obj) {
      let src = content.document.createElement("script");
      let p = new content.Promise(resolve => {
        src.onload = resolve;
      });
      content.document.body.appendChild(src);
      src.src = obj.thirdPartyURL;
      await p;
    }
  );

  checkAltSvcCache([partitionKey1, partitionKey2]);

  info("Removing the tabs");
  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(tab2);
});

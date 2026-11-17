/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This tests checks that the remote tab result is displayed and can be
 * selected.
 */

"use strict";

const { SyncedTabs } = ChromeUtils.importESModule(
  "resource://services-sync/SyncedTabs.sys.mjs"
);
const { WeaveService } = ChromeUtils.importESModule(
  "moz-src:///services/sync/Weave.sys.mjs"
);

const TEST_URL = `${TEST_BASE_URL}dummy_page.html`;

const REMOTE_TAB = {
  id: "7cqCr77ptzX3",
  type: "client",
  lastModified: 1492201200,
  name: "zcarter's Nightly on MacBook-Pro-25",
  clientType: "desktop",
  tabs: [
    {
      type: "tab",
      title: "Test Remote",
      url: TEST_URL,
      icon: UrlbarShared.ICON.DEFAULT,
      client: "7cqCr77ptzX3",
      lastUsed: Math.floor(Date.now() / 1000),
    },
  ],
};

add_setup(async function () {
  sandbox = sinon.createSandbox();

  let originalSyncedTabsInternal = SyncedTabs._internal;
  SyncedTabs._internal = {
    isConfiguredToSyncTabs: true,
    hasSyncedThisSession: true,
    getTabClients() {
      return Promise.resolve([]);
    },
    syncTabs() {
      return Promise.resolve();
    },
  };

  // Tell the Sync service it is initialized.
  let oldWeaveServiceReady = WeaveService.ready;
  WeaveService.ready = true;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill", false],
      ["services.sync.username", "fake"],
    ],
  });

  sandbox
    .stub(SyncedTabs._internal, "getTabClients")
    .callsFake(() => Promise.resolve(Cu.cloneInto([REMOTE_TAB], {})));

  registerCleanupFunction(async () => {
    sandbox.restore();
    WeaveService.ready = oldWeaveServiceReady;
    SyncedTabs._internal = originalSyncedTabsInternal;
  });
});

add_task(async function test_remotetab_opens() {
  await BrowserTestUtils.withNewTab(
    { url: "about:robots", gBrowser },
    async function () {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "Test Remote",
      });

      // There should be two items in the pop-up, the first is the default search
      // suggestion, the second is the remote tab.

      let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

      Assert.equal(
        result.type,
        UrlbarShared.RESULT_TYPE.REMOTE_TAB,
        "Should be the remote tab entry"
      );

      // The URL is going to open in the current tab as it is currently about:blank
      let promiseTabLoaded = BrowserTestUtils.browserLoaded(
        gBrowser.selectedBrowser
      );
      EventUtils.synthesizeKey("KEY_ArrowDown");
      EventUtils.synthesizeKey("KEY_Enter");
      await promiseTabLoaded;

      Assert.equal(
        gBrowser.selectedTab.linkedBrowser.currentURI.spec,
        TEST_URL,
        "correct URL loaded"
      );
    }
  );
});

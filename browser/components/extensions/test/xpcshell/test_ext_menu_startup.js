"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionMenus: "resource://gre/modules/ExtensionMenus.sys.mjs",
  KeyValueService: "moz-src:///toolkit/components/kvstore/kvstore.sys.mjs",
  Management: "resource://gre/modules/Extension.sys.mjs",
});

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

Services.prefs.setBoolPref("extensions.eventPages.enabled", true);

function getExtension(id, background, useAddonManager, version = "1.0") {
  return {
    useAddonManager,
    manifest: {
      version,
      browser_specific_settings: { gecko: { id } },
      permissions: ["menus"],
      background: { persistent: false },
    },
    background,
  };
}

async function expectPersistedMenus(extensionId, extensionVersion, expect) {
  let menusFromStore = await ExtensionMenus._getStoredMenusForTesting(
    extensionId,
    extensionVersion
  );
  equal(menusFromStore.size, expect.length, "stored menus size");
  let createProperties = Array.from(menusFromStore.values());
  // The menus are loaded from disk for extensions using a non-persistent
  // background page and kept in memory in a map, the order is significant
  // for recreating menus on startup.  Ensure that they are in
  // the expected order.
  for (let i in createProperties) {
    Assert.deepEqual(
      createProperties[i],
      expect[i],
      "expected properties exist in the menus store"
    );
  }
}

async function expectExtensionMenus(
  testExtension,
  expect,
  { checkSaved } = {}
) {
  const extension = WebExtensionPolicy.getByID(testExtension.id).extension;
  let menusInMemory = ExtensionMenus.getMenus(extension);
  let createProperties = Array.from(menusInMemory.values());
  equal(menusInMemory.size, expect.length, "menus map size");
  for (let i in createProperties) {
    Assert.deepEqual(
      createProperties[i],
      expect[i],
      "expected properties exist in the menus map"
    );
  }

  if (!checkSaved) {
    return;
  }

  await expectPersistedMenus(testExtension.id, testExtension.version, expect);
}

function promiseExtensionEvent(wrapper, event) {
  return new Promise(resolve => {
    wrapper.extension.once(event, (kind, data) => {
      resolve(data);
    });
  });
}

async function mockBrowserRestart(
  extTestWrapper,
  { shutdownAndRecreateStore = true, waitForMenuRecreated = true } = {}
) {
  if (shutdownAndRecreateStore) {
    info("Mock browser shutdown");
    let menusManager = ExtensionMenus._getManager(extTestWrapper.extension);
    await AddonTestUtils.promiseShutdownManager();
    // Wait data to be flushed as part of the extension shutdown and recreate the store.
    info("Wait for store to be flushed");
    await menusManager._finalizeStoreTaskForTesting();
    info("Recreate menus store");
    ExtensionMenus._recreateStoreForTesting();
  }
  let promiseMenusRecreated;
  if (waitForMenuRecreated) {
    Management.once("startup", (kind, ext) => {
      info(`management ${kind} ${ext.id}`);
      promiseMenusRecreated = promiseExtensionEvent(
        { extension: ext },
        "webext-menus-created"
      );
    });
  }
  info("Mock browser startup");
  await AddonTestUtils.promiseStartupManager();
  await extTestWrapper.awaitStartup();
  if (waitForMenuRecreated) {
    info("Wait for persisted menus to be recreated");
  }
  await promiseMenusRecreated;
}

function extPageScriptWithMenusCreateAndUpdateTestHandler() {
  browser.test.onMessage.addListener((msg, ...args) => {
    switch (msg) {
      case "menusCreate": {
        const menuDetails = args[0];
        browser.menus.create(menuDetails, () => {
          browser.test.assertEq(
            undefined,
            browser.runtime.lastError?.message,
            "Expect the menu to be created successfully"
          );
          browser.test.sendMessage(`${msg}:done`);
        });
        break;
      }
      case "menusUpdate": {
        const menuId = args[0];
        const menuDetails = args[1];
        browser.test.log(`Updating "${menuId}: ${JSON.stringify(menuDetails)}`);
        browser.menus.update(menuId, menuDetails, () => {
          browser.test.assertEq(
            undefined,
            browser.runtime.lastError?.message,
            "Expect the menu to be created successfully"
          );
          browser.test.sendMessage(`${msg}:done`);
        });
        break;
      }
      default:
        browser.test.fail(`Got unexpected test message: ${msg}`);
        browser.test.sendMessage(`${msg}:done`);
    }
  });
  browser.test.sendMessage("extpage:ready");
}

add_setup(async () => {
  // Reduce the amount of time we wait to write menus
  // data on disk while running this test.
  Services.prefs.setIntPref(
    "extensions.webextensions.menus.writeDebounceTime",
    200
  );
  await AddonTestUtils.promiseStartupManager();
});

add_task(async function test_menu_onInstalled() {
  async function background() {
    browser.runtime.onInstalled.addListener(async () => {
      const parentId = browser.menus.create({
        contexts: ["all"],
        title: "parent",
        id: "test-parent",
      });
      browser.menus.create({
        parentId,
        title: "click A",
        id: "test-click-a",
      });
      browser.menus.create(
        {
          parentId,
          title: "click B",
          id: "test-click-b",
        },
        () => {
          browser.test.sendMessage("onInstalled");
        }
      );
    });
    browser.menus.create(
      {
        contexts: ["tab"],
        title: "top-level",
        id: "test-top-level",
      },
      () => {
        browser.test.sendMessage("create", browser.runtime.lastError?.message);
      }
    );

    browser.test.onMessage.addListener(async msg => {
      browser.test.log(`onMessage ${msg}`);
      if (msg == "updatemenu") {
        await browser.menus.update("test-click-a", { title: "click updated" });
      } else if (msg == "removemenu") {
        await browser.menus.remove("test-click-b");
      } else if (msg == "removeall") {
        await browser.menus.removeAll();
      }
      browser.test.sendMessage("updated");
    });
  }

  const extension = ExtensionTestUtils.loadExtension(
    getExtension("test-persist@mochitest", background, "permanent")
  );

  await extension.startup();
  let lastError = await extension.awaitMessage("create");
  Assert.equal(lastError, undefined, "no error creating menu");
  await extension.awaitMessage("onInstalled");
  await extension.terminateBackground();

  await expectExtensionMenus(extension, [
    {
      contexts: ["tab"],
      id: "test-top-level",
      title: "top-level",
    },
    { contexts: ["all"], id: "test-parent", title: "parent" },
    {
      id: "test-click-a",
      parentId: "test-parent",
      title: "click A",
    },
    {
      id: "test-click-b",
      parentId: "test-parent",
      title: "click B",
    },
  ]);

  await extension.wakeupBackground();
  lastError = await extension.awaitMessage("create");
  Assert.equal(
    lastError,
    "The menu id test-top-level already exists in menus.create.",
    "correct error creating menu"
  );

  await mockBrowserRestart(extension);

  // After the extension or the AddonManager has been shutdown,
  // we expect the menu store task to have written the menus
  // on disk and so we expect these menus to be loaded back
  // in memory and also stored on disk.
  await expectExtensionMenus(
    extension,
    [
      {
        contexts: ["tab"],
        id: "test-top-level",
        title: "top-level",
      },
      { contexts: ["all"], id: "test-parent", title: "parent" },
      {
        id: "test-click-a",
        parentId: "test-parent",
        title: "click A",
      },
      {
        id: "test-click-b",
        parentId: "test-parent",
        title: "click B",
      },
    ],
    { checkSaved: true }
  );

  equal(
    extension.extension.backgroundState,
    "stopped",
    "background is not running"
  );
  await extension.wakeupBackground();
  lastError = await extension.awaitMessage("create");
  Assert.equal(
    lastError,
    "The menu id test-top-level already exists in menus.create.",
    "correct error creating menu"
  );

  let promisePersistedMenusUpdated = TestUtils.topicObserved(
    "webext-persisted-menus-updated"
  );

  extension.sendMessage("updatemenu");
  await extension.awaitMessage("updated");
  await extension.terminateBackground();

  // Title change is persisted.
  // (awaiting on the promise resolved when the ExtensionMenus
  // DeferredTask are been executed and the data expected to be
  // written on disk, to confirm the menu data is being persisted
  // on disk also while the extension and the app are still
  // running).
  await promisePersistedMenusUpdated;

  await expectExtensionMenus(
    extension,
    [
      {
        contexts: ["tab"],
        id: "test-top-level",
        title: "top-level",
      },
      { contexts: ["all"], id: "test-parent", title: "parent" },
      {
        id: "test-click-a",
        parentId: "test-parent",
        title: "click updated",
      },
      {
        id: "test-click-b",
        parentId: "test-parent",
        title: "click B",
      },
    ],
    { checkSaved: true }
  );

  await extension.wakeupBackground();
  lastError = await extension.awaitMessage("create");
  Assert.equal(
    lastError,
    "The menu id test-top-level already exists in menus.create.",
    "correct error creating menu"
  );

  // Wait for the removal to be written to the store, so that this write's
  // notification can't resolve the promise waiting for the "removeall" write.
  promisePersistedMenusUpdated = TestUtils.topicObserved(
    "webext-persisted-menus-updated"
  );

  extension.sendMessage("removemenu");
  await extension.awaitMessage("updated");
  await extension.terminateBackground();
  await promisePersistedMenusUpdated;

  // menu removed
  await expectExtensionMenus(extension, [
    {
      contexts: ["tab"],
      id: "test-top-level",
      title: "top-level",
    },
    { contexts: ["all"], id: "test-parent", title: "parent" },
    {
      id: "test-click-a",
      parentId: "test-parent",
      title: "click updated",
    },
  ]);

  await extension.wakeupBackground();
  lastError = await extension.awaitMessage("create");
  Assert.equal(
    lastError,
    "The menu id test-top-level already exists in menus.create.",
    "correct error creating menu"
  );

  promisePersistedMenusUpdated = TestUtils.topicObserved(
    "webext-persisted-menus-updated"
  );

  extension.sendMessage("removeall");
  await extension.awaitMessage("updated");
  await extension.terminateBackground();

  // menus removed

  // We expect the persisted menus store to still have an
  // entry for the extension even when all menus have been
  // removed.
  await promisePersistedMenusUpdated;
  equal(
    await ExtensionMenus._hasStoredExtensionData(extension.id),
    true,
    "persisted menus store have an entry for the test extension"
  );
  await expectExtensionMenus(extension, [], { checkSaved: true });

  promisePersistedMenusUpdated = TestUtils.topicObserved(
    "webext-persisted-menus-updated"
  );
  await extension.unload();
  await promisePersistedMenusUpdated;

  // Expect the entry to have been removed completely from
  // the persised menus store the test extension is uninstalled.
  equal(
    await ExtensionMenus._hasStoredExtensionData(extension.id),
    false,
    "uninstalled extension should NOT have an entry in the persisted menus store"
  );
});

add_task(async function test_menu_persisted_cleared_after_ext_update() {
  async function background() {
    browser.test.onMessage.addListener(async (action, properties) => {
      browser.test.log(`onMessage ${action}`);
      switch (action) {
        case "create":
          await new Promise(resolve => {
            browser.menus.create(properties, resolve);
          });
          break;
        default:
          browser.test.fail(`Got unexpected test message "${action}"`);
          break;
      }
      browser.test.sendMessage("updated");
    });
  }

  const extension = ExtensionTestUtils.loadExtension(
    getExtension("test-nesting@mochitest", background, "permanent", "1.0")
  );
  await extension.startup();

  extension.sendMessage("create", {
    id: "stored-menu",
    contexts: ["all"],
    title: "some-menu",
  });
  await extension.awaitMessage("updated");

  const expectedMenus = [
    { contexts: ["all"], id: "stored-menu", title: "some-menu" },
  ];
  await expectExtensionMenus(extension, expectedMenus);

  info(
    "Re-install the same add-on version and expect persisted menus to still exist"
  );
  await extension.upgrade(
    getExtension("test-nesting@mochitest", background, "permanent", "1.0")
  );
  await expectExtensionMenus(extension, expectedMenus);

  info(
    "Upgrade to a new add-on version and expect persisted menus to be cleared"
  );
  await extension.upgrade(
    getExtension("test-nesting@mochitest", background, "permanent", "2.0")
  );
  await expectExtensionMenus(extension, []);

  await extension.unload();
});

add_task(async function test_menu_nested() {
  async function background() {
    browser.test.onMessage.addListener(async (action, properties) => {
      browser.test.log(`onMessage ${action}`);
      switch (action) {
        case "create":
          await new Promise(resolve => {
            browser.menus.create(properties, resolve);
          });
          break;
        case "update":
          {
            let { id, ...update } = properties;
            await browser.menus.update(id, update);
          }
          break;
        case "remove":
          {
            let { id } = properties;
            await browser.menus.remove(id);
          }
          break;
        case "removeAll":
          await browser.menus.removeAll();
          break;
      }
      browser.test.sendMessage("updated");
    });
  }

  const extension = ExtensionTestUtils.loadExtension(
    getExtension("test-nesting@mochitest", background, "permanent")
  );
  await extension.startup();

  extension.sendMessage("create", {
    id: "first",
    contexts: ["all"],
    title: "first",
  });
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, [
    { contexts: ["all"], id: "first", title: "first" },
  ]);

  extension.sendMessage("create", {
    id: "second",
    contexts: ["all"],
    title: "second",
  });
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, [
    { contexts: ["all"], id: "first", title: "first" },
    { contexts: ["all"], id: "second", title: "second" },
  ]);

  extension.sendMessage("create", {
    id: "third",
    contexts: ["all"],
    title: "third",
    parentId: "first",
  });
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, [
    { contexts: ["all"], id: "first", title: "first" },
    { contexts: ["all"], id: "second", title: "second" },
    {
      contexts: ["all"],
      id: "third",
      parentId: "first",
      title: "third",
    },
  ]);

  extension.sendMessage("create", {
    id: "fourth",
    contexts: ["all"],
    title: "fourth",
  });
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, [
    { contexts: ["all"], id: "first", title: "first" },
    { contexts: ["all"], id: "second", title: "second" },
    {
      contexts: ["all"],
      id: "third",
      parentId: "first",
      title: "third",
    },
    { contexts: ["all"], id: "fourth", title: "fourth" },
  ]);

  extension.sendMessage("update", {
    id: "first",
    parentId: "second",
  });
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, [
    { contexts: ["all"], id: "second", title: "second" },
    { contexts: ["all"], id: "fourth", title: "fourth" },
    {
      contexts: ["all"],
      id: "first",
      title: "first",
      parentId: "second",
    },
    {
      contexts: ["all"],
      id: "third",
      parentId: "first",
      title: "third",
    },
  ]);

  await AddonTestUtils.promiseShutdownManager();
  // We need to attach an event listener before the
  // startup event is emitted.  Fortunately, we
  // emit via Management before emitting on extension.
  let promiseMenus;
  Management.once("startup", (kind, ext) => {
    info(`management ${kind} ${ext.id}`);
    promiseMenus = promiseExtensionEvent(
      { extension: ext },
      "webext-menus-created"
    );
  });
  await AddonTestUtils.promiseStartupManager();
  await extension.awaitStartup();
  await extension.wakeupBackground();

  await expectExtensionMenus(
    extension,
    [
      { contexts: ["all"], id: "second", title: "second" },
      { contexts: ["all"], id: "fourth", title: "fourth" },
      {
        contexts: ["all"],
        id: "first",
        title: "first",
        parentId: "second",
      },
      {
        contexts: ["all"],
        id: "third",
        parentId: "first",
        title: "third",
      },
    ],
    { checkSaved: true }
  );
  // validate nesting
  let menus = await promiseMenus;
  equal(menus.get("first").parentId, "second", "menuitem parent is correct");
  equal(
    menus.get("second").children.length,
    1,
    "menuitem parent has correct number of children"
  );
  equal(
    menus.get("second").root.children.length,
    2, // second and forth
    "menuitem root has correct number of children"
  );

  extension.sendMessage("remove", {
    id: "second",
  });
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, [
    { contexts: ["all"], id: "fourth", title: "fourth" },
  ]);

  extension.sendMessage("removeAll");
  await extension.awaitMessage("updated");
  await expectExtensionMenus(extension, []);

  await extension.unload();
});

add_task(async function test_ExtensionMenus_after_extension_hasShutdown() {
  const assertEmptyMenusManagersMap = () => {
    let weakMapKeys = ChromeUtils.nondeterministicGetWeakMapKeys(
      ExtensionMenus._menusManagers
    );
    Assert.deepEqual(
      weakMapKeys.length,
      0,
      "Expect ExtensionMenus._menusManagers weakmap to be empty"
    );
  };

  // Sanity check.
  assertEmptyMenusManagersMap();

  const addonId = "test-menu-after-shutdown@mochitest";
  const testExtWrapper = ExtensionTestUtils.loadExtension(
    getExtension(addonId, () => {}, "permanent")
  );
  await testExtWrapper.startup();
  const { extension } = testExtWrapper;
  Assert.equal(
    extension.hasShutdown,
    false,
    "Extension hasShutdown should be false"
  );
  await testExtWrapper.unload();
  Assert.equal(
    extension.hasShutdown,
    true,
    "Extension hasShutdown should be true"
  );

  // Sanity check.
  assertEmptyMenusManagersMap();

  await Assert.rejects(
    ExtensionMenus.asyncInitForExtension(extension),
    new RegExp(
      `Error on creating new ExtensionMenusManager after extension shutdown: ${addonId}`
    ),
    "Got the expected error on ExtensionMenus.asyncInitForExtension called for a shutdown extension"
  );
  assertEmptyMenusManagersMap();

  Assert.throws(
    () => ExtensionMenus.getMenus(extension),
    new RegExp(`No ExtensionMenusManager instance found for ${addonId}`),
    "Got the expected error on ExtensionMenus.getMenus called for a shutdown extension"
  );
  assertEmptyMenusManagersMap();
});

// This test ensures that menus created by an extension without a background page
// are not persisted.
add_task(async function test_extension_without_background() {
  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      permissions: ["menus"],
    },
    files: {
      "extpage.html": `<!DOCTYPE html><script src="extpage.js"></script>`,
      "extpage.js": extPageScriptWithMenusCreateAndUpdateTestHandler,
    },
  });

  async function testCreateMenu() {
    const extPageUrl = extension.extension.baseURI.resolve("extpage.html");
    let page = await ExtensionTestUtils.loadContentPage(extPageUrl);
    await extension.awaitMessage("extpage:ready");
    const menuDetails = { id: "test-menu", title: "menu title" };
    extension.sendMessage("menusCreate", menuDetails);
    await extension.awaitMessage("menusCreate:done");
    await page.close();
  }

  await extension.startup();
  await testCreateMenu();

  info(
    "Simulated browser restart and verify no menu was persisted or restored"
  );
  await mockBrowserRestart(extension, { waitForMenuRecreated: false });
  // Try to create the same menu again, if it does fail the menu was unexpectectly
  // restored.
  await testCreateMenu();
  equal(
    await ExtensionMenus._hasStoredExtensionData(extension.id),
    false,
    "Extensions without a background page should not have any data stored for their menus"
  );
  await extension.unload();
});

// Verify that corrupted menus store data is handled gracefully.
add_task(async function test_corrupted_menus_store_data() {
  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      permissions: ["menus"],
      background: { persistent: false },
    },
    background: extPageScriptWithMenusCreateAndUpdateTestHandler,
  });

  await extension.startup();
  await extension.awaitMessage("extpage:ready");

  const menuDetails = { id: "test-menu", title: "menu title" };
  const menuDetailsUnsupported = {
    new_unsupported_property: "fake-prop-value",
  };
  const menuDetailsUpdate = { title: "Updated menu title" };

  extension.sendMessage("menusCreate", menuDetails);
  await extension.awaitMessage("menusCreate:done");

  let menus = ExtensionMenus.getMenus(extension.extension);
  Assert.deepEqual(
    menus.get("test-menu"),
    menuDetails,
    "Got the expected menuDetails from ExtensionMenus.getMenus"
  );
  // Inject invalid menu properties into the store to simulate
  // restoring menus from menus data stored by a future version
  // with additional menus properties older versions would not
  // have support for.
  //
  // This test covers additive changes, but technically changes
  // to the format of existing properties may not be handled
  // gracefully (but changes to the type/format of existing menu
  // properties are more likely to be part of a manifest version
  // update, and so they may be less likely, and downgrades not
  // officially supported).
  info("Inject unsupported properties in the persisted menu details");
  let store = ExtensionMenus._getStoreForTesting();
  menus.set("test-menu", { ...menuDetails, ...menuDetailsUnsupported });
  await store.updatePersistedMenus(extension.id, extension.version, menus);
  equal(
    await ExtensionMenus._hasStoredExtensionData(extension.id),
    true,
    "persisted menus store have an entry for the test extension"
  );

  // Mock a browser restart and verify the unsupported property injected
  // in the persisted menu data is handled gracefully.
  await mockBrowserRestart(extension);
  await extension.awaitMessage("extpage:ready");
  menus = ExtensionMenus.getMenus(extension.extension);

  info("Verify the recreated menu can still be updated as expected");
  extension.sendMessage("menusUpdate", menuDetails.id, menuDetailsUpdate);
  await extension.awaitMessage("menusUpdate:done");
  menus = ExtensionMenus.getMenus(extension.extension);
  Assert.deepEqual(
    menus.get("test-menu"),
    Object.assign({}, menuDetails, menuDetailsUnsupported, menuDetailsUpdate),
    "Got the expected menuDetails from ExtensionMenus.getMenus"
  );

  info("Inject orphan menu entry in the persisted menus data");
  store = ExtensionMenus._getStoreForTesting();
  const orphanedMenuDetails = {
    id: "orphaned-test-menu",
    parentId: "non-existing-parent-id",
    title: "An orphaned menu item",
  };
  menus.set(orphanedMenuDetails.id, orphanedMenuDetails);
  await store.updatePersistedMenus(extension.id, extension.version, menus);

  // Mock a browser restart and verify that orphaned menus
  // from the persisted menu data are handled gracefully.
  {
    const { messages } = await AddonTestUtils.promiseConsoleOutput(async () => {
      await mockBrowserRestart(extension);
      await extension.awaitMessage("extpage:ready");
    });

    // Make sure the test is hitting the expected error.
    AddonTestUtils.checkMessages(messages, {
      expected: [
        {
          message: new RegExp(
            `Unexpected error on recreating persisted menu ${orphanedMenuDetails.id} for ${extension.id}`
          ),
        },
      ],
    });
  }

  menus = ExtensionMenus.getMenus(extension.extension);

  info("Verify the recreated menu can still be updated as expected");
  extension.sendMessage("menusUpdate", menuDetails.id, menuDetailsUpdate);
  await extension.awaitMessage("menusUpdate:done");
  menus = ExtensionMenus.getMenus(extension.extension);
  Assert.deepEqual(
    menus.get("test-menu"),
    Object.assign({}, menuDetails, menuDetailsUnsupported, menuDetailsUpdate),
    "Got the expected menuDetails from ExtensionMenus.getMenus"
  );

  info("Verify the orphaned menu has been dropped");
  Assert.equal(
    menus.has(orphanedMenuDetails.id),
    false,
    "Expect orphaned menu to not exist anymore"
  );

  info("Verify invalid stored json menus data is handled gracefully");

  await AddonTestUtils.promiseShutdownManager();
  ExtensionMenus._recreateStoreForTesting();
  let menuStorePath = PathUtils.join(
    PathUtils.profileDir,
    ExtensionMenus.KVSTORE_DIRNAME
  );
  const kvstore = await KeyValueService.getOrCreateWithOptions(
    menuStorePath,
    "menus",
    { strategy: KeyValueService.RecoveryStrategy.RENAME }
  );
  await kvstore.put(extension.id, "invalid-json-data");

  {
    const { messages } = await AddonTestUtils.promiseConsoleOutput(async () => {
      await mockBrowserRestart(extension, { shutdownAndRecreateStore: false });
      await extension.awaitMessage("extpage:ready");
    });

    // Make sure the test is hitting the expected error.
    AddonTestUtils.checkMessages(messages, {
      expected: [
        {
          message: new RegExp(
            `Error loading ${extension.id} persisted menus: SyntaxError`
          ),
        },
      ],
    });
  }

  menus = ExtensionMenus.getMenus(extension.extension);
  Assert.equal(menus.size, 0, "Expect persisted menus map to be empty");

  // Verify new menu can still be created.
  extension.sendMessage("menusCreate", menuDetails);
  await extension.awaitMessage("menusCreate:done");
  menus = ExtensionMenus.getMenus(extension.extension);
  Assert.equal(menus.size, 1, "Expect persisted menus map to not be empty");

  await extension.unload();
});

// Verify that ExtensionMenus.clearPersistedMenusOnUninstall isn't going
// to create an unnecessary menus kvstore directory when that directory does
// not exist yet, e.g. because the extension never used the contextMenus API.
// This includes builds that do not support the contextMenus API.
add_task(async function test_unnecessary_kvstore_dir_not_created() {
  // Create a new store instance to ensure the lazy store initialization
  // isn't already executed due to the previous test tasks.
  await AddonTestUtils.promiseRestartManager();
  ExtensionMenus._recreateStoreForTesting();

  let menuStorePath = PathUtils.join(
    PathUtils.profileDir,
    ExtensionMenus.KVSTORE_DIRNAME
  );

  await IOUtils.remove(menuStorePath, { ignoreAbsent: true, recursive: true });
  equal(
    await IOUtils.exists(menuStorePath),
    false,
    `Expect no ${ExtensionMenus.KVSTORE_DIRNAME} in the Gecko profile`
  );

  await ExtensionMenus.clearPersistedMenusOnUninstall("fakeextid@test");

  equal(
    await IOUtils.exists(menuStorePath),
    false,
    `Expect no ${ExtensionMenus.KVSTORE_DIRNAME} in the Gecko profile`
  );
});

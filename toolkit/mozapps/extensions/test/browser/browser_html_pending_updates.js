/* eslint max-len: ["error", 80] */

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  PERMISSION_L10N: "resource://gre/modules/ExtensionPermissionMessages.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
});

AddonTestUtils.initMochitest(this);

const server = AddonTestUtils.createHttpServer();

const LOCALE_ADDON_ID = "postponed-langpack@mochi.test";

let gProvider;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.checkUpdateSecurity", false]],
  });

  // Also include a langpack with a pending postponed install.
  const fakeLocalePostponedInstall = {
    name: "updated langpack",
    version: "2.0",
    state: AddonManager.STATE_POSTPONED,
  };

  gProvider = new MockProvider();
  gProvider.createAddons([
    {
      id: LOCALE_ADDON_ID,
      name: "Postponed Langpack",
      type: "locale",
      version: "1.0",
      // Mock pending upgrade property on the mocked langpack add-on.
      pendingUpgrade: {
        install: fakeLocalePostponedInstall,
      },
    },
  ]);

  fakeLocalePostponedInstall.existingAddon = gProvider.addons[0];
  gProvider.createInstalls([fakeLocalePostponedInstall]);

  registerCleanupFunction(() => {
    cleanupPendingNotifications();
  });
});

function createTestExtension({
  id = "test-pending-update@test",
  newManifest = {},
  oldManifest = {},
}) {
  function background() {
    browser.runtime.onUpdateAvailable.addListener(() => {
      browser.test.sendMessage("update-available");
    });

    browser.test.sendMessage("bgpage-ready");
  }

  const serverHost = `http://localhost:${server.identity.primaryPort}`;
  const updatesPath = `/ext-updates-${id}.json`;
  const update_url = `${serverHost}${updatesPath}`;

  const manifest = {
    name: "Test Pending Update",
    ...oldManifest,
    browser_specific_settings: {
      gecko: { id, update_url },
    },
    version: "1",
  };

  let extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      ...oldManifest,
      ...manifest,
      browser_specific_settings: {
        gecko: {
          ...(oldManifest.browser_specific_settings?.gecko ?? {}),
          ...manifest.browser_specific_settings.gecko,
        },
      },
    },
    // Use permanent so the add-on can be updated.
    useAddonManager: "permanent",
  });

  let updateXpi = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      ...manifest,
      ...newManifest,
      version: "2",
    },
  });

  let xpiFilename = `/update-${id}.xpi`;
  server.registerFile(xpiFilename, updateXpi);
  AddonTestUtils.registerJSON(server, updatesPath, {
    addons: {
      [id]: {
        updates: [
          {
            version: "2",
            update_link: serverHost + xpiFilename,
          },
        ],
      },
    },
  });

  return { extension, updateXpi };
}

async function promiseUpdateAvailable(extension) {
  info("Wait for the extension to receive onUpdateAvailable event");
  await extension.awaitMessage("update-available");
}

function expectUpdatesAvailableBadgeCount({ win, expectedNumber }) {
  const categoriesSidebar = win.document.querySelector("categories-box");
  ok(categoriesSidebar, "Found the categories-box element");
  const availableButton =
    categoriesSidebar.getButtonByName("available-updates");
  is(
    availableButton.badgeCount,
    1,
    `Expect only ${expectedNumber} available updates`
  );
  ok(
    !availableButton.hidden,
    "Expecte the available updates category to be visible"
  );
}

async function expectAddonInstallStatePostponed(id) {
  const [addonInstall] = (await AddonManager.getAllInstalls()).filter(
    install => install.existingAddon && install.existingAddon.id == id
  );
  is(
    addonInstall && addonInstall.state,
    AddonManager.STATE_POSTPONED,
    "AddonInstall is in the postponed state"
  );
}

function expectCardOptionsButtonBadged({ id, win, hasBadge = true }) {
  const card = getAddonCard(win, id);
  const moreOptionsEl = card.querySelector(".more-options-button");
  is(
    moreOptionsEl.classList.contains("more-options-button-badged"),
    hasBadge,
    `The options button should${hasBadge || "n't"} have the update badge`
  );
}

function getCardPostponedBar({ id, win }) {
  const card = getAddonCard(win, id);
  return card.querySelector(".update-postponed-bar");
}

function waitCardAndAddonUpdated({ id, win }) {
  const card = getAddonCard(win, id);
  const updatedExtStarted = AddonTestUtils.promiseWebExtensionStartup(id);
  const updatedCard = BrowserTestUtils.waitForEvent(card, "update");
  return Promise.all([updatedExtStarted, updatedCard]);
}

async function testPostponedBarVisibility({ id, win, hidden = false }) {
  const postponedBar = getCardPostponedBar({ id, win });
  is(
    postponedBar.hidden,
    hidden,
    `${id} update postponed message bar should be ${
      hidden ? "hidden" : "visible"
    }`
  );

  if (!hidden) {
    await expectAddonInstallStatePostponed(id);
  }
}

async function assertPostponedBarVisibleInAllViews({ id, win }) {
  info("Test postponed bar visibility in extension list view");
  await testPostponedBarVisibility({ id, win });

  info("Test postponed bar visibility in available view");
  await switchView(win, "available-updates");
  await testPostponedBarVisibility({ id, win });

  info("Test that available updates count do not include postponed langpacks");
  expectUpdatesAvailableBadgeCount({ win, expectedNumber: 1 });

  info("Test postponed langpacks are not listed in the available updates view");
  ok(
    !getAddonCard(win, LOCALE_ADDON_ID),
    "Locale addon is expected to not be listed in the updates view"
  );

  info("Test that postponed bar isn't visible on postponed langpacks");
  await switchView(win, "locale");
  await testPostponedBarVisibility({ id: LOCALE_ADDON_ID, win, hidden: true });

  info("Test postponed bar visibility in extension detail view");
  await switchView(win, "extension");
  await switchToDetailView({ win, id });
  await testPostponedBarVisibility({ id, win });
}

async function completePostponedUpdate({ id, win }) {
  expectCardOptionsButtonBadged({ id, win, hasBadge: false });

  await testPostponedBarVisibility({ id, win });

  let addon = await AddonManager.getAddonByID(id);
  is(addon.version, "1", "Addon version is 1");

  const promiseUpdated = waitCardAndAddonUpdated({ id, win });
  const postponedBar = getCardPostponedBar({ id, win });
  postponedBar.querySelector("button").click();
  await promiseUpdated;

  addon = await AddonManager.getAddonByID(id);
  is(addon.version, "2", "Addon version is 2");

  await testPostponedBarVisibility({ id, win, hidden: true });
}

add_task(async function test_pending_update_with_prompted_permission() {
  const id = "test-pending-update-with-prompted-permission@mochi.test";

  const { extension } = createTestExtension({
    id,
    newManifest: { permissions: ["<all_urls>"] },
  });

  await extension.startup();
  await extension.awaitMessage("bgpage-ready");

  const win = await loadInitialView("extension");

  // Force about:addons to check for updates.
  let promisePermissionHandled = handlePermissionPrompt({
    addonId: extension.id,
    assertIcon: false,
  });
  win.checkForUpdates();
  await promisePermissionHandled;

  await promiseUpdateAvailable(extension);
  await expectAddonInstallStatePostponed(id);

  await completePostponedUpdate({ id, win });

  await closeView(win);
  await extension.unload();
});

add_task(async function test_pending_manual_install_over_existing() {
  const id = "test-pending-manual-install-over-existing@mochi.test";

  const { extension, updateXpi } = createTestExtension({
    id,
  });

  await extension.startup();
  await extension.awaitMessage("bgpage-ready");

  let win = await loadInitialView("extension");

  info("Manually install new xpi over the existing extension");
  const promiseInstalled = AddonTestUtils.promiseInstallFile(updateXpi);
  await promiseUpdateAvailable(extension);

  await assertPostponedBarVisibleInAllViews({ id, win });

  info("Test postponed bar visibility after reopening about:addons");
  await closeView(win);
  win = await loadInitialView("extension");
  await assertPostponedBarVisibleInAllViews({ id, win });

  await completePostponedUpdate({ id, win });
  await promiseInstalled;

  await closeView(win);
  await extension.unload();
});

add_task(async function test_pending_update_no_prompted_permission() {
  const id = "test-pending-update-no-prompted-permission@mochi.test";

  const { extension } = createTestExtension({ id });

  await extension.startup();
  await extension.awaitMessage("bgpage-ready");

  let win = await loadInitialView("extension");

  info("Force about:addons to check for updates");
  win.checkForUpdates();
  await promiseUpdateAvailable(extension);

  await assertPostponedBarVisibleInAllViews({ id, win });

  info("Test postponed bar visibility after reopening about:addons");
  await closeView(win);
  win = await loadInitialView("extension");
  await assertPostponedBarVisibleInAllViews({ id, win });

  await completePostponedUpdate({ id, win });

  info("Reopen about:addons again and verify postponed bar hidden");
  await closeView(win);
  win = await loadInitialView("extension");
  await testPostponedBarVisibility({ id, win, hidden: true });

  await closeView(win);
  await extension.unload();
});

add_task(async function test_pending_update_with_prompted_permission() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const TEST_CASES = [
    {
      title: "With data collection",
      data_collection_permissions: {
        required: ["locationInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-update-data-collection-only-text",
            { extension: extensionId }
          ),
          "Expected header string without perms"
        );
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          1,
          "Expected a permission entry in the list"
        );
        Assert.ok(
          popupContentEl.permsListEl.querySelector(
            "li.webext-data-collection-perm-granted"
          ),
          "Expected data collection item"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-update",
            {
              permissions: "location",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With data collection and required permission",
      permissions: ["bookmarks"],
      data_collection_permissions: {
        required: ["locationInfo", "healthInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-update-data-collection-text",
            { extension: extensionId }
          ),
          "Expected header string with perms"
        );
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.firstChild.textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-bookmarks"),
          "Expected formatted permission string"
        );
        Assert.equal(
          popupContentEl.permsListEl.lastChild.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-update",
            {
              permissions: "location, health information",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
    {
      title: "With data collection changing from required to optional",
      permissions: ["bookmarks"],
      old_data_collection_permissions: {
        required: ["bookmarksInfo"],
      },
      data_collection_permissions: {
        required: ["locationInfo", "healthInfo"],
        optional: ["bookmarksInfo"],
      },
      verifyDialog(popupContentEl, { extensionId }) {
        Assert.equal(
          popupContentEl.querySelector(".popup-notification-description")
            .textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-update-data-collection-text",
            { extension: extensionId }
          ),
          "Expected header string with perms"
        );
        Assert.equal(
          popupContentEl.permsListEl.childElementCount,
          2,
          "Expected two permission entries in the list"
        );
        Assert.equal(
          popupContentEl.permsListEl.childNodes[0].textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-bookmarks"),
          "Expected formatted bookmarks permission string"
        );
        Assert.equal(
          popupContentEl.permsListEl.childNodes[1].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some-update",
            {
              permissions: "location, health information",
            }
          ),
          "Expected formatted data collection permission string"
        );
        Assert.ok(
          popupContentEl.hasAttribute("learnmoreurl"),
          "Expected a learn more link"
        );
      },
    },
  ];

  for (const {
    title,
    permissions,
    data_collection_permissions,
    old_data_collection_permissions,
    verifyDialog,
  } of TEST_CASES) {
    info(title);

    const id = `@${title.toLowerCase().replaceAll(/[^\w]+/g, "-")}`;
    const { extension } = createTestExtension({
      id,
      oldManifest: {
        browser_specific_settings: {
          gecko: {
            ...(old_data_collection_permissions
              ? { data_collection_permissions: old_data_collection_permissions }
              : {}),
          },
        },
      },
      newManifest: {
        name: id,
        permissions,
        browser_specific_settings: {
          gecko: {
            id,
            data_collection_permissions,
          },
        },
      },
    });

    await extension.startup();
    await extension.awaitMessage("bgpage-ready");
    const win = await loadInitialView("extension");

    const dialogPromise = promisePopupNotificationShown(
      "addon-webext-permissions"
    );
    // This `promptPromise` is retrieving data from the prompt internals, while
    // the `dialogPromise` will return the actual dialog element.
    const promptPromise = promisePermissionPrompt(id);
    win.checkForUpdates();
    const [popupContentEl, infoProps] = await Promise.all([
      dialogPromise,
      promptPromise,
    ]);

    verifyDialog(popupContentEl, { extensionId: id });

    // Confirm the update, and proceed.
    const waitForManagementUpdate = new Promise(resolve => {
      const { Management } = ChromeUtils.importESModule(
        "resource://gre/modules/Extension.sys.mjs"
      );
      Management.once("update", resolve);
    });
    infoProps.resolve();
    await promiseUpdateAvailable(extension);
    await completePostponedUpdate({ id, win });
    // Ensure that the bootstrap scope update method has been executed
    // successfully and emitted the update Management event.
    info("Wait for the Management update to be emitted");
    await waitForManagementUpdate;

    await closeView(win);
    await extension.unload();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_pending_update_with_no_prompted_permission() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const TEST_CASES = [
    {
      title: "No data collection",
      data_collection_permissions: {},
    },
    {
      title: "Explicit no data collection",
      data_collection_permissions: {
        required: ["none"],
      },
    },
    {
      title: "Optional data collection",
      data_collection_permissions: {
        optional: ["technicalAndInteraction"],
      },
    },
  ];

  for (const { title, data_collection_permissions } of TEST_CASES) {
    info(title);

    const id = `@${title.toLowerCase().replaceAll(/[^\w]+/g, "-")}`;
    const { extension } = createTestExtension({
      id,
      newManifest: {
        name: id,
        browser_specific_settings: {
          gecko: {
            id,
            data_collection_permissions,
          },
        },
      },
    });

    await extension.startup();
    await extension.awaitMessage("bgpage-ready");
    let win = await loadInitialView("extension");

    win.checkForUpdates();
    await promiseUpdateAvailable(extension);
    await completePostponedUpdate({ id, win });

    await closeView(win);
    await extension.unload();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_pending_update_does_not_grant_technicalAndInteraction() {
    await SpecialPowers.pushPrefEnv({
      set: [["extensions.dataCollectionPermissions.enabled", true]],
    });

    const id = "@test-id";
    const { extension } = createTestExtension({
      id,
      oldManifest: {
        browser_specific_settings: {
          gecko: {
            id,
            data_collection_permissions: {},
          },
        },
      },
      newManifest: {
        permissions: ["bookmarks"],
        browser_specific_settings: {
          gecko: {
            id,
            data_collection_permissions: {
              optional: ["technicalAndInteraction"],
            },
          },
        },
      },
    });

    await extension.startup();
    await extension.awaitMessage("bgpage-ready");
    const win = await loadInitialView("extension");

    const dialogPromise = promisePopupNotificationShown(
      "addon-webext-permissions"
    );
    win.checkForUpdates();
    const popupContentEl = await dialogPromise;

    // Confirm the update, and proceed.
    const waitForManagementUpdate = new Promise(resolve => {
      const { Management } = ChromeUtils.importESModule(
        "resource://gre/modules/Extension.sys.mjs"
      );
      Management.once("update", resolve);
    });
    popupContentEl.button.click();
    await promiseUpdateAvailable(extension);
    await completePostponedUpdate({ id, win });
    // Ensure that the bootstrap scope update method has been executed
    // successfully and emitted the update Management event.
    info("Wait for the Management update to be emitted");
    await waitForManagementUpdate;

    // This test verifies that we don't accidentally grant the
    // "technicalAndInteraction" data collection permission on update because
    // that's controlled by the `showTechnicalAndInteractionCheckbox` value in
    // `ExtensionUI.showPermissionsPrompt()`.
    const perms = await ExtensionPermissions.get(id);
    Assert.deepEqual(
      perms,
      {
        permissions: [],
        origins: [],
        data_collection: [],
      },
      "Expected no stored permission"
    );

    await closeView(win);
    await extension.unload();

    await SpecialPowers.popPrefEnv();
  }
);

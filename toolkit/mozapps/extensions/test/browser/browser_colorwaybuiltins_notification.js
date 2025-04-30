"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const { ColorwayThemeMigration } = ChromeUtils.importESModule(
  "resource://gre/modules/ColorwayThemeMigration.sys.mjs"
);

AddonTestUtils.initMochitest(this);

const CLEANUP_PREF = "extensions.colorway-builtin-themes-cleanup";
const NON_COLORWAY_THEME_ID = "test-non@colorway.org";
const COLORWAY_THEME_ID = "test-colorway@mozilla.org";

function mockAsyncUninstallMethod(mockProviderAddon) {
  // Override the MockAddon uninstall method to mock the behavior
  // of uninstalling an XPIProvider add-on, for which uninstalling
  // is asynchonous and the add-on may not be gone right away
  // (unlike the MockProvider uninstall method which synchonously remove
  // the addon from the MockProvider).
  const mockUninstall = mockProviderAddon.uninstall.bind(mockProviderAddon);
  mockProviderAddon.uninstall = async () => {
    //
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 1000));
    return mockUninstall();
  };
}

function hasNotification() {
  return !!window.gNotificationBox.getNotificationWithValue(
    "colorway-theme-migration"
  );
}

function closeNotification() {
  const notification = window.gNotificationBox.getNotificationWithValue(
    "colorway-theme-migration"
  );
  if (notification) {
    window.gNotificationBox.removeNotification(notification);
  }
}

async function checkColorwayBuiltinTheme(colorwayThemeExists) {
  const colorwayTheme = await AddonManager.getAddonByID(COLORWAY_THEME_ID);
  is(!!colorwayTheme, colorwayThemeExists, "The colorway theme exists");
}

async function checkNonBuiltinTheme(nonColorwayThemeExists) {
  const nonColorwayTheme = await AddonManager.getAddonByID(
    NON_COLORWAY_THEME_ID
  );
  is(
    !!nonColorwayTheme,
    nonColorwayThemeExists,
    "The non colorway theme exists"
  );
}

let gProvider;
async function installThemes() {
  if (!gProvider) {
    gProvider = new MockProvider(["extension"]);
  }

  const [mockNonColorwayTheme, mockColorwayTheme] = gProvider.createAddons([
    {
      id: NON_COLORWAY_THEME_ID,
      name: "Test Non Colorway theme",
      creator: { name: "Artist", url: "https://example.com/artist" },
      description: "A nice tree",
      type: "theme",
      isBuiltinColorwayTheme: false,
      isBuiltin: true,
      screenshots: [],
    },
    {
      id: COLORWAY_THEME_ID,
      name: "Test Colorway theme",
      creator: { name: "Artist", url: "https://example.com/artist" },
      description: "A nice tree",
      type: "theme",
      isBuiltinColorwayTheme: true,
      isBuiltin: true,
      screenshots: [],
    },
  ]);

  mockAsyncUninstallMethod(mockNonColorwayTheme);
  mockAsyncUninstallMethod(mockColorwayTheme);

  await checkColorwayBuiltinTheme(true);
  await checkNonBuiltinTheme(true);
}

add_setup(() => {
  // Make sure we do close the notificationbox when this test file has been fully run
  // (prevents the notificationbox to stay open when other mochitests may run on the
  // same application instance and trigger unexpected failures).
  registerCleanupFunction(closeNotification);
});

add_task(async function no_colorway_themes() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:blank",
    },
    async function () {
      // Before running the test, let's close existing notifications.
      closeNotification();
      ok(!hasNotification(), "No notification found when the test is starting");

      await SpecialPowers.pushPrefEnv({
        set: [[CLEANUP_PREF, 0]],
      });

      await ColorwayThemeMigration.maybeWarn();
      ok(!hasNotification(), "No notification shown with the default theme");

      is(SpecialPowers.getIntPref(CLEANUP_PREF), 1, "The cleanup pref is set");
      await SpecialPowers.popPrefEnv();
    }
  );
});

add_task(async function default_theme_no_notification() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:blank",
    },
    async function () {
      // Before running the test, let's close existing notifications.
      closeNotification();
      ok(!hasNotification(), "No notification found when the test is starting");

      await installThemes();

      await SpecialPowers.pushPrefEnv({
        set: [[CLEANUP_PREF, 0]],
      });

      // Default theme should not trigger the notification.
      const defaultTheme = await AddonManager.getAddonByID(
        "default-theme@mozilla.org"
      );
      ok(!!defaultTheme, "The default theme exists");
      await defaultTheme.enable();

      const promiseUninstalled =
        AddonTestUtils.promiseAddonEvent("onUninstalled");

      await ColorwayThemeMigration.maybeWarn();
      ok(!hasNotification(), "No notification shown with the default theme");

      // No notification shown, but the colorway themes are gone.
      await promiseUninstalled;
      await checkColorwayBuiltinTheme(false);
      await checkNonBuiltinTheme(true);

      is(
        SpecialPowers.getIntPref(CLEANUP_PREF),
        2,
        "The cleanup pref is set (builtin add-ons found)"
      );
      await SpecialPowers.popPrefEnv();
    }
  );
});

add_task(async function non_colorway_theme_no_notification() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:blank",
    },
    async function () {
      // Before running the test, let's close existing notifications.
      closeNotification();
      ok(!hasNotification(), "No notification found when the test is starting");

      await installThemes();

      await SpecialPowers.pushPrefEnv({
        set: [[CLEANUP_PREF, 0]],
      });

      // Let's force a non-colorway theme.
      await (await AddonManager.getAddonByID(NON_COLORWAY_THEME_ID)).enable();
      await SpecialPowers.pushPrefEnv({
        set: [["extensions.activeThemeID", NON_COLORWAY_THEME_ID]],
      });

      const promiseUninstalled =
        AddonTestUtils.promiseAddonEvent("onUninstalled");

      await ColorwayThemeMigration.maybeWarn();
      ok(
        !hasNotification(),
        "No notification shown with a non-existing theme != colorway"
      );

      // No notification shown, but the colorway themes are gone.
      await promiseUninstalled;
      await checkColorwayBuiltinTheme(false);
      await checkNonBuiltinTheme(true);

      is(
        SpecialPowers.getIntPref(CLEANUP_PREF),
        2,
        "The cleanup pref is set (builtin add-ons found)"
      );
      await SpecialPowers.popPrefEnv();
    }
  );
});

add_task(async function colorway_theme_notification() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:blank",
    },
    async function () {
      // Before running the test, let's close existing notifications.
      closeNotification();
      ok(!hasNotification(), "No notification found when the test is starting");

      await installThemes();

      await SpecialPowers.pushPrefEnv({
        set: [[CLEANUP_PREF, 0]],
      });

      // Mock an active colorway builtin theme.
      const mockColorwayTheme =
        await AddonManager.getAddonByID(COLORWAY_THEME_ID);
      await mockColorwayTheme.enable();

      await SpecialPowers.pushPrefEnv({
        set: [["extensions.activeThemeID", COLORWAY_THEME_ID]],
      });

      const promiseUninstalled =
        AddonTestUtils.promiseAddonEvent("onUninstalled");

      await ColorwayThemeMigration.maybeWarn();
      ok(
        hasNotification(),
        "Notification shown with an active colorway builtin theme"
      );

      await promiseUninstalled;
      await checkColorwayBuiltinTheme(false);
      await checkNonBuiltinTheme(true);

      is(
        SpecialPowers.getIntPref(CLEANUP_PREF),
        2,
        "The cleanup pref is set (builtin add-ons found)"
      );
      await SpecialPowers.popPrefEnv();
    }
  );
});

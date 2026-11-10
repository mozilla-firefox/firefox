/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
});

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const NOVA_SUN_ID = "nova-sun@mozilla.org";
const NOVA_SPARK_ID = "nova-spark@mozilla.org";
const DEFAULT_THEME_ID = "default-theme@mozilla.org";

const NOVA_THEMES = {
  [NOVA_SUN_ID]: {
    name: NOVA_SUN_ID,
    version: "1.0",
    browser_specific_settings: { gecko: { id: NOVA_SUN_ID } },
    theme: {
      colors: {
        frame: "#FDE8B5",
        tab_background_text: "#000000",
      },
    },
  },
  [NOVA_SPARK_ID]: {
    name: NOVA_SPARK_ID,
    version: "1.0",
    browser_specific_settings: { gecko: { id: NOVA_SPARK_ID } },
    theme: {
      colors: {
        frame: "#FFDBC5",
        tab_background_text: "#000000",
      },
    },
  },
};

const installNovaTheme = async themeId => {
  let manifest = NOVA_THEMES[themeId];
  let xpi = await AddonTestUtils.createTempWebExtensionFile({ manifest });

  let install = await lazy.AddonManager.getInstallForFile(
    xpi,
    "application/x-xpinstall"
  );
  await install.install();

  let addon = await lazy.AddonManager.getAddonByID(themeId);

  return async () => {
    await addon.uninstall();
    let defaultTheme = await lazy.AddonManager.getAddonByID(
      "default-theme@mozilla.org"
    );
    await defaultTheme.enable();
  };
};

const setup = async () => {
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a profile now");
  return profile;
};

const clickAppearanceButton = async (browser, value, expectedAppearance) => {
  await SpecialPowers.spawn(
    browser,
    [value, expectedAppearance],
    async (val, expected) => {
      let contentEditProfileCard =
        content.document.querySelector("edit-profile-card").wrappedJSObject;
      let contentThemePicker =
        contentEditProfileCard.shadowRoot.querySelector("theme-picker");

      const EventUtils = ContentTaskUtils.getEventUtils(content);
      let button = contentThemePicker.shadowRoot.querySelector(
        `moz-segmented-control-item[value="${val}"]`
      );
      EventUtils.synthesizeMouseAtCenter(button, {}, content);

      await ContentTaskUtils.waitForCondition(
        () => contentThemePicker.appearance === expected,
        `Waiting for ${expected} mode to be set`
      );
    }
  );
};

add_task(async function test_edit_profile_theme() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  await setup();

  let uninstallSun = await installNovaTheme(NOVA_SUN_ID);
  let uninstallSpark = await installNovaTheme(NOVA_SPARK_ID);

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      let profileUpdated = TestUtils.topicObserved("sps-profiles-updated");

      // Test selecting a theme by clicking on it
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        await editProfileCard.updateComplete;

        let themePicker =
          editProfileCard.shadowRoot.querySelector("theme-picker");
        await ContentTaskUtils.waitForCondition(
          () => themePicker?.themes?.length > 0,
          "Waiting for theme picker to be populated"
        );

        Assert.ok(themePicker, "Theme picker should exist");

        let themeGroup =
          editProfileCard.shadowRoot.getElementById("theme-group");
        Assert.ok(
          themeGroup.contains(themePicker),
          "Theme picker should be inside the theme group"
        );
        await themeGroup.updateComplete;
        Assert.equal(
          themeGroup.shadowRoot.querySelector("legend")?.textContent.trim(),
          "Theme",
          "Theme group should be labelled"
        );

        const EventUtils = ContentTaskUtils.getEventUtils(content);

        let sunThemeItem = themePicker.shadowRoot.querySelector(
          "moz-visual-picker-item[value='nova-sun@mozilla.org']"
        );

        Assert.ok(
          !sunThemeItem.checked,
          "Sun theme chip should not be selected initially"
        );

        sunThemeItem.scrollIntoView();
        EventUtils.synthesizeMouseAtCenter(sunThemeItem, {}, content);

        await ContentTaskUtils.waitForCondition(
          () => sunThemeItem.checked,
          "Waiting for the sun theme chip to be selected"
        );

        Assert.ok(
          sunThemeItem.checked,
          "Sun theme chip should be selected after click"
        );
      });

      // Wait for profile to be updated with the new theme
      await profileUpdated;

      let sunAddon = await lazy.AddonManager.getAddonByID(NOVA_SUN_ID);
      Assert.ok(sunAddon, "Nova Sun theme should be installed");
      Assert.ok(sunAddon.isActive, "Nova Sun theme should be active");

      await assertThemePickerThemeChange(NOVA_SUN_ID);
      Services.fog.testResetFOG();

      profileUpdated = TestUtils.topicObserved("sps-profiles-updated");

      // Test selecting a theme via keyboard navigation
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;
        let themePicker =
          editProfileCard.shadowRoot.querySelector("theme-picker");
        const EventUtils = ContentTaskUtils.getEventUtils(content);

        let selectedTheme = themePicker.shadowRoot.querySelector(
          "moz-visual-picker-item[checked]"
        );
        let nextTheme = selectedTheme.nextElementSibling;

        Assert.equal(
          selectedTheme.value,
          "nova-sun@mozilla.org",
          "Sun theme should be selected"
        );

        // Send arrow key right and space to select theme
        EventUtils.synthesizeKey("KEY_ArrowRight", {}, content);
        EventUtils.synthesizeKey("KEY_Enter", {}, content);

        await ContentTaskUtils.waitForCondition(
          () => nextTheme.checked,
          "Waiting for next theme to be selected via keyboard"
        );

        Assert.ok(nextTheme.checked, "Next theme should be checked");
        Assert.equal(
          nextTheme.value,
          "nova-spark@mozilla.org",
          "Spark theme should be selected after arrow key"
        );
      });

      // Wait for profile to be updated with the new theme
      await profileUpdated;

      let sparkAddon = await lazy.AddonManager.getAddonByID(NOVA_SPARK_ID);
      Assert.ok(sparkAddon, "Nova Spark theme should be installed");
      Assert.ok(sparkAddon.isActive, "Nova Spark theme should be active");

      await assertThemePickerThemeChange(NOVA_SPARK_ID);
    }
  );

  await uninstallSpark();
  await uninstallSun();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_theme_picker_appearance_chooser() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  await setup();

  const PREF_SYSTEM_USES_DARK = "ui.systemUsesDarkTheme";
  Services.prefs.clearUserPref(PREF_SYSTEM_USES_DARK);

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        let themePicker =
          editProfileCard.shadowRoot.querySelector("theme-picker");
        await ContentTaskUtils.waitForCondition(
          () => themePicker?.themes?.length > 0,
          "Waiting for theme picker to be populated"
        );

        Assert.equal(
          themePicker.appearance,
          "device",
          "Should start in device mode"
        );
      });

      // Test dark mode
      await clickAppearanceButton(browser, "dark", "dark");

      Assert.equal(
        Services.prefs.getIntPref(PREF_SYSTEM_USES_DARK),
        1,
        "Dark mode pref should be set"
      );

      // Test light mode
      await clickAppearanceButton(browser, "light", "light");

      Assert.equal(
        Services.prefs.getIntPref(PREF_SYSTEM_USES_DARK),
        0,
        "Light mode pref should be set"
      );

      // Test device mode
      await clickAppearanceButton(browser, "device", "device");

      Assert.ok(
        !Services.prefs.prefHasUserValue(PREF_SYSTEM_USES_DARK),
        "Device mode should clear the pref"
      );
    }
  );

  Services.prefs.clearUserPref(PREF_SYSTEM_USES_DARK);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_theme_picker_native_theme_checkbox() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  if (AppConstants.platform !== "linux") {
    ok(true, "Skipping because native theme checkbox is Linux-only");
    return;
  }

  await setup();

  const PREF_NATIVE_THEME = "browser.theme.native-theme";
  Services.prefs.setBoolPref(PREF_NATIVE_THEME, false);

  let defaultTheme = await lazy.AddonManager.getAddonByID(
    "default-theme@mozilla.org"
  );
  await defaultTheme.enable();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        let themePicker =
          editProfileCard.shadowRoot.querySelector("theme-picker");
        await ContentTaskUtils.waitForCondition(
          () => themePicker?.themes?.length > 0,
          "Waiting for theme picker to be populated"
        );

        Assert.ok(!themePicker.nativeTheme, "Native theme should start off");
      });

      // Enable native theme
      await SpecialPowers.spawn(browser, [], async () => {
        let contentEditProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;
        let contentThemePicker =
          contentEditProfileCard.shadowRoot.querySelector("theme-picker");

        const EventUtils = ContentTaskUtils.getEventUtils(content);
        let checkbox =
          contentThemePicker.shadowRoot.querySelector("moz-checkbox");
        EventUtils.synthesizeMouseAtCenter(checkbox, {}, content);

        await ContentTaskUtils.waitForCondition(
          () => contentThemePicker.nativeTheme === true,
          "Waiting for native theme to be enabled"
        );
      });

      Assert.ok(
        Services.prefs.getBoolPref(PREF_NATIVE_THEME),
        "Native theme pref should be enabled"
      );

      // Disable native theme
      await SpecialPowers.spawn(browser, [], async () => {
        let contentEditProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;
        let contentThemePicker =
          contentEditProfileCard.shadowRoot.querySelector("theme-picker");

        const EventUtils = ContentTaskUtils.getEventUtils(content);
        let checkbox =
          contentThemePicker.shadowRoot.querySelector("moz-checkbox");
        EventUtils.synthesizeMouseAtCenter(checkbox, {}, content);

        await ContentTaskUtils.waitForCondition(
          () => contentThemePicker.nativeTheme === false,
          "Waiting for native theme to be disabled"
        );
      });

      Assert.ok(
        !Services.prefs.getBoolPref(PREF_NATIVE_THEME),
        "Native theme pref should be disabled"
      );
    }
  );

  Services.prefs.clearUserPref(PREF_NATIVE_THEME);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_native_theme_checkbox_linux_only() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  await setup();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(
        browser,
        [AppConstants.platform === "linux"],
        async isLinux => {
          let editProfileCard =
            content.document.querySelector("edit-profile-card").wrappedJSObject;

          await ContentTaskUtils.waitForCondition(
            () => editProfileCard.initialized,
            "Waiting for edit-profile-card to be initialized"
          );

          let themePicker =
            editProfileCard.shadowRoot.querySelector("theme-picker");
          await ContentTaskUtils.waitForCondition(
            () => themePicker?.themes?.length > 0,
            "Waiting for theme picker to be populated"
          );
          await themePicker.updateComplete;

          Assert.equal(
            !!themePicker.shadowRoot.querySelector("moz-checkbox"),
            isLinux,
            "The native theme checkbox is present only on Linux."
          );
        }
      );
    }
  );
});

add_task(async function test_avatar_colors_update_with_theme() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  await setup();

  let uninstallTheme = await installNovaTheme(NOVA_SUN_ID);

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let card =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => card.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        let initialFill = card.headerAvatar.style.fill;
        let initialStroke = card.headerAvatar.style.stroke;

        Assert.ok(initialFill, "Avatar should have initial fill color");
        Assert.ok(initialStroke, "Avatar should have initial stroke color");

        const EventUtils = ContentTaskUtils.getEventUtils(content);
        let themePicker = card.shadowRoot.querySelector("theme-picker");
        let sunThemeItem = themePicker.shadowRoot.querySelector(
          "moz-visual-picker-item[value='nova-sun@mozilla.org']"
        );

        EventUtils.synthesizeMouseAtCenter(sunThemeItem, {}, content);

        await ContentTaskUtils.waitForCondition(
          () => card.headerAvatar.style.fill !== initialFill,
          "Waiting for avatar fill color to change"
        );

        Assert.notEqual(
          card.headerAvatar.style.fill,
          initialFill,
          "Avatar fill color should change when theme changes"
        );
        Assert.notEqual(
          card.headerAvatar.style.stroke,
          initialStroke,
          "Avatar stroke color should change when theme changes"
        );
      });

      // Re-enable the default theme programmatically to verify colors are still set
      let defaultTheme = await lazy.AddonManager.getAddonByID(DEFAULT_THEME_ID);
      await defaultTheme.enable();

      await TestUtils.waitForCondition(
        () =>
          SelectableProfileService.currentProfile.theme.themeId ===
          DEFAULT_THEME_ID,
        "Waiting for default theme to be set in profile"
      );

      // Verify avatar still has colors with default theme re-enabled
      await SpecialPowers.spawn(browser, [], async () => {
        let card =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => card.headerAvatar.style.fill && card.headerAvatar.style.stroke,
          "Waiting for avatar colors to be set"
        );

        let avatarFill = card.headerAvatar.style.fill;
        let avatarStroke = card.headerAvatar.style.stroke;

        Assert.ok(
          avatarFill.startsWith("rgb"),
          `Avatar fill should be a valid color value, got: ${avatarFill}`
        );
        Assert.ok(
          avatarStroke.startsWith("rgb"),
          `Avatar stroke should be a valid color value, got: ${avatarStroke}`
        );
      });

      Assert.ok(
        SelectableProfileService.currentProfile.theme.themeFg,
        "Profile should have theme foreground color with default theme"
      );
      Assert.ok(
        SelectableProfileService.currentProfile.theme.themeBg,
        "Profile should have theme background color with default theme"
      );
    }
  );

  await uninstallTheme();
  await SpecialPowers.popPrefEnv();

  await TestUtils.waitForCondition(
    () =>
      SelectableProfileService.currentProfile.theme.themeId === DEFAULT_THEME_ID
  );
});

add_task(async function test_device_appearance_changes_theme_swatch_colors() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  await setup();

  let uninstallTheme = await installNovaTheme(NOVA_SUN_ID);

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let card =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => card.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        let themePicker = card.shadowRoot.querySelector("theme-picker");
        await ContentTaskUtils.waitForCondition(
          () => themePicker?.themes?.length > 0,
          "Waiting for theme picker to be populated"
        );

        Assert.equal(
          themePicker.appearance,
          "device",
          "Should start in device mode"
        );

        let lightThemeEnabled = ContentTaskUtils.waitForCondition(
          () => themePicker.deviceAppearance === "light",
          "Waiting for device appearance to update to light"
        );

        await SpecialPowers.pushPrefEnv({
          set: [["ui.systemUsesDarkTheme", 0]],
        });

        await lightThemeEnabled;

        let sunThemeItem = themePicker.shadowRoot.querySelector(
          "moz-visual-picker-item[value='nova-sun@mozilla.org']"
        );
        let themePreview = sunThemeItem.querySelector(".theme-preview");
        let initialImage =
          content.getComputedStyle(themePreview).backgroundImage;

        Assert.ok(
          initialImage,
          "Theme preview should have initial background image"
        );

        await SpecialPowers.pushPrefEnv({
          set: [["ui.systemUsesDarkTheme", 1]],
        });

        await ContentTaskUtils.waitForCondition(
          () => themePicker.deviceAppearance === "dark",
          "Waiting for device appearance to update to dark"
        );

        // Wait for the theme preview style to actually update
        await ContentTaskUtils.waitForCondition(
          () =>
            content.getComputedStyle(themePreview).backgroundImage !==
            initialImage
        );

        let darkImage = content.getComputedStyle(themePreview).backgroundImage;
        Assert.notEqual(
          darkImage,
          initialImage,
          "Theme swatch color should change in dark mode"
        );

        await SpecialPowers.popPrefEnv();

        await SpecialPowers.pushPrefEnv({
          set: [["ui.systemUsesDarkTheme", 0]],
        });

        await ContentTaskUtils.waitForCondition(
          () => themePicker.deviceAppearance === "light",
          "Waiting for device appearance to update to light"
        );

        let lightImage = content.getComputedStyle(themePreview).backgroundImage;
        Assert.equal(
          lightImage,
          initialImage,
          "Theme swatch color should return to original in light mode"
        );

        await SpecialPowers.popPrefEnv();
      });
    }
  );

  await uninstallTheme();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_theme_picker_responds_to_external_theme_change() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }

  await setup();

  let uninstallSun = await installNovaTheme(NOVA_SUN_ID);

  const { SelectableProfileService } = ChromeUtils.importESModule(
    "resource:///modules/profiles/SelectableProfileService.sys.mjs"
  );
  let initialThemeFg = SelectableProfileService.currentProfile.theme.themeFg;
  let initialThemeBg = SelectableProfileService.currentProfile.theme.themeBg;

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        let themePicker =
          editProfileCard.shadowRoot.querySelector("theme-picker");
        await ContentTaskUtils.waitForCondition(
          () => themePicker?.themes?.length > 0,
          "Waiting for theme picker to be populated"
        );

        Assert.equal(
          themePicker.activeThemeId,
          "default-theme@mozilla.org",
          "Should start with default theme"
        );
      });

      let sunAddon = await lazy.AddonManager.getAddonByID(NOVA_SUN_ID);
      await sunAddon.enable();

      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;
        let themePicker =
          editProfileCard.shadowRoot.querySelector("theme-picker");

        await ContentTaskUtils.waitForCondition(
          () => themePicker.activeThemeId === "nova-sun@mozilla.org",
          "Waiting for theme picker to update to sun theme"
        );

        Assert.equal(
          themePicker.activeThemeId,
          "nova-sun@mozilla.org",
          "Theme picker should update to sun theme"
        );

        let sunThemeItem = themePicker.shadowRoot.querySelector(
          "moz-visual-picker-item[value='nova-sun@mozilla.org']"
        );
        Assert.ok(sunThemeItem.checked, "Sun theme should be checked");
      });

      await TestUtils.waitForCondition(
        () =>
          SelectableProfileService.currentProfile.theme.themeFg !==
            initialThemeFg ||
          SelectableProfileService.currentProfile.theme.themeBg !==
            initialThemeBg,
        "Waiting for profile theme colors to update"
      );

      Assert.notEqual(
        SelectableProfileService.currentProfile.theme.themeFg,
        initialThemeFg,
        "Profile theme foreground color should change"
      );
      Assert.notEqual(
        SelectableProfileService.currentProfile.theme.themeBg,
        initialThemeBg,
        "Profile theme background color should change"
      );

      await Services.fog.testFlushAllChildren();
      Assert.equal(
        Glean.themePicker.change.testGetValue(),
        null,
        "External theme changes should not record theme telemetry"
      );
    }
  );

  let profileUpdated = TestUtils.topicObserved("sps-profiles-updated");
  await uninstallSun();
  await profileUpdated;
  await SpecialPowers.popPrefEnv();
});

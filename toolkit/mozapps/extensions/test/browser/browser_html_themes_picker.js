/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Verifies the aboutaddons-themes-picker component behaviour in about:addons.

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const { getThemesList } = ChromeUtils.importESModule(
  "moz-src:///browser/themes/ThemesList.sys.mjs"
);

const { getL10nIdForThemeProp, themeIdPrefix } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/ThemesBundledLocalization.sys.mjs"
);

AddonTestUtils.initMochitest(this);

const PREF_NOVA_THEMES_PICKER = "browser.aboutaddons.novaThemesPickerEnabled";
const PREF_ACTIVE_THEME_ID = "extensions.activeThemeID";
const PREF_SYSTEM_USES_DARK_THEME = "ui.systemUsesDarkTheme";
// Website Appearance setting exposed in about:preferences, which only
// overrides prefers-color-scheme for content documents and must not
// affect the (chrome-only) theme preview color scheme.
const PREF_CONTENT_COLOR_SCHEME_OVERRIDE =
  "layout.css.prefers-color-scheme.content-override";
const PREF_LINK_PARAMETERS_ENABLED = "layout.css.link-parameters.enabled";

const DEFAULT_THEME_ID = "default-theme@mozilla.org";
const LIGHT_THEME_ID = "firefox-compact-light@mozilla.org";
const DARK_THEME_ID = "firefox-compact-dark@mozilla.org";
const NOVA_SUN_ID = "nova-sun@mozilla.org";
const DEFAULT_THEME_ID_PREFIX = "default-theme";
const NOVA_SUN_ID_PREFIX = "nova-sun";

const DEFAULT_THEME_PREVIEW_URL =
  "chrome://mozapps/content/extensions/default-theme/preview.svg";
const DEFAULT_THEME_PREVIEW_NOVA_URL =
  "chrome://mozapps/content/extensions/default-theme/preview-nova.svg";
const RTL_FLIP_PARAM = "param(--rtl-flip, var(--theme-preview-rtl-flip))";

let ALL_THEME_IDS;

add_setup(async function () {
  const themesListManager = await getThemesList({
    installSource: "about:addons",
  });
  ALL_THEME_IDS = themesListManager
    .getThemesInfo()
    .map(themeInfo => themeInfo.id);
});

const getThemesPicker = doc => doc.querySelector("aboutaddons-themes-picker");

function getThemeCard(picker, idPrefix) {
  return [...picker.themeCards].find(card => card.dataset.theme === idPrefix);
}

function getThemeButton(card) {
  return card.querySelector(".theme-card-footer moz-button");
}

async function getThemePreviewImage(card) {
  const themePreview = card.querySelector("theme-preview");
  await themePreview.updateComplete;
  return card.querySelector(".card-heading-image");
}

async function getThemePreviewSrc(card) {
  return (await getThemePreviewImage(card))?.src;
}

// Asserts that the given card reuses the default theme Nova preview image,
// recolored through link-parameters for the given (non default) theme.
async function assertNovaThemePreview(card, themeId, themesListManager) {
  const img = await getThemePreviewImage(card);
  Assert.equal(
    img.src,
    DEFAULT_THEME_PREVIEW_NOVA_URL,
    `"${themeId}" card reuses the default theme Nova preview image`
  );
  let expectedLinkParameters =
    themeId === DEFAULT_THEME_ID
      ? ""
      : themesListManager.getThemePreviewLinkParameters(themeId);
  if (themeId !== DEFAULT_THEME_ID) {
    Assert.ok(
      expectedLinkParameters.includes("param(--tabbar-background, light-dark("),
      `"${themeId}" has link-parameters for the Nova preview image`
    );
  }
  expectedLinkParameters = expectedLinkParameters
    ? `${expectedLinkParameters}, ${RTL_FLIP_PARAM}`
    : RTL_FLIP_PARAM;
  Assert.equal(
    img.style.linkParameters,
    expectedLinkParameters,
    `"${themeId}" preview image has the expected link-parameters`
  );

  return img;
}

async function waitForThemesPickerReady(picker) {
  await TestUtils.waitForCondition(
    () => picker.themeCards.length,
    "Waiting for the themes picker to finish loading its theme list"
  );
  await picker.updateComplete;
}

function createNovaThemeXPI(themeId, themeName) {
  const mockNovaThemeXPI = AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      name: themeName,
      version: "1.0",
      theme: {},
      browser_specific_settings: { gecko: { id: themeId } },
    },
  });
  // Make sure to uninstall the mock nova theme if still installed when this
  // test file is exiting.
  registerCleanupFunction(async function cleanupInstalledMockNovaTheme() {
    const mockNovaTheme = await AddonManager.getAddonByID(themeId);
    await mockNovaTheme?.uninstall();
  });
  return mockNovaThemeXPI;
}

// Mocks the AddonRepository "get addons" endpoint so that
// ThemesList.updateThemeState's install-via-AddonRepository path (used by
// the picker's Install button) resolves to a local XPI instead of the real
// AMO service. Omit `xpi` to simulate a broken/unreachable theme download
// instead (the resolved XPI url will 404).
function setupAddonRepoServer(themeId, themeName, xpi) {
  const server = AddonTestUtils.createHttpServer({ hosts: ["example.com"] });
  if (xpi) {
    server.registerFile("/theme.xpi", xpi);
  }
  AddonTestUtils.registerJSON(server, "/addons.json", {
    page_size: 1,
    page_count: 1,
    count: 1,
    next: null,
    previous: null,
    results: [
      {
        guid: themeId,
        name: themeName,
        type: "statictheme",
        current_version: {
          version: "1.0",
          // eslint-disable-next-line sdl/no-insecure-url
          files: [{ platform: "all", url: "http://example.com/theme.xpi" }],
        },
      },
    ],
  });

  Services.prefs.setCharPref(
    "extensions.getAddons.get.url",
    // eslint-disable-next-line sdl/no-insecure-url
    "http://example.com/addons.json"
  );

  function cleanupAddonRepoServer() {
    Services.prefs.clearUserPref("extensions.getAddons.get.url");
  }
  registerCleanupFunction(cleanupAddonRepoServer);
  return cleanupAddonRepoServer;
}

// Verifies that the Nova themes picker can be opt-ed out through
// the "browser.aboutaddons.novaThemesPickerEnabled" pref independently
// from Nova (in case other apps built on top of Gecko, e.g. Thunderbird,
// would like to adopt Nova restyle without inheriting the Nova themes
// picker).
add_task(async function test_picker_hidden_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, false]],
  });

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  Assert.ok(picker, "aboutaddons-themes-picker element exists in the DOM");
  await picker.updateComplete;
  Assert.ok(
    !picker.pickerCard,
    "Themes grid should not be rendered when novaThemesPickerEnabled is false"
  );

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that all the Nova themes are shown in the Nova Themes grid
// moz-card, they are all localized as expected, and the theme preview
// svg images bundled into the Firefox build are being used for all
// the AMO-hosted Nova themes shown in the grid.
add_task(async function test_picker_renders_all_known_themes() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  // Make sure the default theme is the active theme.
  const defaultAddon = await AddonManager.getAddonByID(DEFAULT_THEME_ID);
  await defaultAddon.enable();

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  // Expand the grid to see every theme card. The expand/collapse
  // behaviour is covered by test_picker_show_more_show_less below.
  picker.expandToggle.click();
  await picker.updateComplete;

  const themesListManager = await getThemesList({
    installSource: "about:addons",
  });
  for (const themeId of ALL_THEME_IDS) {
    const idPrefix = themeIdPrefix(themeId);
    const card = getThemeCard(picker, idPrefix);
    Assert.ok(card, `theme card for "${idPrefix}" is rendered`);
    Assert.equal(
      card.querySelector(".theme-name").getAttribute("data-l10n-id"),
      getL10nIdForThemeProp(themeId, "name"),
      `"${idPrefix}" card has the expected name l10n-id`
    );

    await assertNovaThemePreview(card, themeId, themesListManager);
  }
  Assert.equal(
    picker.themeCards.length,
    ALL_THEME_IDS.length,
    "Expect no unexpected Nova theme cards"
  );

  // The default theme is currently active: its preview image in the picker
  // grid should be the exact same image shown on its addon-card in the
  // regular themes list.
  const defaultPickerCard = getThemeCard(picker, DEFAULT_THEME_ID_PREFIX);
  const defaultAddonCard = AboutAddonsTestUtils.getAddonCard(
    win,
    DEFAULT_THEME_ID
  );
  Assert.equal(
    await getThemePreviewSrc(defaultPickerCard),
    await getThemePreviewSrc(defaultAddonCard),
    "addon-card should be using the same preview image as the themes picker card"
  );

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that the built-in Light and Dark themes reuse the default theme
// Nova preview image, forced to the matching color scheme.
add_task(async function test_light_dark_themes_preview() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  const win = await loadInitialView("theme");

  for (const [themeId, colorScheme] of [
    [LIGHT_THEME_ID, "light"],
    [DARK_THEME_ID, "dark"],
  ]) {
    const img = await getThemePreviewImage(getAddonCard(win, themeId));
    Assert.equal(
      img.src,
      DEFAULT_THEME_PREVIEW_NOVA_URL,
      `${themeId} uses the expected preview image`
    );
    Assert.equal(
      img.style.colorScheme,
      colorScheme,
      `${themeId} preview image has the expected forced color scheme`
    );
  }

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that for the default theme and the curated AMO hosted extra Nova themes
// their theme preview SVGs color scheme are:
// - being forced to match the aboutaddons-themes-mode selector
// - falling back to the current OS dark/light color scheme when the appearance
//   mode is set to "device"
// - are not affected by the "Website Appearance" setting from about:settings
//   (or the underlying layout.css.prefers-color-scheme.content-override pref).
add_task(async function test_default_and_extra_themes_preview_color_scheme() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  const win = await loadInitialView("theme");
  const themesListManager = await getThemesList({
    installSource: "about:addons",
  });

  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  const systemDarkThemeQuery = win.matchMedia("(-moz-system-dark-theme)");

  for (const [prefValue, expectedColorScheme] of [
    [0, "light"],
    [1, "dark"],
    [undefined, systemDarkThemeQuery.matches ? "dark" : "light"],
  ]) {
    // If the color scheme we expect is the same as the color scheme already active
    // then we expect no "change" event to be emitted by the -moz-system-dark-theme
    // media query after we set or clear the ui.systemUsesDarkTheme about:config pref.
    const expectedSystemDarkMatch = expectedColorScheme === "dark";
    const promiseMediaQueryChangeEvent =
      systemDarkThemeQuery.matches === expectedSystemDarkMatch
        ? Promise.resolve()
        : new Promise(resolve => {
            info("Wait for matchMedia -moz-system-dark-theme change event");
            systemDarkThemeQuery.addEventListener("change", resolve, {
              once: true,
            });
          });

    await SpecialPowers.pushPrefEnv(
      prefValue === undefined
        ? { clear: [[PREF_SYSTEM_USES_DARK_THEME]] }
        : { set: [[PREF_SYSTEM_USES_DARK_THEME, prefValue]] }
    );

    // ui.systemUsesDarkTheme changes propagate to -moz-system-dark-theme
    // asynchronously.
    await promiseMediaQueryChangeEvent;

    // Force the opposite color scheme for content documents' "Website Appearance"
    // (about:preferences), to explicitly verify the theme preview is not
    // affected by it.
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          PREF_CONTENT_COLOR_SCHEME_OVERRIDE,
          expectedColorScheme == "dark" ? 1 : 0,
        ],
      ],
    });

    const defaultThemeImg = await assertNovaThemePreview(
      getAddonCard(win, DEFAULT_THEME_ID),
      DEFAULT_THEME_ID,
      themesListManager
    );
    Assert.equal(
      defaultThemeImg.style.colorScheme,
      expectedColorScheme,
      `default-theme preview has the expected forced color scheme (pref: ${prefValue})`
    );

    const novaSunImg = await assertNovaThemePreview(
      getThemeCard(picker, NOVA_SUN_ID_PREFIX),
      NOVA_SUN_ID,
      themesListManager
    );
    Assert.equal(
      novaSunImg.style.colorScheme,
      expectedColorScheme,
      `nova-sun preview has the expected forced color scheme (pref: ${prefValue})`
    );

    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that in RTL locales the Nova preview svg is asked to mirror its UI
// elements through link-parameters (so that its tabbar background gradient
// is not mirrored), instead of being mirrored as a whole by the page like the
// other theme preview images.
add_task(async function test_themes_preview_rtl() {
  const TEST_THEME_ID = "rtl-test-theme@mochi.test";

  // link-parameters is enabled in chrome on all channels, and so it applies to
  // about:addons everywhere, but getComputedStyle only exposes properties that
  // are enabled for all content (see nsComputedDOMStyle's Entry::IsEnabled).
  // Enable the pref so that the computed link-parameters assertions below can
  // also inspect the resolved param values on non-Nightly channels.
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_NOVA_THEMES_PICKER, true],
      [PREF_LINK_PARAMETERS_ENABLED, true],
    ],
  });

  const themesListManager = await getThemesList({
    installSource: "about:addons",
  });

  const testTheme = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "RTL test theme",
      theme: { colors: {} },
      browser_specific_settings: { gecko: { id: TEST_THEME_ID } },
    },
    files: { "preview.png": "" },
    useAddonManager: "temporary",
  });
  await testTheme.startup();

  async function assertThemesPreviews(win, { isRTL }) {
    const picker = getThemesPicker(win.document);
    await waitForThemesPickerReady(picker);

    for (const [themeId, card] of [
      [DEFAULT_THEME_ID, getAddonCard(win, DEFAULT_THEME_ID)],
      [LIGHT_THEME_ID, getAddonCard(win, LIGHT_THEME_ID)],
      [NOVA_SUN_ID, getThemeCard(picker, NOVA_SUN_ID_PREFIX)],
    ]) {
      const img = await getThemePreviewImage(card);
      Assert.equal(
        img.src,
        DEFAULT_THEME_PREVIEW_NOVA_URL,
        `"${themeId}" card uses the Nova preview image`
      );
      const themeLinkParameters =
        themesListManager.getThemePreviewLinkParameters(themeId);
      const expectedLinkParameters = [];
      if (themeLinkParameters) {
        expectedLinkParameters.push(themeLinkParameters);
      }
      expectedLinkParameters.push(RTL_FLIP_PARAM);
      Assert.equal(
        img.style.linkParameters,
        expectedLinkParameters.join(", "),
        `"${themeId}" preview image has the expected link-parameters`
      );
      const expectedRtlFlip = isRTL ? "scaleX(-1)" : "none";
      Assert.equal(
        win
          .getComputedStyle(img)
          .getPropertyValue("--theme-preview-rtl-flip")
          .trim(),
        expectedRtlFlip,
        `"${themeId}" preview image inherits the expected --theme-preview-rtl-flip value (RTL: ${isRTL})`
      );
      Assert.ok(
        win
          .getComputedStyle(img)
          .linkParameters.includes(`param(--rtl-flip, ${expectedRtlFlip})`),
        `"${themeId}" preview image has the expected computed --rtl-flip link-parameter (RTL: ${isRTL})`
      );
      Assert.equal(
        win.getComputedStyle(img).transform,
        "none",
        `"${themeId}" preview image is not mirrored by the page (RTL: ${isRTL})`
      );
    }

    // The other theme preview images are still expected to be mirrored as a
    // whole by the page in RTL locales.
    const img = await getThemePreviewImage(getAddonCard(win, TEST_THEME_ID));
    // Sanity check (test theme have its own preview image as expected).
    Assert.ok(
      img.src.endsWith("/preview.png"),
      "test theme card uses its own preview image"
    );
    Assert.equal(
      img.style.linkParameters,
      "",
      `test theme preview image has no link-parameters (RTL: ${isRTL})`
    );
    Assert.equal(
      win.getComputedStyle(img).transform,
      isRTL ? "matrix(-1, 0, 0, 1, 0, 0)" : "none",
      `test theme preview image has the expected transform (RTL: ${isRTL})`
    );
  }

  // Sanity check (the test starts in LTR).
  Assert.ok(
    !Services.locale.isAppLocaleRTL,
    "Expect isAppLocaleRTL to be initially false"
  );
  let win = await loadInitialView("theme");
  await assertThemesPreviews(win, { isRTL: false });
  await closeView(win);

  info("Switch to an RTL locale without reloading about:addons");
  await SpecialPowers.pushPrefEnv({ set: [["intl.l10n.pseudo", "bidi"]] });
  Assert.ok(Services.locale.isAppLocaleRTL, "App locale is RTL");
  win = await loadInitialView("theme");
  await assertThemesPreviews(win, { isRTL: true });
  await closeView(win);

  await testTheme.unload();
  await SpecialPowers.popPrefEnv();
});

// Verifies that the Nova themes picker can be expanded and collapsed as
// expected.
add_task(async function test_picker_show_more_show_less() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  Assert.equal(
    picker.visibleRows,
    3,
    "Expect the themes picker grid to have 3 visible rows in collapsed mode"
  );
  const expectedCollapsedCount = picker.visibleRows * 2;
  Assert.equal(
    picker.themeCards.length,
    expectedCollapsedCount,
    `only the first ${expectedCollapsedCount} theme cards are shown initially`
  );

  Assert.ok(
    BrowserTestUtils.isVisible(picker.expandToggle),
    "'Show More'/'Show Less' toggle button is shown"
  );
  Assert.equal(
    picker.expandToggle.getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-see-more",
    "While collapsed, toggle button should show the 'Show More' localized string"
  );

  picker.expandToggle.click();
  await picker.updateComplete;

  Assert.equal(
    picker.themeCards.length,
    ALL_THEME_IDS.length,
    "All theme cards are shown after expanding"
  );
  Assert.equal(
    picker.expandToggle.getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-see-less",
    "While expanded, toggle button should show the 'Show Less' localized string"
  );

  picker.expandToggle.click();
  await picker.updateComplete;

  Assert.equal(
    picker.themeCards.length,
    expectedCollapsedCount,
    "Themes picker collapses back to the initial theme card count"
  );

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that the default theme card in the Nova themes grid
// has the Disable button hidden when the default theme is active
// (because the default theme can only be disabled by enabling
// a different theme).
add_task(async function test_picker_default_theme_button_state() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  // Make sure the default theme has been enabled.
  const defaultAddon = await AddonManager.getAddonByID(DEFAULT_THEME_ID);
  await defaultAddon.enable();

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  const defaultCard = getThemeCard(picker, DEFAULT_THEME_ID_PREFIX);
  Assert.ok(
    getThemeButton(defaultCard).hidden,
    "the default theme button is hidden while it is active"
  );

  let promiseActiveThemePrefChanged = TestUtils.waitForPrefChange(
    PREF_ACTIVE_THEME_ID,
    value => value === NOVA_SUN_ID
  );

  // Mock the theme XPI for one of the non-builtIn Nova theme.
  const xpi = createNovaThemeXPI(NOVA_SUN_ID, "Nova Sun");
  const sunAddon = await AddonManager.installTemporaryAddon(xpi);
  await sunAddon.enable();

  info("Waiting for extensions.activeThemeID to be set to the expected theme");
  await promiseActiveThemePrefChanged;
  await picker.updateComplete;

  Assert.ok(
    !getThemeButton(defaultCard).hidden,
    "Expect default theme button to become visible"
  );
  Assert.equal(
    getThemeButton(defaultCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-enable-button",
    "default theme shows an Enable button once inactive"
  );

  const activeNovaThemeAddonCard = AboutAddonsTestUtils.getAddonCard(
    win,
    NOVA_SUN_ID
  );

  await assertNovaThemePreview(
    activeNovaThemeAddonCard,
    NOVA_SUN_ID,
    await getThemesList({ installSource: "about:addons" })
  );

  await sunAddon.uninstall();
  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that for the Nova themes hosted on AMO the theme card
// button is localized as "Install" when the theme is not installed
// yet, and once it is installed the same button is localized as
// "Enable" and "Disable" based on the currently active theme.
add_task(async function test_picker_amo_hosted_themes_button() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  Assert.ok(
    !(await AddonManager.getAddonByID(NOVA_SUN_ID)),
    "nova-sun is not installed at the start of this test"
  );

  // Nova AMO hosted theme not installed yet.
  {
    const win = await loadInitialView("theme");
    const picker = getThemesPicker(win.document);
    await waitForThemesPickerReady(picker);

    const sunCard = getThemeCard(picker, NOVA_SUN_ID_PREFIX);
    Assert.equal(
      getThemeButton(sunCard).getAttribute("data-l10n-id"),
      "aboutaddons-themes-picker-install-button",
      "not-yet-installed AMO-hosted theme shows an Install button"
    );

    await closeView(win);
  }

  const xpi = createNovaThemeXPI(NOVA_SUN_ID, "Nova Sun");
  const sunAddon = await AddonManager.installTemporaryAddon(xpi);
  await sunAddon.disable();

  // Nova AMO hosted theme already installed.
  {
    const win = await loadInitialView("theme");
    const picker = getThemesPicker(win.document);
    await waitForThemesPickerReady(picker);

    const sunCard = getThemeCard(picker, NOVA_SUN_ID_PREFIX);
    Assert.equal(
      getThemeButton(sunCard).getAttribute("data-l10n-id"),
      "aboutaddons-themes-picker-enable-button",
      "Installed but inactive theme expected to show an Enable button"
    );

    let promiseActiveThemePrefChanged = TestUtils.waitForPrefChange(
      PREF_ACTIVE_THEME_ID,
      value => value === NOVA_SUN_ID
    );

    await sunAddon.enable();

    info(
      "Waiting for extensions.activeThemeID to be set to the expected theme"
    );
    await promiseActiveThemePrefChanged;
    await picker.updateComplete;

    Assert.equal(
      getThemeButton(sunCard).getAttribute("data-l10n-id"),
      "aboutaddons-themes-picker-disable-button",
      "Theme card expected to show a Disable button"
    );

    await sunAddon.uninstall();
    await closeView(win);
  }
  await SpecialPowers.popPrefEnv();
});

// Verifies that when the "Enable/Disable" button is clicked, the currently
// active theme is changing as expected.
add_task(async function test_picker_button_click_updates_active_theme() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  const xpi = createNovaThemeXPI(NOVA_SUN_ID, "Nova Sun");
  const sunAddon = await AddonManager.installTemporaryAddon(xpi);
  await sunAddon.disable();

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  const sunCard = getThemeCard(picker, NOVA_SUN_ID_PREFIX);
  const defaultCard = getThemeCard(picker, DEFAULT_THEME_ID_PREFIX);

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  let promiseActiveThemePrefChanged = TestUtils.waitForPrefChange(
    PREF_ACTIVE_THEME_ID,
    value => value === NOVA_SUN_ID
  );

  getThemeButton(sunCard).click();

  info(
    "Waiting for extensions.activeThemeID to be updated after clicking Enable"
  );
  await promiseActiveThemePrefChanged;

  await picker.updateComplete;
  await Services.fog.testFlushAllChildren();
  const events = Glean.themePicker.change.testGetValue();
  Assert.equal(events?.length, 1, "theme_picker.change is recorded once");
  Assert.equal(events[0].extra.property, "theme", "property is theme");
  Assert.equal(events[0].extra.layout, "full", "layout is full");

  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-disable-button",
    "Theme card expected to show a Disable button"
  );

  promiseActiveThemePrefChanged = TestUtils.waitForPrefChange(
    PREF_ACTIVE_THEME_ID,
    value => value === DEFAULT_THEME_ID
  );

  getThemeButton(sunCard).click();

  info(
    "Waiting for extensions.activeThemeID to be updated after clicking Disable"
  );
  await promiseActiveThemePrefChanged;

  await picker.updateComplete;
  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-enable-button",
    "Theme card expected to show an Enable button after being disabled"
  );
  Assert.equal(
    getThemeButton(defaultCard).hidden,
    true,
    "The default theme button should be hidden"
  );

  await sunAddon.uninstall();
  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that the "Install" button download and actives the Nova themes
// hosted on AMO.
add_task(async function test_picker_install_button_downloads_and_activates() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  const xpi = createNovaThemeXPI(NOVA_SUN_ID, "Nova Sun");
  const cleanupAddonRepoServer = setupAddonRepoServer(
    NOVA_SUN_ID,
    "Nova Sun",
    xpi
  );

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  // Sanity check.
  const sunCard = getThemeCard(picker, NOVA_SUN_ID_PREFIX);
  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-install-button",
    "nova-sun theme should not be installed yet"
  );

  let promiseActiveThemePrefChanged = TestUtils.waitForPrefChange(
    PREF_ACTIVE_THEME_ID,
    value => value === NOVA_SUN_ID
  );

  getThemeButton(sunCard).click();
  await promiseActiveThemePrefChanged;

  await picker.updateComplete;

  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-disable-button",
    "Theme card expected to show a Disable button"
  );

  const sunAddon = await AddonManager.getAddonByID(NOVA_SUN_ID);
  Assert.equal(
    sunAddon?.isActive,
    true,
    "nova-sun theme expected to be installed and activated"
  );

  await sunAddon.uninstall();
  cleanupAddonRepoServer();
  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that the themes picker shows a dismissable error message bar
// when a Nova theme fails to download/install (Bug 2054548), and that the
// bar is cleared again on dismiss.
add_task(async function test_picker_shows_error_on_install_failure() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  // No xpi passed: the resolved XPI url 404s, simulating a broken download.
  const cleanupAddonRepoServer = setupAddonRepoServer(NOVA_SUN_ID, "Nova Sun");

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  Assert.ok(
    !picker.errorBar,
    "no error message bar shown before attempting to install"
  );

  const sunCard = getThemeCard(picker, NOVA_SUN_ID_PREFIX);
  getThemeButton(sunCard).click();

  info("Waiting for the error message bar to be shown");
  await TestUtils.waitForCondition(() => picker.errorBar);
  await picker.updateComplete;

  Assert.equal(
    picker.errorBar.getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-error-message",
    "error message bar shows the expected l10n id"
  );
  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-install-button",
    "theme card still shows the Install button after a failed install"
  );

  picker.errorBar.dismiss();
  await picker.updateComplete;
  Assert.ok(!picker.errorBar, "error message bar is dismissed");

  cleanupAddonRepoServer();
  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that the theme picker is updating also when the themes
// are being updated externally (e.g. from a different about:addons
// tab).
add_task(async function test_picker_reflects_external_addon_install() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  // Sanity check.
  const sunCard = getThemeCard(picker, NOVA_SUN_ID_PREFIX);
  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-install-button",
    "nova-sun starts out not installed"
  );

  let promiseActiveThemePrefChanged = TestUtils.waitForPrefChange(
    PREF_ACTIVE_THEME_ID,
    value => value === NOVA_SUN_ID
  );

  // Install directly through AddonManager, bypassing the picker's own
  // Install button, to verify the theme picker UI is reacting to the
  // AddonManager events.
  const xpi = createNovaThemeXPI(NOVA_SUN_ID, "Nova Sun");
  const sunAddon = await AddonManager.installTemporaryAddon(xpi);

  info("Waiting for the currently active theme to have been changed");
  await promiseActiveThemePrefChanged;

  Assert.equal(
    getThemeButton(sunCard).getAttribute("data-l10n-id"),
    "aboutaddons-themes-picker-disable-button",
    "Expect the related picker theme card button to have been updated accordingly"
  );

  await sunAddon.uninstall();
  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// The theme preview box is not a whole number of device pixels tall, so
// painting snaps it and an SVG used as an image is laid out at the snapped
// size. A preview that relies on the default preserveAspectRatio is therefore
// letterboxed by a varying amount and appears to jiggle while scrolling, see
// bug 2059917. Guard against a new preview reintroducing that.
add_task(async function test_theme_preview_svgs_ignore_aspect_ratio() {
  const previewUrls = [
    DEFAULT_THEME_PREVIEW_URL,
    DEFAULT_THEME_PREVIEW_NOVA_URL,
    "resource://builtin-themes/dark/preview.svg",
    "resource://builtin-themes/light/preview.svg",
    "resource://builtin-themes/alpenglow/preview.svg",
  ];

  for (const url of previewUrls) {
    const response = await fetch(url);
    Assert.ok(
      response.ok,
      `Expect ${url} to be available as bundled in the omnijar`
    );
    const svgRoot = new DOMParser().parseFromString(
      await response.text(),
      "image/svg+xml"
    ).documentElement;

    Assert.equal(
      svgRoot.getAttribute("preserveAspectRatio"),
      "none",
      `Expect ${url} to set preserveAspectRatio="none"`
    );
    // preserveAspectRatio only has an effect when a viewBox maps the drawing
    // onto the viewport, and without one the preview is cropped to the snapped
    // size instead of being scaled to it.
    Assert.equal(
      svgRoot.getAttribute("viewBox"),
      "0 0 680 92",
      `Expect ${url} to declare a viewBox set to the expected value`
    );
    // Sanity and consistency checks (to make sure that all SVG images for the bundled
    // theme previews have the width and height properties set consistently with
    // the viewBox attribute).
    Assert.ok(
      svgRoot.hasAttribute("width"),
      `Expect ${url} to declare an explicit width`
    );
    Assert.ok(
      svgRoot.hasAttribute("height"),
      `Expect ${url} to declare an explicit height`
    );
    Assert.equal(
      svgRoot.getAttribute("viewBox"),
      `0 0 ${svgRoot.getAttribute("width")} ${svgRoot.getAttribute("height")}`
    );
  }
});

// Verifies that opening the Nova themes picker records a theme_picker.shown
// event, and that navigating away and back to the Themes view records it
// again.
add_task(async function test_picker_shown_recorded_on_each_theme_view_load() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, true]],
  });

  Services.fog.testResetFOG();

  const win = await loadInitialView("extension");

  // Sanity check: until the theme view is loaded, the theme_picker.shown
  // event should not have been recorded yet.
  let events = Glean.themePicker.shown.testGetValue();
  Assert.equal(events, undefined, "theme_picker.shown is NOT recorded yet");

  await switchView(win, "theme");
  let picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  events = Glean.themePicker.shown.testGetValue();
  Assert.equal(events?.length, 1, "theme_picker.shown is recorded once");
  Assert.deepEqual(
    {
      source: events[0].extra.source,
      layout: events[0].extra.layout,
    },
    { source: "about:addons", layout: "full" },
    "theme_picker.shown event has the expected source and layout"
  );

  await switchView(win, "extension");
  await switchView(win, "theme");
  picker = getThemesPicker(win.document);
  await waitForThemesPickerReady(picker);

  events = Glean.themePicker.shown.testGetValue();
  Assert.equal(
    events?.length,
    2,
    "theme_picker.shown is recorded again when navigating back to the theme view"
  );
  Assert.deepEqual(
    {
      source: events[1].extra.source,
      layout: events[1].extra.layout,
    },
    { source: "about:addons", layout: "full" },
    "second theme_picker.shown event has the expected source and layout"
  );

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

// Verifies that theme_picker.shown is not recorded when the Nova themes
// picker doesn't render any visible content.
add_task(async function test_picker_shown_not_recorded_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_NOVA_THEMES_PICKER, false]],
  });

  Services.fog.testResetFOG();

  const win = await loadInitialView("theme");
  const picker = getThemesPicker(win.document);
  await picker.updateComplete;

  Assert.equal(
    Glean.themePicker.shown.testGetValue(),
    undefined,
    "theme_picker.shown is not recorded when the picker isn't shown"
  );

  await closeView(win);
  await SpecialPowers.popPrefEnv();
});

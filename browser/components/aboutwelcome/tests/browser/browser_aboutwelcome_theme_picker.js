"use strict";

const { AboutWelcomeDefaults } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeDefaults.sys.mjs"
);

const { ThemePickerParent } = ChromeUtils.importESModule(
  "resource:///actors/ThemePickerParent.sys.mjs"
);

function getThemePickerScreen() {
  const { screens } = AboutWelcomeDefaults.getDefaults();
  return screens.find(screen => screen.id === "AW_THEME_PICKER");
}

add_task(async function test_aboutwelcome_theme_picker_screen_displays() {
  await pushPrefs(
    ["ui.systemUsesDarkTheme", -1],
    ["browser.theme.native-theme", false]
  );
  await setAboutWelcomeMultiStage(JSON.stringify([getThemePickerScreen()]));

  Services.fog.testResetFOG();

  let { cleanup, browser } = await openMRAboutWelcome();

  await test_screen_content(
    browser,
    "theme picker screen renders",
    // Expected selectors:
    ["main.AW_THEME_PICKER", ".main-content", "theme-picker"]
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const renderedThemePicker = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("theme-picker"),
      "theme-picker element should be present"
    );

    await renderedThemePicker.updateComplete;

    await ContentTaskUtils.waitForCondition(
      () =>
        !!renderedThemePicker.shadowRoot.querySelectorAll(
          "moz-visual-picker-item"
        ).length,
      "theme-picker should render at least one theme button"
    );
  });

  await Services.fog.testFlushAllChildren();
  const events = Glean.themePicker.shown.testGetValue();
  Assert.equal(
    events?.length,
    1,
    "Displaying the screen should record shown once"
  );
  Assert.equal(
    events?.[0].extra.source,
    "about:welcome",
    "The about:welcome source should be recorded"
  );
  Assert.equal(
    events?.[0].extra.layout,
    "full",
    "The full picker layout should be recorded"
  );

  Services.fog.testResetFOG();

  const { source, layout, nativeThemeChanged } = await SpecialPowers.spawn(
    browser,
    [],
    async () => {
      const themePicker = content.document.querySelector("theme-picker");
      const picker = themePicker.wrappedJSObject;
      const EventUtils = ContentTaskUtils.getEventUtils(content);

      const appearanceButton = themePicker.shadowRoot.querySelector(
        'moz-segmented-control-item[value="dark"]'
      );
      EventUtils.synthesizeMouseAtCenter(appearanceButton, {}, content);
      await ContentTaskUtils.waitForCondition(
        () => picker.appearance === "dark",
        "The rendered theme picker should update to dark appearance"
      );

      const nativeThemeCheckbox =
        themePicker.shadowRoot.querySelector("moz-checkbox");
      if (nativeThemeCheckbox) {
        EventUtils.synthesizeMouseAtCenter(nativeThemeCheckbox, {}, content);
        await ContentTaskUtils.waitForCondition(
          () => picker.nativeTheme,
          "The rendered theme picker should enable the native theme"
        );
      }

      return {
        source: themePicker.getAttribute("installsource"),
        layout: picker.layout,
        nativeThemeChanged: !!nativeThemeCheckbox,
      };
    }
  );

  await Services.fog.testFlushAllChildren();

  const expectedChangeExtras = [
    {
      source,
      layout,
      property: "appearance",
      appearance: "dark",
    },
  ];
  if (nativeThemeChanged) {
    expectedChangeExtras.push({
      source,
      layout,
      property: "nativeTheme",
      native_theme: "true",
    });
  }

  const changeEvents = Glean.themePicker.change.testGetValue();
  Assert.deepEqual(
    changeEvents?.map(event => event.extra),
    expectedChangeExtras,
    "Clicked picker changes should record their telemetry extras"
  );

  await cleanup();
  await popPrefs();
});

add_task(async function test_theme_picker_parent_retries_failed_manager_load() {
  const parent = new ThemePickerParent();

  async function getManagerError() {
    let error;
    try {
      await parent.getThemesManager();
    } catch (caughtError) {
      error = caughtError;
    }
    Assert.stringContains(
      error?.message,
      "getThemesList installSource option is mandatory",
      "The manager load should fail for a missing install source"
    );
    return error;
  }

  const firstError = await getManagerError();
  const secondError = await getManagerError();

  Assert.notEqual(
    firstError,
    secondError,
    "A failed manager load should be retried"
  );
});

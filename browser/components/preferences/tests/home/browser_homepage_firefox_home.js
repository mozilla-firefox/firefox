/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SHOW_SEARCH_PREF = "browser.newtabpage.activity-stream.showSearch";
const SHOW_WEATHER_SYSTEM_PREF =
  "browser.newtabpage.activity-stream.widgets.system.weather.enabled";
const SHOW_WEATHER_PREF =
  "browser.newtabpage.activity-stream.widgets.weather.enabled";
// Weather only gets its own row when the Widgets group is hidden.
const WIDGETS_SYSTEM_PREF =
  "browser.newtabpage.activity-stream.widgets.system.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Opening preferences initializes FxA code which sets this pref.
      // Track it to avoid test warnings.
      ["identity.fxaccounts.account.device.name", ""],
    ],
  });
});

add_task(async function test_firefox_home_section_visible() {
  let { doc, tab } = await openHomePreferences();

  let homeGroup = doc.querySelector('setting-group[groupid="home"]');
  ok(homeGroup, "Firefox Home setting group exists");
  ok(BrowserTestUtils.isVisible(homeGroup), "Firefox Home section is visible");

  let fieldset = homeGroup.querySelector("moz-fieldset");
  ok(fieldset, "Firefox Home moz-fieldset exists");

  await fieldset.updateComplete;

  let heading = fieldset.shadowRoot.querySelector("h3");
  ok(heading, "Firefox Home heading exists in shadow DOM");
  Assert.greater(
    heading.textContent.length,
    0,
    "Firefox Home heading has localized text"
  );

  let icon = fieldset.shadowRoot.querySelector('img[src*="home.svg"]');
  ok(icon, "Firefox Home icon is present in shadow DOM");

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_web_search_toggle() {
  await SpecialPowers.pushPrefEnv({
    set: [[SHOW_SEARCH_PREF, true]],
  });

  let { win, tab } = await openHomePreferences();

  let webSearchControl = await settingControlRenders("webSearch", win);
  ok(webSearchControl, "Web search control exists");

  let toggle = webSearchControl.querySelector("moz-toggle");
  ok(toggle, "Web search toggle element exists");
  ok(toggle.pressed, "Web search toggle is initially checked");

  let prefChanged = waitForPrefChange(SHOW_SEARCH_PREF, false);
  toggle.click();
  await prefChanged;
  await waitForToggleState(toggle, false);

  ok(
    !Services.prefs.getBoolPref(SHOW_SEARCH_PREF),
    "Web search pref is now false"
  );
  ok(!toggle.pressed, "Web search toggle is now unchecked");

  prefChanged = waitForPrefChange(SHOW_SEARCH_PREF, true);
  toggle.click();
  await prefChanged;
  await waitForToggleState(toggle, true);

  ok(
    Services.prefs.getBoolPref(SHOW_SEARCH_PREF),
    "Web search pref is now true"
  );
  ok(toggle.pressed, "Web search toggle is now checked");

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_weather_widget_visibility() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [WIDGETS_SYSTEM_PREF, false],
      [SHOW_WEATHER_SYSTEM_PREF, false],
    ],
  });

  let { win, tab } = await openHomePreferences();

  let weatherWrapper = getSettingControl("weatherStandalone", win);
  ok(
    !weatherWrapper || BrowserTestUtils.isHidden(weatherWrapper),
    "Weather control is hidden when the weather system pref is false"
  );

  BrowserTestUtils.removeTab(tab);

  await SpecialPowers.pushPrefEnv({
    set: [
      [WIDGETS_SYSTEM_PREF, false],
      [SHOW_WEATHER_SYSTEM_PREF, true],
    ],
  });

  ({ win, tab } = await openHomePreferences());

  weatherWrapper = await settingControlRenders("weatherStandalone", win);
  ok(
    weatherWrapper,
    "Weather control exists when the weather system pref is true"
  );
  ok(
    BrowserTestUtils.isVisible(weatherWrapper),
    "Weather control is visible when the weather system pref is true"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_weather_toggle_functionality() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [WIDGETS_SYSTEM_PREF, false],
      [SHOW_WEATHER_SYSTEM_PREF, true],
      [SHOW_WEATHER_PREF, true],
    ],
  });

  let { win, tab } = await openHomePreferences();

  let weatherControl = await settingControlRenders("weatherStandalone", win);
  ok(weatherControl, "Weather control exists");

  let toggle = weatherControl.querySelector("moz-toggle");
  ok(toggle, "Weather toggle element exists");
  ok(toggle.pressed, "Weather toggle is initially checked");

  let prefChanged = waitForPrefChange(SHOW_WEATHER_PREF, false);
  toggle.click();
  await prefChanged;
  await waitForToggleState(toggle, false);

  ok(
    !Services.prefs.getBoolPref(SHOW_WEATHER_PREF),
    "Weather pref is now false"
  );
  ok(!toggle.pressed, "Weather toggle is now unchecked");

  prefChanged = waitForPrefChange(SHOW_WEATHER_PREF, true);
  toggle.click();
  await prefChanged;
  await waitForToggleState(toggle, true);

  ok(Services.prefs.getBoolPref(SHOW_WEATHER_PREF), "Weather pref is now true");
  ok(toggle.pressed, "Weather toggle is now checked");

  BrowserTestUtils.removeTab(tab);
});

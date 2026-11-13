/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const THEME_PREF = "devtools.theme";

add_task(test_themes_listed);
add_task(test_theme_change);
add_task(test_themes_refresh);
add_task(test_unregistered_theme);
add_task(test_theme_listener_cleanup);

async function test_themes_listed() {
  const { root } = await openSettings();
  const control = await getThemeControl(root);
  const values = [
    ...control.querySelectorAll("moz-segmented-control-item"),
  ].map(item => item.value);
  Assert.deepEqual(
    values,
    ["auto", "dark", "light"],
    "Auto is listed before the registered themes"
  );
  await closeTabAndToolbox();
}

async function test_theme_change() {
  await pushPref(THEME_PREF, "light");
  const { root } = await openSettings();
  const control = await getThemeControl(root);
  await control.closest("devtools-settings-pref").updateComplete;
  is(control.value, "light", "The control reads the theme pref");

  control.querySelector("moz-segmented-control-item[value=dark]").click();
  is(
    Services.prefs.getStringPref(THEME_PREF),
    "dark",
    "Clicking a theme writes the theme pref"
  );

  await closeTabAndToolbox();
}

async function test_themes_refresh() {
  const { root } = await openSettings();
  const control = await getThemeControl(root);
  gDevTools.registerTheme({
    id: "test-theme",
    label: "Test Theme",
    stylesheets: [],
    classList: ["theme-test"],
  });
  const item = await waitFor(() =>
    control.querySelector("moz-segmented-control-item[value=test-theme]")
  );
  is(item.label, "Test Theme", "A registered theme is listed with its label");

  await pushPref(THEME_PREF, "test-theme");
  gDevTools.unregisterTheme("test-theme");
  await waitFor(
    () =>
      !control.querySelector("moz-segmented-control-item[value=test-theme]"),
    "Unregistering the selected theme removes it"
  );

  await closeTabAndToolbox();
}

async function test_unregistered_theme() {
  await pushPref(THEME_PREF, "missing-theme");
  const { root } = await openSettings();
  const control = await getThemeControl(root);
  await control.closest("devtools-settings-pref").updateComplete;
  const items = [...control.querySelectorAll("moz-segmented-control-item")];
  ok(
    items.every(item => !item.checked),
    "An unregistered theme selects no theme"
  );
  is(
    Services.prefs.getStringPref(THEME_PREF),
    "missing-theme",
    "Showing no theme doesn't write the theme pref"
  );
  await closeTabAndToolbox();
}

async function test_theme_listener_cleanup() {
  const { root } = await openSettings();
  const theme = root.shadowRoot.querySelector("#settings-theme");
  await closeTabAndToolbox();
  ok(
    !theme.isConnected,
    "Destroying the toolbox disconnects the theme setting"
  );
}

/**
 * Find the theme control once the theme setting has rendered.
 *
 * @param {DevToolsSettings} root The settings panel's root element
 * @return {Promise<MozSegmentedControl>}
 */
async function getThemeControl(root) {
  const theme = root.shadowRoot.querySelector("#settings-theme");
  await theme.updateComplete;
  const control = theme.shadowRoot.querySelector("moz-segmented-control");
  await control.updateComplete;
  return control;
}

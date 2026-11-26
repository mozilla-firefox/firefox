/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(
  async function test_tabs_browsing_sidebar_visible_when_redesign_enabled() {
    let tab = await openPrefsTab("tabsBrowsing");
    let doc = tab.linkedBrowser.contentDocument;

    is_element_visible(
      doc.getElementById("category-tabs-browsing"),
      "Tabs and Browsing category is visible when settings redesign is enabled"
    );

    await BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_tabs_browsing_pane_loads_setting_groups() {
  let tab = await openPrefsTab("tabsBrowsing");
  let doc = tab.linkedBrowser.contentDocument;

  await BrowserTestUtils.waitForMutationCondition(
    doc.getElementById("mainPrefPane"),
    { childList: true, subtree: true },
    () => doc.querySelector('setting-group[groupid="browserLayout"]')
  );

  for (let groupId of [
    "browserLayout",
    "tabs",
    "pageNavigation",
    "keyboardShortcuts",
    "media",
    "performance",
    "recommendations",
  ]) {
    let group = doc.querySelector(`setting-group[groupid="${groupId}"]`);
    ok(group, `${groupId} setting-group exists`);
    is_element_visible(group, `${groupId} setting-group is visible`);
  }

  let firstGroup = doc.querySelector('setting-group[groupid="browserLayout"]');
  ok(
    firstGroup,
    "Tabs and Browsing pane loaded with setting-groups (no registration errors)"
  );

  info("Check keyboardShortcuts group is rendered as expected");
  const keyboardGroup = doc.querySelector(
    `setting-group[groupid="keyboardShortcuts"]`
  );
  const customkeysLink = keyboardGroup.querySelector(
    "#keyboardCustomkeysLinkTabs"
  );
  ok(BrowserTestUtils.isVisible(customkeysLink), "Customkeys link is visible");
  ok(
    customkeysLink,
    "Customkeys link is included in the Keyboard shortcuts group"
  );
  is(
    customkeysLink.href,
    "about:keyboard",
    "Customkeys moz-box-link's href attribute is set to about:keyboard"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_tab_group_drag_to_create_hidden_when_groups_disabled() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.tabs.groups.enabled", true]],
    });

    let tab = await openPrefsTab("tabsBrowsing");
    let doc = tab.linkedBrowser.contentDocument;
    let win = doc.documentGlobal;

    await BrowserTestUtils.waitForMutationCondition(
      doc.getElementById("mainPrefPane"),
      { childList: true, subtree: true },
      () => doc.getElementById("tabGroupDragToCreate")
    );

    const setting = win.Preferences.getSetting("tabGroupDragToCreate");
    const control = doc.getElementById("tabGroupDragToCreate");
    is_element_visible(
      control,
      "Drag to create tab groups setting is visible when tab groups are enabled"
    );

    let settingChanged = waitForSettingChange(setting);
    await SpecialPowers.pushPrefEnv({
      set: [["browser.tabs.groups.enabled", false]],
    });
    await settingChanged;
    await new Promise(r => win.requestAnimationFrame(r));
    is_element_hidden(
      control,
      "Drag to create tab groups setting is hidden when tab groups are disabled"
    );

    settingChanged = waitForSettingChange(setting);
    await SpecialPowers.pushPrefEnv({
      set: [["browser.tabs.groups.enabled", true]],
    });
    await settingChanged;
    await new Promise(r => win.requestAnimationFrame(r));
    is_element_visible(
      control,
      "Drag to create tab groups setting is visible again when tab groups are re-enabled"
    );

    await BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_tabs_sub_fieldsets_have_heading_level() {
  let tab = await openPrefsTab("tabsBrowsing");
  let doc = tab.linkedBrowser.contentDocument;

  await BrowserTestUtils.waitForMutationCondition(
    doc.getElementById("mainPrefPane"),
    { childList: true, subtree: true },
    () => doc.getElementById("setting-control-tabsOpening")
  );

  for (let id of [
    "tabsOpening",
    "tabsInteraction",
    "browserContainersbox",
    "tabsClosing",
  ]) {
    let fieldset = doc.querySelector(`moz-fieldset#${id}`);
    ok(fieldset, `${id} moz-fieldset is present`);
    await fieldset.updateComplete;
    is(
      fieldset.headingLevel,
      4,
      `${id} moz-fieldset renders its legend as an h4`
    );
    ok(
      fieldset.shadowRoot.querySelector("h4"),
      `${id} legend is rendered as an <h4> in the shadow DOM`
    );
  }

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_tabs_browsing_pane_click_sidebar() {
  let tab = await openPrefsTab("");
  let doc = tab.linkedBrowser.contentDocument;

  let navButton = doc.getElementById("category-tabs-browsing");
  await navButton.updateComplete;

  let paneLoaded = waitForPaneChange("tabsBrowsing");
  synthesizeClick(navButton);
  await paneLoaded;

  await BrowserTestUtils.waitForMutationCondition(
    doc.getElementById("mainPrefPane"),
    { childList: true, subtree: true },
    () => doc.querySelector('setting-group[groupid="browserLayout"]')
  );
  ok(
    doc.querySelector('setting-group[groupid="browserLayout"]'),
    "Browser layout setting-group is present after clicking Tabs and Browsing nav button"
  );

  await BrowserTestUtils.removeTab(tab);
});

async function check_CloseMultiplePBMode_visible(visible) {
  // pref should be visible by default
  let tab = await openPrefsTab("tabsBrowsing");
  let win = tab.linkedBrowser.contentWindow;
  const control = await settingControlRenders("warnCloseMultiplePBMode", win);
  const setting = win.Preferences.getSetting("warnCloseMultiplePBMode");
  is(setting.visible, visible, "variant is shown outside permanent PBM");
  is(!control.hidden, visible, "variant control is visible in the UI");
  await BrowserTestUtils.removeTab(tab);
}

add_task(async function test_warnCloseMultiplePBMode_shown() {
  await check_CloseMultiplePBMode_visible(true);
});

add_task(async function test_warnCloseMultiplePBMode_hiddenInPermaPBM() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.autostart", true]],
  });
  await check_CloseMultiplePBMode_visible(false);
  await SpecialPowers.popPrefEnv();
});

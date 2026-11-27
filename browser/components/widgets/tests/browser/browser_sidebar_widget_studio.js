/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const ENABLED_PREF = "browser.widgets.generated.enabled";
const ORDER_PREF = "browser.widgets.studio.order";

add_setup(mockAuthentication);

function getToolButton(win = window) {
  return [...win.document.querySelector("sidebar-main").toolButtons].find(
    button => button.getAttribute("view") == WIDGETS_VIEW
  );
}

add_task(async function test_tool_follows_pref() {
  SidebarTestUtils.restoreStateAtCleanup(window);
  await SidebarTestUtils.ensureLauncherVisible(window);
  await TestUtils.waitForCondition(() => getToolButton(), "Tool shown");
  let menuItem = document.getElementById("menu_widgetsSidebar");
  ok(!menuItem.hidden, "Menu item shown");

  await SpecialPowers.pushPrefEnv({
    set: [[ENABLED_PREF, false]],
  });
  await TestUtils.waitForCondition(() => !getToolButton(), "Tool hidden");
  ok(menuItem.hidden, "Menu item hidden");

  await SpecialPowers.popPrefEnv();
  await TestUtils.waitForCondition(() => getToolButton(), "Tool shown again");
  ok(!menuItem.hidden, "Menu item shown again");
});

add_task(async function test_nimbus_enrolment_toggles_tool() {
  await SidebarTestUtils.ensureLauncherVisible(window);

  await SpecialPowers.pushPrefEnv({
    clear: [[ENABLED_PREF]],
  });
  await TestUtils.waitForCondition(
    () => !getToolButton(),
    "Tool hidden before enrolling"
  );

  let cleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "generatedWidgets",
    value: { enabled: true },
  });
  ok(Services.prefs.getBoolPref(ENABLED_PREF), "Nimbus enables the tool");
  ok(
    Services.prefs.prefHasUserValue(ENABLED_PREF),
    "Nimbus sets the user pref"
  );
  await TestUtils.waitForCondition(
    () => getToolButton(),
    "Tool shown after enrolling"
  );
  await cleanup();
  ok(
    !Services.prefs.prefHasUserValue(ENABLED_PREF),
    "Nimbus clears the user pref"
  );
  ok(!Services.prefs.getBoolPref(ENABLED_PREF), "Tool pref restored to false");
  await TestUtils.waitForCondition(
    () => !getToolButton(),
    "Tool hidden after unenrolling"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_close_button_closes_panel() {
  let panel = await openWidgetsSidebar();
  ok(BrowserTestUtils.isVisible(SidebarController.panelHeader), "Header shown");

  await panel.updateComplete;
  SidebarController.panelHeader.closeButton.click();
  await TestUtils.waitForCondition(
    () => document.getElementById("sidebar-box").hidden,
    "Panel closed"
  );
});

async function openMenuByKeyboard(panel) {
  let shown = BrowserTestUtils.waitForEvent(panel.menu, "shown");
  panel.menuButton.focus();
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  await shown;
}

async function closeMenu(panel) {
  let hidden = BrowserTestUtils.waitForEvent(panel.menu, "hidden");
  EventUtils.synthesizeKey("KEY_Escape", {}, window);
  await hidden;
}

add_task(async function test_menu_button_position_and_name() {
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  let panelHeader = SidebarController.panelHeader;
  await panelHeader.updateComplete;
  await panel.menuButton.updateComplete;
  is(panel.menuButton.parentNode, panelHeader, "Menu button is in the header");
  is(panel.menuButton.slot, "actions", "Menu button uses the actions slot");
  let menuRect = panel.menuButton.getBoundingClientRect();
  let closeRect = panelHeader.closeButton.getBoundingClientRect();
  let gap = parseFloat(getComputedStyle(panel.menuButton).marginInlineEnd);
  Assert.lessOrEqual(
    Math.abs(closeRect.left - menuRect.right - gap),
    1,
    "Menu button is one end margin before the close button"
  );
  await document.l10n.translateFragment(panel.menuButton);
  await panel.menuButton.updateComplete;
  ok(panel.menuButton.buttonEl.title, "Menu button has a localized name");
  is(
    panel.menuButton.buttonEl.getAttribute("aria-haspopup"),
    "menu",
    "Menu button exposes its menu"
  );
  panel.menuButton.focus();
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  is(
    panelHeader.shadowRoot.activeElement,
    panelHeader.closeButton,
    "Tab moves from the menu button to the close button"
  );
  SidebarController.hide();
});

add_task(async function test_menu_items_open_links() {
  for (let signedIn of [false, true]) {
    setSignedIn(signedIn);
    let panel = await openWidgetsSidebar();
    await panel.updateComplete;
    await panel.menuButton.updateComplete;
    let openLink = sinon.stub(panel.topWindow, "openTrustedLinkIn");
    try {
      await openMenuByKeyboard(panel);
      let items = panel.menu.querySelectorAll("panel-item");
      is(items.length, 2, "Menu has two items");

      let hidden = BrowserTestUtils.waitForEvent(panel.menu, "hidden");
      items[0].click();
      await hidden;
      ok(
        openLink.calledOnceWithExactly("https://connect.mozilla.org/", "tab"),
        "Share Feedback opens Mozilla Connect in a tab"
      );
      openLink.resetHistory();

      await openMenuByKeyboard(panel);
      hidden = BrowserTestUtils.waitForEvent(panel.menu, "hidden");
      items[1].focus();
      EventUtils.synthesizeKey("KEY_Enter", {}, window);
      await hidden;
      ok(
        openLink.calledOnceWithExactly(
          Services.urlFormatter.formatURLPref("app.support.baseURL") +
            "firefox-custom-widgets",
          "tab"
        ),
        "About Custom Widgets opens the support page in a tab"
      );
    } finally {
      openLink.restore();
      SidebarController.hide();
    }
  }
});

add_task(async function test_menu_escape_restores_focus() {
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  await panel.menuButton.updateComplete;
  await openMenuByKeyboard(panel);
  await closeMenu(panel);
  is(
    window.document.activeElement,
    panel.menuButton,
    "Escape restores focus to the menu button"
  );
  SidebarController.hide();
});

add_task(async function test_header_actions_cleanup() {
  let panelHeader = SidebarController.panelHeader;
  try {
    for (let attempt = 0; attempt < 2; attempt++) {
      await openWidgetsSidebar();
      is(
        panelHeader.querySelectorAll(':scope > moz-button[slot="actions"]')
          .length,
        1,
        "Header has exactly one menu button"
      );
      is(
        panelHeader.querySelectorAll(':scope > panel-list[slot="actions"]')
          .length,
        1,
        "Header has exactly one menu"
      );
      if (attempt == 0) {
        SidebarController.hide();
        await TestUtils.waitForCondition(
          () => !panelHeader.querySelector(':scope > [slot="actions"]'),
          "Hiding Widget Studio removes its header actions"
        );
      }
    }

    await SidebarTestUtils.showPanel(window, "viewBookmarksSidebar");
    is(
      panelHeader.querySelectorAll(':scope > [slot="actions"]').length,
      0,
      "Switching panels removes Widget Studio's header actions"
    );
  } finally {
    SidebarController.hide();
  }
});

add_task(async function test_header_menu_removed_while_open() {
  let panelHeader = SidebarController.panelHeader;
  for (let pendingOpen of [false, true]) {
    let panel = await openWidgetsSidebar();
    await panel.menuButton.updateComplete;
    let alignment = Promise.withResolvers();
    let setAlign;
    try {
      if (pendingOpen) {
        setAlign = sinon
          .stub(panel.menu, "setAlign")
          .returns(alignment.promise);
        panel.menuButton.focus();
        EventUtils.synthesizeKey("KEY_Enter", {}, window);
        await TestUtils.waitForCondition(
          () => setAlign.calledOnce,
          "Menu is waiting for alignment"
        );
      } else {
        await openMenuByKeyboard(panel);
      }

      await SidebarTestUtils.showPanel(window, "viewBookmarksSidebar");
      alignment.resolve();
      await TestUtils.waitForTick();

      is(
        panelHeader.querySelectorAll(':scope > [slot="actions"]').length,
        0,
        `Switching panels removes the ${pendingOpen ? "opening" : "open"} menu and button`
      );
      for (let key of ["Tab", "ArrowDown"]) {
        let event = new KeyboardEvent("keydown", {
          key,
          bubbles: true,
          cancelable: true,
        });
        document.dispatchEvent(event);
        ok(
          !event.defaultPrevented,
          `${key} is not intercepted after removing the ${pendingOpen ? "opening" : "open"} menu`
        );
      }
    } finally {
      alignment.resolve();
      setAlign?.restore();
      SidebarController.hide();
    }
  }
});

add_task(async function test_hide_launcher_header() {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.visibility", "hide-launcher"]],
  });
  try {
    setSignedIn(false);
    let panel = await openWidgetsSidebar();
    let panelHeader = SidebarController.panelHeader;
    await panelHeader.updateComplete;
    is(
      panel.menuButton.parentNode,
      panelHeader,
      "Menu button is in the header"
    );
    is(panel.menuButton.slot, "actions", "Menu button uses the actions slot");
    ok(
      !panel.shadowRoot.querySelector("#close-button, #header-buttons"),
      "Page has no floating header buttons"
    );
    ok(panelHeader.heading, "The shared header has a title");
  } finally {
    SidebarController.hide();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_composer_needs_authentication() {
  setSignedIn(false);
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  ok(!panel.composer, "Composer hidden");

  setSignedIn(true);
  await panel.updateComplete;
  ok(panel.composer, "Composer shown");

  setSignedIn(false);
  await panel.updateComplete;
  ok(!panel.composer, "Composer hidden again");

  panel.composing = false;
  setSignedIn(true);
  await panel.updateComplete;
  ok(panel.composeButton, "Compose button shown");

  setSignedIn(false);
  await panel.updateComplete;
  ok(!panel.composeButton, "Compose button hidden");
  SidebarController.hide();
});

add_task(async function test_composer_shows_disclaimer() {
  setSignedIn(true);
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  let { composer } = panel;
  await composer.updateComplete;
  let disclaimer = composer.shadowRoot.querySelector(".disclaimer");
  ok(disclaimer, "Disclaimer shown under the prompt");
  await panel.ownerDocument.l10n.translateFragment(disclaimer);
  is(
    disclaimer.textContent,
    "Widgets are built with AI. Check that yours works as expected.",
    "The disclaimer is translated"
  );
  is(
    composer.shadowRoot.querySelector(".field").nextElementSibling,
    disclaimer,
    "The disclaimer follows the prompt field"
  );
  is(
    composer.shadowRoot
      .querySelector("textarea")
      .getAttribute("aria-describedby"),
    disclaimer.id,
    "The prompt is described by the disclaimer"
  );
  SidebarController.hide();
});

add_task(async function test_panel_composer() {
  setSignedIn(true);
  let panel = await openWidgetsSidebar();
  await panel.updateComplete;
  let { composer } = panel;
  ok(composer, "Composer shown");
  await TestUtils.waitForCondition(
    () => composer.shadowRoot.activeElement?.localName == "textarea",
    "Composer focused"
  );

  EventUtils.synthesizeKey("KEY_Escape", {}, panel.documentGlobal);
  await panel.updateComplete;
  ok(!panel.composer, "Escape hid composer");
  await TestUtils.waitForCondition(
    () => panel.shadowRoot.activeElement == panel.composeButton,
    "Compose button focused"
  );

  SidebarController.panelHeader.closeButton.focus();
  panel.handleSidebarFocusedEvent();
  is(
    panel.shadowRoot.activeElement,
    panel.composeButton,
    "Sidebar focus moved to compose button"
  );

  panel.composeButton.click();
  await panel.updateComplete;
  ({ composer } = panel);
  ok(composer, "Compose button showed composer");
  await TestUtils.waitForCondition(
    () => composer.shadowRoot.activeElement?.localName == "textarea",
    "Composer focused again"
  );
  SidebarController.hide();
});

add_task(async function test_generated_widgets_listed() {
  let { WidgetManager } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs"
  );
  let { GeneratedWidgetsProvider } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs"
  );
  registerCleanupFunction(() => Services.prefs.clearUserPref(ORDER_PREF));
  let panel = await openWidgetsSidebar();
  let a = await installGeneratedWidget({ name: "Widget A" });
  let b = await installGeneratedWidget({ name: "Widget B" });
  let { widgets } = panel;
  let ids = list =>
    list.map(widget => widget.id).filter(id => id == a || id == b);
  await TestUtils.waitForCondition(
    () => ids(widgets.all).length == 2,
    "Widgets listed"
  );
  Assert.deepEqual(ids(widgets.all), [a, b], "Install order");
  Assert.deepEqual(widgets.order.slice(-2), [a, b], "Order saved");
  Assert.deepEqual(ids(widgets.disabled), [a, b], "Disabled");

  widgets.order = [b, a];
  Assert.deepEqual(ids(widgets.all), [b, a], "Saved order");

  WidgetManager.getWidgetByID(b).enabled = true;
  await TestUtils.waitForCondition(
    () => ids(widgets.enabled).length == 1,
    "Widget enabled"
  );
  Assert.deepEqual(ids(widgets.enabled), [b], "Enabled");
  Assert.deepEqual(ids(widgets.disabled), [a], "Disabled");

  SidebarController.hide();
  await GeneratedWidgetsProvider.uninstall(a);
  Assert.ok(widgets.order.includes(a), "Kept while closed");
  ({ widgets } = await openWidgetsSidebar());
  await TestUtils.waitForCondition(
    () => !widgets.order.includes(a),
    "Uninstalled widget unsaved"
  );
  Assert.deepEqual(ids(widgets.all), [b], "Uninstalled widget removed");
  SidebarController.hide();
});

add_task(async function test_generated_widgets_drawn() {
  let { GeneratedWidgetsProvider } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs"
  );
  registerCleanupFunction(() => Services.prefs.clearUserPref(ORDER_PREF));
  let panel = await openWidgetsSidebar();
  let a = await installGeneratedWidget({ name: "Widget A" });
  let b = await installGeneratedWidget({ name: "Widget B" });
  let cards = () =>
    [...panel.querySelectorAll(":scope > moz-widget")].filter(
      card => card.widgetId == a || card.widgetId == b
    );
  let ids = () => cards().map(card => card.widgetId);
  await TestUtils.waitForCondition(() => cards().length == 2, "Cards drawn");
  Assert.deepEqual(ids(), [a, b], "Card order");

  for (let card of cards()) {
    let url = WebExtensionPolicy.getByID(card.widgetId).getURL("widget.html");
    await TestUtils.waitForCondition(
      () =>
        card.frame?.browsingContext?.currentWindowGlobal?.documentURI?.spec ==
        url,
      "Widget page loaded"
    );
    is(card.frame.remoteType, "extension", "Extension remote type");
  }

  panel.widgets.order = panel.widgets.order.toReversed();
  await TestUtils.waitForCondition(() => ids()[0] == b, "Cards reordered");
  Assert.deepEqual(ids(), [b, a], "Reversed order");

  await GeneratedWidgetsProvider.uninstall(a);
  await TestUtils.waitForCondition(() => cards().length == 1, "Card removed");
  Assert.deepEqual(ids(), [b], "Remaining card");
  SidebarController.hide();
});

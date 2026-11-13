/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests for horizontal-tabs "hide-launcher" mode. In this mode the launcher is
// hidden and each panel's header (the sidebar-panel-header Lit element) renders
// a dropdown switcher in place of the title. The toolbar button toggles the
// panel (rather than the launcher) and preserves the last opened panel.
async function getPanelHeader() {
  const header = SidebarController.panelHeader;
  await header.updateComplete;
  return header;
}

async function getPanelSwitcher() {
  const header = await getPanelHeader();
  const switcher = header.shadowRoot.querySelector("sidebar-panel-switcher");
  await switcher.updateComplete;
  return switcher;
}

async function loadSidebarExtension(title) {
  const extension = ExtensionTestUtils.loadExtension({
    ...extData,
    manifest: {
      ...extData.manifest,
      sidebar_action: {
        ...extData.manifest.sidebar_action,
        default_title: title,
      },
    },
  });
  await extension.startup();
  await extension.awaitMessage("sidebar");
  await SidebarController.waitUntilStable();
  return extension;
}

// The command ID of an extension panel is derived from the extension ID, so
// look it up rather than deriving it here.
function getExtensionView(extension) {
  return SidebarController.getExtensions().find(
    ext => ext.extensionId === extension.id
  )?.view;
}

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.animation.enabled", false],
      ["sidebar.main.tools", "syncedtabs,history,bookmarks"],
      ["sidebar.updatedBookmarks.enabled", true],
      [VERTICAL_TABS_PREF, false],
    ],
  });
  await SidebarTestUtils.waitForTabstripOrientation(window, "horizontal");
  await SpecialPowers.pushPrefEnv({
    set: [[SIDEBAR_VISIBILITY_PREF, "hide-launcher"]],
  });
  await SidebarController.waitUntilStable();
});

add_task(async function test_launcher_hidden_and_panel_shown() {
  Assert.ok(
    SidebarController.sidebarContainer.hidden,
    "Launcher is hidden in horizontal tabs hide-launcher mode"
  );

  info("Open a panel.");
  await SidebarController.show("viewHistorySidebar");
  await SidebarController.waitUntilStable();

  Assert.ok(SidebarController.isOpen, "Panel is open");
  Assert.ok(
    SidebarController.sidebarContainer.hidden,
    "Launcher stays hidden while the panel is open"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(document.getElementById("sidebar-box")),
    "Panel box is visible"
  );

  info("The panel header shows the switcher dropdown in place of the title.");
  const header = await getPanelHeader();
  const switcher = header.shadowRoot.querySelector("sidebar-panel-switcher");
  const title = header.shadowRoot.querySelector(".sidebar-panel-title");
  Assert.ok(BrowserTestUtils.isVisible(switcher), "Switcher is visible");
  Assert.ok(BrowserTestUtils.isHidden(title), "Plain panel title is hidden");

  SidebarController.hide();
  await SidebarController.waitUntilStable();
});

add_task(async function test_panel_scrolls_from_window_edge() {
  await SpecialPowers.pushPrefEnv({
    set: [["general.smoothScroll", false]],
  });
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insertMany(
    Array.from({ length: 50 }, (_, i) => ({
      url: `https://example.com/${i}`,
      title: `Example ${i}`,
      visits: [{ date: new Date() }],
    }))
  );
  registerCleanupFunction(() => PlacesUtils.history.clear());

  for (const positionStart of [true, false]) {
    info(`Sidebar at the ${positionStart ? "start" : "end"}`);
    await SpecialPowers.pushPrefEnv({
      set: [[POSITION_SETTING_PREF, positionStart]],
    });
    await SidebarController.waitUntilStable();

    const { component } = await showHistorySidebar();
    const scroller = component.shadowRoot.querySelector(
      ".sidebar-panel-scrollable-content"
    );
    await TestUtils.waitForCondition(
      () => scroller.scrollHeight > scroller.clientHeight,
      "History panel overflows"
    );
    scroller.scrollTop = 0;

    const boxRect = document
      .getElementById("sidebar-box")
      .getBoundingClientRect();
    const browserRect = SidebarController.browser.getBoundingClientRect();
    const scrollerRect = scroller.getBoundingClientRect();
    const x = positionStart ? boxRect.left + 1 : boxRect.right - 1;
    const y = browserRect.top + scrollerRect.top + scrollerRect.height / 2;

    info(`Wheel at the panel's window-edge pixel (${x}, ${y})`);
    EventUtils.synthesizeWheelAtPoint(
      x,
      y,
      { deltaMode: WheelEvent.DOM_DELTA_LINE, deltaY: 3 },
      window
    );
    await TestUtils.waitForCondition(
      () => scroller.scrollTop > 0,
      "History panel scrolls from its window edge"
    );

    SidebarController.hide();
    await SidebarController.waitUntilStable();
    await SpecialPowers.popPrefEnv();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_toolbar_button_toggles_panel() {
  const { sidebarContainer, toolbarButton } = SidebarController;

  const toggleAndWait = async expectedOpen => {
    // The button command is dispatched without being awaited, so call the
    // handler directly to await the full toggle.
    await SidebarController.handleToolbarButtonClick();
    await SidebarController.waitUntilStable();
    Assert.equal(
      SidebarController.isOpen,
      expectedOpen,
      `Panel is ${expectedOpen ? "open" : "closed"} after toggling the button.`
    );
    Assert.equal(
      toolbarButton.checked,
      expectedOpen,
      `Toolbar button is ${expectedOpen ? "checked" : "unchecked"}.`
    );
    Assert.deepEqual(
      document.l10n.getAttributes(toolbarButton),
      {
        id: expectedOpen
          ? "sidebar-widget-hide-sidebar2"
          : "sidebar-widget-show-sidebar2",
        args:
          AppConstants.platform === "macosx"
            ? { shortcut: "⌃Z" }
            : { shortcut: "Alt+Ctrl+Z" },
      },
      "Toolbar button has the correct tooltip."
    );
    Assert.ok(sidebarContainer.hidden, "Launcher stays hidden");
  };

  Assert.ok(!SidebarController.isOpen, "No panel is open initially");

  info("Open the panel using the toolbar button.");
  await toggleAndWait(true);

  info("Close the panel using the toolbar button.");
  await toggleAndWait(false);
});

add_task(async function test_keyboard_shortcut_preserves_state() {
  info("Open the bookmarks panel with the toolbar button.");
  await SidebarController.show("viewBookmarksSidebar");
  await SidebarController.waitUntilStable();
  Assert.ok(SidebarController.isOpen, "Panel is open");

  info("Toggling the same panel (as a keyboard shortcut does) closes it.");
  await SidebarController.toggle("viewBookmarksSidebar");
  await SidebarController.waitUntilStable();
  Assert.ok(!SidebarController.isOpen, "Panel is closed");
  Assert.ok(
    SidebarController.sidebarContainer.hidden,
    "Launcher stays hidden after the keyboard shortcut closes the panel"
  );

  info("Toggling again re-opens the preserved panel.");
  await SidebarController.toggle("viewBookmarksSidebar");
  await SidebarController.waitUntilStable();
  Assert.equal(
    SidebarController.currentID,
    "viewBookmarksSidebar",
    "The previously opened panel was preserved"
  );

  SidebarController.hide();
  await SidebarController.waitUntilStable();
});

add_task(async function test_switcher_dropdown_switches_panel() {
  info("Open the history panel.");
  await SidebarController.show("viewHistorySidebar");
  await SidebarController.waitUntilStable();

  const switcher = await getPanelSwitcher();
  const switcherButton = switcher.shadowRoot.querySelector(".switcher-button");
  const panelList = switcher.shadowRoot.querySelector("panel-list");

  info("Open the panel switcher dropdown.");
  await SimpleTest.promiseFocus(window);
  const shown = BrowserTestUtils.waitForEvent(panelList, "shown");
  switcherButton.click();
  await shown;
  await switcher.updateComplete;

  const items = [...switcher.shadowRoot.querySelectorAll("panel-item")];
  Assert.greater(items.length, 1, "Switcher lists multiple panels");

  const checkedItem = items.find(item => item.hasAttribute("checked"));
  Assert.equal(
    checkedItem?.textContent.trim(),
    switcher.label,
    "The current panel is checked in the switcher"
  );

  const bookmarksItem = await getPanelItemForView(
    items,
    "viewBookmarksSidebar"
  );
  Assert.ok(bookmarksItem, "Bookmarks panel is listed in the switcher");

  info("Switch to the bookmarks panel from the dropdown.");
  bookmarksItem.click();
  await TestUtils.waitForCondition(
    () => SidebarController.currentID === "viewBookmarksSidebar",
    "Waiting for the bookmarks panel to open"
  );

  Assert.equal(
    SidebarController.currentID,
    "viewBookmarksSidebar",
    "Switcher switched to the bookmarks panel"
  );
  Assert.ok(
    SidebarController.sidebarContainer.hidden,
    "Launcher stays hidden after switching panels"
  );

  SidebarController.hide();
  await SidebarController.waitUntilStable();
});

async function getPanelItemForView(items, view) {
  const switcherItems = await SidebarController.getRevampSwitcherItems();
  const target = switcherItems.find(item => item.view === view);
  return items.find(item => item.textContent.trim() === target?.label);
}

add_task(async function test_switcher_label_for_tool_missing_from_pref() {
  // A panel can be opened without being one of the enabled tools, e.g. with the
  // keyboard shortcut or the View menu and we don't want to show an empty string.
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.main.tools", "syncedtabs,history"]],
  });

  info("Open the bookmarks panel while bookmarks isn't an enabled tool.");
  await SidebarController.show("viewBookmarksSidebar");
  await SidebarController.waitUntilStable();

  const switcher = await getPanelSwitcher();
  const labelEl = switcher.shadowRoot.querySelector(".switcher-label");
  await BrowserTestUtils.waitForMutationCondition(
    labelEl,
    { childList: true, characterData: true, subtree: true },
    () => !!switcher.label
  );

  const [message] = await document.l10n.formatMessages([
    { id: "sidebar-menu-bookmarks-label" },
  ]);
  const expectedLabel = message.attributes.find(a => a.name === "label").value;
  Assert.equal(
    switcher.label,
    expectedLabel,
    "Switcher is labelled with the bookmarks panel name"
  );
  Assert.equal(
    labelEl.textContent.trim(),
    expectedLabel,
    "Switcher button renders the bookmarks panel name"
  );

  SidebarController.hide();
  await SidebarController.waitUntilStable();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_switcher_label_for_extension_panel() {
  info("Install an extension with a sidebar panel, which opens its panel.");
  const firstExtension = await loadSidebarExtension("First Extension");
  Assert.equal(
    SidebarController.currentID,
    getExtensionView(firstExtension),
    "The extension panel is open"
  );

  const switcher = await getPanelSwitcher();
  const labelEl = switcher.shadowRoot.querySelector(".switcher-label");
  await BrowserTestUtils.waitForMutationCondition(
    labelEl,
    { childList: true, characterData: true, subtree: true },
    () => !!switcher.label
  );
  Assert.equal(
    switcher.label,
    "First Extension",
    "Switcher is labelled with the extension name"
  );
  Assert.equal(
    labelEl.textContent.trim(),
    "First Extension",
    "Switcher button renders the extension name"
  );

  info("Open the panel switcher dropdown.");
  const switcherButton = switcher.shadowRoot.querySelector(".switcher-button");
  const panelList = switcher.shadowRoot.querySelector("panel-list");
  await SimpleTest.promiseFocus(window);
  const shown = BrowserTestUtils.waitForEvent(panelList, "shown");
  switcherButton.click();
  await shown;
  await switcher.updateComplete;

  const items = [...switcher.shadowRoot.querySelectorAll("panel-item")];
  const checkedItem = items.find(item => item.hasAttribute("checked"));
  Assert.equal(
    checkedItem?.textContent.trim(),
    "First Extension",
    "The extension panel is checked in the switcher"
  );
  panelList.hide();

  info("Install a second extension, which replaces the open panel.");
  const secondExtension = await loadSidebarExtension("Second Extension");
  Assert.equal(
    SidebarController.currentID,
    getExtensionView(secondExtension),
    "The second extension panel is open"
  );
  Assert.equal(
    await getPanelSwitcher(),
    switcher,
    "Extension panels share the same switcher element"
  );
  await BrowserTestUtils.waitForMutationCondition(
    labelEl,
    { childList: true, characterData: true, subtree: true },
    () => switcher.label == "Second Extension"
  );
  Assert.equal(
    labelEl.textContent.trim(),
    "Second Extension",
    "Switcher button renders the newly opened extension's name"
  );

  await secondExtension.unload();
  await firstExtension.unload();
  await SidebarController.waitUntilStable();
  SidebarTestUtils.closePanel(window);
  await SidebarController.waitUntilStable();
});

add_task(async function test_close_button_preserves_panel() {
  info("Open the bookmarks panel.");
  await SidebarController.show("viewBookmarksSidebar");
  await SidebarController.waitUntilStable();
  Assert.ok(SidebarController.isOpen, "Panel is open");

  info("Close the panel using the header close button.");
  const header = await getPanelHeader();
  header.closeButton.click();
  await SidebarController.waitUntilStable();
  Assert.ok(!SidebarController.isOpen, "Panel is closed via the close button");
  Assert.ok(
    SidebarController.sidebarContainer.hidden,
    "Launcher stays hidden after closing the panel"
  );

  info("The last panel is preserved: the toolbar button re-opens it.");
  document.getElementById("sidebar-button").doCommand();
  await SidebarController.waitUntilStable();
  Assert.ok(
    SidebarController.isOpen,
    "Panel re-opened with the toolbar button"
  );
  Assert.equal(
    SidebarController.currentID,
    "viewBookmarksSidebar",
    "The previously opened panel was preserved"
  );

  SidebarController.hide();
  await SidebarController.waitUntilStable();
});

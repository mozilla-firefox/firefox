/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

gReduceMotionOverride = true;

const AREAS = [
  "keyboard",
  "menuBar",
  "tabsBar",
  "navBar",
  "bookmarksBar",
  "appMenu",
  "tabsContext",
  "contentContext",
  "overflowMenu",
  "pinnedOverflowMenu",
  "pageactionUrlbar",
  "pageactionPanel",

  "preferencesPaneHome",
  "preferencesPaneGeneral",
  "preferencesPanePrivacy",
  "preferencesPaneSearch",
  "preferencesPaneSearchResults",
  "preferencesPaneSync",
  "preferencesPaneContainers",
  "preferencesPaneAbout",
  "preferencesPaneAccessibility",
  "preferencesPaneDownloads",
  "preferencesPaneAppearance",
  "preferencesPanePermissionsData",
  "preferencesPaneTabsBrowsing",
];

function resetGleanEvents() {
  Services.fog.testResetFOG();
  GleanPings.prototypeNoCodeEvents.setEnabled(true);
}

// Checks that the correct number of clicks are registered against the correct
// labels in the labeled_counters. Also runs checks against non-area types
// passed in through expectedOther.
function assertInteractionData(expectedAreas, expectedOther = {}) {
  let compareSourceWithExpectations = (source, expected = {}) => {
    const data = Glean.browserUiInteraction[source].testGetValue() ?? {};

    let expectedKeys = new Set(Object.keys(data).concat(Object.keys(expected)));

    for (let key of expectedKeys) {
      Assert.equal(
        data[key],
        expected[key],
        `Expected to see the correct value for ${key} in ${source}.`
      );
    }
  };

  for (let source of AREAS) {
    compareSourceWithExpectations(source, expectedAreas[source]);
  }

  for (let source in expectedOther) {
    compareSourceWithExpectations(source, expectedOther[source]);
  }
  resetGleanEvents();
}

const elem = id => document.getElementById(id);
const click = el => {
  if (typeof el == "string") {
    el = elem(el);
  }

  EventUtils.synthesizeMouseAtCenter(el, {}, window);
};

add_task(async function toolbarButtons() {
  info("Adding a bookmark to the bookmarks toolbar.");
  let addedBookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Test",
    url: "https://example.com",
  });

  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.remove(addedBookmark);
  });

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    let customButton = await new Promise(resolve => {
      CustomizableUI.createWidget({
        // In CSS identifiers cannot start with a number but CustomizableUI accepts that.
        id: "12foo",
        label: "12foo",
        onCreated: resolve,
        defaultArea: "nav-bar",
      });
    });

    resetGleanEvents();
    // We want to record events into this ping, so it has to be enabled.
    GleanPings.prototypeNoCodeEvents.setEnabled(true);

    let newTab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
    let tabClose = BrowserTestUtils.waitForTabClosing(newTab);

    let tabs = elem("tabbrowser-tabs");
    if (!tabs.overflowing) {
      tabs.setAttribute("overflow", "true");
      registerCleanupFunction(() => {
        tabs.removeAttribute("overflow");
      });
    }

    // We intentionally turn off a11y_checks for these click events, because the
    // test is checking the telemetry functionality and the following 3 clicks
    // are targeting disabled controls to test the changes in scalars (for more
    // refer to the bug 1864576 comment 2 and bug 1854999 comment 4):
    AccessibilityUtils.setEnv({
      mustBeEnabled: false,
    });
    click("stop-reload-button");
    click("back-button");
    click("back-button");
    AccessibilityUtils.resetEnv();

    // Make sure the all tabs panel is in the document.
    gTabsPanel.initElements();
    let view = elem("allTabsMenu-allTabsView");
    let shown = BrowserTestUtils.waitForEvent(view, "ViewShown");
    click("alltabs-button");
    await shown;

    let hidden = BrowserTestUtils.waitForEvent(view, "ViewHiding");
    gTabsPanel.hideAllTabsPanel();
    await hidden;

    click(newTab.querySelector(".tab-close-button"));
    await tabClose;

    let bookmarksToolbar = gNavToolbox.querySelector("#PersonalToolbar");

    let bookmarksToolbarReady = BrowserTestUtils.waitForMutationCondition(
      bookmarksToolbar,
      { attributes: true },
      () => {
        return (
          bookmarksToolbar.getAttribute("collapsed") != "true" &&
          bookmarksToolbar.hasAttribute("initialized")
        );
      }
    );

    window.setToolbarVisibility(
      bookmarksToolbar,
      true /* isVisible */,
      false /* persist */,
      false /* animated */
    );
    registerCleanupFunction(() => {
      window.setToolbarVisibility(
        bookmarksToolbar,
        false /* isVisible */,
        false /* persist */,
        false /* animated */
      );
    });
    await bookmarksToolbarReady;

    // The Bookmarks Toolbar does some optimizations to try not to jank the
    // browser when populating itself, and does so asynchronously. We wait
    // until a bookmark item is available in the DOM before continuing.
    let placesToolbarItems = document.getElementById("PlacesToolbarItems");
    await BrowserTestUtils.waitForMutationCondition(
      placesToolbarItems,
      { childList: true },
      () => placesToolbarItems.querySelector(".bookmark-item") != null
    );

    click(placesToolbarItems.querySelector(".bookmark-item"));

    click(customButton);

    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "stop-reload-button"],
        ["nav-bar", "back-button"],
        ["nav-bar", "back-button"],
        ["all-tabs-panel-entrypoint", "alltabs-button"],
        ["tabs-bar", "alltabs-button"],
        ["tabs-bar", "tab-close-button"],
        ["bookmarks-bar", "bookmark-item"],
        ["nav-bar", "12foo"],
      ],
      events
    );
    assertInteractionData(
      {
        navBar: {
          "stop-reload-button": 1,
          "back-button": 2,
          "12foo": 1,
        },
        tabsBar: {
          "alltabs-button": 1,
          "tab-close-button": 1,
        },
        bookmarksBar: {
          "bookmark-item": 1,
        },
      },
      {
        allTabsPanelEntrypoint: {
          "alltabs-button": 1,
        },
      }
    );
    CustomizableUI.destroyWidget("12foo");
  });
});

add_task(async function contextMenu() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    resetGleanEvents();

    let tab = gBrowser.getTabForBrowser(browser);
    let context = elem("tabContextMenu");
    let shown = BrowserTestUtils.waitForEvent(context, "popupshown");
    EventUtils.synthesizeMouseAtCenter(
      tab,
      { type: "contextmenu", button: 2 },
      window
    );
    await shown;

    let hidden = BrowserTestUtils.waitForEvent(context, "popuphidden");
    context.activateItem(document.getElementById("context_toggleMuteTab"));
    await hidden;

    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);

    Assert.deepEqual(
      [
        ["tabs-context", "context-toggleMuteTab"],
        ["tabs-context-entrypoint", "context-toggleMuteTab"],
      ],
      events
    );
    assertInteractionData({
      tabsContext: {
        "context-toggleMuteTab": 1,
      },
    });

    // Check that tab-related items in the toolbar menu also register telemetry:
    context = elem("toolbar-context-menu");
    shown = BrowserTestUtils.waitForEvent(context, "popupshown");
    let scrollbox = elem("tabbrowser-arrowscrollbox");
    EventUtils.synthesizeMouse(
      scrollbox,
      // offset within the scrollbox - somewhere near the end:
      scrollbox.getBoundingClientRect().width - 20,
      5,
      { type: "contextmenu", button: 2 },
      window
    );
    await shown;

    hidden = BrowserTestUtils.waitForEvent(context, "popuphidden");
    context.activateItem(
      document.getElementById("toolbar-context-selectAllTabs")
    );
    await hidden;

    events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["tabs-context", "toolbar-context-selectAllTabs"],
        ["tabs-context-entrypoint", "toolbar-context-selectAllTabs"],
      ],
      events
    );
    assertInteractionData({
      tabsContext: {
        "toolbar-context-selectAllTabs": 1,
      },
    });
    // tidy up:
    gBrowser.clearMultiSelectedTabs();
  });
});

add_task(async function contextMenu_copyLink() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    resetGleanEvents();

    let tab = gBrowser.selectedTab;
    let context = elem("tabContextMenu");
    let shown = BrowserTestUtils.waitForEvent(context, "popupshown");
    EventUtils.synthesizeMouseAtCenter(
      tab,
      { type: "contextmenu", button: 2 },
      window
    );
    await shown;

    let hidden = BrowserTestUtils.waitForEvent(context, "popuphidden");

    await BrowserTestUtils.waitForMutationCondition(
      context,
      { childList: true },
      () => context.querySelector(".share-tab-url-item")
    );
    let shareMenu = context.querySelector(".share-tab-url-item");
    let submenuShown = BrowserTestUtils.waitForPopupEvent(
      shareMenu.menupopup,
      "shown"
    );
    shareMenu.openMenu(true);
    await submenuShown;
    shareMenu.menupopup.activateItem(
      shareMenu.menupopup.querySelector(".share-copy-link")
    );

    await hidden;

    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["tabs-context", "context-copy-url"],
        ["tabs-context-entrypoint", "context-copy-url"],
      ],
      events
    );
    assertInteractionData({
      tabsContext: { "context-copy-url": 1 },
    });
  });
});

add_task(async function contextMenu_copyMultipleLinks() {
  let tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  let tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.org"
  );

  let clicked = BrowserTestUtils.waitForEvent(tab1, "click");
  let multiSelectKey =
    AppConstants.platform == "macosx" ? { metaKey: true } : { ctrlKey: true };
  EventUtils.synthesizeMouseAtCenter(tab1, multiSelectKey, window);
  await clicked;

  resetGleanEvents();

  let context = elem("tabContextMenu");
  let shown = BrowserTestUtils.waitForEvent(context, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    tab2,
    { type: "contextmenu", button: 2 },
    window
  );
  await shown;

  let hidden = BrowserTestUtils.waitForEvent(context, "popuphidden");

  await BrowserTestUtils.waitForMutationCondition(
    context,
    { childList: true },
    () => context.querySelector(".share-tab-url-item")
  );
  let shareMenu = context.querySelector(".share-tab-url-item");
  let submenuShown = BrowserTestUtils.waitForPopupEvent(
    shareMenu.menupopup,
    "shown"
  );
  shareMenu.openMenu(true);
  await submenuShown;
  shareMenu.menupopup.activateItem(
    shareMenu.menupopup.querySelector(".share-copy-link")
  );
  await hidden;

  let events = Glean.browserUsage.interaction
    .testGetValue()
    .map(e => [e.extra.source, e.extra.widget_id]);
  Assert.deepEqual(
    [
      ["tabs-context", "context-copy-multiple-urls"],
      ["tabs-context-entrypoint", "context-copy-multiple-urls"],
    ],
    events
  );
  assertInteractionData({
    tabsContext: { "context-copy-multiple-urls": 1 },
  });

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

add_task(async function contextMenu_entrypoints() {
  /**
   * A utility function for this test task that opens the tab context
   * menu for a particular trigger node, chooses the "Reload Tab" item,
   * and then waits for the context menu to close.
   *
   * @param {Element} triggerNode
   *   The node that the tab context menu should be triggered with.
   * @returns {Promise<undefined>}
   *   Resolves after the context menu has fired the popuphidden event.
   */
  let openAndCloseTabContextMenu = async triggerNode => {
    let contextMenu = document.getElementById("tabContextMenu");
    let popupShown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
    EventUtils.synthesizeMouseAtCenter(triggerNode, {
      type: "contextmenu",
      button: 2,
    });
    await popupShown;

    let popupHidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
    let menuitem = document.getElementById("context_reloadTab");
    contextMenu.activateItem(menuitem);
    await popupHidden;
  };

  Services.fog.testResetFOG();

  await openAndCloseTabContextMenu(gBrowser.selectedTab);
  Assert.deepEqual(
    Glean.browserUiInteraction.tabsContextEntrypoint.testGetValue(),
    { "tabs-bar": 1 }
  );
  Services.fog.testResetFOG();

  gTabsPanel.initElements();
  let allTabsView = document.getElementById("allTabsMenu-allTabsView");
  let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  gTabsPanel.showAllTabsPanel(null);
  await allTabsPopupShownPromise;

  let firstTabItem = gTabsPanel.allTabsViewTabs.children[0];
  await openAndCloseTabContextMenu(firstTabItem);
  Assert.deepEqual(
    Glean.browserUiInteraction.tabsContextEntrypoint.testGetValue(),
    { "alltabs-menu": 1 }
  );

  let allTabsPopupHiddenPromise = BrowserTestUtils.waitForEvent(
    allTabsView.panelMultiView,
    "PanelMultiViewHidden"
  );
  gTabsPanel.hideAllTabsPanel();
  await allTabsPopupHiddenPromise;
});

add_task(async function appMenu() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    resetGleanEvents();

    let shown = BrowserTestUtils.waitForEvent(
      elem("appMenu-popup"),
      "popupshown"
    );
    click("PanelUI-menu-button");
    await shown;

    let hidden = BrowserTestUtils.waitForEvent(
      elem("appMenu-popup"),
      "popuphidden"
    );

    let findButtonID = "appMenu-find-button2";
    click(findButtonID);
    await hidden;

    let expectedData = {
      navBar: {
        "PanelUI-menu-button": 1,
      },
      appMenu: {
        [findButtonID]: 1,
      },
    };

    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "PanelUI-menu-button"],
        ["app-menu", findButtonID],
      ],
      events
    );

    assertInteractionData(expectedData);
  });
});

add_task(async function devtools() {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    resetGleanEvents();

    let shown = BrowserTestUtils.waitForEvent(
      elem("appMenu-popup"),
      "popupshown"
    );
    click("PanelUI-menu-button");
    await shown;

    click("appMenu-more-button2");
    shown = BrowserTestUtils.waitForEvent(
      elem("appmenu-moreTools"),
      "ViewShown"
    );
    await shown;

    let tabOpen = BrowserTestUtils.waitForNewTab(gBrowser);
    let hidden = BrowserTestUtils.waitForEvent(
      elem("appMenu-popup"),
      "popuphidden"
    );
    click(
      document.querySelector(
        "#appmenu-moreTools toolbarbutton[key='key_viewSource']"
      )
    );
    await hidden;

    let tab = await tabOpen;
    BrowserTestUtils.removeTab(tab);

    // Note that item ID's have '_' converted to '-'.
    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "PanelUI-menu-button"],
        ["app-menu", "appMenu-more-button2"],
        ["app-menu", "key-viewSource"],
      ],
      events
    );
    assertInteractionData({
      navBar: {
        "PanelUI-menu-button": 1,
      },
      appMenu: {
        "appMenu-more-button2": 1,
        "key-viewSource": 1,
      },
    });
  });
});

add_task(async function webextension() {
  BrowserUsageTelemetry._resetAddonIds();

  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    resetGleanEvents();

    function background() {
      browser.commands.onCommand.addListener(() => {
        browser.test.sendMessage("oncommand");
      });

      browser.runtime.onMessage.addListener(msg => {
        if (msg == "from-sidebar-action") {
          browser.test.sendMessage("sidebar-opened");
        }
      });

      browser.test.sendMessage("ready");
    }

    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        version: "1",
        browser_specific_settings: {
          gecko: { id: "random_addon@example.com" },
        },
        browser_action: {
          default_icon: "default.png",
          default_title: "Hello",
          default_area: "navbar",
        },
        page_action: {
          default_icon: "default.png",
          default_title: "Hello",
          show_matches: ["https://example.com/*"],
        },
        commands: {
          test_command: {
            suggested_key: {
              default: "Alt+Shift+J",
            },
          },
          _execute_sidebar_action: {
            suggested_key: {
              default: "Alt+Shift+Q",
            },
          },
        },
        sidebar_action: {
          default_panel: "sidebar.html",
          open_at_install: false,
        },
      },
      files: {
        "sidebar.html": `
          <!DOCTYPE html>
          <html>
            <head>
              <meta charset="utf-8">
              <script src="sidebar.js"></script>
            </head>
          </html>
        `,

        "sidebar.js": function () {
          browser.runtime.sendMessage("from-sidebar-action");
        },
      },
      background,
    });

    await extension.startup();
    await extension.awaitMessage("ready");

    // As the first add-on interacted with this should show up as `addon0`.

    click("random_addon_example_com-browser-action");
    let events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["nav-bar", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      navBar: {
        addon0: 1,
      },
    });

    // Wait for the element to show up.
    await TestUtils.waitForCondition(() =>
      elem("pageAction-urlbar-random_addon_example_com")
    );

    click("pageAction-urlbar-random_addon_example_com");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["pageaction-urlbar", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      pageactionUrlbar: {
        addon0: 1,
      },
    });

    EventUtils.synthesizeKey("j", { altKey: true, shiftKey: true });
    await extension.awaitMessage("oncommand");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["keyboard", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      keyboard: {
        addon0: 1,
      },
    });

    EventUtils.synthesizeKey("q", { altKey: true, shiftKey: true });
    await extension.awaitMessage("sidebar-opened");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["keyboard", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      keyboard: {
        addon0: 1,
      },
    });

    const extension2 = ExtensionTestUtils.loadExtension({
      manifest: {
        version: "1",
        browser_specific_settings: {
          gecko: { id: "random_addon2@example.com" },
        },
        browser_action: {
          default_icon: "default.png",
          default_title: "Hello",
          default_area: "navbar",
        },
        page_action: {
          default_icon: "default.png",
          default_title: "Hello",
          show_matches: ["https://example.com/*"],
        },
        commands: {
          test_command: {
            suggested_key: {
              default: "Alt+Shift+9",
            },
          },
        },
      },
      background,
    });

    await extension2.startup();
    await extension2.awaitMessage("ready");

    // A second extension should be `addon1`.

    click("random_addon2_example_com-browser-action");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["nav-bar", "addon1"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      navBar: {
        addon1: 1,
      },
    });

    // Wait for the element to show up.
    await TestUtils.waitForCondition(() =>
      elem("pageAction-urlbar-random_addon2_example_com")
    );

    click("pageAction-urlbar-random_addon2_example_com");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["pageaction-urlbar", "addon1"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      pageactionUrlbar: {
        addon1: 1,
      },
    });

    EventUtils.synthesizeKey("9", { altKey: true, shiftKey: true });
    await extension2.awaitMessage("oncommand");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["keyboard", "addon1"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      keyboard: {
        addon1: 1,
      },
    });

    // The first should have retained its ID.
    click("random_addon_example_com-browser-action");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["nav-bar", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      navBar: {
        addon0: 1,
      },
    });

    EventUtils.synthesizeKey("j", { altKey: true, shiftKey: true });
    await extension.awaitMessage("oncommand");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["keyboard", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      keyboard: {
        addon0: 1,
      },
    });

    click("pageAction-urlbar-random_addon_example_com");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["pageaction-urlbar", "addon0"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      pageactionUrlbar: {
        addon0: 1,
      },
    });

    await extension.unload();

    // Clear the last opened ID so if this test runs again the sidebar won't
    // automatically open when the extension is installed.
    window.SidebarController.lastOpenedId = null;

    // The second should retain its ID.
    click("random_addon2_example_com-browser-action");
    click("random_addon2_example_com-browser-action");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [
        ["nav-bar", "addon1"],
        ["nav-bar", "addon1"],
      ],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      navBar: {
        addon1: 2,
      },
    });

    click("pageAction-urlbar-random_addon2_example_com");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["pageaction-urlbar", "addon1"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      pageactionUrlbar: {
        addon1: 1,
      },
    });

    EventUtils.synthesizeKey("9", { altKey: true, shiftKey: true });
    await extension2.awaitMessage("oncommand");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["keyboard", "addon1"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      keyboard: {
        addon1: 1,
      },
    });

    await extension2.unload();

    // Now test that browser action items in the add-ons panel also get
    // telemetry recorded for them.
    const extension3 = ExtensionTestUtils.loadExtension({
      manifest: {
        version: "1",
        browser_specific_settings: {
          gecko: { id: "random_addon3@example.com" },
        },
        browser_action: {
          default_icon: "default.png",
          default_title: "Hello",
        },
      },
    });

    await extension3.startup();

    const shown = BrowserTestUtils.waitForPopupEvent(
      gUnifiedExtensions.panel,
      "shown"
    );
    await gUnifiedExtensions.togglePanel();
    await shown;

    click("random_addon3_example_com-browser-action");
    events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["unified-extensions-area", "addon2"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      unifiedExtensionsArea: {
        addon2: 1,
      },
    });
    const hidden = BrowserTestUtils.waitForPopupEvent(
      gUnifiedExtensions.panel,
      "hidden"
    );
    await gUnifiedExtensions.panel.hidePopup();
    await hidden;

    await extension3.unload();
  });
});

add_task(async function mainMenu() {
  // macOS does not use the menu bar.
  if (AppConstants.platform == "macosx") {
    return;
  }

  BrowserUsageTelemetry._resetAddonIds();

  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    resetGleanEvents();

    CustomizableUI.setToolbarVisibility("toolbar-menubar", true);

    let shown = BrowserTestUtils.waitForEvent(
      elem("menu_EditPopup"),
      "popupshown"
    );
    click("edit-menu");
    await shown;

    let hidden = BrowserTestUtils.waitForEvent(
      elem("menu_EditPopup"),
      "popuphidden"
    );
    click("menu_selectAll");
    await hidden;

    let events = Glean.browserUsage.interaction.testGetValue();
    Assert.deepEqual(
      [["menu-bar", "menu-selectAll"]],
      events.map(e => [e.extra.source, e.extra.widget_id])
    );
    assertInteractionData({
      menuBar: {
        // Note that the _ is replaced with - for telemetry identifiers.
        "menu-selectAll": 1,
      },
    });

    CustomizableUI.setToolbarVisibility("toolbar-menubar", false);
  });
});

add_task(async function preferences() {
  let finalPaneEvent = Services.prefs.getBoolPref("identity.fxaccounts.enabled")
    ? "sync-pane-loaded"
    : "privacy-pane-loaded";
  let finalPrefPaneLoaded = TestUtils.topicObserved(finalPaneEvent, () => true);
  await BrowserTestUtils.withNewTab("about:preferences", async () => {
    await finalPrefPaneLoaded;

    resetGleanEvents();

    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#browserRestoreSession",
      {},
      gBrowser.selectedBrowser.browsingContext
    );

    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#category-search",
      {},
      gBrowser.selectedBrowser.browsingContext
    );

    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#category-privacy",
      {},
      gBrowser.selectedBrowser.browsingContext
    );
    await BrowserTestUtils.waitForCondition(() =>
      gBrowser.selectedBrowser.contentDocument.getElementById(
        "contentBlockingLearnMore"
      )
    );

    const onLearnMoreOpened = BrowserTestUtils.waitForNewTab(gBrowser);
    gBrowser.selectedBrowser.contentDocument
      .getElementById("contentBlockingLearnMore")
      .scrollIntoView();
    await BrowserTestUtils.synthesizeMouseAtCenter(
      "#contentBlockingLearnMore",
      {},
      gBrowser.selectedBrowser.browsingContext
    );
    await onLearnMoreOpened;
    gBrowser.removeCurrentTab();

    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["preferences_paneGeneral", "browserRestoreSession"],
        ["preferences_panePrivacy", "contentBlockingLearnMore"],
      ],
      events
    );
    assertInteractionData({
      preferencesPaneGeneral: {
        browserRestoreSession: 1,
      },
      preferencesPanePrivacy: {
        contentBlockingLearnMore: 1,
      },
    });
  });
});

/**
 * Context click on a history or bookmark link and open it in a new window.
 *
 * @param {Element} link - The link to open.
 */
async function openLinkUsingContextMenu(link) {
  const placesContext = document.getElementById("placesContext");
  const promisePopup = BrowserTestUtils.waitForEvent(
    placesContext,
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(link, {
    button: 2,
    type: "contextmenu",
  });
  await promisePopup;
  const promiseNewWindow = BrowserTestUtils.waitForNewWindow();
  placesContext.activateItem(
    document.getElementById("placesContext_open:newwindow")
  );
  const win = await promiseNewWindow;
  await BrowserTestUtils.closeWindow(win);
}

async function history_appMenu(useContextClick) {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    let shown = BrowserTestUtils.waitForEvent(
      elem("appMenu-popup"),
      "popupshown"
    );
    click("PanelUI-menu-button");
    await shown;

    click("appMenu-history-button");
    shown = BrowserTestUtils.waitForEvent(elem("PanelUI-history"), "ViewShown");
    await shown;

    let list = document.getElementById("appMenu_historyMenu");
    let listItem = list.querySelector("toolbarbutton");

    if (useContextClick) {
      await openLinkUsingContextMenu(listItem);
    } else {
      EventUtils.synthesizeMouseAtCenter(listItem, {});
    }

    let expectedData = {
      navBar: {
        "PanelUI-menu-button": 1,
      },

      appMenu: { "history-item": 1, "appMenu-history-button": 1 },
    };
    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "PanelUI-menu-button"],
        ["app-menu", "appMenu-history-button"],
        ["app-menu", "history-item"],
      ],
      events
    );
    assertInteractionData(expectedData);
  });
}

add_task(async function history_appMenu_click() {
  await history_appMenu(false);
});

add_task(async function history_appMenu_context_click() {
  await history_appMenu(true);
});

async function bookmarks_appMenu(useContextClick) {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    let shown = BrowserTestUtils.waitForEvent(
      elem("appMenu-popup"),
      "popupshown"
    );

    shown = BrowserTestUtils.waitForEvent(elem("appMenu-popup"), "popupshown");
    click("PanelUI-menu-button");
    await shown;

    click("appMenu-bookmarks-button");
    shown = BrowserTestUtils.waitForEvent(
      elem("PanelUI-bookmarks"),
      "ViewShown"
    );
    await shown;

    let list = document.getElementById("panelMenu_bookmarksMenu");
    let listItem = list.querySelector("toolbarbutton");

    if (useContextClick) {
      await openLinkUsingContextMenu(listItem);
    } else {
      EventUtils.synthesizeMouseAtCenter(listItem, {});
    }

    let expectedData = {
      navBar: {
        "PanelUI-menu-button": 1,
      },

      appMenu: { "bookmark-item": 1, "appMenu-bookmarks-button": 1 },
    };
    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "PanelUI-menu-button"],
        ["app-menu", "appMenu-bookmarks-button"],
        ["app-menu", "bookmark-item"],
      ],
      events
    );
    assertInteractionData(expectedData);
  });
}

add_task(async function bookmarks_appMenu_click() {
  await bookmarks_appMenu(false);
});

add_task(async function bookmarks_appMenu_context_click() {
  await bookmarks_appMenu(true);
});

async function bookmarks_library_navbar(useContextClick) {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    CustomizableUI.addWidgetToArea("library-button", "nav-bar");
    let button = document.getElementById("library-button");
    button.click();
    await BrowserTestUtils.waitForEvent(
      elem("appMenu-libraryView"),
      "ViewShown"
    );

    click("appMenu-library-bookmarks-button");
    await BrowserTestUtils.waitForEvent(elem("PanelUI-bookmarks"), "ViewShown");

    let list = document.getElementById("panelMenu_bookmarksMenu");
    let listItem = list.querySelector("toolbarbutton");

    if (useContextClick) {
      await openLinkUsingContextMenu(listItem);
    } else {
      EventUtils.synthesizeMouseAtCenter(listItem, {});
    }

    let expectedData = {
      navBar: {
        "library-button": 1,
        "bookmark-item": 1,
        "appMenu-library-bookmarks-button": 1,
      },
    };
    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "library-button"],
        ["nav-bar", "appMenu-library-bookmarks-button"],
        ["nav-bar", "bookmark-item"],
      ],
      events
    );
    assertInteractionData(expectedData);
  });

  CustomizableUI.removeWidgetFromArea("library-button");
}

add_task(async function bookmarks_library_navbar_click() {
  await bookmarks_library_navbar(false);
});

add_task(async function bookmarks_library_navbar_context_click() {
  await bookmarks_library_navbar(true);
});

async function history_library_navbar(useContextClick) {
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    CustomizableUI.addWidgetToArea("library-button", "nav-bar");
    let button = document.getElementById("library-button");
    button.click();
    await BrowserTestUtils.waitForEvent(
      elem("appMenu-libraryView"),
      "ViewShown"
    );

    click("appMenu-library-history-button");
    let shown = BrowserTestUtils.waitForEvent(
      elem("PanelUI-history"),
      "ViewShown"
    );
    await shown;

    let list = document.getElementById("appMenu_historyMenu");
    let listItem = list.querySelector("toolbarbutton");

    if (useContextClick) {
      await openLinkUsingContextMenu(listItem);
    } else {
      EventUtils.synthesizeMouseAtCenter(listItem, {});
    }

    let expectedData = {
      navBar: {
        "library-button": 1,
        "history-item": 1,
        "appMenu-library-history-button": 1,
      },
    };
    let events = Glean.browserUsage.interaction
      .testGetValue()
      .map(e => [e.extra.source, e.extra.widget_id]);
    Assert.deepEqual(
      [
        ["nav-bar", "library-button"],
        ["nav-bar", "appMenu-library-history-button"],
        ["nav-bar", "history-item"],
      ],
      events
    );
    assertInteractionData(expectedData);
  });

  CustomizableUI.removeWidgetFromArea("library-button");
}

add_task(async function history_library_navbar_click() {
  await history_library_navbar(false);
});

add_task(async function history_library_navbar_context_click() {
  await history_library_navbar(true);
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// NOTE on usage of sinon spies with THP components
// since THP is lazy-loaded, the tab hover preview component *must*
// be activated at least once in each test prior to setting up
// any spies against this component.
// Since each test reuses the same window, generally this issue will only
// be made evident in chaos-mode tests that run out of order (and
// thus will result in an intermittent).
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const TabHoverPanelSet = ChromeUtils.importESModule(
  "chrome://browser/content/tabbrowser/tab-hover-preview.mjs"
).default;

const { TabNotes } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs"
);

const TAB_PREVIEW_PANEL_ID = "tab-preview-panel";
const TAB_GROUP_PREVIEW_PANEL_ID = "tabgroup-preview-panel";

async function parkNativePointer(win = window) {
  const browserEl = win.gBrowser.selectedBrowser;
  const { width, height } = browserEl.getBoundingClientRect();
  await EventUtils.promiseNativeMouseEvent({
    type: "mousemove",
    target: browserEl,
    offsetX: width - 10,
    offsetY: height - 10,
    win,
  });
}

async function openTabPreview(tab, win = window) {
  const panel = win.document.getElementById(TAB_PREVIEW_PANEL_ID);
  const previewShown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  const previewUpdated = BrowserTestUtils.waitForEvent(
    panel,
    "TabPreviewUpdated"
  );
  EventUtils.synthesizeMouse(tab, 1, 1, { type: "mouseover" }, win);
  await Promise.all([previewShown, previewUpdated]);
}

async function closeTabPreviews(win = window) {
  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    win.document.getElementById(TAB_PREVIEW_PANEL_ID),
    "hidden"
  );
  const tabs = win.document.getElementById("tabbrowser-tabs");
  const tabsRect = tabs.getBoundingClientRect();
  EventUtils.synthesizeMouse(
    tabs,
    0,
    tabsRect.height + 10,
    {
      type: "mouseout",
    },
    win
  );
  return previewHidden;
}

async function openGroupPreview(group, win = window) {
  const previewElement = win.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );
  Assert.ok(previewElement.state, "closed");

  const previewShown = BrowserTestUtils.waitForPopupEvent(
    previewElement,
    "shown"
  );
  const previewUpdated = BrowserTestUtils.waitForEvent(
    previewElement,
    "TabGroupPreviewUpdated"
  );
  EventUtils.synthesizeMouseAtCenter(
    group.labelElement,
    { type: "mouseover" },
    win
  );
  await Promise.all([previewShown, previewUpdated]);
}

async function closeGroupPreviews(win = window) {
  const tabs = win.document.getElementById("tabbrowser-tabs");
  const tabsRect = tabs.getBoundingClientRect();
  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    win.document.getElementById(TAB_GROUP_PREVIEW_PANEL_ID),
    "hidden"
  );
  EventUtils.synthesizeMouse(
    tabs,
    0,
    tabsRect.height + 1,
    {
      type: "mouseout",
    },
    win
  );
  return previewHidden;
}

function getOpenPanels() {
  return document.querySelectorAll(
    "panel[panelopen=true],panel[animating=true],menupopup[open=true]"
  );
}

async function withGroupPanel(fn) {
  // Creates a collapsed tab group
  const tab = await addTabTo(gBrowser, "about:robots");
  const group = gBrowser.addTabGroup([tab]);
  group.collapsed = true;

  // Ensure the lazy-loaded preview component exists
  await openGroupPreview(group);
  await closeGroupPreviews();
  const { tabGroupPanel } = gBrowser.tabContainer.previewPanel;

  // Run `fn` with the group panel and group
  try {
    await fn(tabGroupPanel, group);
  } finally {
    // Clean it up
    await removeTabGroup(group);
    await resetState();
  }
}

// Utility to assert popupOptions given prefs and expected values.
// Waits for RTL_UI to settle when toggling text direction.
async function assertPopupOptions(tabGroupPanel, { prefs, expected, message }) {
  const bidi = prefs.some(
    ([pref, value]) => pref == "intl.l10n.pseudo" && value == "bidi"
  );
  await SpecialPowers.pushPrefEnv({ set: prefs });
  if (bidi) {
    await TestUtils.waitForCondition(
      () => window.RTL_UI,
      "UI direction is RTL"
    );
  }
  Assert.deepEqual(tabGroupPanel.popupOptions, expected, message);
  await SpecialPowers.popPrefEnv();
  if (bidi) {
    await TestUtils.waitForCondition(
      () => !window.RTL_UI,
      "UI direction is LTR"
    );
  }
}

async function resetState() {
  // Ensure the mouse is not hovering over the tab strip.
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {
    type: "mouseover",
  });

  gBrowser.tabContainer.previewPanel?.forceReset();

  for (let panel of getOpenPanels()) {
    let hiddenEvent = BrowserTestUtils.waitForPopupEvent(panel, "hidden");
    panel.hidePopup();
    await hiddenEvent;
  }

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
}

function createFakePanel(win = window) {
  let panel = win.document.createXULElement("panel");
  // Necessary to get the panel open, animating, etc. elements to appear.
  panel.setAttribute("type", "arrow");
  win.document.documentElement.appendChild(panel);

  return panel;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.groups.hoverPreview.enabled", true],
      ["browser.tabs.hoverPreview.enabled", true],
      ["browser.tabs.hoverPreview.showThumbnails", false],
      ["browser.tabs.tooltipsShowPidAndActiveness", false],
      ["test.wait300msAfterTabSwitch", true],
      ["ui.tooltip.delay_ms", 0],
    ],
  });

  // The hover states in these tests are controlled by synthesized events. There
  // has been a history of problems in these tests caused by improper
  // interaction between the synthesized event and the position of the OS
  // pointer. To resolve this, disable all non test events and ensure the OS
  // pointer is not positioned over the browser chrome at the start of each
  // test.
  await parkNativePointer();
  EventUtils.disableNonTestMouseEvents(true);
  registerCleanupFunction(() => {
    EventUtils.disableNonTestMouseEvents(false);
  });

  await resetState();
  registerCleanupFunction(async function () {
    await resetState();
  });
});

/*
 * The tests in this file are split into three groups:
 * 1. Tests dealing specifically with the tab hover preview (THP) panel
 * 2. Tests dealing specifically with the tab group hover preview (TGHP) panel
 * 3. Tests that verify functionality of both kinds of preview, or that test
 *    logic that is used by the main PanelSet component (i.e. is shared code
 *    for both kinds of preview)
 */

/*
 * Tab hover preview tests
 * -----------------------
 */

/**
 * Verify the following:
 *
 * 1. Tab preview card appears when the mouse hovers over a tab
 * 2. Tab preview card shows the correct preview for the tab being hovered
 * 3. Tab preview card is dismissed when the mouse leaves the tab bar
 */
add_task(async function tabHoverTests() {
  const tabUrl1 =
    "data:text/html,<html><head><title>First New Tab</title></head><body>Hello</body></html>";
  const tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl1);
  const tabUrl2 =
    "data:text/html,<html><head><title>Second New Tab</title></head><body>Hello</body></html>";
  const tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl2);
  const previewContainer = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab1);
  Assert.equal(
    previewContainer.querySelector(".tab-preview-title").innerText,
    "First New Tab",
    "Preview of tab1 shows correct title"
  );
  await closeTabPreviews();

  await openTabPreview(tab2);
  Assert.equal(
    previewContainer.querySelector(".tab-preview-title").innerText,
    "Second New Tab",
    "Preview of tab2 shows correct title"
  );
  await closeTabPreviews();

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  await resetState();
});

/**
 * Verify that closing tabs in a row without moving the pointer keeps
 * previewing whatever ends up under it, and that the preview is dismissed once
 * nothing takes the closed tab's place.
 */
add_task(async function tabCloseHoverTests() {
  const titles = ["First Closed", "Second Closed", "Last Closed"];
  const tabs = [];
  for (let title of titles) {
    tabs.push(
      await addTabTo(
        gBrowser,
        `data:text/html,<html><head><title>${title}</title></head><body>Hello</body></html>`
      )
    );
  }
  const previewContainer = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tabs[0]);

  let sawHidden = false;
  const onHidden = () => {
    sawHidden = true;
  };
  previewContainer.addEventListener("popuphidden", onHidden);

  // No mouse events in between: each close has to hand the hover over to the
  // tab moving into its place on its own.
  for (let i = 0; i < 2; i++) {
    BrowserTestUtils.removeTab(tabs[i]);
    await TestUtils.waitForCondition(
      () =>
        previewContainer.querySelector(".tab-preview-title").innerText ==
        titles[i + 1],
      `Preview moved onto ${titles[i + 1]}`
    );
    Assert.ok(tabs[i + 1]._hover, `${titles[i + 1]} took over the hover`);
  }
  Assert.ok(!sawHidden, "Preview stayed open across the tab closes");
  previewContainer.removeEventListener("popuphidden", onHidden);

  // The last tab has no successor, so nothing slides under the pointer.
  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewContainer,
    "hidden"
  );
  BrowserTestUtils.removeTab(tabs[2]);
  await previewHidden;

  await resetState();
});

/**
 * Verify the container (contextual identity) indicator:
 *
 * 1. Container tabs show a pill with the container label and identity classes
 * 2. Non-container tabs hide the indicator
 */
add_task(async function tabContainerIndicatorTests() {
  const { ContextualIdentityService } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
  );

  const userContextId = 1;
  const identity =
    ContextualIdentityService.getPublicIdentityFromId(userContextId);
  const containerTab = await addTabTo(gBrowser, "about:blank", {
    userContextId,
  });
  const plainTab = await addTabTo(gBrowser, "about:blank");
  const previewContainer = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const indicator = previewContainer.querySelector(
    ".tab-preview-container-indicator"
  );

  await openTabPreview(containerTab);
  Assert.ok(
    !indicator.hidden,
    "Container indicator is shown for container tab"
  );
  Assert.equal(
    indicator.querySelector(".tab-preview-container-label").textContent,
    ContextualIdentityService.getUserContextLabel(userContextId),
    "Container indicator shows the correct label"
  );
  Assert.ok(
    indicator.classList.contains(`identity-color-${identity.color}`),
    "Container indicator has the identity color class"
  );
  Assert.ok(
    indicator.classList.contains(`identity-icon-${identity.icon}`),
    "Container indicator has the identity icon class"
  );
  await closeTabPreviews();

  await openTabPreview(plainTab);
  Assert.ok(indicator.hidden, "Container indicator is hidden for plain tab");
  await closeTabPreviews();

  BrowserTestUtils.removeTab(containerTab);
  BrowserTestUtils.removeTab(plainTab);

  await resetState();
});

/**
 * Tab preview should be dismissed when a new tab is focused/selected
 */
add_task(async function tabFocusTests() {
  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  const tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab1);
  Assert.equal(previewPanel.state, "open", "Preview is open");

  let previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewPanel,
    "hidden"
  );
  tab1.click();
  await previewHidden;
  Assert.equal(
    previewPanel.state,
    "closed",
    "Preview is closed after selecting tab"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  await resetState();
});

/**
 * Verify that the pid and activeness statuses are not shown
 * when the flag is not enabled.
 */
add_task(async function tabPidAndActivenessHiddenByDefaultTests() {
  const tabUrl1 =
    "data:text/html,<html><head><title>First New Tab</title></head><body>Hello</body></html>";
  const tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl1);
  const previewContainer = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab1);
  Assert.equal(
    previewContainer.querySelector(".tab-preview-pid").innerText,
    "",
    "Tab PID is not shown"
  );
  Assert.equal(
    previewContainer.querySelector(".tab-preview-activeness").innerText,
    "",
    "Tab activeness is not shown"
  );

  await closeTabPreviews();
  BrowserTestUtils.removeTab(tab1);
  await resetState();
});

add_task(async function tabPidAndActivenessTests() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.tooltipsShowPidAndActiveness", true]],
  });

  const tabUrl1 =
    "data:text/html,<html><head><title>Single process tab</title></head><body>Hello</body></html>";
  const tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl1);
  const tabUrl2 = `data:text/html,<html>
      <head>
        <title>Multi-process tab</title>
      </head>
      <body>
        <iframe
          id="inlineFrameExample"
          title="Inline Frame Example"
          width="300"
          height="200"
          src="https://example.com">
        </iframe>
      </body>
    </html>`;
  const tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl2);
  const previewContainer = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab1);
  Assert.stringMatches(
    previewContainer.querySelector(".tab-preview-pid").innerText,
    /^pid: \d+$/,
    "Tab PID is shown on single process tab"
  );
  Assert.equal(
    previewContainer.querySelector(".tab-preview-activeness").innerText,
    "",
    "Tab activeness is not shown on inactive tab"
  );
  await closeTabPreviews();

  await openTabPreview(tab2);
  Assert.stringMatches(
    previewContainer.querySelector(".tab-preview-pid").innerText,
    /^pids: \d+, \d+$/,
    "Tab PIDs are shown on multi-process tab"
  );
  Assert.equal(
    previewContainer.querySelector(".tab-preview-activeness").innerText,
    "[A]",
    "Tab activeness is shown on active tab"
  );
  await closeTabPreviews();

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  await SpecialPowers.popPrefEnv();
  await resetState();
});

/**
 * Verify that non-selected tabs display a thumbnail in their preview
 * when browser.tabs.hoverPreview.showThumbnails is set to true,
 * while the currently selected tab never displays a thumbnail in its preview.
 */
add_task(async function tabThumbnailTests() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.hoverPreview.showThumbnails", true]],
  });
  const tabUrl1 = "about:blank";
  const tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl1);
  const tabUrl2 = "about:blank";
  const tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl2);
  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);

  let thumbnailUpdated = BrowserTestUtils.waitForEvent(
    previewPanel,
    "TabPreviewThumbnailUpdated",
    false,
    evt => evt.detail.thumbnail
  );
  await openTabPreview(tab1);
  await thumbnailUpdated;
  Assert.ok(
    previewPanel.querySelectorAll(
      ".tab-preview-thumbnail-container img, .tab-preview-thumbnail-container canvas"
    ).length,
    "Tab1 preview contains thumbnail"
  );

  await closeTabPreviews();
  thumbnailUpdated = BrowserTestUtils.waitForEvent(
    previewPanel,
    "TabPreviewThumbnailUpdated"
  );
  await openTabPreview(tab2);
  await thumbnailUpdated;
  Assert.equal(
    previewPanel.querySelectorAll(
      ".tab-preview-thumbnail-container img, .tab-preview-thumbnail-container canvas"
    ).length,
    0,
    "Tab2 (selected) does not contain thumbnail"
  );

  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewPanel,
    "hidden"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  await SpecialPowers.popPrefEnv();

  // Removing the tab should close the preview.
  await previewHidden;
  await resetState();
});

/**
 * Verify that non-selected tabs display a wireframe in their preview
 * when enabled, and the tab is unable to provide a thumbnail (e.g. unloaded).
 */
add_task(async function tabWireframeTests() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.hoverPreview.showThumbnails", true],
      ["browser.history.collectWireframes", true],
    ],
  });

  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<html><head><title>First New Tab</title></head><body>Hello</body></html>"
  );
  const tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  // Discard the first tab so it can't provide a thumbnail image
  await TabStateFlusher.flush(tab1.linkedBrowser);
  gBrowser.discardBrowser(tab1, true);

  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);

  let thumbnailUpdated = BrowserTestUtils.waitForEvent(
    previewPanel,
    "TabPreviewThumbnailUpdated",
    false,
    evt => evt.detail.thumbnail
  );
  await openTabPreview(tab1);
  await thumbnailUpdated;
  Assert.ok(
    previewPanel.querySelectorAll(".tab-preview-thumbnail-container svg")
      .length,
    "Tab1 preview contains wireframe"
  );

  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewPanel,
    "hidden"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  await SpecialPowers.popPrefEnv();

  // Removing the tab should close the preview.
  await previewHidden;
  await resetState();
});

/**
 * preview should be hidden if it is showing when the URLBar receives input
 */
add_task(async function tabUrlBarInputTests() {
  const previewElement = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await openTabPreview(tab1);
  gURLBar.focus();
  Assert.equal(previewElement.state, "open", "Preview is open");

  let previewHidden = BrowserTestUtils.waitForEvent(
    previewElement,
    "popuphidden"
  );
  EventUtils.sendChar("q", window);
  await previewHidden;

  Assert.equal(previewElement.state, "closed", "Preview is closed");
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {
    type: "mousemove",
  });
  await openTabPreview(tab1);
  Assert.equal(previewElement.state, "open", "Preview is open");

  previewHidden = BrowserTestUtils.waitForEvent(previewElement, "popuphidden");
  EventUtils.sendChar("q", window);
  await previewHidden;
  Assert.equal(previewElement.state, "closed", "Preview is closed");

  BrowserTestUtils.removeTab(tab1);
  await resetState();
});

/**
 * The tab panel should be configured to roll up on wheel events if
 * the tab strip is overflowing.
 */
add_task(async function tabWheelTests() {
  let initialTab = gBrowser.tabs[0];
  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  Assert.ok(
    !previewPanel.hasAttribute("rolluponmousewheel"),
    "Panel does not have rolluponmousewheel when no overflow"
  );

  let scrollOverflowEvent = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer.arrowScrollbox,
    "overflow"
  );
  await BrowserTestUtils.overflowTabs(registerCleanupFunction, window, {
    overflowAtStart: false,
  });
  await scrollOverflowEvent;
  await openTabPreview(tab1);

  Assert.equal(
    previewPanel.getAttribute("rolluponmousewheel"),
    "true",
    "Panel has rolluponmousewheel=true when tabs overflow"
  );

  await closeTabPreviews();
  gBrowser.removeAllTabsBut(initialTab);
  await resetState();
});

add_task(async function tabPanelAppearsAsTooltipToAccessibilityToolsTests() {
  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);
  Assert.equal(
    previewPanel.getAttribute("role"),
    "tooltip",
    "The panel appears as a tooltip to assistive technology"
  );
  await resetState();
});

/**
 * Verify that if the browser document title (i.e. tab label) changes,
 * the tab preview panel is updated
 */
add_task(async function tabContentChangeTests() {
  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);

  const tabUrl =
    "data:text/html,<html><head><title>Original Tab Title</title></head><body>Hello</body></html>";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl);
  const newTitle = "New Tab Title";

  await openTabPreview(tab);
  Assert.equal(
    previewPanel.querySelector(".tab-preview-title").innerText,
    "Original Tab Title",
    "Preview of tab shows original tab title"
  );

  let tabRenameEvent = BrowserTestUtils.waitForEvent(tab, "TabAttrModified");
  let previewUpdated = BrowserTestUtils.waitForEvent(
    previewPanel,
    "TabPreviewUpdated"
  );
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [newTitle],
    async newTitleInContentProcess => {
      content.document.title = newTitleInContentProcess;
    }
  );
  await Promise.all([tabRenameEvent, previewUpdated]);

  Assert.equal(
    previewPanel.querySelector(".tab-preview-title").innerText,
    newTitle,
    "Preview of tab shows new tab title"
  );

  await closeTabPreviews();
  BrowserTestUtils.removeTab(tab);
  await resetState();
});

/**
 * Test that tab notes and their UI elements appear correctly in the tab
 * hover preview panel.
 */
add_task(async function tabNotesTests() {
  if (!Services.prefs.getBoolPref("browser.tabs.notes.enabled", false)) {
    // Skip tests if tab notes is not enabled
    // This is necessary because some tab notes functionality only loads at
    // startup if the pref is enabled
    todo(false, "Skip when tab notes is not enabled; see bug2008033");
    return;
  }

  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const noteText = "Hello world";

  const tab = await addTabTo(gBrowser, "https://example.com/");

  info("validate the presentation of an eligible tab with no note");
  await openTabPreview(tab);
  let addNoteButton = previewPanel.querySelector(".tab-preview-add-note");
  Assert.ok(addNoteButton, "add note button exists in the DOM");

  info(
    "validate that hovering over the add note button does not hide the preview panel"
  );
  EventUtils.synthesizeMouseAtCenter(
    addNoteButton,
    { type: "mouseover" },
    window
  );

  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 300));

  Assert.ok(
    previewPanel.hasAttribute("panelopen"),
    "Preview panel is still open"
  );

  info(
    "validate that hovering over the panel outside of the add note button hides the panel"
  );
  let previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewPanel,
    "hidden"
  );
  let nonhoverableArea = document.querySelector(".tab-preview-content-main");
  EventUtils.synthesizeMouseAtCenter(
    nonhoverableArea,
    {
      type: "mouseover",
    },
    window
  );
  await previewHidden;
  Assert.ok(
    !previewPanel.hasAttribute("panelopen"),
    "Preview panel was hidden"
  );

  await openTabPreview(tab);

  info("choose to add a note from the tab hover preview panel");
  let tabNotePanel = document.getElementById("tabNotePanel");
  let panelShown = BrowserTestUtils.waitForPopupEvent(tabNotePanel, "shown");
  previewHidden = BrowserTestUtils.waitForPopupEvent(previewPanel, "hidden");
  addNoteButton.click();
  await Promise.all([panelShown, previewHidden]);

  info("save a new tab note");
  Assert.equal(
    document.activeElement,
    tabNotePanel.querySelector("textarea"),
    "tab note textarea should be focused"
  );
  const input = BrowserTestUtils.waitForEvent(document.activeElement, "input");
  EventUtils.sendString(noteText, window);
  await input;
  let menuHidden = BrowserTestUtils.waitForPopupEvent(tabNotePanel, "hidden");
  let tabNoteCreated = BrowserTestUtils.waitForEvent(tab, "TabNote:Created");
  tabNotePanel.querySelector("#tab-note-editor-button-save").click();
  await Promise.all([menuHidden, tabNoteCreated]);

  await TestUtils.waitForCondition(
    () => Glean.tabNotes.added.testGetValue()?.length,
    "wait for event to be recorded"
  );

  const [addedEvent] = Glean.tabNotes.added.testGetValue();
  Assert.deepEqual(
    addedEvent.extra,
    {
      source: "hover_menu",
      note_length: noteText.length,
    },
    "added event extra data should include length and say the tab note was added from the tab hover preview menu"
  );

  await closeTabPreviews();

  info("validate the presentation of an eligible tab with a tab note");
  await openTabPreview(tab);

  addNoteButton = previewPanel.querySelector(".tab-preview-add-note");
  Assert.ok(!addNoteButton, "add note button does not exist in the DOM");
  await closeTabPreviews();

  info(
    "delete the tab note to return the tab hover preview to the state with no tab note"
  );
  const tabNoteRemoved = BrowserTestUtils.waitForEvent(tab, "TabNote:Removed");
  TabNotes.delete(tab);
  await tabNoteRemoved;

  info(
    "validate the presentation of an eligible tab after its note has been deleted"
  );
  await openTabPreview(tab);
  addNoteButton = previewPanel.querySelector(".tab-preview-add-note");
  Assert.ok(
    !addNoteButton.hasAttribute("hidden"),
    "add note button should be visible on an eligible tab without a tab note after delete"
  );
  await closeTabPreviews();

  BrowserTestUtils.removeTab(tab);
  await resetState();
  await TabNotes.reset();
});

/**
 * Test that the "New" badge in the hover preview panel is displayed when
 * browser.tabs.notes.newBadge.enabled is true.
 */
add_task(async function tabNotesNewBadgeVisibilityTests() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.notes.enabled", true]],
  });
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.notes.newBadge.enabled", true]],
  });

  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const tab = await addTabTo(gBrowser, "https://example.com/");

  await openTabPreview(tab);
  const badge = previewPanel.querySelector(".tab-preview-add-note moz-badge");
  Assert.ok(
    !badge.hasAttribute("hidden"),
    "badge is visible when newBadge pref is true"
  );
  await closeTabPreviews();
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.notes.newBadge.enabled", false]],
  });
  await openTabPreview(tab);
  Assert.ok(
    badge.hasAttribute("hidden"),
    "badge is hidden when newBadge pref is false"
  );
  await closeTabPreviews();

  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
  await resetState();
});

/**
 * Test that clicking "Add Note" in the hover preview panel sets the
 * browser.tabs.notes.newBadge.enabled pref to false, and that the badge
 * is hidden on the next hover
 */
add_task(async function tabNotesNewBadgeDismissedByPreviewPanelTests() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.notes.enabled", true],
      ["browser.tabs.notes.newBadge.enabled", true],
    ],
  });

  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);

  const tab = await addTabTo(gBrowser, "https://example.com/");

  await openTabPreview(tab);

  const addNoteButton = previewPanel.querySelector(".tab-preview-add-note");
  const tabNotePanel = document.getElementById("tabNotePanel");
  const panelShown = BrowserTestUtils.waitForPopupEvent(tabNotePanel, "shown");
  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewPanel,
    "hidden"
  );
  addNoteButton.click();
  await Promise.all([panelShown, previewHidden]);

  Assert.ok(
    !Services.prefs.getBoolPref("browser.tabs.notes.newBadge.enabled"),
    "pref is set to false after clicking Add Note in the preview panel"
  );

  const panelHidden = BrowserTestUtils.waitForPopupEvent(
    tabNotePanel,
    "hidden"
  );
  tabNotePanel.hidePopup();
  await panelHidden;
  await closeTabPreviews();

  await openTabPreview(tab);
  const badge = previewPanel.querySelector(".tab-preview-add-note moz-badge");
  Assert.ok(
    badge.hasAttribute("hidden"),
    "badge is hidden on subsequent hover after dismissal"
  );
  await closeTabPreviews();

  BrowserTestUtils.removeTab(tab);
  await resetState();
  await SpecialPowers.popPrefEnv();
});

/*
 * Tab group hover preview tests
 * -----------------------------
 */

add_task(async function tabGroupPanelAppearsOnTabGroupHover() {
  const tab1 = await addTabTo(gBrowser, "about:robots");
  const tab2 = await addTabTo(gBrowser, "about:mozilla");

  const group = gBrowser.addTabGroup([tab1, tab2]);
  group.collapsed = true;

  // Tabs from the control group should not appear in the list
  const controlTab = await addTabTo(gBrowser, "https://example.com/");
  const controlGroup = gBrowser.addTabGroup([controlTab]);
  controlGroup.collapsed = true;

  await openGroupPreview(group);

  const previewPanel = window.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );
  const panelContent = previewPanel.querySelector("#tabgroup-panel-content");

  Assert.equal(
    panelContent.children.length,
    2,
    "Preview panel has one toolbarbutton for each tab in the group"
  );
  Assert.equal(
    panelContent.children[0].tagName,
    "toolbarbutton",
    "First child is a toolbarbutton"
  );
  Assert.equal(
    panelContent.children[0].label,
    tab1.label,
    "First child has correct label"
  );
  Assert.equal(
    panelContent.children[0].getAttribute("tooltiptext"),
    panelContent.children[0].label,
    "First child has a tooltip that is identical to the label"
  );
  Assert.equal(
    panelContent.children[1].tagName,
    "toolbarbutton",
    "Second child is a toolbarbutton"
  );
  Assert.equal(
    panelContent.children[1].label,
    tab2.label,
    "Second child has correct label"
  );
  Assert.equal(
    panelContent.children[1].getAttribute("tooltiptext"),
    panelContent.children[1].label,
    "Second child has a tooltip that is identical to the label"
  );

  await closeGroupPreviews();

  await removeTabGroup(group);
  await removeTabGroup(controlGroup);
  await resetState();
});

add_task(async function tabGroupPanelDoesNotAppearForExpandedTabGroups() {
  const group = gBrowser.addTabGroup([
    BrowserTestUtils.addTab(gBrowser, "about:robots"),
  ]);
  group.collapsed = true;

  // Panel must be opened at least once to ensure the component is lazy-loaded
  await openGroupPreview(group);
  await closeGroupPreviews();

  const previewPanelComponent = window.gBrowser.tabContainer.previewPanel;

  Assert.notEqual(
    previewPanelComponent,
    null,
    "Sanity check: preview panel component is loaded"
  );
  Assert.equal(
    previewPanelComponent.tabPanel.panelElement.state,
    "closed",
    "Sanity check: tab preview panel is closed"
  );
  Assert.equal(
    previewPanelComponent.tabGroupPanel.panelElement.state,
    "closed",
    "Sanity check: tab group preview panel is closed"
  );

  sinon.spy(previewPanelComponent, "activate");

  group.collapsed = false;

  EventUtils.synthesizeMouseAtCenter(
    group.labelElement,
    { type: "mouseover" },
    window
  );
  await TestUtils.waitForCondition(() => {
    return previewPanelComponent.activate.calledOnce;
  }, "Waiting for activate to be called");

  Assert.equal(
    previewPanelComponent.tabGroupPanel.panelElement.state,
    "closed",
    "Group preview panel does not open on an expanded tab group"
  );

  await removeTabGroup(group);
  sinon.restore();
  await resetState();
});

add_task(async function tabGroupPanelExpandDismissesPanel() {
  const tab1 = await addTabTo(gBrowser, "about:robots");
  const group = gBrowser.addTabGroup([tab1]);
  group.collapsed = true;

  await openGroupPreview(group);

  const previewPanel = window.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );

  Assert.equal(
    previewPanel.state,
    "open",
    "sanity check: tab group preview panel is open"
  );

  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewPanel,
    "hidden"
  );
  EventUtils.synthesizeMouseAtCenter(group.labelElement, {});
  await previewHidden;

  Assert.equal(
    previewPanel.state,
    "closed",
    "tab group preview panel closes on tab label click"
  );

  await removeTabGroup(group);
  await resetState();
});

add_task(async function tabGroupPanelClickElementSwitchesTabs() {
  const tabs = [
    BrowserTestUtils.addTab(gBrowser, "about:robots"),
    BrowserTestUtils.addTab(gBrowser, "about:mozilla"),
  ];
  const group = gBrowser.addTabGroup(tabs);
  group.collapsed = true;

  Assert.equal(
    gBrowser.selectedTab,
    gBrowser.tabs[0],
    "Selected tab is first tab on tab strip before clicking the panel item"
  );

  await openGroupPreview(group);
  const previewPanel = window.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );
  const panelContent = previewPanel.querySelector("#tabgroup-panel-content");

  let tabSelectEvent = BrowserTestUtils.waitForEvent(group, "TabSelect");
  panelContent.children[1].click();
  await tabSelectEvent;

  Assert.equal(
    gBrowser.selectedTab,
    gBrowser.tabs[2],
    "Selected tab is second tab within the tab group after clicking panel item"
  );
  Assert.ok(
    group.collapsed,
    "Group is still in collapsed state even though it has the active tab"
  );

  await removeTabGroup(group);
  await resetState();
});

// bug1983054: The panel moves correctly when moving between two adjacent tab groups
add_task(async function moveBetweenTabGroupsTests() {
  const tab1 = await addTabTo(gBrowser, "about:robots");
  const group1 = gBrowser.addTabGroup([tab1]);
  group1.collapsed = true;

  const tab2 = await addTabTo(gBrowser, "about:blank");
  const group2 = gBrowser.addTabGroup([tab2]);
  group2.collapsed = true;

  const previewPanel = window.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );

  await openGroupPreview(group1);
  await TestUtils.waitForCondition(
    () => previewPanel.anchorNode.parentElement == group1,
    "Panel is anchored to group 1"
  );
  Assert.equal(
    previewPanel.anchorNode.parentElement,
    group1,
    "Panel is anchored to group 1"
  );

  await openGroupPreview(group2);
  await TestUtils.waitForCondition(
    () => previewPanel.anchorNode.parentElement == group2,
    "Panel is anchored to group 2"
  );
  Assert.equal(
    previewPanel.anchorNode.parentElement,
    group2,
    "Panel is anchored to group 2"
  );

  await removeTabGroup(group1);
  await removeTabGroup(group2);
  await resetState();
});

add_task(async function tabGroupPanelHorizontalOffsetOptions() {
  // The vertical-tabs variant enables sidebar.verticalTabs for the whole run.
  await SpecialPowers.pushPrefEnv({ set: [["sidebar.verticalTabs", false]] });

  const testCases = [
    {
      prefs: [["browser.nova.enabled", false]],
      expected: { position: "bottomleft topleft", x: 0, y: 0 },
      message: "Without Nova there is no offset",
    },
    {
      prefs: [["browser.nova.enabled", true]],
      expected: { position: "bottomleft topleft", x: -16, y: 0 },
      message: "LTR Nova anchors to the left edge and offsets by -16px",
    },
    {
      prefs: [
        ["browser.nova.enabled", true],
        ["intl.l10n.pseudo", "bidi"],
      ],
      expected: { position: "bottomright topright", x: 16, y: 0 },
      message: "RTL Nova anchors to the right edge and offsets by +16px",
    },
  ];

  await withGroupPanel(async groupPanel => {
    info("Horizontal Tab Group Panel Offset");
    for (const instance of testCases) {
      await assertPopupOptions(groupPanel, instance);
    }
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function tabGroupPanelHorizontalOpensWithOffset() {
  // The vertical-tabs variant enables sidebar.verticalTabs for the whole run.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.verticalTabs", false],
      ["browser.nova.enabled", true],
    ],
  });

  await withGroupPanel(async (groupPanel, group) => {
    const openPopupSpy = sinon.spy(groupPanel.panelElement, "openPopup");
    await openGroupPreview(group);

    Assert.ok(
      openPopupSpy.calledWith(
        group.labelContainerElement,
        sinon.match({ position: "bottomleft topleft", x: -16, y: 0 })
      ),
      "openPopup is called anchored to the label with the Nova offset"
    );

    await closeGroupPreviews();
    sinon.restore();
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function tabGroupPanelVerticalOffsetOptions() {
  const testCases = [
    {
      nova: false,
      start: true,
      expected: { position: "topright topleft", x: 0, y: -5 },
      message: "Nova off, sidebar at start: keeps the 5px upward offset",
    },
    {
      nova: false,
      start: false,
      expected: { position: "topleft topright", x: 0, y: -5 },
      message: "Nova off, sidebar at end: 5px offset, anchor mirrored",
    },
    {
      nova: true,
      start: true,
      expected: { position: "topright topleft", x: 0, y: -16 },
      message: "Nova on, sidebar at start: 16px upward offset",
    },
    {
      nova: true,
      start: false,
      expected: { position: "topleft topright", x: 0, y: -16 },
      message: "Nova on, sidebar at end: 16px offset, anchor mirrored",
    },
  ];
  await withGroupPanel(async groupPanel => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["sidebar.revamp", true],
        ["sidebar.verticalTabs", true],
      ],
    });
    await TestUtils.waitForCondition(
      () => gBrowser.tabContainer.verticalMode,
      "Tabs are in vertical mode"
    );

    for (const { nova, start, expected, message } of testCases) {
      await assertPopupOptions(groupPanel, {
        prefs: [
          ["browser.nova.enabled", nova],
          ["sidebar.position_start", start],
        ],
        expected,
        message,
      });
    }

    await SpecialPowers.popPrefEnv();
  });
});

add_task(async function tabGroupPanelUpdatesTests() {
  const groupedTab = await addTabTo(gBrowser, "about:robots");
  const group = gBrowser.addTabGroup([groupedTab]);
  group.collapsed = true;

  const previewPanel = window.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );
  const panelContent = previewPanel.querySelector("#tabgroup-panel-content");

  await openGroupPreview(group);
  Assert.equal(panelContent.children.length, 1, "Panel has one tab");
  Assert.equal(
    panelContent.children[0].tab,
    groupedTab,
    "toolbarbutton is associated with the correct tab"
  );

  info(
    "Test that changes to the document title cause the panel tab's title to change"
  );
  let tabRenameEvent = BrowserTestUtils.waitForEvent(
    groupedTab,
    "TabAttrModified"
  );
  let newTitle = "Extremely cool and good website title";
  await SpecialPowers.spawn(
    groupedTab.linkedBrowser,
    [newTitle],
    async newTitleInContentProcess => {
      content.document.title = newTitleInContentProcess;
    }
  );
  await tabRenameEvent;
  Assert.equal(
    panelContent.children[0].label,
    newTitle,
    "Panel toolbarbutton has updated label"
  );

  await closeGroupPreviews();

  info("Test that adding a tab to the group adds the tab to the group's panel");
  let TabOpenEvent = BrowserTestUtils.waitForEvent(group, "TabOpen");
  let newTab = await addTabTo(gBrowser, "about:robots", { tabGroup: group });
  await TabOpenEvent;

  // Re-collapse the group, which was uncollapsed when the new tab was added
  group.collapsed = true;
  await openGroupPreview(group);

  Assert.equal(
    panelContent.children.length,
    2,
    "Panel has two tabs after tab open"
  );
  Assert.equal(
    panelContent.children[1].tab,
    newTab,
    "New toolbarbutton is associated with the new tab"
  );

  info(
    "Test that closing a tab within the group removes the tab from the group's panel"
  );
  let TabCloseEvent = BrowserTestUtils.waitForEvent(group, "TabClose");
  BrowserTestUtils.removeTab(newTab);
  await TabCloseEvent;

  Assert.equal(
    panelContent.children.length,
    1,
    "Panel has one tab after tab close"
  );
  Assert.equal(
    panelContent.children[0].tab,
    groupedTab,
    "toolbarbutton is associated with the original tab"
  );

  info(
    "Test that moving a tab into the group adds the tab to the group's panel"
  );
  newTab = await addTabTo(gBrowser, "about:robots");

  let tabGroupedEvent = BrowserTestUtils.waitForEvent(group, "TabGrouped");
  gBrowser.moveTabToExistingGroup(newTab, group);
  await tabGroupedEvent;

  Assert.equal(panelContent.children.length, 2, "Panel has two tabs");
  Assert.equal(
    panelContent.children[1].tab,
    newTab,
    "Newly grouped tab is associated with the second toolbarbutton"
  );

  info("Test that moving tabs within the group updates the panel");
  let tabMoveEvent = BrowserTestUtils.waitForEvent(group, "TabMove");
  let tabToMove = group.tabs[1];
  gBrowser.moveTabTo(tabToMove, { tabIndex: tabToMove._tPos - 1 });
  await tabMoveEvent;

  Assert.equal(
    panelContent.children[0].tab,
    newTab,
    "New tab has moved to first position in the preview panel"
  );
  Assert.equal(
    panelContent.children[1].tab,
    groupedTab,
    "Original tab has moved to second position in the preview panel"
  );

  info(
    "Test that selecting a tab within the tab group updates the active tab in the group's panel"
  );
  let tabToSelect = group.tabs[1];
  let tabSelectEvent = BrowserTestUtils.waitForEvent(tabToSelect, "TabSelect");
  gBrowser.selectedTab = tabToSelect;
  await tabSelectEvent;

  Assert.ok(
    panelContent.children[1].classList.contains("active-tab"),
    "Selected tab has the active tab class set"
  );

  info(
    "Test that removing a tab from the group removes the tab from the group's panel"
  );
  let tabUngroupedEvent = BrowserTestUtils.waitForEvent(group, "TabUngrouped");
  gBrowser.ungroupTab(newTab);
  await tabUngroupedEvent;

  Assert.equal(panelContent.children.length, 1, "Panel has one tab");
  Assert.equal(
    panelContent.children[0].tab,
    groupedTab,
    "Tab in the panel is the original tab"
  );

  BrowserTestUtils.removeTab(newTab);
  await removeTabGroup(group);
  await resetState();
});

/*
 * Shared tests
 * ------------
 */

// Bug 1897475 - don't show tab previews in background windows
// TODO Bug 1899556: If possible, write a test to confirm tab previews
// aren't shown when /all/ windows are in the background
add_task(async function noPreviewInBackgroundWindowTests() {
  todo(false, "test is failing on CI, bug 2006695");

  /*
  const bgWindow = window;
  const bgTabUngrouped = await addTab("about:robots");
  const bgTabGrouped = await addTab("about:robots");
  const bgGroup = gBrowser.addTabGroup([bgTabGrouped]);
  bgGroup.collapsed = true;

  // tab must be opened at least once to ensure that bgWindow tab preview lazy loads
  await openTabPreview(bgTabUngrouped, bgWindow);
  await closeTabPreviews(bgWindow);

  const bgPreviewComponent = bgWindow.gBrowser.tabContainer.previewPanel;
  sinon.spy(bgPreviewComponent, "activate");

  let fgWindow = await BrowserTestUtils.openNewBrowserWindow();
  let fgTab = fgWindow.gBrowser.tabs[0];
  let fgWindowPreviewContainer =
    fgWindow.document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(fgTab, fgWindow);
  Assert.equal(
    fgWindowPreviewContainer.querySelector(".tab-preview-title").innerText,
    "New Tab",
    "Preview of foreground tab shows correct title"
  );
  await closeTabPreviews(fgWindow);

  // ensure ungrouped tab's preview doesn't open, as it's now in a background window
  EventUtils.synthesizeMouseAtCenter(
    bgTabUngrouped,
    { type: "mouseover" },
    bgWindow
  );
  await TestUtils.waitForCondition(() => {
    return bgPreviewComponent.activate.calledOnce;
  }, "Waiting for activate to be called on bgPreviewComponent after hovering ungrouped tab");
  Assert.equal(
    bgPreviewComponent.tabPanel.panelElement.state,
    "closed",
    "preview does not open from background window"
  );

  bgPreviewComponent.activate.resetHistory();
  Assert.ok(
    !bgPreviewComponent.activate.calledOnce,
    "sanity check that spy has no history"
  );

  // ensure group's preview doesn't open, as it's now in a background window
  EventUtils.synthesizeMouseAtCenter(
    bgGroup.labelElement,
    { type: "mouseover" },
    bgWindow
  );
  await TestUtils.waitForCondition(() => {
    return bgPreviewComponent.activate.calledOnce;
  }, "Waiting for activate to be called on bgPreviewComponent after hovering grouped tab label");
  Assert.equal(
    bgPreviewComponent.tabGroupPanel.panelElement.state,
    "closed",
    "preview does not open from background window"
  );

  BrowserTestUtils.removeTab(fgTab);
  await BrowserTestUtils.closeWindow(fgWindow);

  BrowserTestUtils.removeTab(bgTabUngrouped);
  BrowserTestUtils.removeTab(bgTabGrouped);

  sinon.restore();
  await resetState();
  */
});

/**
 * make sure delay is applied when mouse leaves tabstrip
 * but not when moving between tabs on the tabstrip
 */
add_task(async function delayTests() {
  const tabUrl1 =
    "data:text/html,<html><head><title>First New Tab</title></head><body>Hello</body></html>";
  const tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl1);
  const tabUrl2 =
    "data:text/html,<html><head><title>Second New Tab</title></head><body>Hello</body></html>";
  const tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl2);
  const previewElement = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab1);

  const previewComponent = gBrowser.tabContainer.previewPanel;
  sinon.spy(previewComponent, "deactivate");

  // I can't fake this like in hoverTests, need to send an updated-tab signal
  //await openPreview(tab2);

  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewElement,
    "hidden"
  );
  Assert.ok(
    !previewComponent.deactivate.called,
    "Delay is not reset when moving between tabs"
  );

  EventUtils.synthesizeMouseAtCenter(document.getElementById("back-button"), {
    type: "mousemove",
  });

  await previewHidden;

  Assert.ok(
    previewComponent.deactivate.called,
    "Delay is reset when cursor leaves tabstrip"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  sinon.restore();
  await resetState();
});

/**
 * Quickly moving the mouse off and back on to the tab strip should
 * not reset the delay
 */
add_task(async function zeroDelayTests() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["ui.tooltip.delay_ms", 100],
      ["ui.prefersReducedMotion", 1],
    ],
  });

  const tabUrl =
    "data:text/html,<html><head><title>First New Tab</title></head><body>Hello</body></html>";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, tabUrl);
  const previewComponent = gBrowser.tabContainer.previewPanel;

  await openTabPreview(tab);
  await closeTabPreviews();

  Assert.ok(
    previewComponent.panelOpener.zeroDelayActive,
    "Zero delay is active immediately after the preview is dismissed"
  );

  // Inlined rather than using openTabPreview() so we can assert the zero-delay
  // window is still active at the moment the open is scheduled
  const panel = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const previewShown = BrowserTestUtils.waitForPopupEvent(panel, "shown");
  const previewUpdated = BrowserTestUtils.waitForEvent(
    panel,
    "TabPreviewUpdated"
  );
  EventUtils.synthesizeMouse(tab, 1, 1, { type: "mouseover" });
  Assert.ok(
    previewComponent.panelOpener.zeroDelayActive,
    "Second hover is scheduled while still in the zero-delay window"
  );
  await Promise.all([previewShown, previewUpdated]);
  Assert.equal(panel.state, "open", "Panel opened on the second hover");

  await closeTabPreviews();

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
  await resetState();
});

/**
 * Dragging a tab or a tab group should deactivate the preview
 */
add_task(async function testDragToCancelPreview() {
  await SpecialPowers.pushPrefEnv({
    set: [["ui.tooltip.delay_ms", 1000]],
  });

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const previewElement = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab);

  const previewComponent = gBrowser.tabContainer.previewPanel;
  sinon.spy(previewComponent, "deactivate");

  let previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewElement,
    "hidden"
  );
  let dragend = BrowserTestUtils.waitForEvent(tab, "dragend");

  EventUtils.synthesizePlainDragAndDrop({
    srcElement: tab,
    destElement: null,
    stepX: 5,
    stepY: 0,
  });

  await previewHidden;

  Assert.ok(
    previewComponent.deactivate.called,
    "deactivate is called after tab drag started"
  );

  await dragend;

  const group = gBrowser.addTabGroup([tab]);
  group.collapsed = true;
  await openGroupPreview(group);

  previewComponent.deactivate.resetHistory();

  const groupPreviewElement = document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );

  previewHidden = BrowserTestUtils.waitForPopupEvent(
    groupPreviewElement,
    "hidden"
  );
  dragend = BrowserTestUtils.waitForEvent(group.labelElement, "dragend");
  const groupLabelRect = group.labelElement.getBoundingClientRect();
  EventUtils.synthesizePlainDragAndDrop({
    srcElement: group.labelElement,
    srcX: Math.floor(groupLabelRect.width / 2),
    srcY: Math.floor(groupLabelRect.height / 2),
    destElement: null,
    stepX: 10,
    stepY: 0,
  });
  await dragend;
  await previewHidden;

  Assert.ok(
    previewComponent.deactivate.called,
    "deactivate is called after group drag started"
  );

  // TODO not sure why I need to explicitly wait for this, but the drag tests fail without it
  await TestUtils.waitForCondition(
    () => !previewElement.getAttribute("animating")
  );

  await resetState();
  BrowserTestUtils.removeTab(tab);
  sinon.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(async function tabPreviewHidesWhenDraggingOverPanel() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const previewElement = document.getElementById(TAB_PREVIEW_PANEL_ID);

  await openTabPreview(tab);

  const previewHidden = BrowserTestUtils.waitForPopupEvent(
    previewElement,
    "hidden"
  );
  const dragend = BrowserTestUtils.waitForEvent(tab, "dragend");

  EventUtils.synthesizePlainDragAndDrop({
    srcElement: tab,
    destElement: null,
    stepX: 10,
    stepY: 0,
  });

  await previewHidden;
  Assert.equal(
    previewElement.state,
    "closed",
    "Preview closes when dragging downward over the panel"
  );

  await dragend;

  BrowserTestUtils.removeTab(tab);
  await resetState();

  const groupTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const group = gBrowser.addTabGroup([groupTab]);
  group.collapsed = true;

  await openGroupPreview(group);

  const groupPreviewElement = document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );
  const groupPreviewHidden = BrowserTestUtils.waitForPopupEvent(
    groupPreviewElement,
    "hidden"
  );
  const groupDragend = BrowserTestUtils.waitForEvent(
    group.labelElement,
    "dragend"
  );

  const groupLabelRect = group.labelElement.getBoundingClientRect();
  EventUtils.synthesizePlainDragAndDrop({
    srcElement: group.labelElement,
    srcX: Math.floor(groupLabelRect.width / 2),
    srcY: Math.floor(groupLabelRect.height / 2),
    destElement: null,
    stepX: 10,
    stepY: 0,
  });

  await groupPreviewHidden;
  Assert.equal(
    groupPreviewElement.state,
    "closed",
    "Group preview closes when dragging downward over the panel"
  );

  await groupDragend;

  BrowserTestUtils.removeTab(groupTab);
  await resetState();
});

/**
 * Other open panels should prevent tab preview from opening
 */
add_task(async function panelSuppressionOnPanelTests() {
  const ungroupedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const groupedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const group = gBrowser.addTabGroup([groupedTab]);
  group.collapsed = true;

  // tab preview must be opened at least once to ensure that tab preview lazy loads
  await openTabPreview(ungroupedTab);
  await closeTabPreviews();

  const previewComponent = gBrowser.tabContainer.previewPanel;
  sinon.spy(previewComponent, "activate");

  let fakePanel = createFakePanel();
  const popupShownEvent = BrowserTestUtils.waitForPopupEvent(
    fakePanel,
    "shown"
  );
  fakePanel.openPopup();
  await popupShownEvent;

  EventUtils.synthesizeMouseAtCenter(
    ungroupedTab,
    { type: "mouseover" },
    window
  );
  await TestUtils.waitForCondition(() => {
    return previewComponent.activate.calledOnce;
  });
  Assert.equal(previewComponent.tabPanel.panelElement.state, "closed", "");
  Assert.equal(previewComponent.tabGroupPanel.panelElement.state, "closed", "");

  previewComponent.activate.resetHistory();
  Assert.ok(
    !previewComponent.activate.called,
    "sanity check that spy has no history"
  );

  EventUtils.synthesizeMouseAtCenter(
    group.labelElement,
    { type: "mouseover" },
    window
  );
  await TestUtils.waitForCondition(() => {
    return previewComponent.activate.calledOnce;
  });
  Assert.equal(previewComponent.tabPanel.panelElement.state, "closed", "");
  Assert.equal(previewComponent.tabGroupPanel.panelElement.state, "closed", "");

  const popupHiddenEvent = BrowserTestUtils.waitForPopupEvent(
    fakePanel,
    "hidden"
  );
  fakePanel.hidePopup();
  await popupHiddenEvent;

  BrowserTestUtils.removeTab(ungroupedTab);
  BrowserTestUtils.removeTab(groupedTab);
  fakePanel.remove();
  sinon.restore();
  await resetState();
});

/**
 * Other open context menus should prevent tab preview from opening
 */
add_task(async function panelSuppressionOnContextMenuTests() {
  const ungroupedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const groupedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  const group = gBrowser.addTabGroup([groupedTab]);
  group.collapsed = true;

  // tab preview must be opened at least once to ensure that tab preview lazy loads
  await openTabPreview(ungroupedTab);
  await closeTabPreviews();

  const previewComponent = gBrowser.tabContainer.previewPanel;
  sinon.spy(previewComponent, "activate");

  // Open a context menu.
  const contentAreaContextMenu = document.getElementById(
    "contentAreaContextMenu"
  );
  const contextMenuShown = BrowserTestUtils.waitForPopupEvent(
    contentAreaContextMenu,
    "shown"
  );
  EventUtils.synthesizeMouseAtCenter(
    document.documentElement,
    { type: "contextmenu" },
    window
  );
  await contextMenuShown;

  // Mouse over a tab.
  EventUtils.synthesizeMouseAtCenter(
    ungroupedTab,
    { type: "mouseover" },
    window
  );
  await TestUtils.waitForCondition(() => {
    return previewComponent.activate.called;
  });
  Assert.equal(previewComponent.tabPanel.panelElement.state, "closed", "");
  Assert.equal(previewComponent.tabGroupPanel.panelElement.state, "closed", "");

  previewComponent.activate.resetHistory();
  Assert.ok(
    !previewComponent.activate.calledOnce,
    "sanity check that spy has no history"
  );

  // Mouse over a tab group.
  EventUtils.synthesizeMouseAtCenter(
    group.labelElement,
    { type: "mouseover" },
    window
  );
  await TestUtils.waitForCondition(() => {
    return previewComponent.activate.called;
  });
  Assert.equal(previewComponent.tabPanel.panelElement.state, "closed", "");
  Assert.equal(previewComponent.tabGroupPanel.panelElement.state, "closed", "");

  const contextMenuHidden = BrowserTestUtils.waitForPopupEvent(
    contentAreaContextMenu,
    "hidden"
  );
  contentAreaContextMenu.hidePopup();
  await contextMenuHidden;

  BrowserTestUtils.removeTab(ungroupedTab);
  BrowserTestUtils.removeTab(groupedTab);
  sinon.restore();
  await resetState();
});

/**
 * Ensure that the panel does not open when other panels are active or are in
 * the process of being activated, when THP is being called for the first time
 * (lazy-loaded)
 */
add_task(async function panelSuppressionOnPanelLazyLoadTests() {
  // This needs to be done in a new window to ensure that
  // the previewPanel is being loaded for the first time
  let fgWindow = await BrowserTestUtils.openNewBrowserWindow();
  let fgTab = fgWindow.gBrowser.tabs[0];

  let fakePanel = createFakePanel(fgWindow);
  const popupShownEvent = BrowserTestUtils.waitForPopupEvent(
    fakePanel,
    "shown"
  );
  fakePanel.openPopup();
  await popupShownEvent;

  EventUtils.synthesizeMouseAtCenter(fgTab, { type: "mouseover" }, fgWindow);

  await TestUtils.waitForCondition(() => {
    // Sometimes the tests run slower than the test browser -- it's not always possible
    // to catch the panel in its opening state, so we have to check for both states.
    return (
      (fakePanel.getAttribute("animating") === "true" ||
        fakePanel.getAttribute("panelopen") === "true") &&
      fgWindow.gBrowser.tabContainer.previewPanel !== null
    );
  });
  const previewComponent = fgWindow.gBrowser.tabContainer.previewPanel;

  // We can't spy on the previewComponent and check for calls to `activate` like in other tests,
  // since we can't guarantee that the spy will be set up before the call is made.
  // Therefore the only reliable way to test that the popup isn't open is to reach in and check
  // that it is in a disabled state.
  Assert.equal(
    previewComponent.shouldActivate(),
    false,
    "Preview component is disabled"
  );

  // Reset state: close the popup and move the mouse off the tab
  const tabs = fgWindow.document.getElementById("tabbrowser-tabs");
  const tabsRect = tabs.getBoundingClientRect();
  EventUtils.synthesizeMouse(
    tabs,
    0,
    tabsRect.height + 1,
    {
      type: "mouseout",
    },
    fgWindow
  );

  const popupHiddenEvent = BrowserTestUtils.waitForPopupEvent(
    fakePanel,
    "hidden"
  );
  fakePanel.hidePopup();
  await popupHiddenEvent;

  BrowserTestUtils.removeTab(fgTab);
  fakePanel.remove();
  await BrowserTestUtils.closeWindow(fgWindow);
  await resetState();
});

/**
 * Test that if the panel is opened and is subject to a UI delay, and another
 * panel opens before the delay expires, the panel does not open.
 */
add_task(
  async function panelSuppressionWhenOtherPanelsOpeningDuringDelayTests() {
    // This test verifies timing behavior that can't practically be tested in
    // chaos mode.
    if (parseInt(Services.env.get("MOZ_CHAOSMODE"), 16)) {
      return;
    }

    await SpecialPowers.pushPrefEnv({ set: [["ui.tooltip.delay_ms", 500]] });

    // Without this, the spies would be dependent on this task coming after the
    // above tasks. Set up the preview panel manually if necessary, to make the
    // task fully independent.
    let previewComponent = new TabHoverPanelSet(window);
    gBrowser.tabContainer.previewPanel = previewComponent;

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:robots"
    );

    sinon.spy(previewComponent.panelOpener, "execute");
    sinon.spy(previewComponent.tabPanel.panelElement, "openPopup");

    // Start the timer...
    EventUtils.synthesizeMouseAtCenter(tab, { type: "mouseover" });

    await TestUtils.waitForCondition(
      () => previewComponent.panelOpener.execute.calledOnce,
      "panelOpener execute called"
    );
    Assert.ok(previewComponent.panelOpener.delayActive, "Timer is set");

    let fakePanel = createFakePanel();
    const popupShownEvent = BrowserTestUtils.waitForPopupEvent(
      fakePanel,
      "shown"
    );
    fakePanel.openPopup();
    await popupShownEvent;

    // Wait for timer to finish...
    await TestUtils.waitForCondition(() => {
      return previewComponent.panelOpener._timer == null;
    }, "panelOpener timer finished");
    await TestUtils.waitForTick();

    // As a popup was already open, the preview panel should not have opened.
    Assert.strictEqual(
      previewComponent.tabPanel.panelElement.state,
      "closed",
      "Panel is closed"
    );
    Assert.ok(
      previewComponent.tabPanel.panelElement.openPopup.notCalled,
      "openPopup was not invoked"
    );

    const popupHiddenEvent = BrowserTestUtils.waitForPopupEvent(
      fakePanel,
      "hidden"
    );
    fakePanel.hidePopup();
    await popupHiddenEvent;

    fakePanel.remove();
    BrowserTestUtils.removeTab(tab);
    sinon.restore();
    await SpecialPowers.popPrefEnv();
    await resetState();
  }
);

/**
 * In vertical tabs mode, previews should be displayed to the side
 * and not beneath the tab.
 */
// TODO bug1981197: Modify to support tab group preview
add_task(async function verticalTabsPositioningTests() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
      ["ui.prefersReducedMotion", 1],
    ],
  });

  const previewPanel = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  await openTabPreview(tab);

  let tabRect = tab.getBoundingClientRect();
  let panelRect = previewPanel.getBoundingClientRect();

  Assert.less(
    Math.abs(tabRect.top - panelRect.top),
    5,
    "Preview panel not displayed beneath tab"
  );

  await closeTabPreviews();
  BrowserTestUtils.removeTab(tab);
  await resetState();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that if tabs and tab groups are on the tab strip together,
 * hovering over one and then the other displays the correct preview.
 */
add_task(async function testTabAndTabGroupsWorkTogether() {
  const tabToLeft = await addTabTo(gBrowser, "about:robots");
  const tabInGroup = await addTabTo(gBrowser, "about:robots");
  const group = gBrowser.addTabGroup([tabInGroup]);
  group.collapsed = true;
  const tabToRight = await addTabTo(gBrowser, "about:robots");

  const tabPreviewElement = document.getElementById(TAB_PREVIEW_PANEL_ID);
  const groupPreviewElement = document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );

  let tabPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    tabPreviewElement,
    "shown"
  );
  let groupPreviewEvent;
  EventUtils.synthesizeMouseAtCenter(tabToLeft, {
    type: "mouseover",
  });
  await tabPreviewEvent;
  Assert.equal(
    tabPreviewElement.state,
    "open",
    "Tab panel is open after hovering over left tab"
  );
  Assert.equal(
    groupPreviewElement.state,
    "closed",
    "Group panel is closed after hovering over left tab"
  );

  tabPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    tabPreviewElement,
    "hidden"
  );
  groupPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    groupPreviewElement,
    "shown"
  );
  EventUtils.synthesizeMouseAtCenter(group.labelElement, {
    type: "mouseover",
  });
  await tabPreviewEvent;
  await groupPreviewEvent;
  Assert.equal(
    tabPreviewElement.state,
    "closed",
    "Tab panel is closed after hovering over group label"
  );
  Assert.equal(
    groupPreviewElement.state,
    "open",
    "Group panel is open after hovering over group label"
  );

  tabPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    tabPreviewElement,
    "shown"
  );
  groupPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    groupPreviewElement,
    "hidden"
  );
  EventUtils.synthesizeMouseAtCenter(tabToRight, {
    type: "mouseover",
  });
  await tabPreviewEvent;
  await groupPreviewEvent;
  Assert.equal(
    tabPreviewElement.state,
    "open",
    "Tab panel is open after hovering over right tab"
  );
  Assert.equal(
    groupPreviewElement.state,
    "closed",
    "Group panel is closed after hovering over right tab"
  );

  tabPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    tabPreviewElement,
    "hidden"
  );
  groupPreviewEvent = BrowserTestUtils.waitForPopupEvent(
    groupPreviewElement,
    "shown"
  );
  EventUtils.synthesizeMouseAtCenter(group.labelElement, {
    type: "mouseover",
  });
  await tabPreviewEvent;
  await groupPreviewEvent;
  Assert.equal(
    tabPreviewElement.state,
    "closed",
    "Tab panel is closed after hovering over group label"
  );
  Assert.equal(
    groupPreviewElement.state,
    "open",
    "Group panel is open after hovering over group label"
  );

  await resetState();
  BrowserTestUtils.removeTab(tabToLeft);
  BrowserTestUtils.removeTab(tabToRight);
  await removeTabGroup(group);
});

add_task(async function testTabGroupHoverPreviewTelemetry() {
  const previewPanel = window.document.getElementById(
    TAB_GROUP_PREVIEW_PANEL_ID
  );
  let tabGroups = [];

  for (let i = 0; i < 5; i++) {
    const tab = await addTabTo(gBrowser, `data:text/plain,tab${i + 1}`);
    const tabGroup = gBrowser.addTabGroup([tab], { label: `group${i + 1}` });
    await TabGroupTestUtils.toggleCollapsed(tabGroup, true);
    tabGroups.push(tabGroup);
  }

  Assert.ok(
    !Glean.tabgroup.groupInteractions.hover_preview.testGetValue(),
    "hover preview interaction count should start out not set"
  );

  let interactionCount = 1;

  for (const tabGroup of tabGroups) {
    await openGroupPreview(tabGroup);
    await TestUtils.waitForCondition(
      () => previewPanel.anchorNode?.parentElement == tabGroup,
      "panel re-anchored to the next tab group"
    );
    await TestUtils.waitForCondition(
      () =>
        Glean.tabgroup.groupInteractions.hover_preview.testGetValue() ==
        interactionCount,
      `hover preview interaction count incremented`
    );
    Assert.equal(
      Glean.tabgroup.groupInteractions.hover_preview.testGetValue(),
      interactionCount,
      `hover preview interaction count should be ${interactionCount}`
    );
    interactionCount++;
  }

  await Promise.all(
    tabGroups.map(tabGroup => TabGroupTestUtils.removeTabGroup(tabGroup))
  );

  TabGroupTestUtils.forgetSavedTabGroups();
  await resetState();
});

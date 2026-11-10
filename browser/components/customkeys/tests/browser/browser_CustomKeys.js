/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { CustomKeys } = ChromeUtils.importESModule(
  "moz-src:///browser/components/customkeys/CustomKeys.sys.mjs"
);

/**
 * Test the behavior of assigning keys, resetting keys, etc. via the CustomKeys module. The UI is tested separately.
 */

add_setup(async function () {
  // We need to be sure that the foreground tab has fully loaded before running
  // any tests. Otherwise, when keyboard events are sent to the remote process,
  // there is a race condition where they sometimes won't be handled again in
  // the parent process afterward.
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  registerCleanupFunction(function () {
    CustomKeys.resetAll();
    BrowserTestUtils.removeTab(tab);
  });
});

// Test changing a key.
add_task(async function testChangeKey() {
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Tab document browser is focused"
  );

  is(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    null,
    "key_gotoHistory is not customized"
  );
  info(`Changing key_gotoHistory to ${consts.unusedDisplay}`);
  CustomKeys.changeKey("key_gotoHistory", {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });
  Assert.deepEqual(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    { modifiers: consts.historyModifiers, key: "H" },
    "key_gotoHistory is customized"
  );
  info(`Pressing ${consts.unusedDisplay}`);
  let focused = BrowserTestUtils.waitForEvent(window, "SidebarFocused");
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  await focused;
  ok(true, "Sidebar got focus");
  is(
    SidebarController.currentID,
    "viewHistorySidebar",
    "History sidebar is open"
  );
  info(`Pressing ${consts.unusedDisplay}`);
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  await focused;
  ok(true, "Tab document browser got focus");

  is(
    CustomKeys.getDefaultKey("viewBookmarksSidebarKb"),
    null,
    "viewBookmarksSidebarKb is not customized"
  );
  info(`Changing viewBookmarksSidebarKb to ${consts.historyDisplay}`);
  CustomKeys.changeKey("viewBookmarksSidebarKb", {
    modifiers: consts.historyModifiers,
    key: "H",
  });
  Assert.deepEqual(
    CustomKeys.getDefaultKey("viewBookmarksSidebarKb"),
    { modifiers: "accel", key: "B" },
    "viewBookmarksSidebarKb is customized"
  );
  info(`Pressing ${consts.historyDisplay}`);
  focused = BrowserTestUtils.waitForEvent(window, "SidebarFocused");
  EventUtils.synthesizeKey("H", consts.historyOptions, window);
  await focused;
  ok(true, "Sidebar got focus");
  is(
    SidebarController.currentID,
    "viewBookmarksSidebar",
    "Bookmarks sidebar is open"
  );
  info(`Pressing ${consts.historyDisplay}`);
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  EventUtils.synthesizeKey("H", consts.historyOptions, window);
  await focused;
  ok(true, "Tab document browser got focus");

  info("Resetting all keys");
  CustomKeys.resetAll();
});

// Test that a customization applies to a key element added to an existing
// keyset.
add_task(async function testAddedKey() {
  const id = "testAddedKey";
  CustomKeys.changeKey(id, {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });
  const keyEl = document.createXULElement("key");
  keyEl.id = id;
  document.getElementById("mainKeyset").append(keyEl);
  await BrowserTestUtils.waitForMutationCondition(
    keyEl,
    { attributeFilter: ["key"] },
    () => keyEl.getAttribute("key") == consts.unusedKey
  );
  info(`Pressing ${consts.unusedDisplay}`);
  const command = BrowserTestUtils.waitForEvent(keyEl, "command");
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  await command;
  ok(true, "The added key ran with its customized shortcut");
  keyEl.remove();
  CustomKeys.resetKey(id);
});

// Test clearing a key.
add_task(async function testClearKey() {
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Tab document browser is focused"
  );
  // Move focus into chrome so that accel+l to focus the URL bar reliably occurs
  // immediately. We need this guarantee because we want to test when pressing
  // accel+l does nothing, so we can't rely on an event for that test.
  info("Focusing selected tab");
  let focused = BrowserTestUtils.waitForEvent(gBrowser.selectedTab, "focus");
  gBrowser.selectedTab.focus();
  await focused;
  ok(true, "Selected tab got focus");

  is(
    CustomKeys.getDefaultKey("focusURLBar"),
    null,
    "focusURLBar is not customized"
  );
  info("Pressing accel+L");
  EventUtils.synthesizeKey("L", { accelKey: true }, window);
  is(document.activeElement, gURLBar.inputField, "URL bar is focused");
  info("Focusing selected tab");
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedTab, "focus");
  gBrowser.selectedTab.focus();
  await focused;
  ok(true, "Selected tab got focus");

  info("Clearing focusURLBar");
  CustomKeys.clearKey("focusURLBar");
  Assert.deepEqual(
    CustomKeys.getDefaultKey("focusURLBar"),
    { modifiers: "accel", key: "L" },
    "focusURLBar is customized"
  );
  info("Pressing accel+L");
  EventUtils.synthesizeKey("L", { accelKey: true }, window);
  is(
    document.activeElement,
    gBrowser.selectedTab,
    "Selected tab still focused"
  );

  // The tab bar has focus now. We need to move focus back to the document
  // because otherwise, the focus will remain on the tab bar for the next test.
  info("Focusing tab document browser");
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  gBrowser.selectedBrowser.focus();
  await focused;
  ok(true, "Tab document browser got focus");
  info("Resetting all keys");
  CustomKeys.resetAll();
});

// Test resetting a key.
add_task(async function testResetKey() {
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Tab document browser is focused"
  );

  is(
    CustomKeys.getDefaultKey("focusURLBar"),
    null,
    "focusURLBar is not customized"
  );
  info("Clearing focusURLBar");
  CustomKeys.clearKey("focusURLBar");
  Assert.deepEqual(
    CustomKeys.getDefaultKey("focusURLBar"),
    { modifiers: "accel", key: "L" },
    "focusURLBar is customized"
  );

  info("Resetting focusURLBar");
  CustomKeys.resetKey("focusURLBar");
  is(
    CustomKeys.getDefaultKey("focusURLBar"),
    null,
    "focusURLBar is not customized"
  );
  info("Pressing accel+L");
  let focused = BrowserTestUtils.waitForEvent(gURLBar.inputField, "focus");
  EventUtils.synthesizeKey("L", { accelKey: true }, window);
  await focused;
  ok(true, "URL bar got focus");
  info("Focusing tab document browser");
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  gBrowser.selectedBrowser.focus();
  await focused;
  ok(true, "Tab document browser got focus");

  info("Resetting all keys");
  CustomKeys.resetAll();
});

// Test resetting all keys.
add_task(async function testResetAll() {
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Tab document browser is focused"
  );

  is(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    null,
    "key_gotoHistory is not customized"
  );
  info(`Changing key_gotoHistory to ${consts.unusedDisplay}`);
  CustomKeys.changeKey("key_gotoHistory", {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });
  Assert.deepEqual(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    { modifiers: consts.historyModifiers, key: "H" },
    "key_gotoHistory is customized"
  );

  is(
    CustomKeys.getDefaultKey("focusURLBar"),
    null,
    "focusURLBar is not customized"
  );
  info("Clearing focusURLBar");
  CustomKeys.clearKey("focusURLBar");
  Assert.deepEqual(
    CustomKeys.getDefaultKey("focusURLBar"),
    { modifiers: "accel", key: "L" },
    "focusURLBar is customized"
  );

  info("Resetting all keys");
  CustomKeys.resetAll();

  info(`Pressing ${consts.historyDisplay}`);
  let focused = BrowserTestUtils.waitForEvent(window, "SidebarFocused");
  EventUtils.synthesizeKey("H", consts.historyOptions, window);
  await focused;
  ok(true, "Sidebar got focus");
  is(
    SidebarController.currentID,
    "viewHistorySidebar",
    "History sidebar is open"
  );
  info(`Pressing ${consts.historyDisplay}`);
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  EventUtils.synthesizeKey("H", consts.historyOptions, window);
  await focused;
  ok(true, "Tab document browser got focus");

  info("Pressing accel+L");
  focused = BrowserTestUtils.waitForEvent(gURLBar.inputField, "focus");
  EventUtils.synthesizeKey("L", { accelKey: true }, window);
  await focused;
  ok(true, "URL bar got focus");
  info("Focusing tab document browser");
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  gBrowser.selectedBrowser.focus();
  await focused;
  ok(true, "Tab document browser got focus");
});

// Test that clearing all keys clears shortcuts that haven't been customized.
add_task(async function testClearAll() {
  // Move focus into chrome so that accel+l to focus the URL bar reliably occurs
  // immediately. We need this guarantee because we want to test when pressing
  // accel+l does nothing, so we can't rely on an event for that test.
  info("Focusing selected tab");
  let focused = BrowserTestUtils.waitForEvent(gBrowser.selectedTab, "focus");
  gBrowser.selectedTab.focus();
  await focused;

  is(
    CustomKeys.getDefaultKey("focusURLBar"),
    null,
    "focusURLBar is not customized"
  );
  // key_duplicateTab has no default shortcut, so clearing it is a no-op and it
  // must not be reported as customized.
  is(
    CustomKeys.getDefaultKey("key_duplicateTab"),
    null,
    "key_duplicateTab is not customized"
  );

  const copyKeyEl = document.getElementById("key_copy");
  const copyKey = copyKeyEl.getAttribute("key");
  ok(copyKey, "key_copy has a key attribute");

  info("Clearing all keys");
  CustomKeys.clearAll();
  Assert.deepEqual(
    CustomKeys.getDefaultKey("focusURLBar"),
    { modifiers: "accel", key: "L" },
    "focusURLBar is cleared"
  );
  is(
    CustomKeys.getDefaultKey("key_duplicateTab"),
    null,
    "key_duplicateTab is still not customized"
  );
  is(CustomKeys.getDefaultKey("key_copy"), null, "key_copy is not customized");
  is(
    copyKeyEl.getAttribute("key"),
    copyKey,
    "key_copy still has its key attribute"
  );

  info("Pressing accel+L");
  EventUtils.synthesizeKey("L", { accelKey: true }, window);
  is(
    document.activeElement,
    gBrowser.selectedTab,
    "Selected tab still focused"
  );

  info("Resetting all keys");
  CustomKeys.resetAll();
  // Restore focus to the document so the next test's precondition holds.
  focused = BrowserTestUtils.waitForEvent(gBrowser.selectedBrowser, "focus");
  gBrowser.selectedBrowser.focus();
  await focused;
});

// Test that key_duplicateTab (which has no default binding) can be assigned
// and triggers tab duplication.
add_task(async function testDuplicateTab() {
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Tab document browser is focused"
  );

  is(
    CustomKeys.getDefaultKey("key_duplicateTab"),
    null,
    "key_duplicateTab is not customized"
  );
  info(`Assigning key_duplicateTab to ${consts.unusedDisplay}`);
  CustomKeys.changeKey("key_duplicateTab", {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });

  const originalTab = gBrowser.selectedTab;
  const originalUrl = originalTab.linkedBrowser.currentURI.spec;
  const newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    originalUrl,
    true
  );
  info(`Pressing ${consts.unusedDisplay}`);
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  const newTab = await newTabPromise;
  isnot(newTab, originalTab, "The new tab is different from the original");

  BrowserTestUtils.removeTab(newTab);
  info("Resetting all keys");
  CustomKeys.resetAll();
});

add_task(async function testCopyTabURL() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.query_stripping.strip_on_share.enabled", true],
      ["privacy.query_stripping.enabled", false],
    ],
  });
  await Cc["@mozilla.org/query-stripping-list-service;1"]
    .getService(Ci.nsIURLQueryStrippingListService)
    .testWaitForInit();
  CustomKeys.changeKey("key_copyTabURL", {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });
  try {
    const url = "https://example.com/?copy=tab#fragment";
    const trackedURL =
      "https://example.com/?utm_source=shortcut&copy=tab&utm_medium=share&utm_campaign=test#fragment";
    await BrowserTestUtils.withNewTab(trackedURL, async () => {
      await SimpleTest.promiseClipboardChange(url, () =>
        EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window)
      );
      await BrowserTestUtils.withNewTab("about:robots", async () => {
        await SimpleTest.promiseClipboardChange("about:robots", () =>
          EventUtils.synthesizeKey(
            consts.unusedKey,
            consts.unusedOptions,
            window
          )
        );
      });
    });
  } finally {
    CustomKeys.resetKey("key_copyTabURL");
  }
});

// Test that changes apply to other windows.
add_task(async function testOtherWindow() {
  // Test changing a key before a new window is opened.
  is(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    null,
    "key_gotoHistory is not customized"
  );
  info(`Changing key_gotoHistory to ${consts.unusedDisplay}`);
  CustomKeys.changeKey("key_gotoHistory", {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });
  Assert.deepEqual(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    { modifiers: consts.historyModifiers, key: "H" },
    "key_gotoHistory is customized"
  );

  info("Opening new window");
  const newWin = await BrowserTestUtils.openNewBrowserWindow();
  is(
    newWin.document.activeElement,
    newWin.gURLBar.inputField,
    "URL bar is focused in new window"
  );

  info(`Pressing ${consts.unusedDisplay} in new window`);
  let focused = BrowserTestUtils.waitForEvent(newWin, "SidebarFocused");
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, newWin);
  await focused;
  ok(true, "Sidebar got focus in new window");
  is(
    newWin.SidebarController.currentID,
    "viewHistorySidebar",
    "History sidebar is open in new window"
  );
  info(`Pressing ${consts.unusedDisplay} in new window`);
  focused = BrowserTestUtils.waitForEvent(
    newWin.gBrowser.selectedBrowser,
    "focus"
  );
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, newWin);
  await focused;
  ok(true, "Tab document browser got focus in new window");

  // Test changing a key after a new window is opened.
  is(
    CustomKeys.getDefaultKey("viewBookmarksSidebarKb"),
    null,
    "viewBookmarksSidebarKb is not customized"
  );
  info(`Changing viewBookmarksSidebarKb to ${consts.historyDisplay}`);
  CustomKeys.changeKey("viewBookmarksSidebarKb", {
    modifiers: consts.historyModifiers,
    key: "H",
  });
  Assert.deepEqual(
    CustomKeys.getDefaultKey("viewBookmarksSidebarKb"),
    { modifiers: "accel", key: "B" },
    "viewBookmarksSidebarKb is customized"
  );
  info(`Pressing ${consts.historyDisplay} in new window`);
  focused = BrowserTestUtils.waitForEvent(newWin, "SidebarFocused");
  EventUtils.synthesizeKey("H", consts.historyOptions, newWin);
  await focused;
  ok(true, "Sidebar got focus in new window");
  is(
    newWin.SidebarController.currentID,
    "viewBookmarksSidebar",
    "Bookmarks sidebar is open"
  );
  info(`Pressing ${consts.historyDisplay} in new window`);
  focused = BrowserTestUtils.waitForEvent(
    newWin.gBrowser.selectedBrowser,
    "focus"
  );
  EventUtils.synthesizeKey("H", consts.historyOptions, newWin);
  await focused;
  ok(true, "Tab document browser got focus in new window");

  // Test resetting keys after a new window is opened.
  info("Resetting all keys");
  CustomKeys.resetAll();

  is(
    CustomKeys.getDefaultKey("key_gotoHistory"),
    null,
    "key_gotoHistory is not customized"
  );
  info(`Pressing ${consts.historyDisplay} in new window`);
  focused = BrowserTestUtils.waitForEvent(newWin, "SidebarFocused");
  EventUtils.synthesizeKey("H", consts.historyOptions, newWin);
  await focused;
  ok(true, "Sidebar got focus in new window");
  is(
    newWin.SidebarController.currentID,
    "viewHistorySidebar",
    "History sidebar is open in new window"
  );
  info(`Pressing ${consts.historyDisplay} in new window`);
  focused = BrowserTestUtils.waitForEvent(
    newWin.gBrowser.selectedBrowser,
    "focus"
  );
  EventUtils.synthesizeKey("H", consts.historyOptions, newWin);
  await focused;
  ok(true, "Tab document browser got focus in new window");

  info("Closing new window");
  await BrowserTestUtils.closeWindow(newWin);
});

// Test a key whose shortcut the operating system assigned, as the user can do
// for a menu item in macOS System Settings. The assignment sits above a
// customization: the customization goes into the built-in layer and applies
// once the assignment is gone. The key element used here is not shown by any
// menu item, so that on macOS the widget code, which mirrors real assignments
// for menu items, leaves the simulated one alone.
add_task(async function testSystemAssignedKey() {
  const keyEl = document.getElementById("key_selectTab1");
  const originalKey = keyEl.getAttribute("key");
  const originalModifiers = keyEl.getAttribute("modifiers");
  is(originalKey, "1", "key_selectTab1 selects with the 1 key");

  simulateSystemAssignment(keyEl, "accel,alt,shift", "Y");
  ok(CustomKeys.isSystemAssigned(keyEl), "key_selectTab1 is system assigned");
  is(
    keyEl.getAttribute("data-builtin-key"),
    originalKey,
    "The built-in key is kept"
  );
  is(
    keyEl.getAttribute("data-builtin-modifiers"),
    originalModifiers,
    "The built-in modifiers are kept"
  );

  info(`Customizing key_selectTab1 to ${consts.unusedDisplay} while assigned`);
  CustomKeys.changeKey("key_selectTab1", {
    modifiers: consts.unusedModifiers,
    key: consts.unusedKey,
  });
  Assert.deepEqual(
    CustomKeys.getDefaultKey("key_selectTab1"),
    { modifiers: originalModifiers, key: originalKey },
    "The default is the built-in shortcut, not the assigned one"
  );
  is(keyEl.getAttribute("key"), "Y", "The assigned key is untouched");
  is(
    keyEl.getAttribute("modifiers"),
    "accel,alt,shift",
    "The assigned modifiers are untouched"
  );
  is(
    keyEl.getAttribute("data-builtin-key"),
    consts.unusedKey,
    "The customization went into the built-in key"
  );
  is(
    keyEl.getAttribute("data-builtin-modifiers"),
    consts.unusedModifiers,
    "The customization went into the built-in modifiers"
  );

  info("Removing the assignment");
  simulateSystemAssignmentRemoval(keyEl);
  ok(
    !CustomKeys.isSystemAssigned(keyEl),
    "key_selectTab1 is no longer assigned"
  );
  is(keyEl.getAttribute("key"), consts.unusedKey, "The customization applies");
  is(
    keyEl.getAttribute("modifiers"),
    consts.unusedModifiers,
    "The customized modifiers apply"
  );
  isnot(
    gBrowser.selectedTab,
    gBrowser.tabs[0],
    "The first tab is not selected"
  );
  info(`Pressing ${consts.unusedDisplay}`);
  const selected = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabSelect"
  );
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  await selected;
  is(gBrowser.selectedTab, gBrowser.tabs[0], "The first tab is selected");
  gBrowser.selectedTab = gBrowser.tabs[gBrowser.tabs.length - 1];

  info("Assigning again and resetting the customization underneath");
  simulateSystemAssignment(keyEl, "accel,alt,shift", "Y");
  CustomKeys.resetKey("key_selectTab1");
  is(
    CustomKeys.getDefaultKey("key_selectTab1"),
    null,
    "key_selectTab1 is no longer customized"
  );
  is(keyEl.getAttribute("key"), "Y", "The assigned key is still untouched");
  is(
    keyEl.getAttribute("data-builtin-key"),
    originalKey,
    "The built-in key is back"
  );
  is(
    keyEl.getAttribute("data-builtin-modifiers"),
    originalModifiers,
    "The built-in modifiers are back"
  );
  simulateSystemAssignmentRemoval(keyEl);
  is(keyEl.getAttribute("key"), originalKey, "The original key is back");
  is(
    keyEl.getAttribute("modifiers"),
    originalModifiers,
    "The original modifiers are back"
  );
});

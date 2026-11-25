/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_VALUE = "http://example.com/\xF7?\xF7";
const START_VALUE = "http://example.com/%C3%B7?%C3%B7";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.scotchBonnet.enableOverride", false]],
  });
  const engine = await SearchTestUtils.installOpenSearchEngine({
    url: getRootDirectory(gTestPath) + "searchSuggestionEngine.xml",
    setAsDefault: true,
  });
  engine.alias = "@default";
});

add_task(async function returnKeypress() {
  info("Simple return keypress");
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, START_VALUE);

  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter");
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  // Check url bar and selected tab.
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "Urlbar should preserve the value on return keypress"
  );
  is(gBrowser.selectedTab, tab, "New URL was loaded in the current tab");

  // Cleanup.
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function altReturnKeypress() {
  info("Alt+Return keypress");
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, START_VALUE);

  let tabOpenPromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabOpen"
  );
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter", { altKey: true });

  // wait for the new tab to appear.
  await tabOpenPromise;

  // Check url bar and selected tab.
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "Urlbar should preserve the value on return keypress"
  );
  isnot(gBrowser.selectedTab, tab, "New URL was loaded in a new tab");

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function altGrReturnKeypress() {
  info("AltGr+Return keypress");
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, START_VALUE);

  let tabOpenPromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabOpen"
  );
  gURLBar.focus();
  EventUtils.synthesizeKey("KEY_Enter", { altGraphKey: true });

  // wait for the new tab to appear.
  await tabOpenPromise;

  // Check url bar and selected tab.
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "Urlbar should preserve the value on return keypress"
  );
  isnot(gBrowser.selectedTab, tab, "New URL was loaded in a new tab");

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function searchOnEnterNoPick() {
  info("Search on Enter without picking a urlbar result");
  await SpecialPowers.pushPrefEnv({
    // The test checks that the untrimmed value is equal to the spec.
    // When using showSearchTerms, the untrimmed value becomes
    // the search terms.
    set: [["browser.urlbar.showSearchTerms.featureGate", false]],
  });

  // Why is BrowserTestUtils.openNewForegroundTab not causing the bug?
  let promiseTabOpened = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabOpen"
  );
  EventUtils.synthesizeMouseAtCenter(gBrowser.tabContainer.newTabButton, {});
  let openEvent = await promiseTabOpened;
  let tab = openEvent.target;

  let loadPromise = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    null,
    true
  );
  gURLBar.focus();
  gURLBar.value = "test test";
  EventUtils.synthesizeKey("KEY_Enter");
  await loadPromise;

  Assert.ok(
    gBrowser.selectedBrowser.currentURI.spec.endsWith("test+test"),
    "Should have loaded the correct page"
  );
  Assert.equal(
    gBrowser.selectedBrowser.currentURI.spec,
    gURLBar.untrimmedValue,
    "The location should have changed"
  );

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function searchOnEnterSoon() {
  info("Search on Enter as soon as typing a char");
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    START_VALUE
  );

  const onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  const onPageHide = SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    return new Promise(resolve => {
      content.window.addEventListener("pagehide", () => {
        resolve();
      });
    });
  });
  const onResult = SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    return new Promise(resolve => {
      content.window.addEventListener("keyup", () => {
        resolve("keyup");
      });
      content.window.addEventListener("unload", () => {
        resolve("unload");
      });
    });
  });

  // Focus on the input field in urlbar.
  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  const ownerDocument = gBrowser.selectedBrowser.ownerDocument;
  is(
    ownerDocument.activeElement,
    gURLBar.inputField,
    "The input field in urlbar has focus"
  );

  info("Keydown a char and Enter");
  EventUtils.synthesizeKey("x", { type: "keydown" });
  EventUtils.synthesizeKey("KEY_Enter", { type: "keydown" });

  // Wait for pagehide event in the content.
  await onPageHide;
  is(
    ownerDocument.activeElement,
    gURLBar.inputField,
    "The input field in urlbar still has focus"
  );

  // Keyup both key as soon as pagehide event happens.
  EventUtils.synthesizeKey("x", { type: "keyup" });
  EventUtils.synthesizeKey("KEY_Enter", { type: "keyup" });

  // Wait for moving the focus.
  await TestUtils.waitForCondition(
    () => ownerDocument.activeElement === gBrowser.selectedBrowser
  );
  info("The focus is moved to the browser");

  // Check whether keyup event is not captured before unload event happens.
  const result = await onResult;
  is(result, "unload", "Keyup event is not captured.");

  // Cleanup.
  await onLoad;
  BrowserTestUtils.removeTab(tab);
});

add_task(async function searchByMultipleEnters() {
  info("Search on Enter after selecting the search engine by Enter");
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    START_VALUE
  );

  info("Select a search engine by Enter key");
  gURLBar.focus();
  gURLBar.select();
  EventUtils.sendString("@default");
  EventUtils.synthesizeKey("KEY_Enter");
  await TestUtils.waitForCondition(
    () => gURLBar.searchMode,
    "Wait until entering search mode"
  );
  await UrlbarTestUtils.assertSearchMode(window, {
    engineName: "browser_searchSuggestionEngine searchSuggestionEngine.xml",
    entry: "keywordoffer",
  });
  const ownerDocument = gBrowser.selectedBrowser.ownerDocument;
  is(
    ownerDocument.activeElement,
    gURLBar.inputField,
    "The input field in urlbar has focus"
  );

  info("Search by Enter key");
  const onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.sendString("mozilla");
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoad;
  is(
    ownerDocument.activeElement,
    gBrowser.selectedBrowser,
    "The focus is moved to the browser"
  );

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
});

// Make sure the caret stays at the end after enter keydown, but
// goes to the beginning when the browser is focused (Bug 1676054).
add_task(async function goToBeginningAfterKeyup() {
  info("Search on Enter, keeping the key down");
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    START_VALUE
  );
  const url = "https://example.com/some/url";

  EventUtils.synthesizeMouseAtCenter(gURLBar.inputField, {});
  gURLBar.value = url;
  EventUtils.synthesizeKey("KEY_Enter", { type: "keydown" });

  // Check the caret position a bit after keydown.
  // It should still be at the end
  await TestUtils.waitForTick();
  Assert.equal(
    gURLBar.selectionStart,
    gURLBar.value.length,
    "The selectionStart indicates at ending of the value"
  );

  // Check the caret position again after keyup when the browser is focused.
  // It should now be at the beginning for spoof protection and usability.
  // The parent focuses the browser and replies, so the caret moves a hop after.
  EventUtils.synthesizeKey("KEY_Enter", { type: "keyup" });
  await TestUtils.waitForCondition(
    () => !gURLBar.selectionStart && !gURLBar.selectionEnd,
    "The caret moved to the beginning of the value"
  );

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
});

add_task(async function typeCharWhileProcessingEnter() {
  info("Typing a char while processing enter key");
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    START_VALUE
  );

  const onLoad = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    START_VALUE
  );
  gURLBar.focus();

  info("Keydown Enter");
  EventUtils.synthesizeKey("KEY_Enter", { type: "keydown" });
  await TestUtils.waitForCondition(
    () => gURLBar._keyDownEnterDeferred,
    "Wait for starting process for the enter key"
  );

  info("Keydown a char");
  EventUtils.synthesizeKey("x", { type: "keydown" });

  info("Keyup both");
  EventUtils.synthesizeKey("x", { type: "keyup" });
  EventUtils.synthesizeKey("KEY_Enter", { type: "keyup" });

  Assert.equal(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "The value of urlbar is correct"
  );

  await onLoad;
  Assert.ok("Browser loaded the correct url");

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
});

add_task(async function keyupEnterWhilePressingMeta() {
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  info("Keydown Meta+Enter");
  gURLBar.focus();
  gURLBar.value = "";
  EventUtils.synthesizeKey("KEY_Enter", { type: "keydown", metaKey: true });

  // Pressing Enter key while pressing Meta key, and next, even when releasing
  // Enter key before releasing Meta key, the keyup event is not fired.
  // Therefor, we fire Meta keyup event only.
  info("Keyup Meta");
  EventUtils.synthesizeKey("KEY_Meta", { type: "keyup" });

  // Check whether we can input on URL bar.
  EventUtils.synthesizeKey("a");
  is(gURLBar.value, "a", "Can input a char");

  // Cleanup.
  BrowserTestUtils.removeTab(tab);
});

add_task(async function closeTabRightAfterEnter() {
  info("Closing the tab right after Enter doesn't load in another tab");
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    START_VALUE
  );
  const sandbox = sinon.createSandbox();
  const loadURLSpy = sandbox.spy(gURLBar.parentController, "loadURL");

  gURLBar.focus();
  gURLBar.value = "https://example.com/some/url";
  EventUtils.synthesizeKey("KEY_Enter");
  Assert.ok(loadURLSpy.calledOnce, "Enter started the load");

  BrowserTestUtils.removeTab(tab);
  await loadURLSpy.firstCall.returnValue;
  Assert.equal(
    gBrowser.selectedBrowser.userTypedValue,
    null,
    "The load didn't go to the newly selected tab"
  );

  sandbox.restore();
});

add_task(async function repeatedEnterLoadsOnce() {
  info("Holding Enter loads only once");
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, START_VALUE);
  let loadURLSpy = sinon.spy(gURLBar.parentController, "loadURL");

  gURLBar.focus();
  gURLBar.value = "https://example.com/some/url";
  let onLoad = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  EventUtils.synthesizeKey("KEY_Enter", { repeat: 3 });
  await onLoad;
  Assert.ok(loadURLSpy.calledOnce, "Repeated keydowns started one load");

  BrowserTestUtils.removeTab(tab);
  loadURLSpy.restore();
});

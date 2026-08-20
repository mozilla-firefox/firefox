const PREF_WARN_ON_CLOSE = "browser.tabs.warnOnCloseOtherTabs";
const PREF_SHOWN_DUPE_DIALOG =
  "browser.tabs.haveShownCloseAllDuplicateTabsWarning";

add_setup(async function setPref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      [PREF_WARN_ON_CLOSE, false],
      [PREF_SHOWN_DUPE_DIALOG, true],
    ],
  });
});

add_task(async function withAMultiSelectedTab() {
  let initialTab = gBrowser.selectedTab;
  let tab1 = await addTab();
  let tab2 = await addTab();
  let tab3 = await addTab();
  let tab4 = await addTab();

  is(gBrowser.multiSelectedTabsCount, 0, "Zero multiselected tabs");

  await triggerClickOn(tab1, { ctrlKey: true });

  let tab4Pinned = BrowserTestUtils.waitForEvent(tab4, "TabPinned");
  gBrowser.pinTab(tab4);
  await tab4Pinned;

  ok(initialTab.multiselected, "InitialTab is multiselected");
  ok(tab1.multiselected, "Tab1 is multiselected");
  ok(!tab2.multiselected, "Tab2 is not multiselected");
  ok(!tab3.multiselected, "Tab3 is not multiselected");
  ok(!tab4.multiselected, "Tab4 is not multiselected");
  ok(tab4.pinned, "Tab4 is pinned");
  is(gBrowser.multiSelectedTabsCount, 2, "Two multiselected tabs");
  is(gBrowser.selectedTab, initialTab, "InitialTab is the active tab");

  let closingTabs = [tab2, tab3];
  let tabClosingPromises = [];
  for (let tab of closingTabs) {
    tabClosingPromises.push(BrowserTestUtils.waitForTabClosing(tab));
  }

  gBrowser.removeDuplicateTabs(tab1);

  await Promise.all(tabClosingPromises);

  ok(!initialTab.closing, "InitialTab is not closing");
  ok(!tab1.closing, "Tab1 is not closing");
  ok(tab2.closing, "Tab2 is closing");
  ok(tab3.closing, "Tab3 is closing");
  ok(!tab4.closing, "Tab4 is not closing");
  is(gBrowser.multiSelectedTabsCount, 2, "Two multiselected tabs");
  is(gBrowser.selectedTab, initialTab, "InitialTab is still the active tab");

  gBrowser.clearMultiSelectedTabs();
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab4);
});

add_task(async function withNotAMultiSelectedTab() {
  let initialTab = gBrowser.selectedTab;
  let tab1 = await addTab("http://mochi.test:8888/");
  let tab2 = await addTab("http://mochi.test:8888/");
  let tab3 = await addTab("http://mochi.test:8888/");
  let tab4 = await addTab("http://mochi.test:8888/");
  let tab5 = await addTab("http://mochi.test:8888/");
  let tab6 = await addTab("http://mochi.test:8888/", { userContextId: 1 });

  is(gBrowser.multiSelectedTabsCount, 0, "Zero multiselected tabs");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  await triggerClickOn(tab2, { ctrlKey: true });
  await triggerClickOn(tab5, { ctrlKey: true });

  let tab4Pinned = BrowserTestUtils.waitForEvent(tab4, "TabPinned");
  gBrowser.pinTab(tab4);
  await tab4Pinned;

  let tab5Pinned = BrowserTestUtils.waitForEvent(tab5, "TabPinned");
  gBrowser.pinTab(tab5);
  await tab5Pinned;

  ok(!initialTab.multiselected, "InitialTab is not multiselected");
  ok(tab1.multiselected, "Tab1 is multiselected");
  ok(tab2.multiselected, "Tab2 is multiselected");
  ok(!tab3.multiselected, "Tab3 is not multiselected");
  ok(!tab4.multiselected, "Tab4 is not multiselected");
  ok(tab4.pinned, "Tab4 is pinned");
  ok(tab5.multiselected, "Tab5 is multiselected");
  ok(tab5.pinned, "Tab5 is pinned");
  ok(!tab6.multiselected, "Tab6 is not multiselected");
  ok(!tab6.pinned, "Tab6 is not pinned");
  is(gBrowser.multiSelectedTabsCount, 3, "Three multiselected tabs");
  is(gBrowser.selectedTab, tab1, "Tab1 is the active tab");

  let closingTabs = [tab1, tab2];
  let tabClosingPromises = [];
  for (let tab of closingTabs) {
    tabClosingPromises.push(BrowserTestUtils.waitForTabClosing(tab));
  }

  await BrowserTestUtils.switchTab(
    gBrowser,
    gBrowser.removeDuplicateTabs(tab3)
  );

  await Promise.all(tabClosingPromises);

  ok(!initialTab.closing, "InitialTab is not closing");
  ok(tab1.closing, "Tab1 is closing");
  ok(tab2.closing, "Tab2 is closing");
  ok(!tab3.closing, "Tab3 is not closing");
  ok(!tab4.closing, "Tab4 is not closing");
  ok(!tab5.closing, "Tab5 is not closing");
  ok(!tab6.closing, "Tab6 is not closing");
  is(
    gBrowser.multiSelectedTabsCount,
    0,
    "Zero multiselected tabs, selection is cleared"
  );
  is(gBrowser.selectedTab, tab3, "tab3 is the active tab now");

  for (let tab of [tab3, tab4, tab5, tab6]) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function closeAllDuplicateTabs() {
  let initialTab = gBrowser.selectedTab;
  let tab1 = await addTab("http://mochi.test:8888/one");
  let tab2 = await addTab("http://mochi.test:8888/two", { userContextId: 1 });
  let tab3 = await addTab("http://mochi.test:8888/one");
  let tab4 = await addTab("http://mochi.test:8888/two");
  let tab5 = await addTab("http://mochi.test:8888/one");
  let tab6 = await addTab("http://mochi.test:8888/two");

  let tab1Pinned = BrowserTestUtils.waitForEvent(tab1, "TabPinned");
  gBrowser.pinTab(tab1);
  await tab1Pinned;

  // So we have 1p,2c,1,2,1,2
  // We expect 1p,2c,X,2,X,X because the pinned 1 will dupe the other two 1,
  // but the 2c's userContextId makes it unique against the other two 2,
  // but one of the other two 2 will close.

  // Ensure tab4 remains by making it active more recently than tab6.
  tab4._lastSeenActive = Date.now(); // as recent as it gets.

  // Assert some preconditions:
  ok(tab1.pinned, "Tab1 is pinned");
  Assert.greater(tab4.lastSeenActive, tab6.lastSeenActive);

  let closingTabs = [tab3, tab5, tab6];
  let tabClosingPromises = [];
  for (let tab of closingTabs) {
    tabClosingPromises.push(BrowserTestUtils.waitForTabClosing(tab));
  }

  await BrowserTestUtils.switchTab(gBrowser, gBrowser.removeAllDuplicateTabs());

  await Promise.all(tabClosingPromises);

  ok(!initialTab.closing, "InitialTab is not closing");
  ok(!tab1.closing, "Tab1 is not closing");
  ok(!tab2.closing, "Tab2 is not closing");
  ok(tab3.closing, "Tab3 is closing");
  ok(!tab4.closing, "Tab4 is not closing");
  ok(tab5.closing, "Tab5 is closing");
  ok(tab6.closing, "Tab6 is closing");

  for (let tab of [tab1, tab2, tab4]) {
    BrowserTestUtils.removeTab(tab);
  }
});

/**
 * Tests that if we choose Close Duplicate Tabs, we warn for the close of a
 * single duplicate tab.
 */
add_task(async function closeAllDuplicateTabs_warn_on_single() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_SHOWN_DUPE_DIALOG, false]],
  });

  let initialTab = gBrowser.selectedTab;
  let tab1 = await addTab("http://mochi.test:8888/one");
  let tab2 = await addTab("http://mochi.test:8888/two");
  let tab3 = await addTab("http://mochi.test:8888/one");

  let tabClosedPromise = BrowserTestUtils.waitForTabClosing(tab3);
  let dialogOpenedPromise = BrowserTestUtils.promiseAlertDialogOpen("accept");

  gBrowser.removeAllDuplicateTabs();

  await dialogOpenedPromise;
  await tabClosedPromise;

  ok(!initialTab.closing, "InitialTab is not closing");
  ok(!tab1.closing, "Tab1 is not closing");
  ok(!tab2.closing, "Tab2 is not closing");
  ok(tab3.closing, "Tab3 is closing");

  for (let tab of [tab1, tab2]) {
    BrowserTestUtils.removeTab(tab);
  }

  Assert.ok(
    Services.prefs.getBoolPref(PREF_SHOWN_DUPE_DIALOG),
    "Should have set the PREF_SHOWN_DUPE_DIALOG pref to true"
  );

  await SpecialPowers.popPrefEnv();
});

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test QuickActions.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  UpdateService: "resource://gre/modules/UpdateService.sys.mjs",
  ActionsProviderQuickActions:
    "moz-src:///browser/components/urlbar/ActionsProviderQuickActions.sys.mjs",
});

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/urlbar/tests/browser-tips/head.js",
  this
);

const DUMMY_PAGE =
  "https://example.com/browser/browser/base/content/test/general/dummy_page.html";

let testActionCalled = 0;

const assertAction = async name => {
  await BrowserTestUtils.waitForCondition(() =>
    window.document.querySelector(`.urlbarView-action-btn[data-action=${name}]`)
  );
  Assert.ok(true, `We found action "${name}`);
};

const assertAccessibilityWhenSelected = name => {
  let button = document.querySelector(
    `.urlbarView-action-btn[data-action=${name}]`
  );
  Assert.ok(button.id);
  Assert.equal(
    gURLBar.inputField.getAttribute("aria-activedescendant"),
    button.id
  );
};

const hasQuickActions = win =>
  !!win.document.querySelector(".urlbarView-action-btn");

const onboardingLabelShown = win =>
  !!win.document.querySelector(".urlbarView-press-tab-label");

async function enterActionsMode() {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@act",
  });
  EventUtils.synthesizeKey("KEY_Tab");
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.ACTIONS,
    entry: "keywordoffer",
    restrictType: "keyword",
  });
}

async function exitActionsMode() {
  await UrlbarTestUtils.promisePopupClose(window, () => {
    window.gURLBar.searchMode = null;
    EventUtils.synthesizeKey("KEY_Escape");
  });
}

const TEST_TOP_SITE = "https://example.com";

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["browser.urlbar.quickactions.enabled", true],
      ["browser.urlbar.scotchBonnet.enableOverride", true],
      ["browser.urlbar.suggest.topsites", true],
      ["browser.newtabpage.activity-stream.default.sites", TEST_TOP_SITE],
    ],
  });

  await updateTopSites(sites => sites?.length == 1);

  ActionsProviderQuickActions.addAction("testaction", {
    commands: ["testaction"],
    label: "quickactions-downloads2",
    onPick: () => testActionCalled++,
  });
  ActionsProviderQuickActions.addAction("othertestaction", {
    commands: ["othertestaction"],
    label: "quickactions-downloads2",
    onPick: () => {},
  });

  registerCleanupFunction(() => {
    ActionsProviderQuickActions.removeAction("testaction");
    ActionsProviderQuickActions.removeAction("othertestaction");
  });
});

add_task(async function basic() {
  info("The action isnt shown when not matched");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "nomatch",
  });
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    1,
    "We did no match anything"
  );

  info("A prefix of the command matches");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "testact",
  });

  await assertAction("testaction");

  info("The callback of the action is fired when selected");
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  assertAccessibilityWhenSelected("testaction");
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  Assert.equal(testActionCalled, 1, "Test action was called");
});

add_task(async function match_in_phrase() {
  ActionsProviderQuickActions.addAction("newtestaction", {
    commands: ["matchingstring"],
    label: "quickactions-downloads2",
  });

  info("The action is matched when at end of input");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "Test we match at end of matchingstring",
  });
  await assertAction("newtestaction");
  await UrlbarTestUtils.promisePopupClose(window);
  EventUtils.synthesizeKey("KEY_Escape");
  ActionsProviderQuickActions.removeAction("newtestaction");
});

add_task(async function test_viewsource() {
  info("Check the button status of when the page is not web content");
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "viewsource",
  });

  info("Do view source action");
  const onLoad = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "view-source:https://example.com/"
  );
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  assertAccessibilityWhenSelected("viewsource");
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  const viewSourceTab = await onLoad;

  info("Do view source action on the view-source page");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "viewsource",
  });

  Assert.equal(
    window.document.querySelector(
      `.urlbarView-action-btn[data-action=viewsource]`
    ),
    null,
    "Result for quick actions is hidden"
  );

  // Clean up.
  BrowserTestUtils.removeTab(viewSourceTab);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testAfterTabSwitch() {
  let tab1 = gBrowser.selectedTab;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "testaction",
  });
  await assertAction("testaction");

  let tab2 = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "othertestaction",
  });
  await assertAction("othertestaction");

  await BrowserTestUtils.switchTab(gBrowser, tab1);
  info("Testing if quick action in tab 1 still works.");
  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  assertAccessibilityWhenSelected("testaction");
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  Assert.equal(testActionCalled, 2, "Test action was called");

  BrowserTestUtils.removeTab(tab2);
});

async function doAlertDialogTest({ input, dialogContentURI }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: input,
  });

  const onDialog = BrowserTestUtils.promiseAlertDialog(null, null, {
    isSubDialog: true,
    callback: win => {
      Assert.equal(win.location.href, dialogContentURI, "The dialog is opened");
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
    },
  });

  EventUtils.synthesizeKey("KEY_Tab", {}, window);
  EventUtils.synthesizeKey("KEY_Enter", {}, window);

  await onDialog;
}

add_task(async function test_refresh() {
  // Refresh should be disabled because we are not in a named profile yet
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "refresh",
  });
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    1,
    "We did not match anything"
  );

  // Make this a named profile so we can refresh
  await makeProfileResettable();

  await doAlertDialogTest({
    input: "refresh",
    dialogContentURI: "chrome://global/content/resetProfile.xhtml",
  });
});

add_task(async function test_clear() {
  await doAlertDialogTest({
    input: "clear",
    dialogContentURI: "chrome://browser/content/sanitize_v2.xhtml",
  });
});

async function doUpdateActionTest(isActiveExpected) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "update",
  });

  if (isActiveExpected) {
    await assertAction("update");
  } else {
    Assert.equal(hasQuickActions(window), false, "No QuickActions were shown");
  }
}

add_task(async function test_update() {
  if (!AppConstants.MOZ_UPDATER) {
    await doUpdateActionTest(
      false,
      "Should be disabled since not AppConstants.MOZ_UPDATER"
    );
    return;
  }

  const sandbox = sinon.createSandbox();
  try {
    sandbox
      .stub(UpdateService.prototype, "currentState")
      .get(() => Ci.nsIApplicationUpdateService.STATE_IDLE);
    await doUpdateActionTest(
      false,
      "Should be disabled since current update state is not pending"
    );
    sandbox
      .stub(UpdateService.prototype, "currentState")
      .get(() => Ci.nsIApplicationUpdateService.STATE_PENDING);
    await doUpdateActionTest(
      true,
      "Should be enabled since current update state is pending"
    );
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_update_in_actions_mode() {
  if (!AppConstants.MOZ_UPDATER) {
    return;
  }

  const sandbox = sinon.createSandbox();
  let currentState = Ci.nsIApplicationUpdateService.STATE_IDLE;
  sandbox.stub(UpdateService.prototype, "currentState").get(() => currentState);

  try {
    await enterActionsMode();
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "update",
    });
    let updateButton = await BrowserTestUtils.waitForCondition(() =>
      window.document.querySelector(
        `.urlbarView-action-btn[data-action=update]`
      )
    );
    Assert.ok(
      updateButton.hasAttribute("disabled"),
      "Update action is shown as disabled in actions mode when no update is pending"
    );
    await exitActionsMode();

    currentState = Ci.nsIApplicationUpdateService.STATE_PENDING;
    await enterActionsMode();
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "update",
    });
    updateButton = await BrowserTestUtils.waitForCondition(() =>
      window.document.querySelector(
        `.urlbarView-action-btn[data-action=update]`
      )
    );
    Assert.ok(
      !updateButton.hasAttribute("disabled"),
      "Update action is shown as enabled in actions mode when update is pending"
    );
  } finally {
    sandbox.restore();
    await exitActionsMode();
  }
});

add_task(async function test_whitespace() {
  info("Test with quickactions.showInZeroPrefix pref is false");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quickactions.showInZeroPrefix", false]],
  });
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: " ",
  });
  Assert.equal(
    hasQuickActions(window),
    false,
    "Result for quick actions is not shown"
  );
  await SpecialPowers.popPrefEnv();

  info("Test with quickactions.showInZeroPrefix pref is true");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quickactions.showInZeroPrefix", true]],
  });
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });
  const countForEmpty = window.document.querySelectorAll(
    ".urlbarView-action-btn"
  ).length;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: " ",
  });
  const countForWhitespace = window.document.querySelectorAll(
    ".urlbarView-action-btn"
  ).length;
  Assert.equal(
    countForEmpty,
    countForWhitespace,
    "Count of quick actions of empty and whitespace are same"
  );
  await SpecialPowers.popPrefEnv();
});

async function clickQuickActionOneoffButton() {
  const oneOffButton = await TestUtils.waitForCondition(() =>
    window.document.getElementById("urlbar-engine-one-off-item-actions")
  );
  Assert.ok(oneOffButton, "One off button is available when preffed on");

  EventUtils.synthesizeMouseAtCenter(oneOffButton, {}, window);
  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.ACTIONS,
    entry: "oneoff",
  });
}

add_task(async function test_searchMode() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:home",
    waitForLoad: true,
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "@act",
  });

  EventUtils.synthesizeKey("KEY_Tab");

  await UrlbarTestUtils.assertSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.ACTIONS,
    entry: "keywordoffer",
    restrictType: "keyword",
  });

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "sour",
  });

  const onLoad = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "view-source:about:home"
  );
  EventUtils.synthesizeKey("KEY_Tab");
  EventUtils.synthesizeKey("KEY_Enter");
  const viewSourceTab = await onLoad;

  await BrowserTestUtils.switchTab(gBrowser, tab);

  Assert.equal(window.gURLBar.searchMode, null);

  BrowserTestUtils.removeTab(tab);
  BrowserTestUtils.removeTab(viewSourceTab);
});

let showAction = async testFun => {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "testact",
  });
  await assertAction("testaction");
  await testFun();
  await UrlbarTestUtils.promisePopupClose(window, () => {
    // We need to fully blur the urlbar for `onSearchSessionEnd`
    // to trigger.
    EventUtils.synthesizeKey("KEY_Escape");
    EventUtils.synthesizeKey("KEY_Escape");
    EventUtils.synthesizeKey("KEY_Escape");
  });
};

add_task(async function test_label_shown() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.quickactions.timesShownOnboardingLabel", 0],
      ["browser.urlbar.quickactions.timesToShowOnboardingLabel", 3],
    ],
  });
  await showAction(() => {
    Assert.ok(onboardingLabelShown(window), "Onboarding label is shown once");
  });
  await showAction(() => {
    Assert.ok(onboardingLabelShown(window), "Onboarding label is shown twice");
  });
  await showAction(() => {
    Assert.ok(
      onboardingLabelShown(window),
      "Onboarding label is shown third time"
    );
  });
  await showAction(() => {
    Assert.ok(!onboardingLabelShown(window), "Onboarding label is not shown");
  });
});

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/profiles/tests/browser/head.js",
  this
);
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/customizableui/test/head.js",
  this
);

const {
  FX_MONITOR_OAUTH_CLIENT_ID,
  FX_RELAY_OAUTH_CLIENT_ID,
  VPN_OAUTH_CLIENT_ID,
} = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);
const { SyncedTabs, SyncedTabsManagement } = ChromeUtils.importESModule(
  "resource://services-sync/SyncedTabs.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  CustomizableUITestUtils:
    "resource://testing-common/CustomizableUITestUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
});

add_setup(async function () {
  // gSync.init() is called in a requestIdleCallback. Force its initialization.
  gSync.init();
  // This preference gets set the very first time that the FxA menu gets opened,
  // which can cause a state write to occur, which can confuse this test, since
  // when in the signed-out state, we need to set the state _before_ opening
  // the FxA menu (since the panel cannot be opened) in the signed out state.
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.toolbar.accessed", true]],
  });
});

add_task(async function test_ui_state_notification_calls_updateAllUI() {
  let called = false;
  let updateAllUI = gSync.updateAllUI;
  gSync.updateAllUI = () => {
    called = true;
  };

  Services.obs.notifyObservers(null, UIState.ON_UPDATE);
  ok(called);

  gSync.updateAllUI = updateAllUI;
});

add_task(async function test_navBar_button_visibility() {
  const button = document.getElementById("fxa-toolbar-menu-button");
  ok(button.closest("#nav-bar"), "button is in the #nav-bar");

  const state = {
    status: UIState.STATUS_NOT_CONFIGURED,
    syncEnabled: true,
  };
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_NOT_CONFIGURED"
  );

  state.email = "foo@bar.com";
  state.status = UIState.STATUS_NOT_VERIFIED;
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_NOT_VERIFIED"
  );

  state.status = UIState.STATUS_LOGIN_FAILED;
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_LOGIN_FAILED"
  );

  state.status = UIState.STATUS_SIGNED_IN;
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_SIGNED_IN"
  );

  state.syncEnabled = false;
  gSync.updateAllUI(state);
  is(
    BrowserTestUtils.isVisible(button),
    true,
    "Check button visibility when signed in, but sync disabled"
  );
});

add_task(async function test_overflow_navBar_button_visibility() {
  const button = document.getElementById("fxa-toolbar-menu-button");

  let overflowPanel = document.getElementById("widget-overflow");
  overflowPanel.setAttribute("animate", "false");
  let navbar = document.getElementById(CustomizableUI.AREA_NAVBAR);
  let originalWindowWidth;

  registerCleanupFunction(function () {
    overflowPanel.removeAttribute("animate");
    unensureToolbarOverflow(window, originalWindowWidth);
    return TestUtils.waitForCondition(
      () => !navbar.hasAttribute("overflowing")
    );
  });

  // As of bug 1960002, overflowing the navbar requires adding buttons.
  originalWindowWidth = ensureToolbarOverflow(window, false);
  await TestUtils.waitForCondition(() => navbar.hasAttribute("overflowing"));
  ok(navbar.hasAttribute("overflowing"), "Should have an overflowing toolbar.");

  let chevron = document.getElementById("nav-bar-overflow-button");
  let shownPanelPromise = BrowserTestUtils.waitForEvent(
    overflowPanel,
    "popupshown"
  );
  chevron.click();
  await shownPanelPromise;

  ok(button, "fxa-toolbar-menu-button was found");

  const state = {
    status: UIState.STATUS_NOT_CONFIGURED,
    syncEnabled: true,
  };
  gSync.updateAllUI(state);

  ok(
    BrowserTestUtils.isVisible(button),
    "Button should still be visable even if user sync not configured"
  );

  let hidePanelPromise = BrowserTestUtils.waitForEvent(
    overflowPanel,
    "popuphidden"
  );
  chevron.click();
  await hidePanelPromise;
});

add_task(async function setupForPanelTests() {
  /* Proton hides the FxA toolbar button when in the nav-bar and unconfigured.
     To test the panel in all states, we move it to the tabstrip toolbar where
     it will always be visible.
   */
  CustomizableUI.addWidgetToArea(
    "fxa-toolbar-menu-button",
    CustomizableUI.AREA_TABSTRIP
  );

  // make sure it gets put back at the end of the tests
  registerCleanupFunction(() => {
    CustomizableUI.addWidgetToArea(
      "fxa-toolbar-menu-button",
      CustomizableUI.AREA_NAVBAR
    );
  });
});

add_task(async function test_ui_state_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // Setup profiles db
  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.enabled", true]],
  });
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a profile now");

  const relativeDateAnchor = new Date();

  const sandbox = sinon.createSandbox();
  sandbox.stub(gSync, "getSendTabTargets").returns([
    {
      id: 1,
      name: "Desktop1",
      type: "desktop",
      availableCommands: {
        "https://identity.mozilla.com/cmd/open-uri": "baz",
      },
    },
    {
      id: 2,
      name: "Desktop2",
      type: "desktop",
      availableCommands: {
        "https://identity.mozilla.com/cmd/open-uri": "baz",
      },
    },
  ]);

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-syncnowitem");
  checkPanelHeader();
  checkManageAccountButton(state.displayName || state.email);
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sync-status-button",
      "PanelUI-fxa-menu-account-signout-button",
    ],
    disabledItems: [],
    hiddenItems: [
      "PanelUI-fxa-menu-setup-sync-container",
      // "Get Firefox for mobile" is only offered while sync is off.
      "PanelUI-fxa-menu-get-firefox-mobile",
    ],
    visibleItems: [],
  });

  // A separator sits directly below the manage account button, dividing it
  // from the profiles section that follows.
  const manageAccountSeparator = document.getElementById(
    "PanelUI-fxa-menu-manage-account-separator"
  );
  ok(
    !manageAccountSeparator.hidden,
    "The manage account separator is visible when signed in"
  );
  is(
    manageAccountSeparator.previousElementSibling,
    document.getElementById("PanelUI-fxa-menu-manage-account-button"),
    "The manage account separator sits directly below the manage account button"
  );

  await checkProfilesButtons(manageAccountSeparator, true);

  // In the account (toolbar) menu the sign-out separator and button sit at the
  // bottom of the secure sync section, directly below the connected devices
  // list.
  is(
    document.getElementById("PanelUI-sign-out-separator")
      .previousElementSibling,
    document.getElementById("PanelUI-fxa-menu-devices-list"),
    "The sign-out separator sits directly below the connected devices list"
  );
  is(
    document.getElementById("PanelUI-fxa-menu-account-signout-button")
      .previousElementSibling,
    document.getElementById("PanelUI-sign-out-separator"),
    "The sign-out button sits directly below the sign-out separator"
  );

  checkFxAAvatar("signedin");
  gSync.relativeTimeFormat = origRelativeTimeFormat;
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  // Ensure the profiles menuitem is not shown in the FxA panel when it is
  // displayed as a subpanel of the app menu.
  let appMenuFxAStatusButton = document.getElementById("appMenu-fxa-label2");
  appMenuFxAStatusButton.click();
  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  await BrowserTestUtils.waitForEvent(fxaView, "ViewShown");

  // Verify the manage button is shown, just as a basic check.
  let manageButton = fxaView.querySelector(
    "#PanelUI-fxa-menu-manage-account-button"
  );
  ok(
    BrowserTestUtils.isVisible(manageButton),
    "expected manage button to be visible after opening"
  );
  let profileButtonsContainer = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-profile-buttons"
  );
  ok(
    !BrowserTestUtils.isVisible(profileButtonsContainer),
    "expected profile buttons container to not be visible"
  );
  // The profiles separator has to be hidden along with the rest of the profiles
  // section, otherwise it doubles up with the manage account separator.
  ok(
    !BrowserTestUtils.isVisible(
      PanelMultiView.getViewNode(
        document,
        "PanelUI-fxa-menu-profiles-separator"
      )
    ),
    "expected profiles separator to not be visible"
  );

  // In the app menu the sign-out button sits directly below the sync status
  // section and above the connected devices list, with a separator either side.
  is(
    document.getElementById("PanelUI-fxa-menu-account-signout-button")
      .previousElementSibling,
    document.getElementById("PanelUI-sign-out-separator"),
    "The sign-out button sits directly below the sign-out separator in the app menu"
  );
  is(
    document.getElementById("PanelUI-fxa-menu-devices-list")
      .previousElementSibling,
    document.getElementById("PanelUI-fxa-menu-account-signout-button"),
    "The connected devices list sits directly below the sign-out button in the app menu"
  );

  await closeTabAndMainPanel();
  sandbox.restore();
});

add_task(async function test_ui_state_signedin_profiles_disabled() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(SelectableProfileService, "isEnabled").get(() => false);

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  });

  await openFxaPanel();

  const manageAccountSeparator = document.getElementById(
    "PanelUI-fxa-menu-manage-account-separator"
  );
  ok(
    isElementVisible(manageAccountSeparator),
    "The manage account separator is visible when signed in"
  );
  for (const id of [
    "PanelUI-fxa-menu-profiles-header-label",
    "PanelUI-fxa-menu-profile-buttons",
  ]) {
    ok(
      !isElementVisible(document.getElementById(id)),
      `${id} is not visible when the profiles feature is disabled`
    );
  }

  // With the profiles section hidden, the manage account separator is the only
  // line dividing the account and secure sync sections.
  await checkProfilesButtons(manageAccountSeparator, false);

  await closeFxaPanel();
  sandbox.restore();
});

add_task(async function test_ui_state_syncing_panel_closed() {
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: true,
  };

  gSync.updateAllUI(state);

  checkSyncNowButtons(true);

  // Be good citizens and remove the "syncing" state.
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    lastSync: new Date(),
    syncing: false,
  });
  // Because we switch from syncing to non-syncing, and there's a timeout involved.
  await promiseObserver("test:browser-sync:activity-stop");
});

add_task(async function test_ui_state_syncing_panel_open() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkSyncNowButtons(false);

  state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: true,
  };

  gSync.updateAllUI(state);

  checkSyncNowButtons(true);

  // Be good citizens and remove the "syncing" state.
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    lastSync: new Date(),
    syncing: false,
  });
  // Because we switch from syncing to non-syncing, and there's a timeout involved.
  await promiseObserver("test:browser-sync:activity-stop");

  await closeFxaPanel();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_ui_state_panel_open_after_syncing() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: true,
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkSyncNowButtons(true);

  // Be good citizens and remove the "syncing" state.
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    lastSync: new Date(),
    syncing: false,
  });
  // Because we switch from syncing to non-syncing, and there's a timeout involved.
  await promiseObserver("test:browser-sync:activity-stop");

  await closeFxaPanel();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_ui_state_unconfigured() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // Setup profiles db
  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.enabled", true]],
  });
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a profile now");

  let state = {
    status: UIState.STATUS_NOT_CONFIGURED,
  };

  gSync.updateAllUI(state);

  checkMenuBarItem("sync-setup");

  checkFxAAvatar("not_configured");

  await openMainPanel();

  // With no menu message or update banner present, the app menu shows the
  // sign-in promo in place of the compact "Sync and Save Data" sign-in row.
  ok(
    BrowserTestUtils.isVisible(
      PanelMultiView.getViewNode(document, "appMenu-fxa-sign-in-promo")
    ),
    "sign-in promo is visible in the app menu when signed out"
  );
  ok(
    BrowserTestUtils.isHidden(
      PanelMultiView.getViewNode(document, "appMenu-fxa-status2")
    ),
    "compact sign-in row is hidden when the promo is shown"
  );
  await closeTabAndMainPanel();

  await openFxaPanel();

  // When signed out, "Get Firefox for mobile" is offered directly below the
  // sync status button ("Sync your data").
  const mobileButton = document.getElementById(
    "PanelUI-fxa-menu-get-firefox-mobile"
  );
  ok(
    isElementVisible(mobileButton),
    "Get Firefox for mobile button is visible when signed out"
  );
  is(
    mobileButton.previousElementSibling,
    document.getElementById("PanelUI-fxa-menu-sync-status-button"),
    "Get Firefox for mobile sits directly below the sync status button"
  );

  await checkProfilesButtons(
    document.getElementById("PanelUI-signedin-panel"),
    true
  );

  await closeFxaPanel();
});

add_task(async function test_ui_state_signed_in() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-enable");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: "Foo Bar",
    enabledItems: [
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-menu-sync-status-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: ["PanelUI-fxa-menu-get-firefox-mobile"],
  });
  checkFxAAvatar("signedin");
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_signed_in_no_display_name() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    avatarURL: "https://foo.bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-enable");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: "foo@bar.com",
    enabledItems: [
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-menu-sync-status-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: ["PanelUI-fxa-menu-get-firefox-mobile"],
  });
  checkFxAAvatar("signedin");
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "foo@bar.com",
    titleHidden: true,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_unverified() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_NOT_VERIFIED,
    email: "foo@bar.com",
    syncing: false,
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  const expectedLabel = gSync.fluentStrings.formatValueSync(
    "account-finish-account-setup"
  );

  checkMenuBarItem("sync-unverifieditem");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: expectedLabel,
    headerDescription: state.email,
    enabledItems: [
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-menu-sync-status-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: ["PanelUI-fxa-menu-get-firefox-mobile"],
  });
  checkFxAAvatar("unverified");
  await checkSignedOutCard(
    state.email,
    "fxa-menu-signed-out-message-unverified"
  );
  await closeFxaPanel();
  await openMainPanel();

  checkPanelUIStatusBar({
    description: state.email,
    title: expectedLabel,
    titleHidden: false,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_loginFailed() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_LOGIN_FAILED,
    email: "foo@bar.com",
    displayName: "Foo Bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  const expectedLabel = gSync.fluentStrings.formatValueSync(
    "account-disconnected2"
  );

  checkMenuBarItem("sync-reauthitem");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: expectedLabel,
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-menu-sync-status-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: ["PanelUI-fxa-menu-get-firefox-mobile"],
  });
  checkFxAAvatar("login-failed");
  await checkSignedOutCard(
    state.email,
    "fxa-menu-signed-out-message-login-failed"
  );
  await closeFxaPanel();
  await openMainPanel();

  checkPanelUIStatusBar({
    description: state.displayName,
    title: expectedLabel,
    titleHidden: false,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_app_menu_fxa_disabled() {
  const newWin = await BrowserTestUtils.openNewBrowserWindow();

  Services.prefs.setBoolPref("identity.fxaccounts.enabled", true);
  newWin.gSync.onFxaDisabled();

  let menuButton = newWin.document.getElementById("PanelUI-menu-button");
  menuButton.click();
  await BrowserTestUtils.waitForEvent(newWin.PanelUI.mainView, "ViewShown");

  [...newWin.document.querySelectorAll(".sync-ui-item")].forEach(
    e => (e.hidden = false)
  );

  let hidden = BrowserTestUtils.waitForEvent(
    newWin.document,
    "popuphidden",
    true
  );
  newWin.PanelUI.hide();
  await hidden;
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_history_menu_fxa_disabled() {
  if (AppConstants.platform === "macosx") {
    info(
      "skipping test because the history menu can't be opened in tests on mac"
    );
    return;
  }

  const newWin = await BrowserTestUtils.openNewBrowserWindow();

  Services.prefs.setBoolPref("identity.fxaccounts.enabled", true);
  newWin.gSync.onFxaDisabled();

  const historyMenubarItem = window.document.getElementById("history-menu");
  const historyMenu = window.document.getElementById("historyMenuPopup");
  const syncedTabsItem = historyMenu.querySelector("#sync-tabs-menuitem");
  const menuShown = BrowserTestUtils.waitForEvent(historyMenu, "popupshown");
  historyMenubarItem.openMenu(true);
  await menuShown;

  Assert.equal(
    syncedTabsItem.hidden,
    true,
    "Synced Tabs item should not be displayed when FxAccounts is disabled"
  );
  const menuHidden = BrowserTestUtils.waitForEvent(historyMenu, "popuphidden");
  historyMenu.hidePopup();
  await menuHidden;
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_sync_promo_state() {
  const SELF = [{ isCurrentDevice: true }];
  const SELF_AND_OTHER = [...SELF, { isCurrentDevice: false }];

  // Iterate through cases we expect to see the promo
  const cases = [
    {
      desc: "history menu promo - FxA disabled",
      fxaEnabled: false,
      expected: null,
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - signed out",
      status: UIState.STATUS_NOT_CONFIGURED,
      expected: "signin",
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - unverified",
      status: UIState.STATUS_NOT_VERIFIED,
      expected: "signin",
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - login failed",
      status: UIState.STATUS_LOGIN_FAILED,
      expected: null,
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - sync disabled",
      syncEnabled: false,
      expected: "turnonsync",
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - tabs engine off in CWTS",
      tabsEngine: false,
      expected: "turnonsync",
      promoEngine: ["tabs"],
    },
    {
      desc: "history engine off does not gate tabs promo",
      historyEngine: false,
      expected: null,
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - device list still loading",
      devices: null,
      expected: null,
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - no other devices",
      devices: SELF,
      expected: "connectdevice",
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - has another device",
      expected: null,
      promoEngine: ["tabs"],
    },
    {
      desc: "history menu promo - only a remote device, current not yet in list",
      devices: [{ isCurrentDevice: false }],
      expected: null,
      promoEngine: ["tabs"],
    },
    {
      desc: "bookmarks menu promo - FxA disabled",
      fxaEnabled: false,
      expected: null,
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - signed out",
      status: UIState.STATUS_NOT_CONFIGURED,
      expected: "signin",
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - unverified",
      status: UIState.STATUS_NOT_VERIFIED,
      expected: "signin",
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - login failed",
      status: UIState.STATUS_LOGIN_FAILED,
      expected: null,
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - sync disabled",
      syncEnabled: false,
      expected: "turnonsync",
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - bookmarks engine off in CWTS",
      bookmarksEngine: false,
      expected: "turnonsync",
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - device list still loading",
      devices: null,
      expected: null,
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - no other devices",
      devices: SELF,
      expected: "connectdevice",
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - has another device",
      expected: null,
      promoEngine: ["bookmarks"],
    },
    {
      desc: "bookmarks menu promo - only a remote device, current not yet in list",
      devices: [{ isCurrentDevice: false }],
      expected: null,
      promoEngine: ["bookmarks"],
    },
  ];

  for (const c of cases) {
    const {
      desc,
      fxaEnabled = true,
      status = UIState.STATUS_SIGNED_IN,
      syncEnabled = true,
      bookmarksEngine = true,
      historyEngine = true,
      tabsEngine = true,
      promoEngine = [],
      devices = SELF_AND_OTHER,
      expected,
    } = c;

    const sandbox = sinon.createSandbox();
    sandbox.stub(gSync, "FXA_ENABLED").get(() => fxaEnabled);
    sandbox.stub(UIState, "get").returns({ status, syncEnabled });
    sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => devices);
    Services.prefs.setBoolPref(
      "services.sync.engine.bookmarks",
      bookmarksEngine
    );
    Services.prefs.setBoolPref("services.sync.engine.history", historyEngine);
    Services.prefs.setBoolPref("services.sync.engine.tabs", tabsEngine);

    Assert.equal(
      gSync.getSyncPromoState(promoEngine),
      expected,
      `Promo state when ${desc}`
    );

    sandbox.restore();
    Services.prefs.clearUserPref("services.sync.engine.bookmarks");
    Services.prefs.clearUserPref("services.sync.engine.history");
    Services.prefs.clearUserPref("services.sync.engine.tabs");
  }
});

add_task(async function test_open_sync_setup_for_entry_point() {
  const ENTRYPOINT = "remote-tabs-top-menu-history";

  // Account with sync keys (has a password): go straight to "Choose what to
  // sync" in preferences.
  const hasKeys = sinon.stub(fxAccounts.keys, "hasKeysForScope").resolves(true);
  const openPrefs = sinon.stub(gSync, "openPrefs");
  await gSync.openSyncSetupForEntryPoint(ENTRYPOINT);
  Assert.ok(
    openPrefs.calledOnceWith(ENTRYPOINT, null, {
      action: "choose-what-to-sync",
    }),
    "Opens Choose what to sync when the account has sync keys"
  );
  hasKeys.restore();
  openPrefs.restore();

  // Passwordless account (no sync keys): send to the set-password flow.
  const sandbox = sinon.createSandbox();
  sandbox.stub(fxAccounts.keys, "hasKeysForScope").resolves(false);
  sandbox.stub(FxAccounts, "canConnectAccount").resolves(true);
  const setPasswordURI = sandbox
    .stub(FxAccounts.config, "promiseSetPasswordURI")
    .resolves("https://example.com/set_password");
  const switchToTab = sandbox.stub(window, "switchToTabHavingURI");
  await gSync.openSyncSetupForEntryPoint(ENTRYPOINT);
  Assert.ok(
    setPasswordURI.calledOnceWith(ENTRYPOINT),
    "Builds the set-password URL for a passwordless account"
  );
  Assert.ok(
    switchToTab.calledOnceWith("https://example.com/set_password"),
    "Opens the set-password flow for a passwordless account"
  );
  sandbox.restore();
});

add_task(async function test_history_menu_remote_tabs_promo() {
  if (AppConstants.platform === "macosx") {
    info(
      "skipping test because the history menu can't be opened in tests on mac"
    );
    return;
  }

  const historyMenubarItem = document.getElementById("history-menu");
  const historyMenu = document.getElementById("historyMenuPopup");
  const promo = document.getElementById("historyRemoteTabsPromo");
  const syncedTabsItem = document.getElementById("sync-tabs-menuitem");

  async function openHistoryMenu() {
    const shown = BrowserTestUtils.waitForEvent(historyMenu, "popupshown");
    historyMenubarItem.openMenu(true);
    await shown;
  }
  async function closeHistoryMenu() {
    const hidden = BrowserTestUtils.waitForEvent(historyMenu, "popuphidden");
    historyMenu.hidePopup();
    await hidden;
  }

  // Signed out: promo visible with the sign-in action, synced-tabs item hidden.
  let uiState = sinon
    .stub(UIState, "get")
    .returns({ status: UIState.STATUS_NOT_CONFIGURED });
  await openHistoryMenu();
  Assert.ok(!promo.hidden, "Promo is visible when signed out");
  Assert.equal(
    promo.dataset.action,
    "signin",
    "Promo performs the sign-in action when signed out"
  );
  Assert.equal(
    promo.getAttribute("data-l10n-id"),
    "menu-history-remote-tabs-promo",
    "Promo keeps its fixed label regardless of state"
  );
  Assert.ok(syncedTabsItem.hidden, "Synced tabs item hidden while promo shown");
  await closeHistoryMenu();
  uiState.restore();

  // Signed in with another device (eligible): the promo is hidden and the
  // legacy synced-tabs item is shown, renamed to "Tabs from Other Devices".
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(UIState, "get")
    .returns({ status: UIState.STATUS_SIGNED_IN, syncEnabled: true });
  sandbox
    .stub(fxAccounts.device, "recentDeviceList")
    .get(() => [{ isCurrentDevice: true }, { isCurrentDevice: false }]);
  sandbox
    .stub(PlacesUIUtils, "shouldShowTabsFromOtherComputersMenuitem")
    .returns(true);
  await openHistoryMenu();
  Assert.ok(promo.hidden, "Promo hidden when other devices are available");
  Assert.ok(!syncedTabsItem.hidden, "Legacy synced-tabs item is shown");
  Assert.equal(
    syncedTabsItem.getAttribute("data-l10n-id"),
    "menu-history-remote-tabs-promo",
    "Legacy item shares the 'Tabs from Other Devices' label"
  );
  await closeHistoryMenu();
  sandbox.restore();
});

add_task(async function test_bookmarks_menu_remote_tabs_promo() {
  if (AppConstants.platform === "macosx") {
    info(
      "skipping test because the bookmarks menu can't be opened in tests on mac"
    );
    return;
  }

  const bookmarksMenubarItem = document.getElementById("bookmarksMenu");
  const bookmarksMenu = document.getElementById("bookmarksMenuPopup");
  const promo = document.getElementById("bookmarksRemoteTabsPromo");

  async function openBookmarksMenu() {
    const shown = BrowserTestUtils.waitForEvent(bookmarksMenu, "popupshown");
    bookmarksMenubarItem.openMenu(true);
    await shown;
  }
  async function closeBookmarksMenu() {
    const hidden = BrowserTestUtils.waitForEvent(bookmarksMenu, "popuphidden");
    bookmarksMenu.hidePopup();
    await hidden;
  }

  // Signed out: promo visible offering sign-in.
  let uiState = sinon
    .stub(UIState, "get")
    .returns({ status: UIState.STATUS_NOT_CONFIGURED });
  await openBookmarksMenu();
  Assert.ok(!promo.hidden, "Promo is visible when signed out");
  Assert.equal(
    promo.dataset.action,
    "signin",
    "Promo offers sign-in when signed out"
  );
  await closeBookmarksMenu();
  uiState.restore();

  // Signed in with another device: promo hidden.
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(UIState, "get")
    .returns({ status: UIState.STATUS_SIGNED_IN, syncEnabled: true });
  sandbox
    .stub(fxAccounts.device, "recentDeviceList")
    .get(() => [{ isCurrentDevice: true }, { isCurrentDevice: false }]);
  await openBookmarksMenu();
  Assert.ok(promo.hidden, "Promo hidden when other devices are available");
  await closeBookmarksMenu();
  sandbox.restore();
});

// When signed out, the sign-in promo replaces the account header button. The
// email of a remembered account can't be recovered from the stored hashed UID,
// so the promo is shown whether or not a previous account is remembered.
add_task(async function test_signed_out_sign_in_promo() {
  const LAST_USER_PREF = "identity.fxaccounts.lastSignedInUserIdHash";
  const promo = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sign-in-promo"
  );

  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_NOT_CONFIGURED,
  });

  for (const cachedUser of [false, true]) {
    if (cachedUser) {
      Services.prefs.setStringPref(LAST_USER_PREF, "cached-uid-hash");
    } else {
      Services.prefs.clearUserPref(LAST_USER_PREF);
    }
    gSync.updateAllUI(UIState.get());
    await openFxaPanel();
    ok(
      BrowserTestUtils.isVisible(promo),
      `Sign-in promo is visible when signed out (cachedUser=${cachedUser})`
    );
    await closeFxaPanel();
  }

  Services.prefs.clearUserPref(LAST_USER_PREF);
  sandbox.restore();
});

// If the PXI experiment is enabled, we need to ensure we can see the CTAs when signed out
add_task(async function test_experiment_ui_state_unconfigured() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // The experiment enables this bool, found in FeatureManifest.yaml
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    true
  );
  let state = {
    status: UIState.STATUS_NOT_CONFIGURED,
  };

  gSync.updateAllUI(state);

  checkMenuBarItem("sync-setup");

  checkFxAAvatar("not_configured");

  let expectedLabel = gSync.fluentStrings.formatValueSync(
    "synced-tabs-fxa-sign-in"
  );

  let expectedDescriptionLabel = gSync.fluentStrings.formatValueSync(
    "fxa-menu-sync-description"
  );

  await openMainPanel();

  checkFxaToolbarButtonPanel({
    headerTitle: expectedLabel,
    headerDescription: expectedDescriptionLabel,
    enabledItems: [
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: [],
    visibleItems: [],
  });

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  await closeTabAndMainPanel();
});

// Ensure we can see the regular signed in flow + the extra PXI CTAs when
// the experiment is enabled
add_task(async function test_experiment_ui_state_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // The experiment enables this bool, found in FeatureManifest.yaml
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    true
  );

  const relativeDateAnchor = new Date();
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-syncnowitem");
  checkPanelHeader();
  checkManageAccountButton(state.displayName || state.email);
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sync-status-button",
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: [],
  });
  checkFxAAvatar("signedin");
  gSync.relativeTimeFormat = origRelativeTimeFormat;
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  await closeTabAndMainPanel();
});

add_task(async function test_new_sync_setup_ui() {
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    hasSyncKeys: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-enable");
  checkPanelHeader();

  // The "Sync is Off" status button now stands in for the old sync-setup
  // container when signed in with sync off.
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: "Foo Bar",
    enabledItems: [
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-menu-sync-status-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: ["PanelUI-fxa-menu-get-firefox-mobile"],
  });

  await closeFxaPanel();
});

// When a privacy tool is already in use by the account, it stays under the
// "Privacy tools" header (there is no longer a separate "My services" section)
// but shows an action-oriented title and no description.
add_task(async function test_ui_privacy_tools_in_use_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    true
  );

  const relativeDateAnchor = new Date();
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    hasSyncKeys: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  const sandbox = sinon.createSandbox();
  // isSignedIn reads from UIState.get(), so stub it to reflect a signed-in
  // account, and stub the network fetch so our injected client list is not
  // clobbered.
  sandbox.stub(UIState, "get").returns(state);
  sandbox.stub(gSync, "fetchListOfOAuthClients").resolves(true);

  gSync.updateAllUI(state);

  // Pretend the user has signed up for Relay.
  gSync._attachedClients = [
    {
      id: FX_RELAY_OAUTH_CLIENT_ID,
    },
  ];

  await openFxaPanel();

  checkMenuBarItem("sync-syncnowitem");
  checkPanelHeader();
  checkManageAccountButton(state.displayName || state.email);
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sync-status-button",
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: [
      // The in-use tool stays visible under "Privacy tools".
      "PanelUI-fxa-menu-relay-button",
    ],
  });

  const relayButton = document.getElementById("PanelUI-fxa-menu-relay-button");
  is(
    relayButton.querySelector(".cta-menu-title").getAttribute("data-l10n-id"),
    "appmenuitem-relay-title-signed-in",
    "in-use Relay button shows the signed-in title"
  );
  ok(
    relayButton.querySelector(".cta-menu-description").hidden,
    "in-use Relay button hides its description"
  );

  // Tools that are not in use keep their promo title and description.
  const monitorButton = document.getElementById(
    "PanelUI-fxa-menu-monitor-button"
  );
  is(
    monitorButton.querySelector(".cta-menu-title").getAttribute("data-l10n-id"),
    "appmenuitem-monitor-title2",
    "unused Monitor button shows the promo title"
  );
  ok(
    !monitorButton.querySelector(".cta-menu-description").hidden,
    "unused Monitor button shows its description"
  );

  checkFxAAvatar("signedin");
  gSync.relativeTimeFormat = origRelativeTimeFormat;
  gSync._attachedClients = [];
  sandbox.restore();
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  await closeTabAndMainPanel();
});

// When the account has signed up for every privacy tool, each one stays under
// the "Privacy tools" header with its action-oriented title and no description.
add_task(async function test_ui_privacy_tools_all_in_use_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    true
  );

  const relativeDateAnchor = new Date();
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    hasSyncKeys: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns(state);
  sandbox.stub(gSync, "fetchListOfOAuthClients").resolves(true);

  gSync.updateAllUI(state);

  // Pretend the user has signed up for Monitor, Relay and VPN.
  gSync._attachedClients = [
    { id: FX_MONITOR_OAUTH_CLIENT_ID },
    { id: FX_RELAY_OAUTH_CLIENT_ID },
    { id: VPN_OAUTH_CLIENT_ID },
  ];

  await openFxaPanel();

  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: [],
    visibleItems: [
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
  });

  const inUseTools = [
    {
      buttonId: "PanelUI-fxa-menu-monitor-button",
      titleId: "appmenuitem-monitor-title-signed-in",
    },
    {
      buttonId: "PanelUI-fxa-menu-relay-button",
      titleId: "appmenuitem-relay-title-signed-in",
    },
    {
      buttonId: "PanelUI-fxa-menu-vpn-button",
      titleId: "appmenuitem-vpn-title-signed-in1",
    },
  ];

  for (const { buttonId, titleId } of inUseTools) {
    const button = document.getElementById(buttonId);
    is(
      button.querySelector(".cta-menu-title").getAttribute("data-l10n-id"),
      titleId,
      `${buttonId} shows the signed-in title`
    );
    ok(
      button.querySelector(".cta-menu-description").hidden,
      `${buttonId} hides its description`
    );
  }

  gSync.relativeTimeFormat = origRelativeTimeFormat;
  gSync._attachedClients = [];
  sandbox.restore();
  await closeFxaPanel();

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_experiment_signin_button_signed_out() {
  // Enroll in Nimbus experiment
  await ExperimentAPI.ready();
  let cleanupNimbus = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: NimbusFeatures.expandSignInButton.featureId,
    value: {
      ctaCopyVariant: "fxa-avatar-sign-in",
    },
  });

  // Set UI state to STATUS_NOT_CONFIGURED (signed out)
  let state = { status: UIState.STATUS_NOT_CONFIGURED };
  gSync.updateAllUI(state);

  const fxaAvatarLabel = document.getElementById("fxa-avatar-label");
  ok(fxaAvatarLabel, "Avatar label element should exist");
  is(
    fxaAvatarLabel.hidden,
    false,
    "Avatar label should be visible when Nimbus experiment is enabled"
  );

  const expectedLabel =
    gSync.fluentStrings.formatValueSync("fxa-avatar-sign-in");

  is(
    fxaAvatarLabel.getAttribute("value"),
    expectedLabel,
    `Avatar label should have the expected localized value: ${expectedLabel}`
  );

  // Clean up experiment
  await cleanupNimbus();

  // Reset UI state after experiment cleanup
  gSync.updateAllUI(state);

  is(
    fxaAvatarLabel.hidden,
    true,
    "Avatar label should be hidden after Nimbus experiment is cleaned up"
  );
});

add_task(async function test_experiment_signin_button_signed_in() {
  // Enroll in Nimbus experiment
  await ExperimentAPI.ready();
  let cleanupNimbus = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: NimbusFeatures.expandSignInButton.featureId,
    value: {
      ctaCopyVariant: "fxa-avatar-sign-in",
    },
  });

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };
  gSync.updateAllUI(state);

  const fxaAvatarLabel = document.getElementById("fxa-avatar-label");
  is(
    fxaAvatarLabel.hidden,
    true,
    "Avatar label should never be visible when signed in"
  );

  // Clean up experiment
  await cleanupNimbus();

  // Reset UI state after experiment cleanup
  gSync.updateAllUI(state);

  is(
    fxaAvatarLabel.hidden,
    true,
    "Avatar label should still be hidden when signed in without experiment"
  );
});

function checkPanelUIStatusBar({
  description,
  title,
  titleHidden,
  hideFxAText,
}) {
  checkAppMenuFxAText(hideFxAText);
  let appMenuHeaderTitle = PanelMultiView.getViewNode(
    document,
    "appMenu-header-title"
  );
  let appMenuHeaderDescription = PanelMultiView.getViewNode(
    document,
    "appMenu-header-description"
  );
  is(
    appMenuHeaderDescription.value,
    description,
    "app menu description has correct value"
  );
  is(appMenuHeaderTitle.hidden, titleHidden, "title has correct hidden status");
  if (!titleHidden) {
    is(appMenuHeaderTitle.value, title, "title has correct value");
  }
}

function checkMenuBarItem(expectedShownItemId) {
  checkItemsVisibilities(
    [
      "sync-setup",
      "sync-enable",
      "sync-syncnowitem",
      "sync-reauthitem",
      "sync-unverifieditem",
    ],
    expectedShownItemId
  );
}

async function checkSignedOutCard(email, messageL10nId) {
  const card = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-signed-out-card"
  );

  ok(
    BrowserTestUtils.isVisible(card),
    "Signed-out card is visible when a remembered account needs to sign in"
  );
  is(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-menu-signed-out-email")
      .value,
    email,
    "Signed-out card shows the remembered account email"
  );
  is(
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-signed-out-message"
    ).getAttribute("data-l10n-id"),
    messageL10nId,
    "Signed-out card shows the status-specific reason"
  );
}

function checkManageAccountButton(expectedLabel) {
  const manageButton = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-manage-account-button"
  );
  ok(
    BrowserTestUtils.isVisible(manageButton),
    "Manage account button is visible when signed in"
  );
  is(
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-manage-account-email"
    ).value,
    expectedLabel,
    "Manage account button shows the account display name or email"
  );
}

function checkPanelHeader() {
  let fxaPanelView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  is(
    fxaPanelView.getAttribute("title"),
    gSync.fluentStrings.formatValueSync("appmenu-account-header"),
    "Panel title is correct"
  );
}

function checkSyncNowButtons(syncing, tooltip = null) {
  const syncButtons = document.querySelectorAll(".syncNowBtn");

  for (const syncButton of syncButtons) {
    is(
      syncButton.getAttribute("syncstatus"),
      syncing ? "active" : null,
      "button active has the right value"
    );
    if (tooltip) {
      is(
        syncButton.getAttribute("tooltiptext"),
        tooltip,
        "button tooltiptext is set to the right value"
      );
    }
  }

  const syncLabels = document.querySelectorAll(".syncnow-label");

  for (const syncLabel of syncLabels) {
    if (syncing) {
      is(
        syncLabel.getAttribute("data-l10n-id"),
        syncLabel.getAttribute("syncing-data-l10n-id"),
        "label is set to the right value"
      );
    } else {
      is(
        syncLabel.getAttribute("data-l10n-id"),
        syncLabel.getAttribute("sync-now-data-l10n-id"),
        "label is set to the right value"
      );
    }
  }
}

async function checkFxaToolbarButtonPanel({
  headerTitle,
  headerDescription,
  enabledItems,
  disabledItems,
  hiddenItems,
  visibleItems,
}) {
  is(
    document.getElementById("fxa-menu-header-title").value,
    headerTitle,
    "has correct title"
  );
  is(
    document.getElementById("fxa-menu-header-description").value,
    headerDescription,
    "has correct description"
  );

  for (const id of enabledItems) {
    const el = document.getElementById(id);
    is(el.hasAttribute("disabled"), false, id + " is enabled");
  }

  for (const id of disabledItems) {
    const el = document.getElementById(id);
    is(el.getAttribute("disabled"), "true", id + " is disabled");
  }

  for (const id of hiddenItems) {
    const el = document.getElementById(id);
    ok(el.hasAttribute("hidden"), id + " is hidden");
  }

  for (const id of visibleItems) {
    const el = document.getElementById(id);
    ok(isElementVisible(el), `${id} is visible`);
  }
}

function isElementVisible(el) {
  if (!el) {
    return false;
  }
  let style = window.getComputedStyle(el);
  // The “hidden” property on the element itself
  // might not exist or might be false, so we also
  // check that the computed style is not hiding it
  // (display: none or visibility: hidden).
  return (
    !el.hidden && style.display !== "none" && style.visibility !== "hidden"
  );
}

async function checkProfilesButtons(
  previousElementSibling,
  separatorVisible = false
) {
  const profilesHeaderLabel = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-profiles-header-label"
  );
  const profilesSeparator = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-profiles-separator"
  );

  is(
    !profilesSeparator.hidden,
    separatorVisible,
    "The profiles separator has the correct visibility"
  );

  is(
    previousElementSibling,
    profilesHeaderLabel.previousElementSibling,
    "The profiles section starts after " + previousElementSibling.id
  );
}

async function checkFxABadged() {
  const button = document.getElementById("fxa-toolbar-menu-button");
  await TestUtils.waitForCondition(() => {
    return button.querySelector("label.feature-callout");
  });
  const badge = button.querySelector("label.feature-callout");
  ok(badge, "expected feature-callout style badge");
  ok(BrowserTestUtils.isVisible(badge), "expected the badge to be visible");
}

// fxaStatus is one of 'not_configured', 'unverified', 'login-failed', or 'signedin'.
function checkFxAAvatar(fxaStatus) {
  // Unhide the panel so computed styles can be read
  document.querySelector("#appMenu-popup").hidden = false;

  const avatarContainers = [document.getElementById("fxa-avatar-image")];
  for (const avatar of avatarContainers) {
    const avatarURL = getComputedStyle(avatar).listStyleImage;
    const expected = {
      not_configured: 'url("chrome://browser/skin/fxa/avatar-empty.svg")',
      unverified: 'url("chrome://browser/skin/fxa/avatar.svg")',
      signedin: 'url("chrome://browser/skin/fxa/avatar.svg")',
      "login-failed": 'url("chrome://browser/skin/fxa/avatar.svg")',
    };
    Assert.equal(
      avatarURL,
      expected[fxaStatus],
      `expected avatar URL to be ${expected[fxaStatus]}, got ${avatarURL}`
    );
  }
}

function checkAppMenuFxAText(hideStatus) {
  let fxaText = document.getElementById("appMenu-fxa-text");
  let isHidden = fxaText.hidden || fxaText.style.visibility == "collapse";
  Assert.equal(isHidden, hideStatus, "FxA text has correct hidden state");
}

// Only one item visible at a time.
function checkItemsVisibilities(itemsIds, expectedShownItemId) {
  for (let id of itemsIds) {
    if (id == expectedShownItemId) {
      ok(
        !document.getElementById(id).hidden,
        "menuitem " + id + " should be visible"
      );
    } else {
      ok(
        document.getElementById(id).hidden,
        "menuitem " + id + " should be hidden"
      );
    }
  }
}

function promiseObserver(topic) {
  return new Promise(resolve => {
    let obs = (aSubject, aTopic) => {
      Services.obs.removeObserver(obs, aTopic);
      resolve(aSubject);
    };
    Services.obs.addObserver(obs, topic);
  });
}

async function openTabAndFxaPanel() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");
  await openFxaPanel();
}

async function openFxaPanel() {
  let fxaButton = document.getElementById("fxa-toolbar-menu-button");
  fxaButton.click();

  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  await BrowserTestUtils.waitForEvent(fxaView, "ViewShown");
}

async function closeFxaPanel() {
  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  fxaView.closest("panel").hidePopup();
  await hidden;
}

async function openMainPanel() {
  let menuButton = document.getElementById("PanelUI-menu-button");
  menuButton.click();
  await BrowserTestUtils.waitForEvent(window.PanelUI.mainView, "ViewShown");
}

async function closeTabAndMainPanel() {
  await gCUITestUtils.hideMainMenu();

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
}

/**
 * When the user's account is unverified, the device list is hidden because
 * sync is inactive without verification. Without a device list, there is no
 * per-device "Send Current Page to This Device" button to interact with.
 */
add_task(async function test_device_list_hidden_when_unverified() {
  gSync.updateAllUI({
    status: UIState.STATUS_NOT_VERIFIED,
    syncEnabled: false,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-menu-devices-list")
      .hidden,
    "Device list is hidden when account is unverified"
  );

  await closeFxaPanel();
});

/**
 * When signed out, the device list is hidden. There are no synced devices
 * so no per-device "Send Current Page to This Device" button is reachable.
 */
add_task(async function test_device_list_hidden_when_signed_out() {
  gSync.updateAllUI({ status: UIState.STATUS_NOT_CONFIGURED });
  await openFxaPanel();

  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-menu-devices-list")
      .hidden,
    "Device list is hidden when signed out"
  );

  await closeFxaPanel();
});

/**
 * When signed in with sync disabled, the device list is hidden because
 * FxAMenuDeviceList hides it when syncEnabled is false.
 */
add_task(async function test_device_list_hidden_when_sync_disabled() {
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    displayName: "Foo Bar",
  });
  await openFxaPanel();

  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-menu-devices-list")
      .hidden,
    "Device list is hidden when sync is disabled"
  );

  await closeFxaPanel();
});

/**
 * In the account menu, only the first few devices are shown inline; when there
 * are more, an "All Devices" button opens a panel listing every device along
 * with the Add / Manage / Don't-see-your-device actions.
 */
add_task(async function test_all_devices_button_and_panel() {
  await promiseSyncReady();
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  // The device list is driven by the full FxA account device list; each
  // account device here also has a matching Sync tab-client.
  let devices = [1, 2, 3, 4].map(i => ({
    id: `dev${i}`,
    name: `Device ${i}`,
    isCurrentDevice: false,
    lastAccessTime: Date.now(),
    availableCommands: {},
  }));
  let clients = [1, 2, 3, 4].map(i => ({
    id: `client${i}`,
    name: `Device ${i}`,
    lastModified: Date.now(),
    tabs: [],
  }));
  sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => devices);
  sandbox.stub(SyncedTabs, "getTabClients").resolves(clients);
  sandbox
    .stub(Weave.Service.clientsEngine, "getClientFxaDeviceId")
    .callsFake(clientId => clientId.replace("client", "dev"));
  // Keep the panel's own init from racing with the manual update below.
  sandbox.stub(SyncedTabs, "isConfiguredToSyncTabs").get(() => false);

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesList = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  await panelview.syncedTabsPanelList._doUpdateDeviceList();

  Assert.equal(
    devicesList.querySelectorAll(".PanelUI-fxa-menu-device-entry").length,
    window.FxAMenuDeviceList.MAX_DEVICES,
    "Only MAX_DEVICES device entries are shown inline"
  );
  let allDevicesButton = devicesList.querySelector(
    "#PanelUI-fxa-menu-all-devices-button"
  );
  Assert.ok(
    allDevicesButton,
    "All Devices button is shown when there are more than MAX_DEVICES devices"
  );

  let allDevicesPanel = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-all-devices"
  );
  let panelShown = BrowserTestUtils.waitForEvent(allDevicesPanel, "ViewShown");
  allDevicesButton.click();
  await panelShown;

  let allDevicesList = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-all-devices-list"
  );
  Assert.equal(
    allDevicesList.querySelectorAll(".PanelUI-fxa-menu-device-entry").length,
    devices.length,
    "Every account device is listed in the All Devices panel"
  );

  for (const id of [
    "PanelUI-fxa-menu-all-devices-add-device",
    "PanelUI-fxa-menu-all-devices-manage-devices",
    "PanelUI-fxa-menu-all-devices-device-missing",
  ]) {
    Assert.ok(
      BrowserTestUtils.isVisible(PanelMultiView.getViewNode(document, id)),
      `${id} is visible in the All Devices panel`
    );
  }

  await closeFxaPanel();
  sandbox.restore();
});

/**
 * Every device on the account is shown in the connected devices list, including
 * a device that has not synced any tabs (and therefore has no Sync tab-client).
 * The current device is excluded, and a device that has synced tabs keeps them.
 */
add_task(async function test_device_list_shows_all_account_devices() {
  await promiseSyncReady();
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  // Keep the panel's own init from racing with the manual update below.
  sandbox.stub(SyncedTabs, "isConfiguredToSyncTabs").get(() => false);

  let devices = [
    {
      id: "dev-with-tabs",
      name: "Laptop",
      isCurrentDevice: false,
      lastAccessTime: Date.now(),
      availableCommands: {},
    },
    {
      // No synced tabs, but Send Tab-compatible, so it is still shown.
      id: "dev-no-tabs",
      name: "Phone",
      isCurrentDevice: false,
      lastAccessTime: Date.now(),
      availableCommands: {
        "https://identity.mozilla.com/cmd/open-uri": "encrypted-payload",
      },
    },
    {
      id: "dev-current",
      name: "This Device",
      isCurrentDevice: true,
      lastAccessTime: Date.now(),
      availableCommands: {},
    },
  ];
  // Only the laptop has synced tabs, so only it has a matching Sync tab-client.
  let clients = [
    {
      id: "client-laptop",
      name: "Laptop",
      lastModified: Date.now(),
      tabs: [
        {
          title: "Example",
          url: "https://example.com/",
          icon: "",
          lastUsed: Date.now(),
          inactive: false,
        },
      ],
    },
  ];
  sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => devices);
  sandbox.stub(SyncedTabs, "getTabClients").resolves(clients);
  sandbox
    .stub(Weave.Service.clientsEngine, "getClientFxaDeviceId")
    .callsFake(clientId =>
      clientId === "client-laptop" ? "dev-with-tabs" : null
    );

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesList = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  await panelview.syncedTabsPanelList._doUpdateDeviceList();

  let entries = Array.from(
    devicesList.querySelectorAll(".PanelUI-fxa-menu-device-entry")
  );
  Assert.deepEqual(
    entries.map(e => e.getAttribute("label")).sort(),
    ["Laptop", "Phone"],
    "Both the tab-syncing device and the Send Tab-compatible device without synced tabs are shown, and the current device is excluded"
  );

  // The device without synced tabs must be functional: opening it shows the
  // recent tabs subview with the "no open tabs" empty state.
  let phoneEntry = entries.find(e => e.getAttribute("label") == "Phone");
  let subviewShown = BrowserTestUtils.waitForEvent(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
    "ViewShown"
  );
  phoneEntry.click();
  await subviewShown;
  ok(
    !PanelMultiView.getViewNode(document, "PanelUI-fxa-device-no-open-tabs")
      .hidden,
    "The device with no synced tabs shows the 'no open tabs' label"
  );

  await closeFxaPanel();
  sandbox.restore();
});

/**
 * When a device does not support the sendTab command, the
 * "Send Current Page to This Device" button is hidden in the recent tabs panel.
 */
add_task(async function test_send_page_button_hidden_for_incompatible_device() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  sandbox.stub(BrowserUtils, "getShareableURL").returnsArg(0);
  sandbox
    .stub(fxAccounts.commands.sendTab, "isDeviceCompatible")
    .returns(false);
  sandbox
    .stub(fxAccounts.commands.closeTab, "isDeviceCompatible")
    .returns(false);

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesListContainer = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  let mockDevice = { id: 1, name: "Device 1", availableCommands: {} };
  let mockClient = {
    id: "client1",
    name: "Device 1",
    lastModified: Date.now(),
    tabs: [
      {
        title: "Tab 1",
        url: "https://example.com/",
        icon: "",
        lastUsed: Date.now(),
        inactive: false,
      },
    ],
  };

  let subviewShown = BrowserTestUtils.waitForEvent(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
    "ViewShown"
  );
  panelview.syncedTabsPanelList._showDeviceRecentTabs(
    mockClient,
    mockDevice,
    devicesListContainer,
    new PointerEvent("click")
  );
  await subviewShown;

  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-send-current-page")
      .hidden,
    "Send Current Page button is hidden when device does not support sendTab"
  );

  await closeFxaPanel();
  sandbox.restore();
});

/**
 * When the current page's URL is not shareable (e.g. an about: page),
 * the "Send Current Page to This Device" button is hidden in the recent tabs panel.
 */
add_task(async function test_send_page_button_hidden_for_non_shareable_url() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  sandbox.stub(BrowserUtils, "getShareableURL").returns(null);
  sandbox.stub(fxAccounts.commands.sendTab, "isDeviceCompatible").returns(true);
  sandbox
    .stub(fxAccounts.commands.closeTab, "isDeviceCompatible")
    .returns(false);

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesListContainer = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  let mockDevice = {
    id: 1,
    name: "Device 1",
    availableCommands: {
      "https://identity.mozilla.com/cmd/open-uri": "baz",
    },
  };
  let mockClient = {
    id: "client1",
    name: "Device 1",
    lastModified: Date.now(),
    tabs: [
      {
        title: "Tab 1",
        url: "https://example.com/",
        icon: "",
        lastUsed: Date.now(),
        inactive: false,
      },
    ],
  };

  let subviewShown = BrowserTestUtils.waitForEvent(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
    "ViewShown"
  );
  panelview.syncedTabsPanelList._showDeviceRecentTabs(
    mockClient,
    mockDevice,
    devicesListContainer,
    new PointerEvent("click")
  );
  await subviewShown;

  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-send-current-page")
      .hidden,
    "Send Current Page button is hidden when URL is not shareable"
  );

  await closeFxaPanel();
  sandbox.restore();
});

/**
 * When a device has no open tabs, the recent tabs panel hides the tabs list,
 * footer separator and "View all" button, and shows the "No open tabs" label.
 */
add_task(async function test_recent_tabs_panel_no_open_tabs() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  sandbox.stub(BrowserUtils, "getShareableURL").returnsArg(0);
  sandbox
    .stub(fxAccounts.commands.sendTab, "isDeviceCompatible")
    .returns(false);
  sandbox
    .stub(fxAccounts.commands.closeTab, "isDeviceCompatible")
    .returns(false);

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesListContainer = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  let mockDevice = { id: 1, name: "Device 1", availableCommands: {} };
  let mockClient = {
    id: "client1",
    name: "Device 1",
    lastModified: Date.now(),
    tabs: [],
  };

  let subviewShown = BrowserTestUtils.waitForEvent(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
    "ViewShown"
  );
  panelview.syncedTabsPanelList._showDeviceRecentTabs(
    mockClient,
    mockDevice,
    devicesListContainer,
    new PointerEvent("click")
  );
  await subviewShown;

  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs-list")
      .hidden,
    "Tabs list is hidden when the device has no open tabs"
  );
  ok(
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-device-recent-tabs-footer-separator"
    ).hidden,
    "Footer separator is hidden when the device has no open tabs"
  );
  ok(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-view-all-tabs")
      .hidden,
    "View all tabs button is hidden when the device has no open tabs"
  );
  ok(
    !PanelMultiView.getViewNode(document, "PanelUI-fxa-device-no-open-tabs")
      .hidden,
    "No open tabs label is shown when the device has no open tabs"
  );

  await closeFxaPanel();
  sandbox.restore();
});

/**
 * When a device has no open tabs but a tab can still be sent to it, the footer
 * separator is kept so it visually divides the "no open tabs" label from the
 * "Send Current Page to This Device" button.
 */
add_task(async function test_recent_tabs_panel_no_open_tabs_can_send() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  sandbox.stub(BrowserUtils, "getShareableURL").returnsArg(0);
  sandbox.stub(fxAccounts.commands.sendTab, "isDeviceCompatible").returns(true);
  sandbox
    .stub(fxAccounts.commands.closeTab, "isDeviceCompatible")
    .returns(false);

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesListContainer = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  let mockDevice = {
    id: 1,
    name: "Device 1",
    availableCommands: {
      "https://identity.mozilla.com/cmd/open-uri": "encrypted-payload",
    },
  };
  let mockClient = {
    id: "client1",
    name: "Device 1",
    lastModified: Date.now(),
    tabs: [],
  };

  let subviewShown = BrowserTestUtils.waitForEvent(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
    "ViewShown"
  );
  panelview.syncedTabsPanelList._showDeviceRecentTabs(
    mockClient,
    mockDevice,
    devicesListContainer,
    new PointerEvent("click")
  );
  await subviewShown;

  ok(
    !PanelMultiView.getViewNode(document, "PanelUI-fxa-device-no-open-tabs")
      .hidden,
    "No open tabs label is shown when the device has no open tabs"
  );
  ok(
    !PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-device-send-current-page"
    ).hidden,
    "Send Current Page button is shown when a tab can be sent to the device"
  );
  ok(
    !PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-device-recent-tabs-footer-separator"
    ).hidden,
    "Footer separator is kept between the no-open-tabs label and the send button"
  );

  await closeFxaPanel();
  sandbox.restore();
});

/**
 * The FxA device list is refreshed asynchronously, so it may not be populated
 * when a device entry is first created (e.g. on the first menu open right after
 * signing in). Clicking the entry must resolve the device again so the "Send
 * Current Page to This Device" button shows as soon as the device is known to
 * be sendTab-capable, rather than only on a later open.
 */
add_task(
  async function test_send_page_button_visible_when_device_resolved_late() {
    const sandbox = sinon.createSandbox();
    sandbox.stub(UIState, "get").returns({
      status: UIState.STATUS_SIGNED_IN,
      syncEnabled: true,
    });
    sandbox.stub(SyncedTabs, "isConfiguredToSyncTabs").get(() => false);
    sandbox.stub(BrowserUtils, "getShareableURL").returnsArg(0);
    sandbox
      .stub(fxAccounts.commands.sendTab, "isDeviceCompatible")
      .returns(true);
    sandbox
      .stub(fxAccounts.commands.closeTab, "isDeviceCompatible")
      .returns(false);

    let device = {
      id: "dev-1",
      name: "Device 1",
      isCurrentDevice: false,
      lastAccessTime: Date.now(),
      availableCommands: {
        "https://identity.mozilla.com/cmd/open-uri": "encrypted-payload",
      },
    };
    let client = {
      id: "client-1",
      name: "Device 1",
      lastModified: Date.now(),
      tabs: [],
    };

    // The device list is not populated yet when the entry is created, then the
    // async refresh completes before the user clicks.
    let deviceListReady = false;
    sandbox
      .stub(fxAccounts.device, "recentDeviceList")
      .get(() => (deviceListReady ? [device] : null));
    sandbox
      .stub(Weave.Service.clientsEngine, "getClientFxaDeviceId")
      .callsFake(id => (id === "client-1" ? "dev-1" : null));

    gSync.updateAllUI({
      status: UIState.STATUS_SIGNED_IN,
      syncEnabled: true,
      email: "foo@bar.com",
    });
    await openFxaPanel();

    let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
    let devicesList = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-devices-list"
    );
    let list = panelview.syncedTabsPanelList;

    // Create the entry while the device list is still unresolved.
    let entry = list._createDeviceEntry(
      client,
      list._getDeviceForClient(client)
    );
    devicesList.appendChild(entry);

    // The async device refresh completes before the click.
    deviceListReady = true;

    let subviewShown = BrowserTestUtils.waitForEvent(
      PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
      "ViewShown"
    );
    entry.click();
    await subviewShown;

    ok(
      !PanelMultiView.getViewNode(
        document,
        "PanelUI-fxa-device-send-current-page"
      ).hidden,
      "Send Current Page button is visible on first click once the device is resolved"
    );

    await closeFxaPanel();
    sandbox.restore();
  }
);

/**
 * Regression test for bug 2057197 / bug 2058595: in the FxA menu's per-device
 * recent tabs panel, each row is a toolbaritem holding the tab button and its
 * sibling close/undo buttons. Closing a tab disables the tab button; because
 * Undo is a sibling (not nested inside the disabled button), it stays reachable
 * - both clickable and exposed via the accessibility API - so the close can be
 * undone. This synthesizes real mouse events so that pointer-events are honored
 * (element.click() would bypass them).
 */
add_task(async function test_recent_tabs_close_then_undo() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
  });
  sandbox.stub(BrowserUtils, "getShareableURL").returnsArg(0);
  sandbox
    .stub(fxAccounts.commands.sendTab, "isDeviceCompatible")
    .returns(false);
  sandbox
    .stub(fxAccounts.commands.closeTab, "isDeviceCompatible")
    .returns(true);

  // Avoid touching the real remote command store; just observe the calls.
  let enqueueStub = sandbox
    .stub(SyncedTabsManagement, "enqueueTabToClose")
    .resolves();
  let removeStub = sandbox
    .stub(SyncedTabsManagement, "removePendingTabToClose")
    .resolves();

  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
  });
  await openFxaPanel();

  let panelview = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let devicesListContainer = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-devices-list"
  );
  let mockDevice = {
    id: "dev-1",
    name: "Device 1",
    availableCommands: {
      "https://identity.mozilla.com/cmd/close-uri": "baz",
    },
  };
  let mockClient = {
    id: "client-1",
    name: "Device 1",
    lastModified: Date.now(),
    tabs: [
      {
        title: "Tab 1",
        url: "https://example.com/",
        icon: "",
        lastUsed: Date.now(),
        inactive: false,
      },
    ],
  };

  let subviewShown = BrowserTestUtils.waitForEvent(
    PanelMultiView.getViewNode(document, "PanelUI-fxa-device-recent-tabs"),
    "ViewShown"
  );
  panelview.syncedTabsPanelList._showDeviceRecentTabs(
    mockClient,
    mockDevice,
    devicesListContainer,
    new PointerEvent("click")
  );
  await subviewShown;

  let tabsList = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-device-recent-tabs-list"
  );
  let tabItem = tabsList.querySelector("toolbaritem.all-tabs-item");
  let tabButton = tabItem.querySelector(".all-tabs-button");
  let closeBtn = tabItem.querySelector(".all-tabs-close-button");
  let undoBtn = tabItem.querySelector(".remote-tabs-undo-button");
  ok(closeBtn, "Close button is present");
  ok(undoBtn, "Undo button is present");
  ok(undoBtn.hidden, "Undo button is hidden before closing");

  EventUtils.synthesizeMouseAtCenter(closeBtn, {}, window);

  ok(enqueueStub.calledOnce, "Closing the tab queued a remote close");
  ok(
    tabButton.disabled,
    "Tab button is disabled after closing (Undo stays reachable as a sibling)"
  );
  ok(closeBtn.hidden, "Close button is hidden after closing");
  ok(!undoBtn.hidden, "Undo button is shown after closing");

  EventUtils.synthesizeMouseAtCenter(undoBtn, {}, window);

  ok(
    removeStub.calledOnce,
    "Clicking Undo removed the pending remote close (bug 2057197)"
  );
  ok(!tabButton.disabled, "Tab button is re-enabled after undo");
  ok(undoBtn.hidden, "Undo button is hidden after undo");
  ok(!closeBtn.hidden, "Close button is shown again after undo");

  await closeFxaPanel();
  sandbox.restore();
});

add_task(async function test_sync_status_button_visible_when_sync_on() {
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };
  gSync.updateAllUI(state);
  await openFxaPanel();

  const syncStatusBtn = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-button"
  );
  ok(
    BrowserTestUtils.isVisible(syncStatusBtn),
    "Sync status button is visible when signed in with sync on"
  );
  is(
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-title"
    ).getAttribute("value"),
    gSync.fluentStrings.formatValueSync("fxa-menu-sync-status-on"),
    "Sync status title reads 'Sync is on'"
  );

  await closeFxaPanel();
});

add_task(async function test_sync_status_button_sync_off_signed_in() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    displayName: "Foo Bar",
  });
  let openPrefsStub = sandbox.stub(gSync, "openPrefsFromFxaMenu");

  gSync.updateAllUI(UIState.get());
  await openFxaPanel();

  const syncStatusBtn = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-button"
  );
  ok(
    syncStatusBtn.hidden,
    "Navigable sync status button is hidden when signed in but sync is off"
  );

  const offCard = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-off-card"
  );
  ok(
    BrowserTestUtils.isVisible(offCard),
    "Sync-off card is visible when signed in but sync is off"
  );
  ok(
    offCard.querySelector(".fxa-menu-sync-status-icon"),
    "Sync-off card shows the sync status icon"
  );
  is(
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-off-title"
    ).getAttribute("value"),
    gSync.fluentStrings.formatValueSync("fxa-menu-sync-status-off"),
    "Sync status title reads 'Sync is Off'"
  );
  const descEl = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-off-description"
  );
  is(
    descEl.getAttribute("value"),
    gSync.fluentStrings.formatValueSync("fxa-menu-sync-off-data-description"),
    "Description reads 'Your data isn't syncing'"
  );
  ok(
    descEl.classList.contains("fxa-menu-sync-status-description-error"),
    "Description uses the error color"
  );

  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-off-button"
  ).click();
  await hidden;
  ok(
    openPrefsStub.called,
    "Clicking 'Turn on' opens preferences to turn sync on when signed in"
  );

  sandbox.restore();
});

add_task(async function test_sync_status_button_sync_off_signed_out() {
  const sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_NOT_CONFIGURED,
  });
  let signInStub = sandbox.stub(gSync, "openFxAEmailFirstPageFromFxaMenu");

  gSync.updateAllUI(UIState.get());

  const syncStatusBtn = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-button"
  );
  ok(!syncStatusBtn.hidden, "Sync status button is shown when signed out");
  is(
    PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-title"
    ).getAttribute("value"),
    gSync.fluentStrings.formatValueSync("fxa-menu-sync-your-data"),
    "Sync status title reads 'Sync Your Data' when never signed in"
  );
  const descEl = PanelMultiView.getViewNode(
    document,
    "PanelUI-fxa-menu-sync-status-description"
  );
  ok(
    !descEl.hasAttribute("value"),
    "No description value is set when never signed in"
  );
  ok(descEl.hidden, "Description label is not rendered when never signed in");
  ok(
    !descEl.classList.contains("fxa-menu-sync-status-description-error"),
    "Description does not use the error color when never signed in"
  );

  gSync._onSyncStatusButtonClick(syncStatusBtn, new PointerEvent("click"));
  ok(signInStub.called, "Clicking leads to the sign-in page when signed out");

  sandbox.restore();
});

add_task(
  async function test_secure_sync_subpanel_opens_and_has_correct_buttons() {
    let state = {
      status: UIState.STATUS_SIGNED_IN,
      syncEnabled: true,
      email: "foo@bar.com",
      displayName: "Foo Bar",
      avatarURL: "https://foo.bar",
      lastSync: new Date(),
      syncing: false,
    };
    // The sync status button routes its click based on the live UIState, so it
    // needs to report sync as on to open the secure sync subpanel.
    const sandbox = sinon.createSandbox();
    sandbox.stub(UIState, "get").returns(state);
    gSync.updateAllUI(state);
    await openFxaPanel();

    const syncStatusBtn = PanelMultiView.getViewNode(
      document,
      "PanelUI-fxa-menu-sync-status-button"
    );
    let subviewShown = BrowserTestUtils.waitForEvent(
      PanelMultiView.getViewNode(
        document,
        "PanelUI-fxa-menu-secure-sync-subpanel"
      ),
      "ViewShown"
    );
    syncStatusBtn.click();
    await subviewShown;

    for (const id of [
      "PanelUI-fxa-menu-secure-sync-now",
      "PanelUI-fxa-menu-secure-sync-settings",
      "PanelUI-fxa-menu-secure-sync-add-device",
      "PanelUI-fxa-menu-secure-sync-manage-devices",
      "PanelUI-fxa-menu-secure-sync-device-missing",
    ]) {
      ok(
        BrowserTestUtils.isVisible(PanelMultiView.getViewNode(document, id)),
        `${id} is visible in the secure sync subpanel`
      );
    }

    await closeFxaPanel();
    sandbox.restore();
  }
);

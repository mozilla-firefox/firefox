/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MenuMessage } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/MenuMessage.sys.mjs"
);

const { PanelTestProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/PanelTestProvider.sys.mjs"
);

const { AboutWelcomeTelemetry } = ChromeUtils.importESModule(
  "resource:///modules/aboutwelcome/AboutWelcomeTelemetry.sys.mjs"
);

const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);

const { AppMenuNotifications } = ChromeUtils.importESModule(
  "resource://gre/modules/AppMenuNotifications.sys.mjs"
);

const { RemoteL10n } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/RemoteL10n.sys.mjs"
);

/**
 * Hides all popups for all sources for a given window if any of them are
 * open.
 *
 * @param {DOMWindow} win
 *   The window to close popups for.
 * @returns {Promise<undefined>}
 */
async function hideAllPopups(win = window) {
  if (win.PanelUI.panel.state === "open") {
    let panelHidden = BrowserTestUtils.waitForEvent(
      win.PanelUI.panel,
      "popuphidden"
    );
    win.PanelUI.hide();
    await panelHidden;
  }
  let widgetPanel = win.document.getElementById("customizationui-widget-panel");
  // The customizationui-widget-panel is created lazily, and destroyed upon
  // closing, meaning that if we didn't find it, it's not open.
  if (widgetPanel) {
    let panelHidden = BrowserTestUtils.waitForEvent(widgetPanel, "popuphidden");
    widgetPanel.hidePopup();
    await panelHidden;
  }
}

/**
 * Asserts that a given message is correctly rendered and visible in the
 * opened menu source for `win`.
 *
 * This function checks if the `menu-message` element exists within the
 * source panel and ensures that it contains the correct content and properties
 * based on the provided message object.
 *
 * It validates that:
 *  - The element is present and visible.
 *  - The primary text, secondary text, image URL, and button text match the
 *    content of the message.
 *  - The element has a `navigableWithTabOnly` data attribute set to "true".
 *
 * For the MenuMessage.SOURCES.APP_MENU source, it also ensures that the default
 * sign-in button as well as the default separator are hidden when the message
 * is visible.
 *
 * For the MenuMessage.SOURCES.PXI_MENU source, it ensures that the default
 * sign-in button is hidden when the message is visible.
 *
 * @param {string} source
 *   The menu message source panel to check. One of the string constants from
 *   MenuMessage.SOURCES (example: MenuMessage.SOURCES.APP_MENU).
 * @param {object} message
 *   The message object containing the content to be checked.
 * @param {DOMWindow} [win=window]
 *   The window object where the panel UI and the app menu message are located
 *   (defaults to the main window).
 */
async function assertMessageInMenuSource(source, message, win = window) {
  let messageEl;
  if (source === MenuMessage.SOURCES.APP_MENU) {
    messageEl = win.PanelUI.mainView.querySelector("menu-message");
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    messageEl = win.document.querySelector("#PanelUI-fxa menu-message");
  }
  await messageEl.updateComplete;

  Assert.ok(messageEl, "Found the menu-message element.");
  Assert.ok(BrowserTestUtils.isVisible(messageEl, "menu-message is visible."));

  Assert.equal(
    messageEl.primaryText,
    message.content.primaryText,
    "The primary text was set."
  );

  // Simple layout messages do not have secondary text
  if (message.content.layout === "simple") {
    Assert.equal(
      win.getComputedStyle(messageEl.shadowRoot.querySelector("#secondary"))
        .display,
      "none",
      "Secondary text is not visible, even if provided."
    );
  } else {
    Assert.equal(
      messageEl.secondaryText,
      message.content.secondaryText,
      "The secondary text was set."
    );
  }

  Assert.equal(
    messageEl.imageURL,
    message.content.imageURL,
    "The imageURL property was set."
  );
  Assert.equal(
    messageEl.buttonText,
    message.content.primaryActionText,
    "The buttonText property was set."
  );
  Assert.equal(
    messageEl.dataset.navigableWithTabOnly,
    "true",
    "The element should be configured for tab navigation."
  );

  const expectedLayout = message.content.layout ?? "column";
  Assert.equal(
    messageEl.layout,
    expectedLayout,
    `The layout should be '${expectedLayout}'.`
  );

  const cs = win.getComputedStyle(messageEl);
  if (expectedLayout === "row") {
    if (message.content.imageVerticalBottomOffset !== undefined) {
      Assert.equal(
        cs.getPropertyValue("--illustration-margin-block-end-offset"),
        `${message.content.imageVerticalBottomOffset}px`,
        "Row layout: bottom illustration offset matches."
      );
    }
    if (message.content.imageVerticalTopOffset !== undefined) {
      Assert.equal(
        cs.getPropertyValue("--illustration-margin-block-start-offset"),
        `${message.content.imageVerticalTopOffset}px`,
        "Row layout: top illustration offset matches."
      );
    }
  } else if (expectedLayout === "column") {
    if (message.content.imageVerticalTopOffset !== undefined) {
      Assert.equal(
        cs.getPropertyValue("--illustration-margin-block-start-offset"),
        `${message.content.imageVerticalTopOffset}px`,
        "Column layout: top illustration offset matches."
      );
    }
  } else if (expectedLayout === "simple") {
    // No close button, no secondary text, no images shown
    const shadow = messageEl.shadowRoot;
    const closeBtn = shadow.querySelector("#close-button");
    const secondary = shadow.querySelector("#secondary");
    Assert.equal(
      getComputedStyle(closeBtn).display,
      "none",
      "Close button hidden in simple layout."
    );
    Assert.equal(
      getComputedStyle(secondary).display,
      "none",
      "Secondary text hidden in simple layout."
    );
    for (const img of shadow.querySelectorAll("img")) {
      Assert.equal(
        getComputedStyle(img).display,
        "none",
        "Images hidden in simple layout."
      );
    }
  }

  if (source === MenuMessage.SOURCES.APP_MENU) {
    // The zap gradient and the default sign-in button should be hidden.
    const isFxAMessage =
      message.content.messageType === MenuMessage.MESSAGE_TYPES.FXA_CTA;

    if (isFxAMessage) {
      // fxa_cta replaces the FxA row in the App Menu.
      Assert.ok(
        BrowserTestUtils.isHidden(
          win.PanelUI.mainView.querySelector("#appMenu-fxa-separator")
        ),
        "Zap gradient separator is hidden in the AppMenu for fxa_cta."
      );
      Assert.ok(
        BrowserTestUtils.isHidden(
          win.PanelUI.mainView.querySelector("#appMenu-fxa-status2")
        ),
        "Default FxA sign-in button is hidden in the AppMenu for fxa_cta."
      );
    } else {
      // default_cta should not replace fxa row in the App menu.
      Assert.ok(
        BrowserTestUtils.isVisible(
          win.PanelUI.mainView.querySelector("#appMenu-fxa-separator")
        ),
        "Zap gradient separator is visible in the AppMenu for default_cta."
      );
      Assert.ok(
        BrowserTestUtils.isVisible(
          win.PanelUI.mainView.querySelector("#appMenu-fxa-status2")
        ),
        "Default FxA sign-in button is visible in the AppMenu for default_cta."
      );
    }
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    Assert.ok(
      BrowserTestUtils.isHidden(
        win.document.querySelector("#PanelUI-fxa-menu-sign-in-promo")
      ),
      "Default FxA sign-in promo in the PXI panel is hidden."
    );
  }

  return messageEl;
}

/**
 * Asserts that no menu-message is rendered in the opened menu source for
 * `win`.
 *
 * For the MenuMessage.SOURCES.APP_MENU source, it also ensures that the default
 * sign-in button as well as the default separator are visible.
 *
 * For the MenuMessage.SOURCES.PXI_MENU source, it ensures that the default
 * sign-in button is visible.
 *
 * @param {string} source
 *   The menu message source panel to check. One of the string constants from
 *   MenuMessage.SOURCES (example: MenuMessage.SOURCES.APP_MENU).
 * @param {DOMWindow} [win=window]
 *   The window object for the panel to check (defaults to the main window).
 */
function assertNoMessageInMenuSource(source, win = window) {
  let messageEl;
  if (source === MenuMessage.SOURCES.APP_MENU) {
    messageEl = win.PanelUI.mainView.querySelector("menu-message");
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    messageEl = win.document.querySelector("#PanelUI-fxa menu-message");
  }

  Assert.ok(!messageEl, "Should not have found an menu-message");

  if (source === MenuMessage.SOURCES.APP_MENU) {
    // The zap gradient should be visible.
    Assert.ok(
      BrowserTestUtils.isVisible(
        win.PanelUI.mainView.querySelector("#appMenu-fxa-separator")
      ),
      "Zap gradient separator is visible."
    );
    // When signed out, the default sign-in affordance is shown: the richer
    // promo when nothing else occupies the top of the menu, or the compact
    // sign-in row when an update banner is present.
    const updateBanner = win.PanelUI.mainView.querySelector(
      "#appMenu-update-banner"
    );
    if (updateBanner && BrowserTestUtils.isVisible(updateBanner)) {
      Assert.ok(
        BrowserTestUtils.isVisible(
          win.PanelUI.mainView.querySelector("#appMenu-fxa-status2")
        ),
        "Compact FxA sign-in row is visible when an update banner is present."
      );
    } else {
      Assert.ok(
        BrowserTestUtils.isVisible(
          win.PanelUI.mainView.querySelector("#appMenu-fxa-sign-in-promo")
        ),
        "Default FxA sign-in promo is visible."
      );
    }
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    Assert.ok(
      BrowserTestUtils.isVisible(
        win.document.querySelector("#PanelUI-fxa-menu-sign-in-promo")
      ),
      "Default FxA sign-in promo in the PXI panel is visible."
    );
  }
}

/**
 * Closes and re-opens one of the source menus for a browser window, and then
 * checks to see if the expected message (or no message) is displayed within
 * it. Afterwards, it closes the panel.
 *
 * @param {string} source
 *   The menu message source panel to open. One of the string constants from
 *   MenuMessage.SOURCES (example: MenuMessage.SOURCES.APP_MENU).
 * @param {object|null} expectedMessage
 *   The message that is expected to be displayed in the menu source, or null
 *   if no message is expected.
 * @param {DOMWindow} [win=window]
 *   The browser window to open the AppMenu for.
 * @param {Function} taskFn
 *   An optional async function to call after the panel is opened and before
 *   it is closed again. The taskFn is passed the msgElement as its first
 *   argument, and the containing <panel> as the second.
 * @returns {Promise<undefined>}
 */
async function reopenMenuSource(source, expectedMessage, win = window, taskFn) {
  await hideAllPopups(win);

  let promiseViewShown;
  let panel;

  if (source === MenuMessage.SOURCES.APP_MENU) {
    promiseViewShown = BrowserTestUtils.waitForEvent(
      win.PanelUI.panel,
      "ViewShown"
    );
    win.PanelUI.show();
    panel = win.PanelUI.panel;
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    promiseViewShown = BrowserTestUtils.waitForEvent(
      PanelMultiView.getViewNode(win.document, "PanelUI-fxa"),
      "ViewShown"
    );
    await win.gSync.toggleAccountPanel(
      win.document.getElementById("fxa-toolbar-menu-button"),
      new MouseEvent("mousedown")
    );
    panel = win.document.getElementById("customizationui-widget-panel");
  }

  info(`Waiting for menu source ${source} to open`);
  await promiseViewShown;
  info(`Menu source ${source} opened`);

  let messageEl = null;
  if (expectedMessage) {
    messageEl = await assertMessageInMenuSource(source, expectedMessage, win);
  } else {
    assertNoMessageInMenuSource(source, win);
  }

  if (taskFn) {
    await taskFn(messageEl, panel);
  }

  await hideAllPopups(win);
  // Now ensure that there are no menu-message's in the window anymore,
  // now that all the panels are closed.
  Assert.ok(
    !win.document.querySelector("menu-message"),
    "Should not find any menu-message elements"
  );
}

/**
 * A utility function to iterate all of the current menu message sources and
 * run some async function for each.
 *
 * @param {function} taskFn
 *   An async function that is passed the source string for each source.
 * @returns {Promise<undefined>}
 */
async function withEachSource(taskFn) {
  for (let source of [
    MenuMessage.SOURCES.APP_MENU,
    MenuMessage.SOURCES.PXI_MENU,
  ]) {
    info(`Trying source ${source}`);
    await taskFn(source);
  }
}

let gTestFxAMessage;

// Pref that flips when the PXI panel is opened.
const PREF_PXI_PANEL_ACCESSED =
  "identity.fxaccounts.toolbar.syncSetup.panelAccessed";

add_setup(async function () {
  Services.fog.testResetFOG();

  gTestFxAMessage = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(msg => msg.id === "FXA_ACCOUNTS_PXIMENU_ROW_LAYOUT")
  );
  Assert.ok(gTestFxAMessage, "Found a test fxa_cta message to use.");

  // The testing message defaults to displaying in the AppMenu via the
  // testingTriggerContext property. That's only useful for manual testing,
  // and will confuse things for automated testing, so we remove that property
  // here.
  delete gTestFxAMessage.testingTriggerContext;

  await SpecialPowers.pushPrefEnv({
    set: [["browser.newtabpage.activity-stream.telemetry", true]],
  });
  registerCleanupFunction(async () => {
    Services.fog.testResetFOG();
  });

  // Ensure the current window has had gSync.init run so that the PXI panel
  // works correctly.
  gSync.init();

  // Make sure that we always end the test with the panels closed.
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(
      "identity.fxaccounts.toolbar.syncSetup.panelAccessed"
    );
    await hideAllPopups();
  });
});

function buildDefaultCtaMessage({
  id = "TEST_DEFAULT_CTA",
  layout = "column",
} = {}) {
  return {
    id,
    template: "menu_message",
    content: {
      layout,
      messageType: "default_cta",
      primaryText: "Firefox is not your default browser",
      primaryActionText: "Set as default",
      primaryButtonSize: "small",
      logoURL: "chrome://branding/content/about-logo.svg",
      secondaryText:
        "Make Firefox your default browser to open links from other apps.",
      primaryAction: {},
      closeAction: {},
    },
    targeting: "true",
    trigger: { id: "menuOpened" },
    groups: [],
  };
}

/**
 * Tests that opening each menu source causes the menuOpened trigger to fire.
 * We stub ASRouter to return no messages so this test is message-free.
 */
add_task(async function test_trigger() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(ASRouter, "sendTriggerMessage");
  const handleMessageRequestStub = sandbox
    .stub(ASRouter, "handleMessageRequest")
    .resolves([]);

  await reopenMenuSource(MenuMessage.SOURCES.APP_MENU);
  Assert.ok(
    ASRouter.sendTriggerMessage.calledWith({
      browser: gBrowser.selectedBrowser,
      id: "menuOpened",
      context: {
        source: MenuMessage.SOURCES.APP_MENU,
        browserIsSelected: true,
        isAIWindow: false,
        onThirdPartyPage: false,
      },
    }),
    "sendTriggerMessage was called when opening the AppMenu panel."
  );

  ASRouter.sendTriggerMessage.resetHistory();

  await reopenMenuSource(MenuMessage.SOURCES.PXI_MENU);
  Assert.ok(
    ASRouter.sendTriggerMessage.calledWith({
      browser: gBrowser.selectedBrowser,
      id: "menuOpened",
      context: {
        source: MenuMessage.SOURCES.PXI_MENU,
        browserIsSelected: true,
        isAIWindow: false,
        onThirdPartyPage: false,
      },
    }),
    "sendTriggerMessage was called when opening the PXI panel."
  );

  handleMessageRequestStub.restore();
  sandbox.restore();
});

/**
 * Tests that a registered MenuMessage of type fxa_cta will cause an
 * menu-message element to appear in either menu source panel with the right
 * attributes. This also tests that upon becoming visible, an impression is
 * recorded. This also tests that clicking upon a non-button part of the message
 * doesn't cause the panel to be closed.
 */
add_task(async function test_show_fxa_cta_message() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(ASRouter, "addImpression");
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  await withTestMessage(sandbox, gTestFxAMessage, async () => {
    await withEachSource(async source => {
      info(`Testing source ${source}`);
      await reopenMenuSource(
        source,
        gTestFxAMessage,
        window,
        async (msgElement, panel) => {
          // Let's make sure that the panel stays open if we dispatch a
          // generic click event on the message. This isn't an interactive
          // element, so we intentionally turn off the a11y check.
          AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
          msgElement.click();
          AccessibilityUtils.resetEnv();

          Assert.equal(
            panel.state,
            "open",
            "Panel should still be in the open state."
          );
        }
      );

      Assert.ok(
        ASRouter.addImpression.calledWith(gTestFxAMessage),
        "The test message had an impression recorded for it."
      );
      Assert.ok(
        AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
          sinon.match({
            message_id: gTestFxAMessage.id,
            event: "IMPRESSION",
            pingType: "menu",
            source,
          })
        ),
        "The test message had an impression recorded for it."
      );

      ASRouter.addImpression.resetHistory();
      AboutWelcomeTelemetry.prototype.submitGleanPingForPing.resetHistory();
    });
  });

  sandbox.restore();
});

/**
 * Tests that a default_cta menu message upon becoming visible should record an
 * impression.
 */
add_task(async function test_show_default_cta_message() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(ASRouter, "addImpression");
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  // Mimic slow Fluent lookups to ensure impressions are still recorded before
  // the message finishes rendering.
  let viewShowing = BrowserTestUtils.waitForEvent(
    PanelUI.mainView,
    "ViewShowing"
  );
  let formatStub = sandbox
    .stub(RemoteL10n, "formatLocalizableText")
    .callsFake(async (...args) => {
      await viewShowing;
      return formatStub.wrappedMethod.apply(RemoteL10n, args);
    });

  let defaultMsg = buildDefaultCtaMessage();

  Assert.ok(defaultMsg, "Found a test default_cta message to use.");

  await withTestMessage(sandbox, defaultMsg, async () => {
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, defaultMsg);
    // default_cta messsage type should not be shown in PXI menu
    await reopenMenuSource(MenuMessage.SOURCES.PXI_MENU, null);

    Assert.ok(
      ASRouter.addImpression.calledWith(defaultMsg),
      "The test message had an impression recorded for it."
    );
    Assert.ok(
      AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
        sinon.match({
          message_id: defaultMsg.id,
          event: "IMPRESSION",
          pingType: "menu",
          source: MenuMessage.SOURCES.APP_MENU,
        })
      ),
      "The test message had an impression recorded for it."
    );

    ASRouter.addImpression.resetHistory();
    AboutWelcomeTelemetry.prototype.submitGleanPingForPing.resetHistory();
  });

  sandbox.restore();
});

/**
 * Tests that a registered MenuMessage of type fxa_cta will cause an
 * menu-message element to appear in the menu sources for all newly
 * opened windows - and that, once blocked, will disappear from all menu
 * sources.
 */
add_task(async function test_show_fxa_cta_message_multiple_windows() {
  let sandbox = sinon.createSandbox();
  let win1 = window;
  let win2 = await BrowserTestUtils.openNewBrowserWindow();
  let win3 = await BrowserTestUtils.openNewBrowserWindow();

  // Ensure each new window has had gSync.init run so that the PXI panel works
  // correctly.
  win2.gSync.init();
  win3.gSync.init();

  await withTestMessage(
    sandbox,
    gTestFxAMessage,
    async handleMessageRequestStub => {
      let message = gTestFxAMessage;

      for (let win of [win1, win2, win3]) {
        await SimpleTest.promiseFocus(win);
        await withEachSource(async source => {
          await reopenMenuSource(source, message, win);
        });
      }

      // Now simulate blocking the message. We're not fully exercising ASRouter's
      // blocking mechanism here - we assume that if the action for blocking a
      // message occurs that ASRouter will do the right thing and stop returning
      // the blocked message with each `handleMessageRequest` call.
      await reopenMenuSource(
        MenuMessage.SOURCES.APP_MENU,
        gTestFxAMessage,
        win3,
        async () => {
          let win3Message = win3.PanelUI.mainView.querySelector("menu-message");
          await win3Message.updateComplete;
          win3Message.closeButton.click();
          Assert.ok(
            !win3Message.isConnected,
            "Closed message should have been immediately removed from the DOM."
          );
        }
      );

      // Fake out the blocking of the message.
      handleMessageRequestStub.resolves([]);

      for (let win of [win1, win2, win3]) {
        await SimpleTest.promiseFocus(win);
        await withEachSource(async source => {
          await reopenMenuSource(source, null, win);
        });
      }
    }
  );

  await BrowserTestUtils.closeWindow(win2);
  await BrowserTestUtils.closeWindow(win3);

  sandbox.restore();
});

/**
 * Tests that a registered MenuMessage of type fxa_cta will cause the
 * defined special message actions of the message to occur on both the
 * sign-in action button, as well as the close action button. This should
 * cause CLICK and DISMISS telemetry events, respectively.
 */
add_task(async function test_fxa_cta_actions() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");
  sandbox.stub(SpecialMessageActions, "handleAction");

  await withTestMessage(sandbox, gTestFxAMessage, async () => {
    await withEachSource(async source => {
      AboutWelcomeTelemetry.prototype.submitGleanPingForPing.resetHistory();
      SpecialMessageActions.handleAction.resetHistory();

      await reopenMenuSource(
        source,
        gTestFxAMessage,
        window,
        async (messageEl, panel) => {
          messageEl.primaryButton.click();
          Assert.notEqual(
            panel.state,
            "open",
            "Panel should have started to close."
          );
        }
      );

      // Depending on which source that has been opened, the entrypoint passed
      // in the SpecialMessageAction will be different.
      let clonedPrimaryAction = structuredClone(
        gTestFxAMessage.content.primaryAction
      );
      if (source === MenuMessage.SOURCES.APP_MENU) {
        clonedPrimaryAction.data.entrypoint = "fxa_app_menu";
        clonedPrimaryAction.data.extraParams.utm_content += "-app_menu";
      } else if (source === MenuMessage.SOURCES.PXI_MENU) {
        clonedPrimaryAction.data.entrypoint = "fxa_avatar_menu";
        clonedPrimaryAction.data.extraParams.utm_content += "-avatar";
      }

      Assert.ok(
        SpecialMessageActions.handleAction.calledWith(
          clonedPrimaryAction,
          gBrowser.selectedBrowser
        ),
        "The message action for signing up for an account was passed."
      );

      // Wait a tick for the telemetry to go through.
      await TestUtils.waitForTick();
      Assert.ok(
        AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
          sinon.match({
            message_id: gTestFxAMessage.id,
            event: "CLICK",
            pingType: "menu",
            source,
          })
        ),
        "A ping for clicking the message should have been passed."
      );

      AboutWelcomeTelemetry.prototype.submitGleanPingForPing.resetHistory();
      SpecialMessageActions.handleAction.resetHistory();
      await reopenMenuSource(
        source,
        gTestFxAMessage,
        window,
        async messageEl => {
          messageEl.closeButton.click();
        }
      );

      Assert.ok(
        SpecialMessageActions.handleAction.calledWith(
          gTestFxAMessage.content.closeAction,
          gBrowser.selectedBrowser
        ),
        "The message action for closing the message should have been passed."
      );
      // Wait a tick for the telemetry to go through.
      await TestUtils.waitForTick();
      Assert.ok(
        AboutWelcomeTelemetry.prototype.submitGleanPingForPing.calledWithMatch(
          sinon.match({
            message_id: gTestFxAMessage.id,
            event: "DISMISS",
            pingType: "menu",
            source,
          })
        ),
        "A ping for dismissing the message should have been passed."
      );
    });
  });

  sandbox.restore();
});

/**
 * Tests that a registered AppMenuMessage will NOT be displayed if an
 * AppMenuNotification exists. Once the AppMenuNotification exists, the
 * message can display.
 */
add_task(async function test_fxa_cta_notification_precedence() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(AboutWelcomeTelemetry.prototype, "submitGleanPingForPing");

  await withTestMessage(sandbox, gTestFxAMessage, async () => {
    const NOTIFICATION_ID = "update-restart";
    AppMenuNotifications.showNotification(NOTIFICATION_ID);
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, null);

    AppMenuNotifications.removeNotification(NOTIFICATION_ID);
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, gTestFxAMessage);
  });

  sandbox.restore();
});

add_task(async function test_default_cta_allowed_sources() {
  let sandbox = sinon.createSandbox();

  let defaultMsg = buildDefaultCtaMessage();

  Assert.ok(defaultMsg, "Found a test default_cta message to use.");

  await withTestMessage(sandbox, defaultMsg, async () => {
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, defaultMsg);
    // default_cta messsage type should not be shown in PXI menu
    await reopenMenuSource(MenuMessage.SOURCES.PXI_MENU, null);
  });

  sandbox.restore();
});

function stubSignedIn(sandbox, signedIn) {
  const { UIState } = ChromeUtils.importESModule(
    "resource://services-sync/UIState.sys.mjs"
  );
  return sandbox.stub(UIState, "get").returns({
    status: signedIn ? UIState.STATUS_SIGNED_IN : UIState.STATUS_NOT_CONFIGURED,
  });
}

add_task(async function test_message_type_suppression_rules() {
  let defaultMsg = buildDefaultCtaMessage({
    id: "TEST_DEFAULT_CTA_SIMPLE_LAYOUT",
    layout: "simple",
  });

  Assert.ok(defaultMsg, "Found a test default_cta message to use.");

  let sandbox = sinon.createSandbox();
  const signedInStub = stubSignedIn(sandbox, true);

  // fxa_cta suppressed when signed in
  await withTestMessage(sandbox, gTestFxAMessage, async () => {
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, null);
  });

  // fxa_cta allowed when allowWhenSignedIn = true
  const allowedFXA = structuredClone(gTestFxAMessage);
  allowedFXA.content = { ...allowedFXA.content, allowWhenSignedIn: true };
  await withTestMessage(sandbox, allowedFXA, async () => {
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, allowedFXA);
  });

  // default_cta never suppressed
  await withTestMessage(sandbox, defaultMsg, async () => {
    await reopenMenuSource(MenuMessage.SOURCES.APP_MENU, defaultMsg);
  });

  signedInStub.restore?.();
  sandbox.restore();
});

/**
 * 'default_cta' messages shown in the App Menu should not replace the FxA
 * sign-in row and should appear as its own banner above it.
 */
add_task(async function test_default_cta_does_not_replace_fxa_row() {
  let sandbox = sinon.createSandbox();

  const defaultMsg = buildDefaultCtaMessage({
    id: "TEST_DEFAULT_CTA_SIMPLE_LAYOUT",
    layout: "simple",
  });

  await withTestMessage(sandbox, defaultMsg, async () => {
    await reopenMenuSource(
      MenuMessage.SOURCES.APP_MENU,
      defaultMsg,
      window,
      async (_msgEl, panel) => {
        const view = panel.documentGlobal.PanelUI.mainView;
        const separator = view.querySelector("#appMenu-fxa-separator");
        const fxaRow = view.querySelector("#appMenu-fxa-status2");

        Assert.ok(
          BrowserTestUtils.isVisible(separator),
          "FxA separator remains visible for default_cta."
        );
        Assert.ok(
          BrowserTestUtils.isVisible(fxaRow),
          "FxA sign-in row remains visible for default_cta."
        );
      }
    );
  });

  sandbox.restore();
});

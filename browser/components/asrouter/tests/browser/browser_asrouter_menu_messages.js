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
 * This function checks if the `fxa-menu-message` element exists within the
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
    messageEl = win.PanelUI.mainView.querySelector("fxa-menu-message");
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    messageEl = win.document.querySelector("#PanelUI-fxa fxa-menu-message");
  }
  await messageEl.updateComplete;

  Assert.ok(messageEl, "Found the fxa-menu-message element.");
  Assert.ok(
    BrowserTestUtils.isVisible(messageEl, "fxa-menu-message is visible.")
  );

  Assert.equal(
    messageEl.primaryText,
    message.content.primaryText,
    "The primary text was set."
  );

  Assert.equal(
    messageEl.secondaryText,
    message.content.secondaryText,
    "The secondary text was set."
  );

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

  Assert.equal(
    messageEl.layout,
    "column",
    "The default layout should be 'column'."
  );

  let messageElStyles = window.getComputedStyle(messageEl);
  Assert.equal(
    messageElStyles.getPropertyValue(
      "--illustration-margin-block-start-offset"
    ),
    `${message.content.imageVerticalTopOffset}px`
  );

  if (source === MenuMessage.SOURCES.APP_MENU) {
    // The zap gradient and the default sign-in button should be hidden.
    Assert.ok(
      BrowserTestUtils.isHidden(
        win.PanelUI.mainView.querySelector("#appMenu-fxa-separator")
      ),
      "Zap gradient separator is hidden in the AppMenu."
    );
    Assert.ok(
      BrowserTestUtils.isHidden(
        win.PanelUI.mainView.querySelector("#appMenu-fxa-status2")
      ),
      "Default FxA sign-in button is hidden in the AppMenu."
    );
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    Assert.ok(
      BrowserTestUtils.isHidden(
        win.document.querySelector("#fxa-manage-account-button")
      ),
      "Default FxA sign-in button in the PXI panel is hidden."
    );
  }

  return messageEl;
}

/**
 * Asserts that no fxa-menu-message is rendered in the opened menu source for
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
    messageEl = win.PanelUI.mainView.querySelector("fxa-menu-message");
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    messageEl = win.document.querySelector("#PanelUI-fxa fxa-menu-message");
  }

  Assert.ok(!messageEl, "Should not have found an fxa-menu-message");

  if (source === MenuMessage.SOURCES.APP_MENU) {
    // The zap gradient and the default sign-in button should be visible.
    Assert.ok(
      BrowserTestUtils.isVisible(
        win.PanelUI.mainView.querySelector("#appMenu-fxa-separator")
      ),
      "Zap gradient separator is visible."
    );
    Assert.ok(
      BrowserTestUtils.isVisible(
        win.PanelUI.mainView.querySelector("#appMenu-fxa-status2")
      ),
      "Default FxA sign-in button is visible."
    );
  } else if (source === MenuMessage.SOURCES.PXI_MENU) {
    Assert.ok(
      BrowserTestUtils.isVisible(
        win.document.querySelector("#fxa-manage-account-button")
      ),
      "Default FxA sign-in button in the PXI panel is visible."
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
  // Now ensure that there are no fxa-menu-message's in the window anymore,
  // now that all the panels are closed.
  Assert.ok(
    !win.document.querySelector("fxa-menu-message"),
    "Should not find any fxa-menu-message elements"
  );
}

/**
 * Sets up stubs for ASRouter methods to simulate a scenario where a specific
 * menu message is made available via the ASRouter system.
 *
 * This function stubs:
 *  - `ASRouter.handleMessageRequest` to resolve the provided message.
 *  - `ASRouter.messagesEnabledInAutomation` to consider the message enabled for automation.
 *  - `ASRouter.getMessageById` to return the provided message when queried by its ID.
 *
 * After the provided task function `taskFn` is executed, it restores all the stubs.
 *
 * @param {SinonSandbox} sandbox - The Sinon sandbox used to create the stubs and ensure cleanup.
 * @param {object} message - The message object to be used in the stubs and passed for testing.
 * @param {function} taskFn - The function to be executed with the stubs in place.
 */
async function withTestMessage(sandbox, message, taskFn) {
  let handleMessageRequestStub = sandbox.stub(ASRouter, "handleMessageRequest");
  handleMessageRequestStub.resolves([message]);

  let messagesEnabledInAutomationStub = sandbox.stub(
    ASRouter,
    "messagesEnabledInAutomation"
  );
  messagesEnabledInAutomationStub.value([message.id]);

  let getMessageByIdStub = sandbox.stub(ASRouter, "getMessageById");
  getMessageByIdStub.withArgs(message.id).returns(message);

  await taskFn(handleMessageRequestStub);

  handleMessageRequestStub.restore();
  messagesEnabledInAutomationStub.restore();
  getMessageByIdStub.restore();
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

add_setup(async function () {
  Services.fog.testResetFOG();

  gTestFxAMessage = await PanelTestProvider.getMessages().then(msgs =>
    msgs.find(msg => msg.id === "FXA_ACCOUNTS_APPMENU_PROTECT_BROWSING_DATA")
  );
  Assert.ok(gTestFxAMessage, "Found a test AppMenu message to use.");

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
    await hideAllPopups();
  });
});

/**
 * Tests that opening each menu source causes the menuOpened trigger to fire.
 */
add_task(async function test_trigger() {
  let sandbox = sinon.createSandbox();
  sandbox.spy(ASRouter, "sendTriggerMessage");

  await reopenMenuSource(MenuMessage.SOURCES.APP_MENU);
  Assert.ok(
    ASRouter.sendTriggerMessage.calledWith({
      browser: gBrowser.selectedBrowser,
      id: "menuOpened",
      context: {
        source: MenuMessage.SOURCES.APP_MENU,
        browserIsSelected: true,
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
      },
    }),
    "sendTriggerMessage was called when opening the PXI panel."
  );

  sandbox.restore();
});

/**
 * Tests that a registered MenuMessage of type fxa_cta will cause an
 * fxa-menu-message element to appear in either menu source panel with the right
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
 * Tests that a registered MenuMessage of type fxa_cta will cause an
 * fxa-menu-message element to appear in the menu sources for all newly
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
          let win3Message =
            win3.PanelUI.mainView.querySelector("fxa-menu-message");
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
          messageEl.signUpButton.click();
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

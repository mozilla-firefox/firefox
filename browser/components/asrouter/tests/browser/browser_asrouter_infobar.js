/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { InfoBar } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/InfoBar.sys.mjs"
);
const { CFRMessageProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/CFRMessageProvider.sys.mjs"
);
const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);

add_task(async function show_and_send_telemetry() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );

  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    {
      ...message,
      content: {
        priority: window.gNotificationBox.PRIORITY_WARNING_HIGH,
        ...message.content,
      },
    },
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");
  // This is the call to increment impressions for frequency capping
  Assert.equal(dispatchStub.firstCall.args[0].type, "IMPRESSION");
  Assert.equal(dispatchStub.firstCall.args[0].data.id, message.id);
  // This is the telemetry ping
  Assert.equal(dispatchStub.secondCall.args[0].data.event, "IMPRESSION");
  Assert.equal(dispatchStub.secondCall.args[0].data.message_id, message.id);
  Assert.equal(
    infobar.notification.priority,
    window.gNotificationBox.PRIORITY_WARNING_HIGH,
    "Has the priority level set in the message definition"
  );

  let primaryBtn = infobar.notification.buttonContainer.querySelector(
    ".notification-button.primary"
  );

  Assert.ok(primaryBtn, "Has a primary button");
  primaryBtn.click();

  Assert.equal(dispatchStub.callCount, 4, "Called again with CLICK + removed");
  Assert.equal(dispatchStub.thirdCall.args[0].type, "USER_ACTION");
  Assert.equal(
    dispatchStub.lastCall.args[0].data.event,
    "CLICK_PRIMARY_BUTTON"
  );

  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for notification to be dismissed by primary btn click."
  );
});

add_task(async function dismiss_telemetry() {
  let message = {
    ...(await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    ),
  };
  message.content.type = "tab";

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  // Remove any IMPRESSION pings
  dispatchStub.reset();

  infobar.notification.closeButton.click();

  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Set to null by `removed` event"
  );

  Assert.equal(dispatchStub.callCount, 1, "Only called once");
  Assert.equal(
    dispatchStub.firstCall.args[0].data.event,
    "DISMISSED",
    "Called with dismissed"
  );

  // Remove DISMISSED ping
  dispatchStub.reset();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  await BrowserTestUtils.waitForCondition(
    () => dispatchStub.callCount > 0,
    "Wait for impression ping"
  );

  // Remove IMPRESSION ping
  dispatchStub.reset();
  BrowserTestUtils.removeTab(tab);

  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Set to null by `disconnect` event"
  );

  // Called by closing the tab and triggering "disconnect"
  Assert.equal(dispatchStub.callCount, 1, "Only called once");
  Assert.equal(
    dispatchStub.firstCall.args[0].data.event,
    "DISMISSED",
    "Called with dismissed"
  );
});

add_task(async function prevent_multiple_messages() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );

  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");

  // Try to stack 2 notifications
  await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Impression count did not increase");

  // Dismiss the first notification
  infobar.notification.closeButton.click();
  Assert.equal(InfoBar._activeInfobar, null, "Cleared the active notification");

  // Reset impressions count
  dispatchStub.reset();
  // Try show the message again
  infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );
  Assert.ok(InfoBar._activeInfobar, "activeInfobar is set");
  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");
  // Dismiss the notification again
  infobar.notification.closeButton.click();
  Assert.equal(InfoBar._activeInfobar, null, "Cleared the active notification");
});

add_task(async function default_dismissable_button_shows() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(message, "Found the message");

  // Use the base message which has no dismissable property by default.
  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.ok(
    infobar.notification.closeButton,
    "Default message should display a close button"
  );

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Wait for default message notification to be dismissed."
  );
});

add_task(
  async function non_dismissable_notification_does_not_show_close_button() {
    let baseMessage = (await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    );
    Assert.ok(baseMessage, "Found the base message");

    let message = {
      ...baseMessage,
      content: {
        ...baseMessage.content,
        dismissable: false,
      },
    };

    // Add a footer button we can close the infobar with
    message.content.buttons.push({
      label: "Cancel",
      action: {
        type: "CANCEL",
      },
    });

    let dispatchStub = sinon.stub();
    let infobar = await InfoBar.showInfoBarMessage(
      BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
      message,
      dispatchStub
    );

    Assert.ok(
      !infobar.notification.closeButton,
      "Non-dismissable message should not display a close button"
    );

    let cancelButton = infobar.notification.querySelector(
      ".footer-button:not(.primary)"
    );

    Assert.ok(cancelButton, "Non-primary footer button exists");

    cancelButton.click();
    await BrowserTestUtils.waitForCondition(
      () => infobar.notification === null,
      "Wait for default message notification to close."
    );
  }
);

function getMeaningfulNodes(infobar) {
  return [...infobar.notification.messageText.childNodes].filter(
    n =>
      n.nodeType === Node.ELEMENT_NODE ||
      (n.nodeType === Node.TEXT_NODE && n.textContent.trim())
  );
}

async function showInfobar(text, box, browser) {
  let msg = {
    id: "Test Infobar",
    content: {
      text,
      type: "global",
      priority: box.PRIORITY_INFO_LOW,
      buttons: [{ label: "Close", action: { type: "CANCEL" } }],
    },
  };
  let stub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(browser, msg, stub);
  return { infobar, stub };
}

add_task(async function test_formatMessageConfig_single_string() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  let { infobar } = await showInfobar("Just a plain string", box, browser);
  const nodes = getMeaningfulNodes(infobar);

  Assert.equal(nodes.length, 1, "One meaningful node for single string");
  Assert.equal(nodes[0].nodeType, Node.TEXT_NODE, "That node is a text node");
  Assert.equal(nodes[0].textContent.trim(), "Just a plain string");

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

add_task(async function test_formatMessageConfig_array() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  let parts = [
    "A",
    { raw: "B" },
    { string_id: "launch-on-login-infobar-message" },
    { href: "https://x.test/", raw: "LINK" },
    "Z",
  ];
  let { infobar } = await showInfobar(parts, box, browser);
  const nodes = getMeaningfulNodes(infobar);

  Assert.equal(nodes.length, parts.length, "One node per array part");
  Assert.equal(nodes[0].textContent, "A", "Plain text");
  Assert.equal(nodes[1].textContent, "B", "Raw text");
  Assert.equal(nodes[2].localName, "remote-text", "L10n element");
  Assert.equal(
    nodes[2].getAttribute("fluent-remote-id"),
    "launch-on-login-infobar-message",
    "Fluent ID"
  );
  const [, , , a] = nodes;
  Assert.equal(a.localName, "a", "It's a link");
  Assert.equal(a.getAttribute("href"), "https://x.test/", "hred preserved");
  Assert.equal(a.textContent, "LINK", "Link text");
  Assert.equal(nodes[4].textContent, "Z", "Trailing text");

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

add_task(async function test_specialMessageAction_onLinkClick() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  const { SpecialMessageActions } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
  );
  let handleStub = sinon.stub(SpecialMessageActions, "handleAction");

  const parts = [
    "Click ",
    { raw: "here", href: "https://example.com/foo", where: "tab" },
    " to continue",
  ];
  let { infobar } = await showInfobar(parts, box, browser);

  let link = infobar.notification.messageText.querySelector("a[href]");
  Assert.ok(link, "Found the link");
  EventUtils.synthesizeMouseAtCenter(link, {}, browser.ownerGlobal);

  Assert.equal(handleStub.callCount, 1, "handleAction was invoked once");
  let [actionArg, browserArg] = handleStub.firstCall.args;
  Assert.deepEqual(
    actionArg,
    {
      type: "OPEN_URL",
      data: { args: "https://example.com/foo", where: "tab" },
    },
    "Passed correct action to handleAction"
  );
  Assert.equal(
    browserArg,
    browser,
    "Passed the selectedBrowser to handleAction"
  );

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);

  handleStub.restore();
});

add_task(async function test_showInfoBarMessage_skipsPrivateWindow() {
  const { PrivateBrowsingUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
  );
  sinon.stub(PrivateBrowsingUtils, "isWindowPrivate").returns(true);

  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let dispatch = sinon.stub();

  let result = await InfoBar.showInfoBarMessage(
    browser,
    {
      id: "Private Win Test",
      content: { type: "global", buttons: [], text: "t", dismissable: true },
    },
    dispatch
  );

  Assert.equal(result, null);
  Assert.equal(dispatch.callCount, 0);

  // Cleanup
  sinon.restore();
});

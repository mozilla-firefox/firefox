/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const TEST_ORIGIN = "https://example.com";
const OTHER_ORIGIN = "https://example.org";
const TEST_URI = pageURL(
  TEST_ORIGIN,
  "browser_registerProtocolHandler_notification.html"
);
const RELATIVE_PAGE = "browser_registerProtocolHandler_relative.html";
const PROTOCOL = "web+testprotocol";
const NOTIFICATION_VALUE = "Protocol Registration: " + PROTOCOL;
const PERM = "register-protocol-handler";

const Registrar = ChromeUtils.importESModule(
  "moz-src:///browser/components/protocolhandler/WebProtocolHandlerRegistrar.sys.mjs"
).WebProtocolHandlerRegistrar.prototype;

function getNotification(browser = gBrowser.selectedBrowser) {
  return gBrowser
    .getNotificationBox(browser)
    .getNotificationWithValue(NOTIFICATION_VALUE);
}

function waitForNotification(browser = gBrowser.selectedBrowser) {
  return BrowserTestUtils.waitForMutationCondition(
    gBrowser.getNotificationBox(browser).stack,
    { childList: true },
    () => getNotification(browser)
  );
}

function getButton(notification, index) {
  return notification.buttonContainer.getElementsByClassName(
    "notification-button"
  )[index];
}

async function clickButton(notification, index) {
  let button = getButton(notification, index);
  await BrowserTestUtils.waitForMutationCondition(
    button,
    { attributes: true, attributeFilter: ["disabled"] },
    () => !button.disabled
  );
  EventUtils.synthesizeMouseAtCenter(button, {}, window);
}

/**
 * Calls the registrar synchronously from the parent, the same way the content
 * call ends up there, and asserts whether it tried to show the prompt.
 * Watching appendNotification avoids racing the asynchronous element insertion.
 *
 * @param {MozBrowser} browser
 * @param {boolean} expectPrompt
 * @param {string} message
 */
function checkRegistrationPrompts(browser, expectPrompt, message) {
  let spy = sinon.spy(
    gBrowser.getNotificationBox(browser),
    "appendNotification"
  );
  try {
    Registrar.registerProtocolHandler(
      PROTOCOL,
      Services.io.newURI(TEST_ORIGIN + "/foobar?uri=%s"),
      "Test Protocol",
      Services.io.newURI(TEST_URI),
      browser
    );
    is(spy.called, expectPrompt, message);
  } finally {
    spy.restore();
  }
}

// The prompt is only shown when the protocol is not already registered. Ensure
// a clean slate so a handler left over from another test does not suppress it.
function unregisterHandler() {
  let protoSvc = Cc[
    "@mozilla.org/uriloader/external-protocol-service;1"
  ].getService(Ci.nsIExternalProtocolService);
  let handlerSvc = Cc["@mozilla.org/uriloader/handler-service;1"].getService(
    Ci.nsIHandlerService
  );
  let protoInfo = protoSvc.getProtocolHandlerInfo(PROTOCOL);
  let handlers = protoInfo.possibleApplicationHandlers;
  while (handlers.length) {
    handlers.removeElementAt(0);
  }
  protoInfo.preferredApplicationHandler = null;
  handlerSvc.store(protoInfo);
}

function principalFor(origin) {
  return Services.scriptSecurityManager.createContentPrincipalFromOrigin(
    origin
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["security.notification_enable_delay", 0]],
  });
  unregisterHandler();
  registerCleanupFunction(() => {
    SitePermissions.removeFromPrincipal(principalFor(TEST_ORIGIN), PERM);
    SitePermissions.removeFromPrincipal(principalFor(OTHER_ORIGIN), PERM);
    unregisterHandler();
  });
});

add_task(async function testNeverAllowPersistsBlock() {
  await BrowserTestUtils.withNewTab(TEST_URI, async browser => {
    let notification = await waitForNotification();
    let neverButton = getButton(notification, 1);
    let { id } = document.l10n.getAttributes(neverButton);
    is(id, "protocolhandler-never-allow-button", "Never allow button l10n id");
    await TestUtils.waitForCondition(
      () => neverButton.label,
      "Wait for Never allow button label"
    );
    let [{ attributes }] = await document.l10n.formatMessages([{ id }]);
    let expected = Object.fromEntries(attributes.map(a => [a.name, a.value]));
    is(neverButton.label, expected.label, "Never allow button label");
    is(
      neverButton.accessKey,
      expected.accesskey,
      "Never allow button access key"
    );
    await clickButton(notification, 1);

    Assert.deepEqual(
      SitePermissions.getForPrincipal(browser.contentPrincipal, PERM),
      { state: SitePermissions.BLOCK, scope: SitePermissions.SCOPE_PERSISTENT },
      "Never allow persists a BLOCK permission."
    );
    checkRegistrationPrompts(
      browser,
      false,
      "No prompt after a persistent block."
    );

    SitePermissions.removeFromPrincipal(browser.contentPrincipal, PERM);
    checkRegistrationPrompts(
      browser,
      true,
      "Prompting resumes once the block is removed."
    );
    await waitForNotification();
    getNotification().close();
  });
});

add_task(async function testDismissPersistsTemporaryBlock() {
  await BrowserTestUtils.withNewTab(TEST_URI, async browser => {
    let notification = await waitForNotification();
    notification.dismiss();

    Assert.deepEqual(
      SitePermissions.getForPrincipal(browser.contentPrincipal, PERM, browser),
      { state: SitePermissions.BLOCK, scope: SitePermissions.SCOPE_TEMPORARY },
      "Dismissing sets a temporary BLOCK."
    );
    is(
      SitePermissions.getForPrincipal(browser.contentPrincipal, PERM).state,
      SitePermissions.UNKNOWN,
      "Dismissing stores no persistent permission."
    );
    checkRegistrationPrompts(
      browser,
      false,
      "No prompt while temporarily blocked."
    );

    SitePermissions.clearTemporaryBlockPermissions(browser);
    checkRegistrationPrompts(
      browser,
      true,
      "Prompting resumes once the temporary block is cleared."
    );
    await waitForNotification();
    getNotification().close();
  });
});

// A background tab's request must be attributed to that tab's site, not to
// the site in the selected tab.
add_task(async function testBackgroundTabUsesRequestingSite() {
  await BrowserTestUtils.withNewTab(OTHER_ORIGIN, async () => {
    let backgroundTab = BrowserTestUtils.addTab(gBrowser, TEST_URI);
    let backgroundBrowser = backgroundTab.linkedBrowser;
    await BrowserTestUtils.browserLoaded(backgroundBrowser, false, TEST_URI);
    await waitForNotification(backgroundBrowser);

    await BrowserTestUtils.switchTab(gBrowser, backgroundTab);
    await clickButton(getNotification(backgroundBrowser), 1);

    is(
      SitePermissions.getForPrincipal(principalFor(TEST_ORIGIN), PERM).state,
      SitePermissions.BLOCK,
      "The requesting site is blocked."
    );
    is(
      SitePermissions.getForPrincipal(principalFor(OTHER_ORIGIN), PERM).state,
      SitePermissions.UNKNOWN,
      "The site that was selected during the request is not blocked."
    );

    BrowserTestUtils.removeTab(backgroundTab);
    SitePermissions.removeFromPrincipal(principalFor(TEST_ORIGIN), PERM);
  });
});

// Requests from iframes, including cross-origin ones, are attributed to the
// top-level site.
add_task(async function testCrossOriginIframeUsesTopLevelSite() {
  let iframeURL = pageURL(OTHER_ORIGIN, RELATIVE_PAGE);
  await BrowserTestUtils.withNewTab(TEST_ORIGIN, async browser => {
    await addIframe(browser, iframeURL);
    await clickButton(await waitForNotification(), 1);

    is(
      SitePermissions.getForPrincipal(principalFor(TEST_ORIGIN), PERM).state,
      SitePermissions.BLOCK,
      "Never allow from an iframe blocks the top-level site."
    );
    is(
      SitePermissions.getForPrincipal(principalFor(OTHER_ORIGIN), PERM).state,
      SitePermissions.UNKNOWN,
      "The iframe's site is not blocked."
    );

    let registrarSpy = sinon.spy(Registrar, "registerProtocolHandler");
    let appendSpy = sinon.spy(
      gBrowser.getNotificationBox(browser),
      "appendNotification"
    );
    try {
      await addIframe(browser, iframeURL);
      await TestUtils.waitForCondition(
        () => registrarSpy.called,
        "Wait for the iframe's request to reach the registrar."
      );
      ok(
        !appendSpy.called,
        "No prompt for an iframe request once the top-level site is blocked."
      );
    } finally {
      registrarSpy.restore();
      appendSpy.restore();
    }

    SitePermissions.removeFromPrincipal(principalFor(TEST_ORIGIN), PERM);
  });
});

add_task(async function testGlobalBlockDefault() {
  await SpecialPowers.pushPrefEnv({
    set: [["permissions.default." + PERM, SitePermissions.BLOCK]],
  });

  await BrowserTestUtils.withNewTab(TEST_ORIGIN, async browser => {
    checkRegistrationPrompts(
      browser,
      false,
      "No prompt when blocked by default."
    );
  });

  await SpecialPowers.popPrefEnv();
});

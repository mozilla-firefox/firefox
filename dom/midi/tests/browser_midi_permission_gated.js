/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const EXAMPLE_COM_URL =
  "https://example.com/document-builder.sjs?html=<h1>Test midi permission with synthetic site permission addon</h1>";
const PAGE_WITH_IFRAMES_URL = `https://example.org/document-builder.sjs?html=
  <h1>Test midi permission with synthetic site permission addon in iframes</h1>
  <iframe id=sameOrigin src="${encodeURIComponent(
    'https://example.org/document-builder.sjs?html=SameOrigin"'
  )}"></iframe>
  <iframe id=crossOrigin  src="${encodeURIComponent(
    'https://example.net/document-builder.sjs?html=CrossOrigin"'
  )}"></iframe>`;
const USE_COUNTER_URL =
  "https://example.net/document-builder.sjs?html=<h1>Test midi use counter</h1>";

const l10n = new Localization(
  [
    "browser/addonNotifications.ftl",
    "toolkit/global/extensions.ftl",
    "toolkit/global/extensionPermissions.ftl",
    "branding/brand.ftl",
  ],
  true
);

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
ChromeUtils.defineESModuleGetters(this, {
  AddonTestUtils: "resource://testing-common/AddonTestUtils.sys.mjs",
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
});

/* import-globals-from ../../../toolkit/mozapps/extensions/test/xpinstall/helpers_addons_install_dialogs.js */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/dom/midi/tests/helpers_addons_install_dialogs.js",
  this
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["midi.prompt.testing", false]],
  });

  AddonTestUtils.initMochitest(this);

  // Once the addon is installed, a dialog is displayed as a confirmation.
  // This could interfere with tests running after this one, so we set up a listener
  // that will always accept post install dialogs so we don't have  to deal with them in
  // the test.
  alwaysAcceptAddonPostInstallDialogs();

  registerCleanupFunction(async () => {
    // Remove the permission.
    await SpecialPowers.removePermission("midi-sysex", {
      url: EXAMPLE_COM_URL,
    });
    await SpecialPowers.removePermission("midi-sysex", {
      url: PAGE_WITH_IFRAMES_URL,
    });
    await SpecialPowers.removePermission("midi", {
      url: EXAMPLE_COM_URL,
    });
    await SpecialPowers.removePermission("midi", {
      url: PAGE_WITH_IFRAMES_URL,
    });
    await SpecialPowers.removePermission("install", {
      url: EXAMPLE_COM_URL,
    });
    await SpecialPowers.removePermission("midi-sysex", {
      url: USE_COUNTER_URL,
    });
    await SpecialPowers.removePermission("midi", {
      url: USE_COUNTER_URL,
    });
    await SpecialPowers.removePermission("install", {
      url: USE_COUNTER_URL,
    });

    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.selectedTab);
    }
  });
});

add_task(async function testRequestMIDIAccess() {
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, EXAMPLE_COM_URL);
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
  const testPageHost = gBrowser.selectedTab.linkedBrowser.documentURI.host;
  Services.fog.testResetFOG();

  // Track the site_permission values observed across this test task
  // and assert the unique values collected at the end.
  const sitePermissionsSeen = new Set();
  const trackSitePermission = evt =>
    sitePermissionsSeen.add(evt.site_permission);

  info("Check that midi-sysex isn't set");
  ok(
    await SpecialPowers.testPermission(
      "midi-sysex",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "midi-sysex value should have UNKNOWN permission"
  );

  info("Request midi-sysex access");
  let onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });

  info("Deny site permission addon install in first popup");
  let addonInstallPanel = await onAddonInstallBlockedNotification;
  const [installPopupHeader, installPopupMessage] =
    addonInstallPanel.querySelectorAll(
      "description.popup-notification-description"
    );
  is(
    installPopupHeader.textContent,
    l10n.formatValueSync("site-permission-install-first-prompt-midi-header"),
    "First popup has expected header text"
  );
  is(
    installPopupMessage.textContent,
    l10n.formatValueSync("site-permission-install-first-prompt-midi-message"),
    "First popup has expected message"
  );

  let notification = addonInstallPanel.childNodes[0];
  // secondaryButton is the "Don't allow" button
  notification.secondaryButton.click();

  let rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.midiAccessRequestPromise;
      } catch (e) {
        errorMessage = `${e.name}: ${e.message}`;
      }

      delete content.midiAccessRequestPromise;
      return errorMessage;
    }
  );
  is(
    rejectionMessage,
    "SecurityError: WebMIDI requires a site permission add-on to activate"
  );

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: ["site_warning", "cancelled"],
    resetFOG: true,
    onEvent: trackSitePermission,
  });

  info("Deny site permission addon install in second popup");
  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });
  addonInstallPanel = await onAddonInstallBlockedNotification;
  notification = addonInstallPanel.childNodes[0];
  let dialogPromise = waitForInstallDialog();
  notification.button.click();
  let installDialog = await dialogPromise;
  is(
    installDialog.querySelector(".popup-notification-description").textContent,
    l10n.formatValueSync(
      "webext-site-perms-header-with-gated-perms-midi-sysex",
      { hostname: testPageHost }
    ),
    "Install dialog has expected header text"
  );
  is(
    installDialog.querySelector("popupnotificationcontent description")
      .textContent,
    l10n.formatValueSync("webext-site-perms-description-gated-perms-midi"),
    "Install dialog has expected description"
  );

  // secondaryButton is the "Cancel" button
  installDialog.secondaryButton.click();

  rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.midiAccessRequestPromise;
      } catch (e) {
        errorMessage = `${e.name}: ${e.message}`;
      }

      delete content.midiAccessRequestPromise;
      return errorMessage;
    }
  );
  is(
    rejectionMessage,
    "SecurityError: WebMIDI requires a site permission add-on to activate"
  );

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: ["site_warning", "permissions_prompt", "cancelled"],
    resetFOG: true,
    onEvent: trackSitePermission,
  });

  info("Request midi-sysex access again");
  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });

  info("Accept site permission addon install");
  addonInstallPanel = await onAddonInstallBlockedNotification;
  notification = addonInstallPanel.childNodes[0];
  dialogPromise = waitForInstallDialog();
  notification.button.click();
  installDialog = await dialogPromise;
  installDialog.button.click();

  info("Wait for the midi-sysex access request promise to resolve");
  let accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.midiAccessRequestPromise;
        return true;
      } catch (e) {}

      delete content.midiAccessRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestMIDIAccess resolved");

  info("Check that midi-sysex is now set");
  ok(
    await SpecialPowers.testPermission(
      "midi-sysex",
      SpecialPowers.Services.perms.ALLOW_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "midi-sysex value should have ALLOW permission"
  );
  ok(
    await SpecialPowers.testPermission(
      "midi",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "but midi should have UNKNOWN permission"
  );

  info("Check that we don't prompt user again once they installed the addon");
  const accessPromiseState = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      return content.navigator
        .requestMIDIAccess({ sysex: true })
        .then(() => "resolved");
    }
  );
  is(
    accessPromiseState,
    "resolved",
    "requestMIDIAccess resolved without user prompt"
  );

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: ["site_warning", "permissions_prompt", "completed"],
    resetFOG: true,
    onEvent: trackSitePermission,
  });

  info("Request midi access without sysex");
  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.midiNoSysexAccessRequestPromise =
      content.navigator.requestMIDIAccess();
  });

  info("Accept site permission addon install");
  addonInstallPanel = await onAddonInstallBlockedNotification;
  notification = addonInstallPanel.childNodes[0];

  is(
    notification
      .querySelector("#addon-install-blocked-info")
      .getAttribute("href"),
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "site-permission-addons",
    "Got the expected SUMO page as a learn more link in the addon-install-blocked panel"
  );

  dialogPromise = waitForInstallDialog();
  notification.button.click();
  installDialog = await dialogPromise;

  is(
    installDialog.querySelector(".popup-notification-description").textContent,
    l10n.formatValueSync("webext-site-perms-header-with-gated-perms-midi", {
      hostname: testPageHost,
    }),
    "Install dialog has expected header text"
  );
  is(
    installDialog.querySelector("popupnotificationcontent description")
      .textContent,
    l10n.formatValueSync("webext-site-perms-description-gated-perms-midi"),
    "Install dialog has expected description"
  );

  installDialog.button.click();

  info("Wait for the midi access request promise to resolve");
  accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.midiNoSysexAccessRequestPromise;
        return true;
      } catch (e) {}

      delete content.midiNoSysexAccessRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestMIDIAccess resolved");

  info("Check that both midi-sysex and midi are now set");
  ok(
    await SpecialPowers.testPermission(
      "midi-sysex",
      SpecialPowers.Services.perms.ALLOW_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "midi-sysex value should have ALLOW permission"
  );
  ok(
    await SpecialPowers.testPermission(
      "midi",
      SpecialPowers.Services.perms.ALLOW_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "and midi value should also have ALLOW permission"
  );

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: ["site_warning", "permissions_prompt", "completed"],
    resetFOG: true,
    onEvent: trackSitePermission,
  });

  info("Check that we don't prompt user again when they perm denied");
  // remove permission to have a clean state
  await SpecialPowers.removePermission("midi-sysex", {
    url: EXAMPLE_COM_URL,
  });

  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });

  info("Perm-deny site permission addon install");
  addonInstallPanel = await onAddonInstallBlockedNotification;
  // Click the "Report Suspicious Site" menuitem, which has the same effect as
  // "Never Allow" and also submits a telemetry event (which we check below).
  notification.menupopup.querySelectorAll("menuitem")[1].click();

  rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.midiAccessRequestPromise;
      } catch (e) {
        errorMessage = e.name;
      }

      delete content.midiAccessRequestPromise;
      return errorMessage;
    }
  );
  is(rejectionMessage, "SecurityError", "requestMIDIAccess was rejected");

  info("Request midi-sysex access again");
  let denyIntervalStart = performance.now();
  rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.navigator.requestMIDIAccess({
          sysex: true,
        });
      } catch (e) {
        errorMessage = e.name;
      }
      return errorMessage;
    }
  );
  is(
    rejectionMessage,
    "SecurityError",
    "requestMIDIAccess was rejected without user prompt"
  );
  let denyIntervalElapsed = performance.now() - denyIntervalStart;
  Assert.greaterOrEqual(
    denyIntervalElapsed,
    3000,
    `Rejection should be delayed by a randomized interval no less than 3 seconds (got ${
      denyIntervalElapsed / 1000
    } seconds)`
  );

  Assert.deepEqual(
    [{ suspicious_site: "example.com", permission_type: "midi-sysex" }],
    AddonTestUtils.getAMGleanEvents("reportSuspiciousSite"),
    "Expected Glean event recorded."
  );

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: ["site_warning", "cancelled"],
    resetFOG: true,
    onEvent: trackSitePermission,
  });

  Assert.deepEqual(
    Array.from(sitePermissionsSeen).sort(),
    ["midi", "midi-sysex"],
    "Install telemetry events distinguish the midi and midi-sysex site permissions"
  );
});

add_task(async function testIframeRequestMIDIAccess() {
  gBrowser.selectedTab = BrowserTestUtils.addTab(
    gBrowser,
    PAGE_WITH_IFRAMES_URL
  );
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  info("Check that midi-sysex isn't set");
  ok(
    await SpecialPowers.testPermission(
      "midi-sysex",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: PAGE_WITH_IFRAMES_URL }
    ),
    "midi-sysex value should have UNKNOWN permission"
  );

  info("Request midi-sysex access from the same-origin iframe");
  const sameOriginIframeBrowsingContext = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      return content.document.getElementById("sameOrigin").browsingContext;
    }
  );

  let onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(sameOriginIframeBrowsingContext, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });

  info("Accept site permission addon install");
  const addonInstallPanel = await onAddonInstallBlockedNotification;
  const notification = addonInstallPanel.childNodes[0];
  const dialogPromise = waitForInstallDialog();
  notification.button.click();
  let installDialog = await dialogPromise;
  installDialog.button.click();

  info("Wait for the midi-sysex access request promise to resolve");
  const accessGranted = await SpecialPowers.spawn(
    sameOriginIframeBrowsingContext,
    [],
    async () => {
      try {
        await content.midiAccessRequestPromise;
        return true;
      } catch (e) {}

      delete content.midiAccessRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestMIDIAccess resolved");

  info("Check that midi-sysex is now set");
  ok(
    await SpecialPowers.testPermission(
      "midi-sysex",
      SpecialPowers.Services.perms.ALLOW_ACTION,
      { url: PAGE_WITH_IFRAMES_URL }
    ),
    "midi-sysex value should have ALLOW permission"
  );

  info(
    "Check that we don't prompt user again once they installed the addon from the same-origin iframe"
  );
  const accessPromiseState = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      return content.navigator
        .requestMIDIAccess({ sysex: true })
        .then(() => "resolved");
    }
  );
  is(
    accessPromiseState,
    "resolved",
    "requestMIDIAccess resolved without user prompt"
  );

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: ["site_warning", "permissions_prompt", "completed"],
    resetFOG: true,
  });

  info("Check that request is rejected when done from a cross-origin iframe");
  const crossOriginIframeBrowsingContext = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      return content.document.getElementById("crossOrigin").browsingContext;
    }
  );

  const onConsoleErrorMessage = new Promise(resolve => {
    const errorListener = {
      observe(error) {
        if (error.message.includes("WebMIDI access request was denied")) {
          resolve(error);
          Services.console.unregisterListener(errorListener);
        }
      },
    };
    Services.console.registerListener(errorListener);
  });

  const rejectionMessage = await SpecialPowers.spawn(
    crossOriginIframeBrowsingContext,
    [],
    async () => {
      let errorName;
      try {
        await content.navigator.requestMIDIAccess({
          sysex: true,
        });
      } catch (e) {
        errorName = e.name;
      }
      return errorName;
    }
  );

  is(
    rejectionMessage,
    "SecurityError",
    "requestMIDIAccess from the remote iframe was rejected"
  );

  const consoleErrorMessage = await onConsoleErrorMessage;
  ok(
    consoleErrorMessage.message.includes(
      `WebMIDI access request was denied: ❝SitePermsAddons can't be installed from cross origin subframes❞`,
      "an error message is sent to the console"
    )
  );
  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: [],
    resetFOG: true,
  });
});

add_task(async function testRequestMIDIAccessLocalhost() {
  const httpServer = new HttpServer();
  httpServer.start(-1);
  httpServer.registerPathHandler(`/test`, function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(`
      <!DOCTYPE html>
      <meta charset=utf8>
      <h1>Test requestMIDIAccess on lcoalhost</h1>`);
  });
  const localHostTestUrl = `http://localhost:${httpServer.identity.primaryPort}/test`;

  registerCleanupFunction(async function cleanup() {
    await new Promise(resolve => httpServer.stop(resolve));
  });

  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, localHostTestUrl);
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  info("Check that midi-sysex isn't set");
  ok(
    await SpecialPowers.testPermission(
      "midi-sysex",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: localHostTestUrl }
    ),
    "midi-sysex value should have UNKNOWN permission"
  );

  info(
    "Request midi-sysex access should not prompt for addon install on locahost, but for permission"
  );
  let popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });
  await popupShown;
  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "midi-notification",
    "midi notification was displayed"
  );

  info("Accept permission");
  PopupNotifications.panel
    .querySelector(".popup-notification-primary-button")
    .click();

  info("Wait for the midi-sysex access request promise to resolve");
  let accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.midiAccessRequestPromise;
        return true;
      } catch (e) {}

      delete content.midiAccessRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestMIDIAccess resolved");

  // We're remembering permission grants temporarily on the tab since Bug 1754005.
  info(
    "Check that a new request is automatically granted because we granted before in the same tab."
  );

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async () => {
    content.navigator.requestMIDIAccess({ sysex: true });
  });

  accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.midiAccessRequestPromise;
        return true;
      } catch (e) {}

      delete content.midiAccessRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestMIDIAccess resolved");

  AddonTestUtils.assertSitePermissionInstallSteps({
    expectedSteps: [],
    resetFOG: true,
  });
});

add_task(async function testDisabledRequestMIDIAccessFile() {
  let dir = getChromeDir(getResolvedURI(gTestPath));
  dir.append("blank.html");
  const fileSchemeTestUri = Services.io.newFileURI(dir).spec;

  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, fileSchemeTestUri);
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  info("Check that requestMIDIAccess isn't set on navigator on file scheme");
  const isRequestMIDIAccessDefined = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    () => {
      return "requestMIDIAccess" in content.wrappedJSObject.navigator;
    }
  );
  is(
    isRequestMIDIAccessDefined,
    false,
    "navigator.requestMIDIAccess is not defined on file scheme"
  );
});

add_task(async function testMIDIAccessGrantedUseCounter() {
  // Verify the MIDIAccessGranted use counter is set on the document when
  // requestMIDIAccess succeeds, and not set when permission is denied.
  // Use counters are reported when the document is destroyed (tab close),
  // so we open/close tabs and snapshot counters before and after.
  //
  // navigator_requestmidiaccess is a [UseCounter] on the method itself
  // (fires whether or not the call resolves), so we use it as a sentinel to
  // know the destroyed document's counters have been flushed before reading
  // midiaccess_granted.

  async function snapshotCounters() {
    await Services.fog.testFlushAllChildren();
    return {
      grantedPage: Glean.useCounterPage.midiaccessGranted.testGetValue() ?? 0,
      grantedDoc: Glean.useCounterDoc.midiaccessGranted.testGetValue() ?? 0,
      requestPage:
        Glean.useCounterPage.navigatorRequestmidiaccess.testGetValue() ?? 0,
    };
  }

  async function waitForFlush(prevRequestPage) {
    await TestUtils.waitForCondition(async () => {
      await Services.fog.testFlushAllChildren();
      return (
        (Glean.useCounterPage.navigatorRequestmidiaccess.testGetValue() ?? 0) >
        prevRequestPage
      );
    }, "Wait for use counter ping from destroyed document");
  }

  // ===== Deny phase =====
  info("Deny phase: open tab on a fresh origin and reject addon install");
  let before = await snapshotCounters();

  let denyTab = BrowserTestUtils.addTab(gBrowser, USE_COUNTER_URL);
  gBrowser.selectedTab = denyTab;
  await BrowserTestUtils.browserLoaded(denyTab.linkedBrowser);

  let onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(denyTab.linkedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });

  let addonInstallPanel = await onAddonInstallBlockedNotification;
  // secondaryButton is the "Don't allow" button
  addonInstallPanel.childNodes[0].secondaryButton.click();

  let denyRejected = await SpecialPowers.spawn(
    denyTab.linkedBrowser,
    [],
    async () => {
      try {
        await content.midiAccessRequestPromise;
      } catch (e) {
        return true;
      } finally {
        delete content.midiAccessRequestPromise;
      }
      return false;
    }
  );
  ok(denyRejected, "requestMIDIAccess was rejected in deny phase");

  await BrowserTestUtils.removeTab(denyTab);
  await waitForFlush(before.requestPage);

  let afterDeny = await snapshotCounters();
  is(
    afterDeny.grantedPage,
    before.grantedPage,
    "midiaccess_granted page counter unchanged when permission denied"
  );
  is(
    afterDeny.grantedDoc,
    before.grantedDoc,
    "midiaccess_granted document counter unchanged when permission denied"
  );

  // ===== Allow phase =====
  info("Allow phase: open tab on same origin and accept addon install");
  let allowTab = BrowserTestUtils.addTab(gBrowser, USE_COUNTER_URL);
  gBrowser.selectedTab = allowTab;
  await BrowserTestUtils.browserLoaded(allowTab.linkedBrowser);

  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(allowTab.linkedBrowser, [], () => {
    content.midiAccessRequestPromise = content.navigator.requestMIDIAccess({
      sysex: true,
    });
  });

  addonInstallPanel = await onAddonInstallBlockedNotification;
  let dialogPromise = waitForInstallDialog();
  addonInstallPanel.childNodes[0].button.click();
  let installDialog = await dialogPromise;
  // This is the last add-on installed by this file, so its post install dialog
  // has to be accepted before the end of the task: once the file is over,
  // alwaysAcceptAddonPostInstallDialogs has stopped accepting them and the
  // dialog would stay open for the next test file.
  let postInstallDialogShown = BrowserTestUtils.waitForEvent(
    PanelUI.notificationPanel,
    "popupshown"
  );
  installDialog.button.click();

  let allowResolved = await SpecialPowers.spawn(
    allowTab.linkedBrowser,
    [],
    async () => {
      try {
        await content.midiAccessRequestPromise;
        return true;
      } catch (e) {
      } finally {
        delete content.midiAccessRequestPromise;
      }
      return false;
    }
  );
  ok(allowResolved, "requestMIDIAccess resolved in allow phase");

  await BrowserTestUtils.removeTab(allowTab);
  await waitForFlush(afterDeny.requestPage);

  let afterAllow = await snapshotCounters();
  is(
    afterAllow.grantedPage,
    afterDeny.grantedPage + 1,
    "midiaccess_granted page counter incremented when permission granted"
  );
  is(
    afterAllow.grantedDoc,
    afterDeny.grantedDoc + 1,
    "midiaccess_granted document counter incremented when permission granted"
  );

  info("Wait for the post install dialog to be shown and accepted");
  await postInstallDialogShown;
  await BrowserTestUtils.waitForPopupEvent(PanelUI.notificationPanel, "hidden");
  is(
    AppMenuNotifications.activeNotification,
    null,
    "the post install dialog has been accepted"
  );
});

// The "install" permission is not isolated by container, so a site the user
// never allows to install add-ons is denied without prompt in a container tab.
add_task(async function testRequestMIDIAccessInstallDeniedContainer() {
  await SpecialPowers.removePermission("midi", { url: EXAMPLE_COM_URL });
  await SpecialPowers.removePermission("midi-sysex", { url: EXAMPLE_COM_URL });
  await SpecialPowers.addPermission("install", false, {
    url: EXAMPLE_COM_URL,
  });

  const tab = BrowserTestUtils.addTab(gBrowser, EXAMPLE_COM_URL, {
    userContextId: 2,
  });
  gBrowser.selectedTab = tab;
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  const denyIntervalStart = performance.now();
  const rejectionMessage = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    async () => {
      try {
        await content.navigator.requestMIDIAccess();
      } catch (e) {
        return e.name;
      }
      return null;
    }
  );
  is(
    rejectionMessage,
    "SecurityError",
    "requestMIDIAccess was rejected without user prompt"
  );
  Assert.greaterOrEqual(
    performance.now() - denyIntervalStart,
    2000,
    "Rejection should be noticeably delayed"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.removePermission("install", {
    url: EXAMPLE_COM_URL,
  });
});

// See helpers_addons_install_dialogs.js for shared helpers. If needed, update the shared
// helpers defined there instead of re-introducing local copies.

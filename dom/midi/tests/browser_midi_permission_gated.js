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
});

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["midi.prompt.testing", false]],
  });

  AddonTestUtils.initMochitest(this);
  AddonTestUtils.hookAMTelemetryEvents();

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
    "NotAllowedError: WebMIDI requires a site permission add-on to activate"
  );

  assertSitePermissionInstallTelemetryEvents(["site_warning", "cancelled"]);

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
    "NotAllowedError: WebMIDI requires a site permission add-on to activate"
  );

  assertSitePermissionInstallTelemetryEvents([
    "site_warning",
    "permissions_prompt",
    "cancelled",
  ]);

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

  assertSitePermissionInstallTelemetryEvents([
    "site_warning",
    "permissions_prompt",
    "completed",
  ]);

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

  assertSitePermissionInstallTelemetryEvents([
    "site_warning",
    "permissions_prompt",
    "completed",
  ]);

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
  is(rejectionMessage, "NotAllowedError", "requestMIDIAccess was rejected");

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
    "NotAllowedError",
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
    [{ suspicious_site: "example.com" }],
    AddonTestUtils.getAMGleanEvents("reportSuspiciousSite"),
    "Expected Glean event recorded."
  );

  assertSitePermissionInstallTelemetryEvents(["site_warning", "cancelled"]);
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

  assertSitePermissionInstallTelemetryEvents([
    "site_warning",
    "permissions_prompt",
    "completed",
  ]);

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
    "NotAllowedError",
    "requestMIDIAccess from the remote iframe was rejected"
  );

  const consoleErrorMessage = await onConsoleErrorMessage;
  ok(
    consoleErrorMessage.message.includes(
      `WebMIDI access request was denied: ❝SitePermsAddons can't be installed from cross origin subframes❞`,
      "an error message is sent to the console"
    )
  );
  assertSitePermissionInstallTelemetryEvents([]);
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

  assertSitePermissionInstallTelemetryEvents([]);
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

// Ignore any additional telemetry events collected in this file.
// Unfortunately it doesn't work to have this in a cleanup function.
// Keep this as the last task done.
add_task(function teardown_telemetry_events() {
  AddonTestUtils.getAMTelemetryEvents();
});

/**
 *  Check that the expected sitepermission install events are recorded.
 *
 * @param {Array<string>} expectedSteps: An array of the expected extra.step values recorded.
 */
function assertSitePermissionInstallTelemetryEvents(expectedSteps) {
  let amInstallEvents = AddonTestUtils.getAMTelemetryEvents()
    .filter(evt => evt.method === "install" && evt.object === "sitepermission")
    .map(evt => evt.extra.step);

  Assert.deepEqual(amInstallEvents, expectedSteps);
}

async function waitForInstallDialog(id = "addon-webext-permissions") {
  let panel = await waitForNotification(id);
  return panel.childNodes[0];
}

/**
 * Adds an event listener that will listen for post-install dialog event and automatically
 * close the dialogs.
 */
function alwaysAcceptAddonPostInstallDialogs() {
  // Once the addon is installed, a dialog is displayed as a confirmation.
  // This could interfere with tests running after this one, so we set up a listener
  // that will always accept post install dialogs so we don't have  to deal with them in
  // the test.
  const abortController = new AbortController();

  const { AppMenuNotifications } = ChromeUtils.importESModule(
    "resource://gre/modules/AppMenuNotifications.sys.mjs"
  );
  info("Start listening and accept addon post-install notifications");
  PanelUI.notificationPanel.addEventListener(
    "popupshown",
    async function popupshown() {
      let notification = AppMenuNotifications.activeNotification;
      if (!notification || notification.id !== "addon-installed") {
        return;
      }

      let popupnotificationID = PanelUI._getPopupId(notification);
      if (popupnotificationID) {
        info("Accept post-install dialog");
        let popupnotification = document.getElementById(popupnotificationID);
        popupnotification?.button.click();
      }
    },
    {
      signal: abortController.signal,
    }
  );

  registerCleanupFunction(async () => {
    // Clear the listener at the end of the test file, to prevent it to stay
    // around when the same browser instance may be running other unrelated
    // test files.
    abortController.abort();
  });
}

const PROGRESS_NOTIFICATION = "addon-progress";
async function waitForNotification(notificationId) {
  info(`Waiting for ${notificationId} notification`);

  let topic = getObserverTopic(notificationId);

  let observerPromise;
  if (notificationId !== "addon-webext-permissions") {
    observerPromise = new Promise(resolve => {
      Services.obs.addObserver(function observer(aSubject, aTopic) {
        // Ignore the progress notification unless that is the notification we want
        if (
          notificationId != PROGRESS_NOTIFICATION &&
          aTopic == getObserverTopic(PROGRESS_NOTIFICATION)
        ) {
          return;
        }
        Services.obs.removeObserver(observer, topic);
        resolve();
      }, topic);
    });
  }

  let panelEventPromise = new Promise(resolve => {
    window.PopupNotifications.panel.addEventListener(
      "PanelUpdated",
      function eventListener(e) {
        // Skip notifications that are not the one that we are supposed to be looking for
        if (!e.detail.includes(notificationId)) {
          return;
        }
        window.PopupNotifications.panel.removeEventListener(
          "PanelUpdated",
          eventListener
        );
        resolve();
      }
    );
  });

  await observerPromise;
  await panelEventPromise;
  await waitForTick();

  info(`Saw a ${notificationId} notification`);
  await SimpleTest.promiseFocus(window.PopupNotifications.window);
  return window.PopupNotifications.panel;
}

// This function is similar to the one in
// toolkit/mozapps/extensions/test/xpinstall/browser_doorhanger_installs.js,
// please keep both in sync!
function getObserverTopic(aNotificationId) {
  let topic = aNotificationId;
  if (topic == "xpinstall-disabled") {
    topic = "addon-install-disabled";
  } else if (topic == "addon-progress") {
    topic = "addon-install-started";
  } else if (topic == "addon-installed") {
    topic = "webextension-install-notify";
  }
  return topic;
}

function waitForTick() {
  return new Promise(resolve => executeSoon(resolve));
}

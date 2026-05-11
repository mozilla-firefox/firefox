/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const EXAMPLE_COM_URL =
  "https://example.com/document-builder.sjs?html=<h1>Test serial permission with synthetic site permission addon</h1>";
const PAGE_WITH_IFRAMES_URL = `https://example.org/document-builder.sjs?html=
  <h1>Test serial permission with synthetic site permission addon in iframes</h1>
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
    set: [["dom.webserial.gated", true]],
  });

  AddonTestUtils.initMochitest(this);
  AddonTestUtils.hookAMTelemetryEvents();

  alwaysAcceptAddonPostInstallDialogs();

  registerCleanupFunction(async () => {
    await SpecialPowers.removePermission("serial", {
      url: EXAMPLE_COM_URL,
    });
    await SpecialPowers.removePermission("serial", {
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

add_task(async function testRequestPort() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, EXAMPLE_COM_URL);
  const testPageHost = gBrowser.selectedTab.linkedBrowser.documentURI.host;
  Services.fog.testResetFOG();

  ok(
    await SpecialPowers.testPermission(
      "serial",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "serial value should have UNKNOWN permission"
  );

  info("Request serial port access");
  let onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });

  info("Deny site permission addon install in first popup");
  let addonInstallPanel = await onAddonInstallBlockedNotification;
  const [installPopupHeader, installPopupMessage] =
    addonInstallPanel.querySelectorAll(
      "description.popup-notification-description"
    );
  is(
    installPopupHeader.textContent,
    l10n.formatValueSync("site-permission-install-first-prompt-serial-header"),
    "First popup has expected header text"
  );
  is(
    installPopupMessage.textContent,
    l10n.formatValueSync("site-permission-install-first-prompt-serial-message"),
    "First popup has expected message"
  );

  let notification = addonInstallPanel.childNodes[0];
  notification.secondaryButton.click();

  let rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.portRequestPromise;
      } catch (e) {
        errorMessage = `${e.name}: ${e.message}`;
      }

      delete content.portRequestPromise;
      return errorMessage;
    }
  );
  is(rejectionMessage, "NotFoundError: No port selected");

  assertSitePermissionInstallTelemetryEvents(["site_warning", "cancelled"]);

  info("Deny site permission addon install in second popup");
  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  addonInstallPanel = await onAddonInstallBlockedNotification;
  notification = addonInstallPanel.childNodes[0];
  let dialogPromise = waitForInstallDialog();
  notification.button.click();
  let installDialog = await dialogPromise;
  is(
    installDialog.querySelector(".popup-notification-description").textContent,
    l10n.formatValueSync("webext-site-perms-header-with-gated-perms-serial", {
      hostname: testPageHost,
    }),
    "Install dialog has expected header text"
  );
  is(
    installDialog.querySelector("popupnotificationcontent description")
      .textContent,
    l10n.formatValueSync("webext-site-perms-description-gated-perms-webserial"),
    "Install dialog has expected description"
  );

  installDialog.secondaryButton.click();

  rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.portRequestPromise;
      } catch (e) {
        errorMessage = `${e.name}: ${e.message}`;
      }

      delete content.portRequestPromise;
      return errorMessage;
    }
  );
  is(
    rejectionMessage,
    "NotFoundError: No port selected",
    "got expected error when rejecting add-on"
  );

  assertSitePermissionInstallTelemetryEvents([
    "site_warning",
    "permissions_prompt",
    "cancelled",
  ]);

  info("Request serial port access again");
  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.navigator.serial.autoselectPorts = true;
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });

  info("Accept site permission addon install");
  addonInstallPanel = await onAddonInstallBlockedNotification;
  notification = addonInstallPanel.childNodes[0];
  dialogPromise = waitForInstallDialog();
  is(
    notification
      .querySelector("#addon-install-blocked-info")
      .getAttribute("href"),
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "site-permission-addons",
    "Got the expected SUMO page as a learn more link in the addon-install-blocked panel"
  );
  notification.button.click();
  installDialog = await dialogPromise;
  installDialog.button.click();

  info("Wait for the serial port access request promise to resolve");
  let accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.portRequestPromise;
        return true;
      } catch (e) {}

      delete content.portRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestPort resolved");

  ok(
    await SpecialPowers.testPermission(
      "serial",
      SpecialPowers.Services.perms.ALLOW_ACTION,
      { url: EXAMPLE_COM_URL }
    ),
    "serial value should have ALLOW permission"
  );

  info("Check that we don't prompt user again once they installed the addon");
  const accessPromiseState = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    () => {
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      return content.navigator.serial.requestPort().then(() => "resolved");
    }
  );
  is(
    accessPromiseState,
    "resolved",
    "requestPort resolved without user prompt"
  );

  assertSitePermissionInstallTelemetryEvents([
    "site_warning",
    "permissions_prompt",
    "completed",
  ]);

  info("Check that we don't prompt user again when they perm denied");
  await SpecialPowers.removePermission("serial", {
    url: EXAMPLE_COM_URL,
  });

  onAddonInstallBlockedNotification = waitForNotification(
    "addon-install-blocked"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });

  info("Perm-deny site permission addon install");
  addonInstallPanel = await onAddonInstallBlockedNotification;
  notification = addonInstallPanel.childNodes[0];
  notification.menupopup.querySelectorAll("menuitem")[1].click();

  rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorMessage;
      try {
        await content.portRequestPromise;
      } catch (e) {
        errorMessage = e.name;
      }

      delete content.portRequestPromise;
      return errorMessage;
    }
  );
  is(rejectionMessage, "NotFoundError", "requestPort was rejected");

  info("Request serial port access again");
  let denyIntervalStart = performance.now();
  rejectionMessage = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      let errorMessage;
      try {
        await content.navigator.serial.requestPort();
      } catch (e) {
        errorMessage = e.name;
      }
      return errorMessage;
    }
  );
  is(
    rejectionMessage,
    "NotFoundError",
    "requestPort was rejected without user prompt"
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

add_task(async function testIframeRequestPort() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, PAGE_WITH_IFRAMES_URL);

  info("Check that serial permission isn't set");
  ok(
    await SpecialPowers.testPermission(
      "serial",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: PAGE_WITH_IFRAMES_URL }
    ),
    "serial value should have UNKNOWN permission"
  );

  info("Request serial port access from the same-origin iframe");
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
    content.navigator.serial.autoselectPorts = true;
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });

  info("Accept site permission addon install");
  const addonInstallPanel = await onAddonInstallBlockedNotification;
  const notification = addonInstallPanel.childNodes[0];
  const dialogPromise = waitForInstallDialog();
  notification.button.click();
  let installDialog = await dialogPromise;
  installDialog.button.click();

  info("Wait for the serial port access request promise to resolve");
  const accessGranted = await SpecialPowers.spawn(
    sameOriginIframeBrowsingContext,
    [],
    async () => {
      try {
        await content.portRequestPromise;
        return true;
      } catch (e) {}

      delete content.portRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestPort resolved");

  info("Check that serial permission is now set");
  ok(
    await SpecialPowers.testPermission(
      "serial",
      SpecialPowers.Services.perms.ALLOW_ACTION,
      { url: PAGE_WITH_IFRAMES_URL }
    ),
    "serial value should have ALLOW permission"
  );

  info(
    "Check that we don't prompt user again once they installed the addon from the same-origin iframe"
  );
  const accessPromiseState = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    () => {
      content.navigator.serial.autoselectPorts = true;
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      return content.navigator.serial.requestPort().then(() => "resolved");
    }
  );
  is(
    accessPromiseState,
    "resolved",
    "requestPort resolved without user prompt"
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
        if (error.message.includes("WebSerial access request was denied")) {
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
      SpecialPowers.wrap(content.document).notifyUserGestureActivation();
      let errorName;
      try {
        await content.navigator.serial.requestPort();
      } catch (e) {
        errorName = e.name;
      }
      return errorName;
    }
  );

  is(
    rejectionMessage,
    "SecurityError",
    "requestPort from the remote iframe was rejected"
  );

  const consoleErrorMessage = await onConsoleErrorMessage;
  ok(
    consoleErrorMessage.message.includes(
      "WebSerial access request was denied:"
    ),
    "an error message is sent to the console"
  );
  assertSitePermissionInstallTelemetryEvents([]);
});

add_task(async function testRequestPortLocalhost() {
  const httpServer = new HttpServer();
  httpServer.start(-1);
  httpServer.registerPathHandler(`/test`, function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.write(`
      <!DOCTYPE html>
      <meta charset=utf8>
      <h1>Test requestPort on localhost</h1>`);
  });
  const localHostTestUrl = `http://localhost:${httpServer.identity.primaryPort}/test`;

  registerCleanupFunction(async function cleanup() {
    await new Promise(resolve => httpServer.stop(resolve));
  });

  await BrowserTestUtils.openNewForegroundTab(gBrowser, localHostTestUrl);

  ok(
    await SpecialPowers.testPermission(
      "serial",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: localHostTestUrl }
    ),
    "serial value should have UNKNOWN permission"
  );

  info(
    "Request serial port access should not prompt for addon install on localhost, but for permission"
  );
  let popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.navigator.serial.autoselectPorts = false;
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  await popupShown;
  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "webSerial-choosePort-notification",
    "webserial notification was displayed"
  );

  info("Accept permission");
  PopupNotifications.panel
    .querySelector(".popup-notification-primary-button")
    .click();

  info("Wait for the serial port access request promise to resolve");
  let accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.portRequestPromise;
        return true;
      } catch (e) {}

      delete content.portRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestPort resolved");

  info(
    "Check that requestPort() shows the chooser again (per spec, it always requires user interaction)"
  );

  popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  await popupShown;

  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "webSerial-choosePort-notification",
    "webserial notification was displayed again"
  );

  info("Accept permission again");
  PopupNotifications.panel
    .querySelector(".popup-notification-primary-button")
    .click();

  accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.portRequestPromise;
        return true;
      } catch (e) {}

      delete content.portRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestPort resolved after user interaction");

  info("Check that blocking the requestPort() chooser returns the right error");

  popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  await popupShown;

  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "webSerial-choosePort-notification",
    "webserial notification was displayed again"
  );

  info("Block permission");
  PopupNotifications.panel
    .querySelector(".popup-notification-secondary-button")
    .click();

  let errorInfo = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorName, errorMessage;
      try {
        await content.portRequestPromise;
      } catch (e) {
        errorName = e.name;
        errorMessage = e.message;
      }
      delete content.portRequestPromise;
      return { name: errorName, message: errorMessage };
    }
  );
  is(errorInfo.name, "NotFoundError", "Rejection is NotFoundError");
  is(errorInfo.message, "No port selected", "Error message is correct");

  assertSitePermissionInstallTelemetryEvents([]);
});

add_task(async function testRequestPortFile() {
  let dir = getChromeDir(getResolvedURI(gTestPath));
  dir.append("blank.html");
  const fileSchemeTestUri = Services.io.newFileURI(dir).spec;
  await BrowserTestUtils.openNewForegroundTab(gBrowser, fileSchemeTestUri);

  ok(
    await SpecialPowers.testPermission(
      "serial",
      SpecialPowers.Services.perms.UNKNOWN_ACTION,
      { url: fileSchemeTestUri }
    ),
    "serial value should have UNKNOWN permission"
  );

  info(
    "Request serial port access should not prompt for addon install on file, but for permission"
  );
  let popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.navigator.serial.autoselectPorts = false;
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  await popupShown;
  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "webSerial-choosePort-notification",
    "webserial notification was displayed"
  );

  info("Accept permission");
  PopupNotifications.panel
    .querySelector(".popup-notification-primary-button")
    .click();

  info("Wait for the serial port access request promise to resolve");
  let accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.portRequestPromise;
        return true;
      } catch (e) {}

      delete content.portRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestPort resolved");

  info(
    "Check that requestPort() shows the chooser again (per spec, it always requires user interaction)"
  );

  popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  await popupShown;

  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "webSerial-choosePort-notification",
    "webserial notification was displayed again"
  );

  info("Accept permission again");
  PopupNotifications.panel
    .querySelector(".popup-notification-primary-button")
    .click();

  accessGranted = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      try {
        await content.portRequestPromise;
        return true;
      } catch (e) {}

      delete content.portRequestPromise;
      return false;
    }
  );
  ok(accessGranted, "requestPort resolved after user interaction");

  info("Check that blocking the requestPort() chooser returns the right error");

  popupShown = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popupshown"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.portRequestPromise = content.navigator.serial.requestPort();
  });
  await popupShown;

  is(
    PopupNotifications.panel.querySelector("popupnotification").id,
    "webSerial-choosePort-notification",
    "webserial notification was displayed again"
  );

  info("Block permission");
  PopupNotifications.panel
    .querySelector(".popup-notification-secondary-button")
    .click();

  let errorInfo = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async () => {
      let errorName, errorMessage;
      try {
        await content.portRequestPromise;
      } catch (e) {
        errorName = e.name;
        errorMessage = e.message;
      }
      delete content.portRequestPromise;
      return { name: errorName, message: errorMessage };
    }
  );
  is(errorInfo.name, "NotFoundError", "Rejection is NotFoundError");
  is(errorInfo.message, "No port selected", "Error message is correct");

  assertSitePermissionInstallTelemetryEvents([]);
});

add_task(function teardown_telemetry_events() {
  AddonTestUtils.getAMTelemetryEvents();
});

function assertSitePermissionInstallTelemetryEvents(expectedSteps) {
  let amInstallEvents = AddonTestUtils.getAMTelemetryEvents()
    .filter(evt => evt.method === "install" && evt.object === "sitepermission")
    .map(evt => evt.extra.step);

  Assert.deepEqual(
    amInstallEvents,
    expectedSteps,
    "got expected site permission install telemetry events"
  );
}

async function waitForInstallDialog(id = "addon-webext-permissions") {
  let panel = await waitForNotification(id);
  return panel.childNodes[0];
}

function alwaysAcceptAddonPostInstallDialogs() {
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
    abortController.abort();
  });
}

async function waitForNotification(notificationId) {
  info(`Waiting for ${notificationId} notification`);

  let topic = getObserverTopic(notificationId);
  let observerPromise;
  if (notificationId !== "addon-webext-permissions") {
    observerPromise = new Promise(resolve => {
      Services.obs.addObserver(function observer(_aSubject, _aTopic) {
        Services.obs.removeObserver(observer, topic);
        resolve();
      }, topic);
    });
  }

  let panelEventPromise = new Promise(resolve => {
    window.PopupNotifications.panel.addEventListener(
      "PanelUpdated",
      function eventListener(e) {
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

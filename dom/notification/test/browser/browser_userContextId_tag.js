const URI =
  "https://example.com/browser/dom/notification/test/browser/empty.html";

const MOCK_CID = Components.ID("{2a0f83c4-8818-4914-a184-f1172b4eaaa7}");
const SYSTEM_CID = Components.ID("{a0ccaaf8-09da-44d8-b250-9ac3e93c8117}");
const ALERTS_SERVICE_CONTRACT_ID = "@mozilla.org/alerts-service;1";

// Set up a mock alert service that works as a MITM observer, to observe alert
// topics while still using the system alerts service (as we depend on system
// backend for tag processing)

let alertsService = Cc["@mozilla.org/alerts-service;1"].getService(
  Ci.nsIAlertsService
);

let mockAlertsService = {
  history: [],

  showAlertWithCallbacks(alert, alertCallbacks) {
    alertsService.showAlertWithCallbacks(
      alert,
      new Proxy(alertCallbacks, {
        get: (target, prop) => {
          if (prop.startsWith("onAlert")) {
            this.history.push({ prop });
          }
          return target[prop];
        },
      })
    );
  },

  // Notification.close() reaches the alerts service through this mock.
  closeAlert(name, contextClosed) {
    alertsService.closeAlert(name, contextClosed);
  },

  QueryInterface: ChromeUtils.generateQI(["nsIAlertsService"]),

  createInstance(aIID) {
    return this.QueryInterface(aIID);
  },
};

registerCleanupFunction(() => {
  const registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
  registrar.unregisterFactory(MOCK_CID, mockAlertsService);
  registrar.registerFactory(SYSTEM_CID, "", ALERTS_SERVICE_CONTRACT_ID, null);
});

add_setup(() => {
  Components.manager
    .QueryInterface(Ci.nsIComponentRegistrar)
    .registerFactory(
      MOCK_CID,
      "alerts service",
      ALERTS_SERVICE_CONTRACT_ID,
      mockAlertsService
    );
});

add_task(async function no_tag_collision_between_containers() {
  // Open tabs on two different containers

  let tab1 = BrowserTestUtils.addTab(gBrowser, URI, {
    userContextId: 3,
  });
  let browser1 = gBrowser.getBrowserForTab(tab1);

  let tab2 = BrowserTestUtils.addTab(gBrowser, URI, {
    userContextId: 4,
  });
  let browser2 = gBrowser.getBrowserForTab(tab2);

  await Promise.all([
    BrowserTestUtils.browserLoaded(browser1),
    BrowserTestUtils.browserLoaded(browser2),
  ]);

  // Open notifications on each tab with the same tag

  await Promise.all(
    [browser1, browser2].map(browser =>
      SpecialPowers.spawn(browser, [], async () => {
        await SpecialPowers.pushPermissions([
          {
            type: "desktop-notification",
            allow: SpecialPowers.Services.perms.ALLOW_ACTION,
            context: content.document,
          },
        ]);

        const notification = new content.Notification("foo", { tag: "bar" });
        const { promise, resolve, reject } = Promise.withResolvers();
        notification.onshow = () => resolve();
        notification.onerror = () => reject("onerror");
        notification.onclose = () => {};
        content.wrappedJSObject.notification = notification;
        return promise;
      })
    )
  );

  // The notifications should not interfere across containers

  is(mockAlertsService.history.length, 2, "Should observe two alert topics");
  for (const entry of mockAlertsService.history) {
    is(entry.prop, "onAlertShow", "Should only observe alertshow");
  }

  // Closing the tab leaves the system notification in the Windows Action
  // Center, and the next run's notifications with the same tag then report
  // onAlertDismissedFromForeground, so close the notifications explicitly.
  await Promise.all(
    [browser1, browser2].map(browser =>
      SpecialPowers.spawn(browser, [], async () => {
        const { notification } = content.wrappedJSObject;
        const closed = new Promise(resolve => (notification.onclose = resolve));
        notification.close();
        await closed;
      })
    )
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

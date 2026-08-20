/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Shared helpers for browser tests that exercise addon install dialogs
 * (e.g. WebMIDI, WebSerial, and the toolkit xpinstall doorhanger tests).
 */

"use strict";

/* exported waitForInstallDialog, alwaysAcceptAddonPostInstallDialogs, waitForNotification, getObserverTopic, waitForTick */

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
  // that will always accept post install dialogs so we don't have to deal with them in
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

async function waitForNotification(
  aId,
  aExpectedCount = 1,
  expectedAnchorID = "unified-extensions-button",
  win = window
) {
  const PROGRESS_NOTIFICATION = "addon-progress";
  info(`Waiting for ${aId} notification`);

  let topic = getObserverTopic(aId);

  let observerPromise;
  if (aId !== "addon-webext-permissions") {
    observerPromise = new Promise(resolve => {
      Services.obs.addObserver(function observer(aSubject, aTopic) {
        // Ignore the progress notification unless that is the notification we want
        if (
          aId != PROGRESS_NOTIFICATION &&
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
    win.PopupNotifications.panel.addEventListener(
      "PanelUpdated",
      function eventListener(e) {
        // Skip notifications that are not the one that we are supposed to be looking for
        if (!e.detail.includes(aId)) {
          return;
        }
        win.PopupNotifications.panel.removeEventListener(
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

  // PanelUpdated fires while the panel may still be animating open. Wait for it
  // to be fully open, as its contents are not clickable before that.
  await BrowserTestUtils.waitForPopupEvent(
    win.PopupNotifications.panel,
    "shown"
  );

  info(`Saw a ${aId} notification`);
  is(win.PopupNotifications.panel.state, "open", "Panel should be open");
  is(
    win.PopupNotifications.panel.childNodes.length,
    aExpectedCount,
    "Should be the right number of notifications"
  );
  if (win.PopupNotifications.panel.childNodes.length) {
    let nodes = Array.from(win.PopupNotifications.panel.childNodes);
    let notification = nodes.find(n => n.id == aId + "-notification");
    ok(notification, `Should have seen the ${aId} notification`);

    let n = win.PopupNotifications.getNotification(aId);
    is(
      n?.anchorElement?.id || n?.anchorElement?.parentElement?.id,
      expectedAnchorID,
      "expected the right anchor ID"
    );
  }
  await SimpleTest.promiseFocus(win.PopupNotifications.window);

  return win.PopupNotifications.panel;
}

function getObserverTopic(aNotificationId) {
  let topic = aNotificationId;
  if (topic == "xpinstall-disabled") {
    topic = "addon-install-disabled";
  } else if (topic == "addon-progress") {
    topic = "addon-install-started";
  } else if (topic == "addon-installed") {
    topic = "webextension-install-notify";
  } else if (topic == "addon-install-failed-blocklist") {
    topic = "addon-install-failed";
  }
  return topic;
}

function waitForTick() {
  return new Promise(resolve => executeSoon(resolve));
}

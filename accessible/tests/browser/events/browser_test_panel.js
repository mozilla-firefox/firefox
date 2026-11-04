/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

// Verify we receive hide and show notifications when the chrome
// XUL alert is closed or opened. Mac expects both notifications to
// properly communicate live region changes.
async function runTests(browser) {
  // Enable accesskey underlining on all platforms for consistent test behavior.
  await SpecialPowers.pushPrefEnv({
    set: [["ui.key.menuAccessKey", 1]],
  });
  ok(PopupNotifications, "PopupNotifications object exists");
  ok(PopupNotifications.panel, "PopupNotifications panel exists");

  // When available, the popup panel makes itself a child of the chrome window.
  // To verify it isn't accessible without reproducing the entirety of the chrome
  // window tree, we check instead that the panel is not accessible.
  ok(!isAccessible(PopupNotifications.panel), "Popup panel is not accessible");

  const panelShown = waitForEvent(EVENT_SHOW, PopupNotifications.panel);
  const notification = PopupNotifications.show(
    browser,
    "test-notification",
    "hello world",
    PopupNotifications.panel.id
  );

  await panelShown;
  ok(isAccessible(PopupNotifications.panel), "Popup panel is accessible");

  const primaryButton = PopupNotifications.panel.querySelector(
    ".popup-notification-primary-button"
  );
  const buttonLabel = primaryButton.shadowRoot.querySelector("label");
  await TestUtils.waitForCondition(() => {
    try {
      return getAccessible(buttonLabel, [nsIAccessible])?.childCount > 0;
    } catch {
      return false;
    }
  }, "Primary button accessible label has text child");

  testAccessibleTree(PopupNotifications.panel, {
    ALERT: [
      { LABEL: [{ TEXT_LEAF: [] }] },
      { PUSHBUTTON: [] },
      {
        TEXT_CONTAINER: [
          {
            // The primary button label has two text leaves because of the
            // accesskey underlining.
            PUSHBUTTON: [{ LABEL: [{ TEXT_LEAF: [] }, { TEXT_LEAF: [] }] }],
          },
        ],
      },
    ],
  });
  // Verify the popup panel is associated with the chrome window.
  is(
    PopupNotifications.panel.documentGlobal,
    getMainChromeWindow(window),
    "Popup panel is associated with the chrome window"
  );

  const panelHidden = waitForEvent(EVENT_HIDE, PopupNotifications.panel);
  PopupNotifications.remove(notification);

  await panelHidden;

  ok(!isAccessible(PopupNotifications.panel), "Popup panel is not accessible");
}

addAccessibleTask(``, runTests);

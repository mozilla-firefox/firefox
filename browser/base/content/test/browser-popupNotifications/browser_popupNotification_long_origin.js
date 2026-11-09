/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that long origins in both the name and secondName placeholders wrap
// inside the description instead of overflowing it.
add_task(async function test_long_origins_wrap() {
  let longOrigin =
    "abc.login.secures.documents.drives.accounts.support.google.com.example.org";
  let popupShownPromise = waitForNotificationPanel();
  let notification = PopupNotifications.show(
    gBrowser.selectedBrowser,
    "test-long-origin",
    "Allow <> to give {} permission?",
    null,
    { label: "Allow", accessKey: "A", callback() {} },
    [],
    { name: longOrigin, secondName: longOrigin }
  );
  await popupShownPromise;

  let description = PopupNotifications.panel.querySelector(
    ".popup-notification-description"
  );
  let descriptionRight = description.getBoundingClientRect().right;
  let [name, secondName] = description.querySelectorAll("b");
  for (let [label, element] of [
    ["name", name],
    ["secondName", secondName],
  ]) {
    is(element.textContent, longOrigin, `${label} has the origin`);
    Assert.lessOrEqual(
      element.getBoundingClientRect().right,
      descriptionRight + 1,
      `${label} does not overflow the description`
    );
  }

  let popupHiddenPromise = waitForNotificationPanelHidden();
  notification.remove();
  await popupHiddenPromise;
});

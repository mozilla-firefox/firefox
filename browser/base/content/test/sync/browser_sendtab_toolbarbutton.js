/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function promiseLayout() {
  // Wait for layout to have happened.
  return new Promise(resolve =>
    requestAnimationFrame(() => requestAnimationFrame(resolve))
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.enabled", true]],
  });

  registerCleanupFunction(() => CustomizableUI.reset());
});

async function withOpenSendTabToolbarMenu(callback) {
  const button = document.getElementById("send-tab-button");
  const popup = button.querySelector("menupopup");

  const popupShownPromise = BrowserTestUtils.waitForEvent(popup, "popupshown");

  try {
    button.click();
    await popupShownPromise;
    await callback(popup);
  } finally {
    popup.hidePopup();
  }
}

function checkPopup(expectedItems, popup) {
  is(popup.state, "open", "Popup should be open.");
  const menuItems = popup.children;
  for (let i = 0; i < menuItems.length; i++) {
    const menuItem = menuItems[i];
    const expectedItem = expectedItems[i];
    if (expectedItem.isSeparator) {
      is(menuItem.nodeName, "menuseparator", "Should have found a separator");
      continue;
    }
    is(menuItem.nodeName, "menuitem", "Should have found a menu item");
    is(
      menuItem.label,
      expectedItem.label,
      "Should have menu item label: " + expectedItem.label
    );
    is(
      menuItem.disabled,
      !!expectedItem.disabled,
      "Should have correct menu item disabled state"
    );
  }
  is(
    menuItems.length,
    expectedItems.length,
    "Popup has the expected child count"
  );
}

add_task(async function test_toolbar_button_signed_in() {
  const sandbox = setupSendTabMocks({
    fxaDevices: [
      {
        id: 1,
        name: "Foo",
        availableCommands: {
          "https://identity.mozilla.com/cmd/open-uri": "baz",
        },
        lastAccessTime: Date.now(),
      },
      {
        id: 2,
        name: "Bar",
        availableCommands: {
          "https://identity.mozilla.com/cmd/open-uri": "qux",
        },
        lastAccessTime: Date.now() + 60000,
      },
    ],
  });

  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  CustomizableUI.addWidgetToArea(
    "send-tab-button",
    CustomizableUI.AREA_NAVBAR,
    0
  );
  await promiseLayout();

  const [allDevicesLabel, manageDevicesLabel] =
    gSync.fluentStrings.formatValuesSync([
      "account-send-to-all-devices-titlecase",
      "account-manage-devices-titlecase",
    ]);

  await withOpenSendTabToolbarMenu(
    checkPopup.bind(null, [
      { label: "Bar" },
      { label: "Foo" },
      { isSeparator: true },
      { label: allDevicesLabel },
      { label: manageDevicesLabel },
    ])
  );

  sandbox.restore();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

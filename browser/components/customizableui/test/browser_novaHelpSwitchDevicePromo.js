/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function openHelpView() {
  await gCUITestUtils.openMainMenu();
  PanelUI.showHelpView(document.getElementById("PanelUI-menu-button"));
  let helpView = document.getElementById("PanelUI-helpView");
  await BrowserTestUtils.waitForEvent(helpView, "ViewShowing");
  return helpView;
}

add_task(async function testNovaPromoReplacesHelpSwitchDevice() {
  let helpView = await openHelpView();

  ok(
    !helpView.querySelector("#appMenu_helpSwitchDevice"),
    "appMenu_helpSwitchDevice should not be present"
  );

  let promo = helpView.querySelector("#appMenu-nova-switch-device-promo");
  ok(promo, "Switch device promo should be present");

  await gCUITestUtils.hideMainMenu();
});

add_task(
  async function testNovaPromoSupportLinkCallsOpenSwitchingDevicesPage() {
    let switchingDevicesPageOpened = false;
    let originalFn = window.openSwitchingDevicesPage;
    window.openSwitchingDevicesPage = () => {
      switchingDevicesPageOpened = true;
    };

    let helpView = await openHelpView();

    let link = helpView.querySelector("#appMenu-nova-switch-device-link");
    ok(link, "Switch device support link should exist");
    link.click();

    ok(
      switchingDevicesPageOpened,
      "openSwitchingDevicesPage should be called when the support link is clicked"
    );

    window.openSwitchingDevicesPage = originalFn;
    await gCUITestUtils.hideMainMenu();
  }
);

add_task(async function testNovaPromoLinkOpensForegroundTab() {
  await SpecialPowers.pushPrefEnv({
    set: [["app.support.baseURL", "https://example.com/support/"]],
  });

  let helpView = await openHelpView();

  let tabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    url => url.startsWith("https://example.com/support/switching-devices"),
    false
  );
  helpView.querySelector("#appMenu-nova-switch-device-link").click();
  let tab = await tabPromise;

  Assert.equal(
    gBrowser.selectedTab,
    tab,
    "The switching devices page should open in a foreground tab"
  );

  BrowserTestUtils.removeTab(tab);
  if (PanelUI.panel.state == "open") {
    await gCUITestUtils.hideMainMenu();
  }
  await SpecialPowers.popPrefEnv();
});

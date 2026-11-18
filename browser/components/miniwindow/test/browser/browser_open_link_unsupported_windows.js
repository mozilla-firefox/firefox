/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Adds a link to `browser`'s page, opens its context menu and returns whether
 * "Open Link in Mini Window" is hidden.
 *
 * @param {MozBrowser} browser
 * @returns {Promise<boolean>}
 */
async function isOpenLinkItemHidden(browser) {
  await SpecialPowers.spawn(browser, [], () => {
    let a = content.document.createElement("a");
    a.id = "link";
    a.href = "https://example.org/";
    a.textContent = "link";
    content.document.body.prepend(a);
  });
  let doc = browser.documentGlobal.document;
  let menu = doc.getElementById("contentAreaContextMenu");
  let shown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link",
    { type: "contextmenu" },
    browser
  );
  await shown;
  let hidden = doc.getElementById("context-openlinkminiwindow").hidden;
  let closed = BrowserTestUtils.waitForEvent(menu, "popuphidden");
  menu.hidePopup();
  await closed;
  return hidden;
}

add_task(async function test_hidden_in_popup() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  let url = "https://example.com/?popup";
  let popupPromise = BrowserTestUtils.waitForNewWindow({ url });
  await SpecialPowers.spawn(tab.linkedBrowser, [url], u => {
    content.document.notifyUserGestureActivation();
    content.open(u, "_blank", "popup");
  });
  let popup = await popupPromise;
  Assert.ok(!popup.toolbar.visible, "Opened a popup window");

  Assert.ok(
    await isOpenLinkItemHidden(popup.gBrowser.selectedBrowser),
    "Open Link in Mini Window is hidden in a popup"
  );

  await BrowserTestUtils.closeWindow(popup);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_hidden_in_document_pip() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  let pipPromise = BrowserTestUtils.waitForNewWindow();
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.notifyUserGestureActivation();
    await content.wrappedJSObject.documentPictureInPicture.requestWindow();
  });
  let pip = await pipPromise;
  Assert.ok(pip.browsingContext.isDocumentPiP, "Opened a Document PiP window");

  Assert.ok(
    await isOpenLinkItemHidden(pip.gBrowser.selectedBrowser),
    "Open Link in Mini Window is hidden in a Document PiP window"
  );

  await BrowserTestUtils.closeWindow(pip);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_shown_in_normal_window() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  Assert.ok(
    !(await isOpenLinkItemHidden(tab.linkedBrowser)),
    "Open Link in Mini Window is shown in a normal window"
  );
  BrowserTestUtils.removeTab(tab);
});

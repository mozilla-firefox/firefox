/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const chrome_base =
  "chrome://mochitests/content/browser/browser/base/content/test/browser-contextMenu/";

Services.scriptloader.loadSubScript(
  chrome_base + "contextmenu_common.js",
  this
);

const ALLOWED_CHROME_IMAGE_URLS = [
  "resource://content-accessible/illustrations/security-error.svg",
  "resource://content-accessible/illustrations/no-connection.svg",
  "resource://content-accessible/close-12.svg",
];

add_task(async function test_view_allowed_chrome_image_in_new_tab() {
  for (const imageUrl of ALLOWED_CHROME_IMAGE_URLS) {
    info(`Testing view image in new tab for ${imageUrl}`);

    const htmlContent = `
      <!DOCTYPE html>
      <html>
        <body>
          <img id="test-image" src="${imageUrl}" />
        </body>
      </html>
    `;

    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "data:text/html;charset=utf-8," + encodeURIComponent(htmlContent),
      },
      async browser => {
        await SpecialPowers.spawn(browser, ["#test-image"], async selector => {
          const img = content.document.querySelector(selector);
          if (!img.complete) {
            await ContentTaskUtils.waitForEvent(img, "load");
          }
        });

        let contextMenu = document.getElementById("contentAreaContextMenu");
        let popupShown = BrowserTestUtils.waitForEvent(
          contextMenu,
          "popupshown"
        );

        await BrowserTestUtils.synthesizeMouse(
          "#test-image",
          2,
          2,
          { type: "contextmenu", button: 2 },
          browser
        );
        await popupShown;

        let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser, imageUrl);

        let viewImageItem = document.getElementById("context-viewimage");
        ok(viewImageItem, "View Image menu item should exist");
        ok(!viewImageItem.disabled, "View Image menu item should be enabled");

        contextMenu.activateItem(viewImageItem);

        let newTab = await newTabPromise;
        ok(newTab, "New tab should open with the image");

        is(
          newTab.linkedBrowser.currentURI.spec,
          imageUrl,
          "New tab should have the correct chrome:// URL"
        );

        await BrowserTestUtils.removeTab(newTab);
      }
    );
  }
});

add_task(async function test_save_allowed_chrome_image() {
  const MockFilePicker = SpecialPowers.MockFilePicker;

  for (const imageUrl of ALLOWED_CHROME_IMAGE_URLS) {
    info(`Testing save image for ${imageUrl}`);

    const htmlContent = `
      <!DOCTYPE html>
      <html>
        <body>
          <img id="test-image" src="${imageUrl}" />
        </body>
      </html>
    `;

    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "data:text/html;charset=utf-8," + encodeURIComponent(htmlContent),
      },
      async browser => {
        MockFilePicker.init();

        await SpecialPowers.spawn(browser, ["#test-image"], async selector => {
          const img = content.document.querySelector(selector);
          if (!img.complete) {
            await ContentTaskUtils.waitForEvent(img, "load");
          }
        });

        let contextMenu = document.getElementById("contentAreaContextMenu");
        let popupShown = BrowserTestUtils.waitForEvent(
          contextMenu,
          "popupshown"
        );

        await BrowserTestUtils.synthesizeMouse(
          "#test-image",
          2,
          2,
          { type: "contextmenu", button: 2 },
          browser
        );
        await popupShown;

        let savePromise = new Promise(resolve => {
          MockFilePicker.showCallback = function (fp) {
            is(
              fp.defaultString,
              imageUrl.split("/").pop(),
              "File picker should have correct default filename"
            );
            setTimeout(resolve, 0);
            return Ci.nsIFilePicker.returnCancel;
          };
        });

        let saveImageItem = document.getElementById("context-saveimage");
        ok(saveImageItem, "Save Image menu item should exist");
        ok(!saveImageItem.disabled, "Save Image menu item should be enabled");

        contextMenu.activateItem(saveImageItem);

        await savePromise;

        MockFilePicker.cleanup();
      }
    );
  }
});

add_task(async function test_context_menu_items_for_allowed_chrome_images() {
  for (const imageUrl of ALLOWED_CHROME_IMAGE_URLS) {
    info(`Testing context menu items for ${imageUrl}`);

    const htmlContent = `
      <!DOCTYPE html>
      <html>
        <body>
          <img id="test-image" src="${imageUrl}" />
        </body>
      </html>
    `;

    await BrowserTestUtils.withNewTab(
      {
        gBrowser,
        url: "data:text/html;charset=utf-8," + encodeURIComponent(htmlContent),
      },
      async browser => {
        await SpecialPowers.spawn(browser, ["#test-image"], async selector => {
          const img = content.document.querySelector(selector);
          if (!img.complete) {
            await ContentTaskUtils.waitForEvent(img, "load");
          }
        });

        let contextMenu = document.getElementById("contentAreaContextMenu");
        let popupShown = BrowserTestUtils.waitForEvent(
          contextMenu,
          "popupshown"
        );

        await BrowserTestUtils.synthesizeMouse(
          "#test-image",
          2,
          2,
          { type: "contextmenu", button: 2 },
          browser
        );
        await popupShown;

        let viewImageItem = document.getElementById("context-viewimage");
        ok(viewImageItem, "View Image menu item should exist");
        ok(!viewImageItem.hidden, "View Image menu item should be visible");
        ok(!viewImageItem.disabled, "View Image menu item should be enabled");

        let saveImageItem = document.getElementById("context-saveimage");
        ok(saveImageItem, "Save Image menu item should exist");
        ok(!saveImageItem.hidden, "Save Image menu item should be visible");
        ok(!saveImageItem.disabled, "Save Image menu item should be enabled");

        contextMenu.hidePopup();
      }
    );
  }
});

"use strict";

let extData = {
  manifest: {
    permissions: ["contextMenus"],
    page_action: {
      default_popup: "popup.html",
    },
  },
  useAddonManager: "temporary",

  files: {
    "popup.html": `
      <!DOCTYPE html>
      <html>
      <head><meta charset="utf-8"/>
      </head>
      <body>
      <span id="text">A Test Popup</span>
      <img id="testimg" src="data:image/svg+xml,<svg></svg>" height="10" width="10">
      </body></html>
    `,
  },

  background: function () {
    browser.contextMenus.create({
      id: "clickme-page",
      title: "Click me!",
      contexts: ["all"],
    });
    browser.tabs.query({ active: true, currentWindow: true }, tabs => {
      const tabId = tabs[0].id;

      browser.pageAction.show(tabId).then(() => {
        browser.test.sendMessage("action-shown");
      });
    });
  },
};

let contextMenuItems = {
  "context-sep-navigation": "hidden",
  "context-viewsource": "",
  "inspect-separator": "hidden",
  "context-inspect": "hidden",
  "context-inspect-a11y": "hidden",
  "context-bookmarkpage": "hidden",
};
if (AppConstants.platform == "macosx") {
  contextMenuItems["context-back"] = "hidden";
  contextMenuItems["context-forward"] = "hidden";
  contextMenuItems["context-reload"] = "hidden";
  contextMenuItems["context-stop"] = "hidden";
} else {
  contextMenuItems["context-navigation"] = "hidden";
}

add_task(async function pageaction_popup_contextmenu() {
  let extension = ExtensionTestUtils.loadExtension(extData);
  await extension.startup();
  await extension.awaitMessage("action-shown");

  // Deliberately not awaited: awaitExtensionPanel, called from
  // openContextMenuInPopup, has to start listening for WebExtPopupLoaded before
  // the popup is shown.
  let clicked = clickPageAction(extension, window);

  let contentAreaContextMenu = await openContextMenuInPopup(extension);
  await clicked;
  let item = contentAreaContextMenu.getElementsByAttribute(
    "label",
    "Click me!"
  );
  is(item.length, 1, "contextMenu item for page was found");
  await closeContextMenu(contentAreaContextMenu);

  await extension.unload();
});

add_task(async function pageaction_popup_contextmenu_hidden_items() {
  let extension = ExtensionTestUtils.loadExtension(extData);
  await extension.startup();
  await extension.awaitMessage("action-shown");

  // Deliberately not awaited: awaitExtensionPanel, called from
  // openContextMenuInPopup, has to start listening for WebExtPopupLoaded before
  // the popup is shown.
  let clicked = clickPageAction(extension, window);

  let contentAreaContextMenu = await openContextMenuInPopup(extension, "#text");
  await clicked;

  let item, state;
  for (const itemID in contextMenuItems) {
    item = contentAreaContextMenu.querySelector(`#${itemID}`);
    state = contextMenuItems[itemID];

    if (state !== "") {
      ok(item[state], `${itemID} is ${state}`);

      if (state !== "hidden") {
        ok(!item.hidden, `Disabled ${itemID} is not hidden`);
      }
    } else {
      ok(!item.hidden, `${itemID} is not hidden`);
      ok(!item.disabled, `${itemID} is not disabled`);
    }
  }

  await closeContextMenu(contentAreaContextMenu);

  await extension.unload();
});

add_task(async function pageaction_popup_image_contextmenu() {
  let extension = ExtensionTestUtils.loadExtension(extData);
  await extension.startup();
  await extension.awaitMessage("action-shown");

  // Deliberately not awaited: awaitExtensionPanel, called from
  // openContextMenuInPopup, has to start listening for WebExtPopupLoaded before
  // the popup is shown.
  let clicked = clickPageAction(extension, window);

  let contentAreaContextMenu = await openContextMenuInPopup(
    extension,
    "#testimg"
  );
  await clicked;

  let item = contentAreaContextMenu.querySelector("#context-copyimage");
  ok(!item.hidden);
  ok(!item.disabled);

  await closeContextMenu(contentAreaContextMenu);

  await extension.unload();
});

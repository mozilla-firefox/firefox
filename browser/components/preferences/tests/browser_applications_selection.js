SimpleTest.requestCompleteLog();
const { HandlerServiceTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/HandlerServiceTestUtils.sys.mjs"
);

let gHandlerService = Cc["@mozilla.org/uriloader/handler-service;1"].getService(
  Ci.nsIHandlerService
);

let gOldMailHandlers = [];
let gDummyHandlers = [];
let gOriginalPreferredMailHandler;
let gOriginalPreferredPDFHandler;

registerCleanupFunction(function () {
  function removeDummyHandlers(handlers) {
    // Remove any of the dummy handlers we created.
    for (let i = handlers.Count() - 1; i >= 0; i--) {
      try {
        if (
          gDummyHandlers.some(
            h =>
              h.uriTemplate ==
              handlers.queryElementAt(i, Ci.nsIWebHandlerApp).uriTemplate
          )
        ) {
          handlers.removeElementAt(i);
        }
      } catch (ex) {
        /* ignore non-web-app handlers */
      }
    }
  }
  // Re-add the original protocol handlers:
  let mailHandlerInfo = HandlerServiceTestUtils.getHandlerInfo("mailto");
  let mailHandlers = mailHandlerInfo.possibleApplicationHandlers;
  for (let h of gOldMailHandlers) {
    mailHandlers.appendElement(h);
  }
  removeDummyHandlers(mailHandlers);
  mailHandlerInfo.preferredApplicationHandler = gOriginalPreferredMailHandler;
  gHandlerService.store(mailHandlerInfo);

  let pdfHandlerInfo =
    HandlerServiceTestUtils.getHandlerInfo("application/pdf");
  removeDummyHandlers(pdfHandlerInfo.possibleApplicationHandlers);
  pdfHandlerInfo.preferredApplicationHandler = gOriginalPreferredPDFHandler;
  gHandlerService.store(pdfHandlerInfo);

  gBrowser.removeCurrentTab();
});

function scrubMailtoHandlers(handlerInfo) {
  // Remove extant web handlers because they have icons that
  // we fetch from the web, which isn't allowed in tests.
  let handlers = handlerInfo.possibleApplicationHandlers;
  for (let i = handlers.Count() - 1; i >= 0; i--) {
    try {
      let handler = handlers.queryElementAt(i, Ci.nsIWebHandlerApp);
      gOldMailHandlers.push(handler);
      // If we get here, this is a web handler app. Remove it:
      handlers.removeElementAt(i);
    } catch (ex) {}
  }
}

add_setup(async function () {
  // Create our dummy handlers
  let handler1 = Cc["@mozilla.org/uriloader/web-handler-app;1"].createInstance(
    Ci.nsIWebHandlerApp
  );
  handler1.name = "Handler 1";
  handler1.uriTemplate = "https://example.com/first/%s";

  let handler2 = Cc["@mozilla.org/uriloader/web-handler-app;1"].createInstance(
    Ci.nsIWebHandlerApp
  );
  handler2.name = "Handler 2";
  handler2.uriTemplate = "https://example.org/second/%s";
  gDummyHandlers.push(handler1, handler2);

  function substituteWebHandlers(handlerInfo) {
    // Append the dummy handlers to replace them:
    let handlers = handlerInfo.possibleApplicationHandlers;
    handlers.appendElement(handler1);
    handlers.appendElement(handler2);
    gHandlerService.store(handlerInfo);
  }
  // Set up our mailto handler test infrastructure.
  let mailtoHandlerInfo = HandlerServiceTestUtils.getHandlerInfo("mailto");
  scrubMailtoHandlers(mailtoHandlerInfo);
  gOriginalPreferredMailHandler = mailtoHandlerInfo.preferredApplicationHandler;
  substituteWebHandlers(mailtoHandlerInfo);

  // Now do the same for pdf handler:
  let pdfHandlerInfo =
    HandlerServiceTestUtils.getHandlerInfo("application/pdf");
  // PDF doesn't have built-in web handlers, so no need to scrub.
  gOriginalPreferredPDFHandler = pdfHandlerInfo.preferredApplicationHandler;
  substituteWebHandlers(pdfHandlerInfo);

  await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
  info("Preferences page opened on the general pane.");

  await gBrowser.selectedBrowser.contentWindow.promiseLoadHandlersList;
  info("Apps list loaded.");
});

async function selectStandardOptions(itemToUse) {
  async function selectItemInPopup(item) {
    let popupShown = BrowserTestUtils.waitForEvent(popup, "popupshown");
    // Synthesizing the mouse on the .actionsMenu menulist somehow just selects
    // the top row. Probably something to do with the multiple layers of anon
    // content - workaround by using the `.open` setter instead.
    list.open = true;
    await popupShown;
    let popupHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    if (typeof item == "function") {
      item = item();
    }
    popup.activateItem(item);
    await popupHidden;
    return item;
  }

  let itemType = itemToUse.getAttribute("type");
  // Center the item. Center rather than top so it doesn't get blocked by
  // the search header.
  itemToUse.scrollIntoView({ block: "center" });
  itemToUse.closest("richlistbox").selectItem(itemToUse);
  Assert.ok(itemToUse.selected, "Should be able to select our item.");
  // Force reflow to make sure it's visible and the container dropdown isn't
  // hidden.
  itemToUse.getBoundingClientRect().top;
  let list = itemToUse.querySelector(".actionsMenu");
  let popup = list.menupopup;

  // select one of our test cases:
  let handlerItem = list.querySelector("menuitem[data-l10n-args*='Handler 1']");
  await selectItemInPopup(handlerItem);
  let { preferredAction, alwaysAskBeforeHandling } =
    HandlerServiceTestUtils.getHandlerInfo(itemType);
  Assert.notEqual(
    preferredAction,
    Ci.nsIHandlerInfo.alwaysAsk,
    "Should have selected something other than 'always ask' (" + itemType + ")"
  );
  Assert.ok(
    !alwaysAskBeforeHandling,
    "Should have turned off asking before handling (" + itemType + ")"
  );

  // Test the alwaysAsk option
  let alwaysAskItem = list.getElementsByAttribute(
    "action",
    Ci.nsIHandlerInfo.alwaysAsk
  )[0];
  await selectItemInPopup(alwaysAskItem);
  Assert.equal(
    list.selectedItem,
    alwaysAskItem,
    "Should have selected always ask item (" + itemType + ")"
  );
  alwaysAskBeforeHandling =
    HandlerServiceTestUtils.getHandlerInfo(itemType).alwaysAskBeforeHandling;
  Assert.ok(
    alwaysAskBeforeHandling,
    "Should have turned on asking before handling (" + itemType + ")"
  );

  let useDefaultItem = list.getElementsByAttribute(
    "action",
    Ci.nsIHandlerInfo.useSystemDefault
  );
  useDefaultItem = useDefaultItem && useDefaultItem[0];
  if (useDefaultItem) {
    await selectItemInPopup(useDefaultItem);
    Assert.equal(
      list.selectedItem,
      useDefaultItem,
      "Should have selected 'use default' item (" + itemType + ")"
    );
    preferredAction =
      HandlerServiceTestUtils.getHandlerInfo(itemType).preferredAction;
    Assert.equal(
      preferredAction,
      Ci.nsIHandlerInfo.useSystemDefault,
      "Should have selected 'use default' (" + itemType + ")"
    );
  } else {
    // Whether there's a "use default" item depends on the OS, so it's not
    // possible to rely on it being the case or not.
    info("No 'Use default' item, so not testing (" + itemType + ")");
  }

  // Select a web app item.
  let webAppItems = Array.from(
    popup.getElementsByAttribute("action", Ci.nsIHandlerInfo.useHelperApp)
  );
  webAppItems = webAppItems.filter(
    item => item.handlerApp instanceof Ci.nsIWebHandlerApp
  );
  Assert.equal(
    webAppItems.length,
    2,
    "Should have 2 web application handler. (" + itemType + ")"
  );
  Assert.notEqual(
    webAppItems[0].label,
    webAppItems[1].label,
    "Should have 2 different web app handlers"
  );
  let selectedItem = await selectItemInPopup(webAppItems[0]);

  // Test that the selected item label is the same as the label
  // of the menu item.
  let win = gBrowser.selectedBrowser.contentWindow;
  await win.document.l10n.translateFragment(selectedItem);
  await win.document.l10n.translateFragment(itemToUse);
  Assert.equal(
    selectedItem.label,
    itemToUse.querySelector(".actionContainer label").value,
    "Should have selected correct item (" + itemType + ")"
  );
  let { preferredApplicationHandler } =
    HandlerServiceTestUtils.getHandlerInfo(itemType);
  preferredApplicationHandler.QueryInterface(Ci.nsIWebHandlerApp);
  Assert.equal(
    selectedItem.handlerApp.uriTemplate,
    preferredApplicationHandler.uriTemplate,
    "App should actually be selected in the backend. (" + itemType + ")"
  );

  // select the other web app item
  selectedItem = await selectItemInPopup(webAppItems[1]);

  // Test that the selected item label is the same as the label
  // of the menu item
  await win.document.l10n.translateFragment(selectedItem);
  await win.document.l10n.translateFragment(itemToUse);
  Assert.equal(
    selectedItem.label,
    itemToUse.querySelector(".actionContainer label").value,
    "Should have selected correct item (" + itemType + ")"
  );
  preferredApplicationHandler =
    HandlerServiceTestUtils.getHandlerInfo(
      itemType
    ).preferredApplicationHandler;
  preferredApplicationHandler.QueryInterface(Ci.nsIWebHandlerApp);
  Assert.equal(
    selectedItem.handlerApp.uriTemplate,
    preferredApplicationHandler.uriTemplate,
    "App should actually be selected in the backend. (" + itemType + ")"
  );
}

add_task(async function checkDropdownBehavior() {
  let win = gBrowser.selectedBrowser.contentWindow;

  let container = win.document.getElementById("handlersView");

  // First check a protocol handler item.
  let mailItem = container.querySelector("richlistitem[type='mailto']");
  Assert.ok(mailItem, "mailItem is present in handlersView.");
  await selectStandardOptions(mailItem);

  // Then check a content menu item.
  let pdfItem = container.querySelector("richlistitem[type='application/pdf']");
  Assert.ok(pdfItem, "pdfItem is present in handlersView.");
  await selectStandardOptions(pdfItem);
});

add_task(async function sortingCheck() {
  let win = gBrowser.selectedBrowser.contentWindow;
  const handlerView = win.document.getElementById("handlersView");
  const typeColumn = win.document.getElementById("typeColumn");
  Assert.ok(typeColumn, "typeColumn is present in handlersView.");

  let expectedNumberOfItems =
    handlerView.querySelectorAll("richlistitem").length;

  // Test default sorting
  assertSortByType("ascending");

  const oldDir = typeColumn.getAttribute("sortDirection");

  // click on an item and sort again:
  let itemToUse = handlerView.querySelector("richlistitem[type=mailto]");
  itemToUse.scrollIntoView({ block: "center" });
  itemToUse.closest("richlistbox").selectItem(itemToUse);

  // Test sorting on the type column
  typeColumn.click();
  assertSortByType("descending");
  Assert.notEqual(
    oldDir,
    typeColumn.getAttribute("sortDirection"),
    "Sort direction should change"
  );

  typeColumn.click();
  assertSortByType("ascending");

  const actionColumn = win.document.getElementById("actionColumn");
  Assert.ok(actionColumn, "actionColumn is present in handlersView.");

  // Test sorting on the action column
  const oldActionDir = actionColumn.getAttribute("sortDirection");
  actionColumn.click();
  assertSortByAction("ascending");
  Assert.notEqual(
    oldActionDir,
    actionColumn.getAttribute("sortDirection"),
    "Sort direction should change"
  );

  actionColumn.click();
  assertSortByAction("descending");

  // Restore the default sort order
  typeColumn.click();
  assertSortByType("ascending");

  function assertSortByAction(order) {
    Assert.equal(
      actionColumn.getAttribute("sortDirection"),
      order,
      `Sort direction should be ${order}`
    );
    let siteItems = handlerView.getElementsByTagName("richlistitem");
    Assert.equal(
      siteItems.length,
      expectedNumberOfItems,
      "Number of items should not change."
    );
    for (let i = 0; i < siteItems.length - 1; ++i) {
      let aType = (
        siteItems[i].getAttribute("actionDescription") || ""
      ).toLowerCase();
      let bType = (
        siteItems[i + 1].getAttribute("actionDescription") || ""
      ).toLowerCase();
      let result = 0;
      if (aType > bType) {
        result = 1;
      } else if (bType > aType) {
        result = -1;
      }
      if (order == "ascending") {
        Assert.lessOrEqual(
          result,
          0,
          "Should sort applications in the ascending order by action"
        );
      } else {
        Assert.greaterOrEqual(
          result,
          0,
          "Should sort applications in the descending order by action"
        );
      }
    }
  }

  function assertSortByType(order) {
    Assert.equal(
      typeColumn.getAttribute("sortDirection"),
      order,
      `Sort direction should be ${order}`
    );

    let siteItems = handlerView.getElementsByTagName("richlistitem");
    Assert.equal(
      siteItems.length,
      expectedNumberOfItems,
      "Number of items should not change."
    );
    for (let i = 0; i < siteItems.length - 1; ++i) {
      let aType = (
        siteItems[i].getAttribute("typeDescription") || ""
      ).toLowerCase();
      let bType = (
        siteItems[i + 1].getAttribute("typeDescription") || ""
      ).toLowerCase();
      let result = 0;
      if (aType > bType) {
        result = 1;
      } else if (bType > aType) {
        result = -1;
      }
      if (order == "ascending") {
        Assert.lessOrEqual(
          result,
          0,
          "Should sort applications in the ascending order by type"
        );
      } else {
        Assert.greaterOrEqual(
          result,
          0,
          "Should sort applications in the descending order by type"
        );
      }
    }
  }
});

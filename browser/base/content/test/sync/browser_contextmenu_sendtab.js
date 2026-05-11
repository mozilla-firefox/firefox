/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/base/content/test/general/head.js",
  this
);
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/customizableui/test/head.js",
  this
);

const fxaDevices = [
  {
    id: 1,
    name: "Foo",
    availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "baz" },
    lastAccessTime: Date.now(),
  },
  {
    id: 2,
    name: "Bar",
    availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "boo" },
    lastAccessTime: Date.now() + 60000, // add 30min
  },
  {
    id: 3,
    name: "Baz",
    clientRecord: "bar",
    lastAccessTime: Date.now() + 120000, // add 60min
  }, // Legacy send tab target (no availableCommands).
  { id: 4, name: "Homer" }, // Incompatible target.
];

let [testTab] = gBrowser.visibleTabs;

function updateTabContextMenu(tab = gBrowser.selectedTab) {
  let menu = document.getElementById("tabContextMenu");
  var evt = new Event("");
  tab.dispatchEvent(evt);
  // The TabContextMenu initializes its strings only on a focus or mouseover event.
  // Calls focus event on the TabContextMenu early in the test
  gBrowser.selectedTab.focus();
  menu.openPopup(tab, "end_after", 0, 0, true, false, evt);
  is(
    window.TabContextMenu.contextTab,
    tab,
    "TabContextMenu context is the expected tab"
  );
  menu.hidePopup();
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });

  await promiseSyncReady();
  await SearchService.init();
  // gSync.init() is called in a requestIdleCallback. Force its initialization.
  gSync.init();
  sinon
    .stub(Weave.Service.clientsEngine, "getClientByFxaDeviceId")
    .callsFake(fxaDeviceId => {
      let target = fxaDevices.find(c => c.id == fxaDeviceId);
      return target ? target.clientRecord : null;
    });
  sinon.stub(Weave.Service.clientsEngine, "getClientType").returns("desktop");
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:mozilla");
  registerCleanupFunction(() => {
    gBrowser.removeCurrentTab();
  });
  is(gBrowser.visibleTabs.length, 2, "there are two visible tabs");
});

add_task(async function test_sendTabToDevice_callsFlushLogFile() {
  const sandbox = setupSendTabMocks({ fxaDevices });
  updateTabContextMenu(testTab);
  await openTabContextMenu("context_sendTabToDevice");
  let promiseObserved = promiseObserver("service:log-manager:flush-log-file");

  await activateMenuItem();
  await promiseObserved;
  ok(true, "Got flush-log-file observer message");

  await closeConfirmationHint();
  sandbox.restore();
});

async function checkForConfirmationHint(targetId) {
  const sandbox = setupSendTabMocks({ fxaDevices });
  updateTabContextMenu(testTab);

  await openTabContextMenu("context_sendTabToDevice");
  await activateMenuItem();
  is(
    ConfirmationHint._panel.anchorNode.id,
    targetId,
    `Hint anchored to ${targetId}`
  );
  await closeConfirmationHint();
  sandbox.restore();
}

add_task(async function test_sendTabToDevice_showsConfirmationHint_fxa() {
  // We need to change the fxastatus from "not_configured" to show the FxA button.
  is(
    document.documentElement.getAttribute("fxastatus"),
    "not_configured",
    "FxA button is hidden"
  );
  document.documentElement.setAttribute("fxastatus", "foo");
  await checkForConfirmationHint("fxa-toolbar-menu-button");
  document.documentElement.setAttribute("fxastatus", "not_configured");
});

add_task(
  async function test_sendTabToDevice_showsConfirmationHint_onOverflowMenu() {
    // We need to change the fxastatus from "not_configured" to show the FxA button.
    is(
      document.documentElement.getAttribute("fxastatus"),
      "not_configured",
      "FxA button is hidden"
    );
    document.documentElement.setAttribute("fxastatus", "foo");

    let navbar = document.getElementById("nav-bar");

    // Resize the window so that the account button is in the overflow menu.
    // As of bug 1960002, overflowing the navbar also requires adding extra
    // buttons.
    let originalWidth = ensureToolbarOverflow(window, false);

    await TestUtils.waitForCondition(() => navbar.hasAttribute("overflowing"));

    await checkForConfirmationHint("PanelUI-menu-button");
    document.documentElement.setAttribute("fxastatus", "not_configured");

    unensureToolbarOverflow(window, originalWidth);
    await TestUtils.waitForCondition(() => !navbar.hasAttribute("overflowing"));
    CustomizableUI.reset();
  }
);

add_task(async function test_sendTabToDevice_showsConfirmationHint_appMenu() {
  ensureToolbarOverflow(window);

  // If fxastatus is "not_configured" then the FxA button is hidden, and we
  // should use the appMenu.
  is(
    document.documentElement.getAttribute("fxastatus"),
    "not_configured",
    "FxA button is hidden"
  );
  await checkForConfirmationHint("PanelUI-menu-button");
});

add_task(async function test_tab_contextmenu() {
  const sandbox = setupSendTabMocks({ fxaDevices });
  let expectation = sandbox
    .mock(gSync)
    .expects("sendTabToDevice")
    .once()
    .withExactArgs(
      {
        url: "about:mozilla",
        title: "The Book of Mozilla, 6:27",
        private: false,
      },
      [fxaDevices[1]]
    )
    .returns(true);

  updateTabContextMenu(testTab);
  await openTabContextMenu("context_sendTabToDevice");
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    false,
    "Send tab to device is shown"
  );
  is(
    document.getElementById("context_sendTabToDevice").label,
    "Send to Device",
    "Send tab to device label changed"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    false,
    "Send tab to device separator is shown"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    false,
    "Send tab to device is enabled"
  );

  await activateMenuItem();
  await closeConfirmationHint();

  expectation.verify();
  sandbox.restore();
});

add_task(async function test_tab_contextmenu_send_to_mobile() {
  let mobileFxaDevices = [
    {
      id: 1,
      name: "Foo",
      type: "mobile",
      availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "baz" },
      lastAccessTime: Date.now(),
    },
    {
      id: 2,
      name: "Bar",
      type: "tablet",
      availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "boo" },
      lastAccessTime: Date.now() + 60000, // add 30min
    },
  ];

  const sandbox = setupSendTabMocks({ fxaDevices: mobileFxaDevices });
  let expectation = sandbox
    .mock(gSync)
    .expects("sendTabToDevice")
    .once()
    .withExactArgs(
      {
        url: "about:mozilla",
        title: "The Book of Mozilla, 6:27",
        private: false,
      },
      [mobileFxaDevices[1]]
    )
    .returns(true);

  updateTabContextMenu(testTab);
  await openTabContextMenu("context_sendTabToDevice");
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    false,
    "Send tab to mobile is shown"
  );
  is(
    document.getElementById("context_sendTabToDevice").label,
    "Send to Mobile",
    "Send tab to mobile label changed"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    false,
    "Send tab to device separator is shown"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    false,
    "Send tab to mobile is enabled"
  );

  await activateMenuItem();
  await closeConfirmationHint();

  expectation.verify();
  sandbox.restore();
});

add_task(async function test_tab_contextmenu_unconfigured() {
  const sandbox = setupSendTabMocks({ state: UIState.STATUS_NOT_CONFIGURED });

  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    false,
    "Send tab to device should not be hidden"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    false,
    "Send tab to device separator should not be hidden"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    false,
    "Send tab to device is enabled"
  );

  sandbox.restore();
});

add_task(async function test_tab_contextmenu_not_sendable() {
  const sandbox = setupSendTabMocks({ fxaDevices, isSendableURI: false });

  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    true,
    "Send tab to device is hidden"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    true,
    "Send tab to device is hidden"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    true,
    "Send tab to device is disabled"
  );

  sandbox.restore();
});

add_task(async function test_tab_contextmenu_not_synced_yet() {
  const sandbox = setupSendTabMocks({ fxaDevices: null });

  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    true,
    "Send tab to device is hidden"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    true,
    "Send tab to device separator is hidden"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    true,
    "Send tab to device is disabled"
  );

  sandbox.restore();
});

add_task(async function test_tab_contextmenu_sync_not_ready_configured() {
  const sandbox = setupSendTabMocks({ syncReady: false });

  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    true,
    "Send tab to device is hidden"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    true,
    "Send tab to device is hidden"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    true,
    "Send tab to device is disabled"
  );

  sandbox.restore();
});

add_task(async function test_tab_contextmenu_sync_not_ready_other_state() {
  const sandbox = setupSendTabMocks({
    syncReady: false,
    state: UIState.STATUS_NOT_VERIFIED,
  });

  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    true,
    "Send tab to device is hidden"
  );
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    true,
    "Send tab to device separator is hidden"
  );
  is(
    document.getElementById("context_sendTabToDevice").disabled,
    false,
    "Send tab to device is enabled"
  );

  sandbox.restore();
});

add_task(async function test_tab_contextmenu_fxa_disabled() {
  const getter = sinon.stub(gSync, "FXA_ENABLED").get(() => false);
  // Simulate onFxaDisabled() being called on window open.
  gSync.onFxaDisabled();

  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDevice").hidden,
    true,
    "Send tab to device is hidden"
  );
  updateTabContextMenu(testTab);
  is(
    document.getElementById("context_sendTabToDeviceSeparator").hidden,
    true,
    "Send tab to device separator is hidden"
  );

  getter.restore();
  [...document.querySelectorAll(".sync-ui-item")].forEach(
    e => (e.hidden = false)
  );
});

add_task(async function teardown() {
  Weave.Service.clientsEngine.getClientByFxaDeviceId.restore();
  Weave.Service.clientsEngine.getClientType.restore();
});

async function openTabContextMenu(openSubmenuId = null) {
  const contextMenu = document.getElementById("tabContextMenu");
  is(contextMenu.state, "closed", "checking if popup is closed");

  const awaitPopupShown = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(gBrowser.selectedTab, {
    type: "contextmenu",
    button: 2,
  });
  await awaitPopupShown;

  if (openSubmenuId) {
    const menuPopup = document.getElementById(openSubmenuId).menupopup;
    const menuPopupPromise = BrowserTestUtils.waitForEvent(
      menuPopup,
      "popupshown"
    );
    menuPopup.openPopup();
    await menuPopupPromise;
  }
}

function promiseObserver(topic) {
  return new Promise(resolve => {
    let obs = (aSubject, aTopic) => {
      Services.obs.removeObserver(obs, aTopic);
      resolve(aSubject);
    };
    Services.obs.addObserver(obs, topic);
  });
}

function waitForConfirmationHint() {
  return BrowserTestUtils.waitForEvent(ConfirmationHint._panel, "popuphidden");
}

async function activateMenuItem() {
  let popupHidden = BrowserTestUtils.waitForEvent(
    document.getElementById("tabContextMenu"),
    "popuphidden"
  );
  let hintShown = BrowserTestUtils.waitForEvent(
    ConfirmationHint._panel,
    "popupshown"
  );
  let menuitem = document
    .getElementById("context_sendTabToDevicePopupMenu")
    .querySelector("menuitem");
  menuitem.closest("menupopup").activateItem(menuitem);
  await popupHidden;
  await hintShown;
}

async function closeConfirmationHint() {
  let hintHidden = BrowserTestUtils.waitForEvent(
    ConfirmationHint._panel,
    "popuphidden"
  );
  ConfirmationHint._panel.hidePopup();
  await hintHidden;
}

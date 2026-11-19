ChromeUtils.defineESModuleGetters(this, {
  MockRegistrar: "resource://testing-common/MockRegistrar.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  WindowLock: "moz-src:///browser/modules/WindowLock.sys.mjs",
});

/**
 * An utility function to write some text in the search input box
 * in a content page.
 *
 * @param {object} browser
 *        The browser that contains the content.
 * @param {string} text
 *        The string to write in the search field.
 * @param {string} fieldName
 *        The name of the field to write to.
 */
let typeInSearchField = async function (browser, text, fieldName) {
  await SpecialPowers.spawn(
    browser,
    [fieldName, text],
    async function (contentFieldName, contentText) {
      // Put the focus on the search box.
      let searchInput = content.document.getElementById(contentFieldName);
      searchInput.focus();
      searchInput.value = contentText;
    }
  );
};

/**
 * Given a <xul:browser> at some non-internal web page,
 * return something that resembles an nsIContentPermissionRequest,
 * using the browsers currently loaded document to get a principal.
 *
 * @param browser (<xul:browser>)
 *        The browser that we'll create a nsIContentPermissionRequest
 *        for.
 * @returns A nsIContentPermissionRequest-ish object.
 */
function makeMockPermissionRequest(browser) {
  let type = {
    options: Cc["@mozilla.org/array;1"].createInstance(Ci.nsIArray),
    QueryInterface: ChromeUtils.generateQI(["nsIContentPermissionType"]),
  };
  let types = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
  types.appendElement(type);
  let principal = browser.contentPrincipal;
  let result = {
    types,
    isHandlingUserInput: false,
    principal,
    topLevelPrincipal: principal,
    requester: null,
    _cancelled: false,
    cancel() {
      this._cancelled = true;
    },
    _allowed: false,
    allow() {
      this._allowed = true;
    },
    getDelegatePrincipal() {
      return principal;
    },
    QueryInterface: ChromeUtils.generateQI(["nsIContentPermissionRequest"]),
  };

  // In the e10s-case, nsIContentPermissionRequest will have
  // element defined. window is defined otherwise.
  if (browser.isRemoteBrowser) {
    result.element = browser;
  } else {
    result.window = browser.contentWindow;
  }

  return result;
}

/**
 * For an opened PopupNotification, clicks on the main action,
 * and waits for the panel to fully close.
 *
 * @return {Promise}
 *         Resolves once the panel has fired the "popuphidden"
 *         event.
 */
async function clickMainAction() {
  let removePromise = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popuphidden"
  );
  let popupNotification = getPopupNotificationNode();
  await popupNotification.button.updateComplete;
  popupNotification.button.click();
  return removePromise;
}

/**
 * For an opened PopupNotification, clicks on the secondary action,
 * and waits for the panel to fully close.
 *
 * @param actionIndex (Number)
 *        The index of the secondary action to be clicked. The default
 *        secondary action (the button shown directly in the panel) is
 *        treated as having index 0.
 *
 * @return {Promise}
 *         Resolves once the panel has fired the "popuphidden"
 *         event.
 */
function clickSecondaryAction(actionIndex) {
  let removePromise = BrowserTestUtils.waitForEvent(
    PopupNotifications.panel,
    "popuphidden"
  );
  let popupNotification = getPopupNotificationNode();
  if (!actionIndex) {
    return (async function () {
      await popupNotification.secondaryButton.updateComplete;
      popupNotification.secondaryButton.click();
      await removePromise;
    })();
  }

  return (async function () {
    // Click the dropmarker arrow and wait for the menu to show up.
    let dropdownPromise = BrowserTestUtils.waitForEvent(
      popupNotification.menupopup,
      "popupshown"
    );
    await popupNotification.secondaryButton.updateComplete;
    await EventUtils.synthesizeMouseAtCenter(
      popupNotification.secondaryButton.chevronButtonEl,
      {}
    );
    await dropdownPromise;

    // The menuitems in the dropdown are accessible as direct children of the panel,
    // because they are injected into a <children> node in the XBL binding.
    // The target action is the menuitem at index actionIndex - 1, because the first
    // secondary action (index 0) is the button shown directly in the panel.
    let actionMenuItem =
      popupNotification.querySelectorAll("menuitem")[actionIndex - 1];
    if (popupNotification.menupopup.isNativeMenu) {
      popupNotification.menupopup.activateItem(actionMenuItem);
    } else {
      EventUtils.synthesizeMouseAtCenter(actionMenuItem, {});
    }
    await removePromise;
  })();
}

/**
 * Makes sure that 1 (and only 1) <xul:popupnotification> is being displayed
 * by PopupNotification, and then returns that <xul:popupnotification>.
 *
 * @return {<xul:popupnotification>}
 */
function getPopupNotificationNode() {
  // PopupNotification is a bit overloaded here, so to be
  // clear, popupNotifications is a list of <xul:popupnotification>
  // nodes.
  let popupNotifications = PopupNotifications.panel.childNodes;
  Assert.equal(
    popupNotifications.length,
    1,
    "Should be showing a <xul:popupnotification>"
  );
  return popupNotifications[0];
}

function assertActivatedPageActionPanelHidden() {
  Assert.ok(
    !document.getElementById(BrowserPageActions._activatedActionPanelID)
  );
}

function promiseOpenPageActionPanel() {
  let dwu = window.windowUtils;
  return TestUtils.waitForCondition(() => {
    // Wait for the main page action button to become visible.  It's hidden for
    // some URIs, so depending on when this is called, it may not yet be quite
    // visible.  It's up to the caller to make sure it will be visible.
    info("Waiting for main page action button to have non-0 size");
    let bounds = dwu.getBoundsWithoutFlushing(
      BrowserPageActions.mainButtonNode
    );
    return bounds.width > 0 && bounds.height > 0;
  })
    .then(() => {
      // Wait for the panel to become open, by clicking the button if necessary.
      info("Waiting for main page action panel to be open");
      if (BrowserPageActions.panelNode.state == "open") {
        return Promise.resolve();
      }
      let shownPromise = promisePageActionPanelShown();
      EventUtils.synthesizeMouseAtCenter(BrowserPageActions.mainButtonNode, {});
      return shownPromise;
    })
    .then(() => {
      // Wait for items in the panel to become visible.
      return promisePageActionViewChildrenVisible(
        BrowserPageActions.mainViewNode
      );
    });
}

function promisePageActionPanelShown() {
  return promisePanelShown(BrowserPageActions.panelNode);
}

function promisePageActionPanelHidden() {
  return promisePanelHidden(BrowserPageActions.panelNode);
}

function promisePanelShown(panelIDOrNode) {
  return promisePanelEvent(panelIDOrNode, "popupshown");
}

function promisePanelHidden(panelIDOrNode) {
  return promisePanelEvent(panelIDOrNode, "popuphidden");
}

function promisePanelEvent(panelIDOrNode, eventType) {
  return new Promise(resolve => {
    let panel = panelIDOrNode;
    if (typeof panel == "string") {
      panel = document.getElementById(panelIDOrNode);
      if (!panel) {
        throw new Error(`Panel with ID "${panelIDOrNode}" does not exist.`);
      }
    }
    if (
      (eventType == "popupshown" && panel.state == "open") ||
      (eventType == "popuphidden" && panel.state == "closed")
    ) {
      executeSoon(resolve);
      return;
    }
    panel.addEventListener(
      eventType,
      () => {
        executeSoon(resolve);
      },
      { once: true }
    );
  });
}

function promisePageActionViewShown() {
  info("promisePageActionViewShown waiting for ViewShown");
  return BrowserTestUtils.waitForEvent(
    BrowserPageActions.panelNode,
    "ViewShown"
  ).then(async event => {
    let panelViewNode = event.originalTarget;
    await promisePageActionViewChildrenVisible(panelViewNode);
    return panelViewNode;
  });
}

async function promisePageActionViewChildrenVisible(panelViewNode) {
  info(
    "promisePageActionViewChildrenVisible waiting for a child node to be visible"
  );
  await new Promise(requestAnimationFrame);
  let dwu = window.windowUtils;
  return TestUtils.waitForCondition(() => {
    let bodyNode = panelViewNode.firstElementChild;
    for (let childNode of bodyNode.children) {
      let bounds = dwu.getBoundsWithoutFlushing(childNode);
      if (bounds.width > 0 && bounds.height > 0) {
        return true;
      }
    }
    return false;
  });
}

async function initPageActionsTest() {
  // Make the main button visible. It's not unless the window is narrow. This
  // test isn't concerned with that behavior. We have other tests for that.
  BrowserPageActions.mainButtonNode.style.display = "flex";
  registerCleanupFunction(() => {
    BrowserPageActions.mainButtonNode.style.removeProperty("display");
  });
}

const WINDOW_LOCK_PREF = "browser.privatebrowsing.lockOnScreenLock.enabled";
const WINDOW_LOCK_FEATURE_GATE_PREF =
  "browser.privatebrowsing.lockOnScreenLock.featureGate";

function getLockDialog(win) {
  return win.document.getElementById("window-lock-dialog");
}

function isWindowLocked(win) {
  return WindowLock.isWindowLocked(win);
}

function authErrorShown(win) {
  return !getLockDialog(win).querySelector(".window-lock-error").hidden;
}

function commandsDisabled(win) {
  return win.document
    .getElementById("cmd_newNavigatorTab")
    .hasAttribute("disabled");
}

// Outcomes of the mocked OS reauthentication prompt.
const ReauthResult = Object.freeze({
  SUCCESS: "success",
  CANCELED: "canceled",
  ERROR: "error",
});
let gReauthResult = ReauthResult.CANCELED;
let gOnReauth = null;

const mockReauthenticator = {
  QueryInterface: ChromeUtils.generateQI(["nsIOSReauthenticator"]),
  async asyncReauthenticateUser() {
    gOnReauth?.();
    if (gReauthResult == ReauthResult.ERROR) {
      throw new Components.Exception(
        "Simulated OS reauthentication error",
        Cr.NS_ERROR_FAILURE
      );
    }
    return [gReauthResult == ReauthResult.SUCCESS ? 1 : 0, 0];
  },
};

async function unlockWith(win, result) {
  gReauthResult = result;
  let reauthPromise = new Promise(resolve => (gOnReauth = resolve));
  getLockDialog(win).querySelector(".window-lock-button").click();
  await reauthPromise;
  gOnReauth = null;
  await TestUtils.waitForTick();
}

async function setLockPrivateWindows(enabled) {
  await SpecialPowers.pushPrefEnv({ set: [[WINDOW_LOCK_PREF, enabled]] });
}

/**
 * Replaces the OS reauthentication prompt with mockReauthenticator for the
 * duration of the test.
 */
function registerMockReauthenticator() {
  let cid = MockRegistrar.register(
    "@mozilla.org/security/osreauthenticator;1",
    mockReauthenticator
  );
  registerCleanupFunction(() => {
    MockRegistrar.unregister(cid);
  });
}

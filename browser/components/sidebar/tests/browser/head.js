/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const imageBuffer = imageBufferFromDataURI(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQImWNgYGBgAAAABQABh6FO1AAAAABJRU5ErkJggg=="
);

function imageBufferFromDataURI(encodedImageData) {
  const decodedImageData = atob(encodedImageData);
  return Uint8Array.from(decodedImageData, byte => byte.charCodeAt(0)).buffer;
}

const SIDEBAR_VISIBILITY_PREF = "sidebar.visibility";
const POSITION_SETTING_PREF = "sidebar.position_start";
const VERTICAL_TABS_PREF = "sidebar.verticalTabs";
const kPrefCustomizationState = "browser.uiCustomization.state";
const kPrefCustomizationHorizontalTabstrip =
  "browser.uiCustomization.horizontalTabstrip";
const kPrefCustomizationNavBarWhenVerticalTabs =
  "browser.uiCustomization.navBarWhenVerticalTabs";

const MODIFIED_PREFS = Object.freeze([
  kPrefCustomizationState,
  kPrefCustomizationHorizontalTabstrip,
  kPrefCustomizationNavBarWhenVerticalTabs,
]);

// Ensure we clear any previous pref values
for (const pref of MODIFIED_PREFS) {
  Services.prefs.clearUserPref(pref);
}

/* global browser */
const extData = {
  manifest: {
    sidebar_action: {
      default_icon: {
        16: "icon.png",
        32: "icon@2x.png",
      },
      default_panel: "default.html",
      default_title: "Default Title",
    },
  },
  useAddonManager: "temporary",

  files: {
    "default.html": `
          <!DOCTYPE html>
          <html>
          <head><meta charset="utf-8"/>
          <script src="sidebar.js"></script>
          </head>
          <body>
          A Test Sidebar
          </body></html>
        `,
    "sidebar.js": function () {
      window.onload = () => {
        browser.test.sendMessage("sidebar");
      };
    },
    "1.html": `
          <!DOCTYPE html>
          <html>
          <head><meta charset="utf-8"/></head>
          <body>
          A Test Sidebar
          </body></html>
        `,
    "icon.png": imageBuffer,
    "icon@2x.png": imageBuffer,
    "updated-icon.png": imageBuffer,
  },

  background() {
    browser.test.onMessage.addListener(async ({ msg, data }) => {
      switch (msg) {
        case "set-icon":
          await browser.sidebarAction.setIcon({ path: data });
          break;
        case "set-panel":
          await browser.sidebarAction.setPanel({ panel: data });
          break;
        case "set-title":
          await browser.sidebarAction.setTitle({ title: data });
          break;
      }
      browser.test.sendMessage("done");
    });
  },
};

// Ensure each test leaves the sidebar in its initial state when it completes
const initialSidebarState = { ...SidebarController.getUIState(), command: "" };
async function resetSidebarToInitialState() {
  info(
    `Restoring sidebar state from: ${JSON.stringify(SidebarController.getUIState())}, back to: ${JSON.stringify(initialSidebarState)}`
  );
  await SidebarController.initializeUIState(initialSidebarState);
}
registerCleanupFunction(async () => {
  await resetSidebarToInitialState();
  // Reset the Glean events after each test.
  Services.fog.testResetFOG();
});

function waitForBrowserWindowActive(win) {
  // eslint-disable-next-line consistent-return
  return new Promise(resolve => {
    if (Services.focus.activeWindow == win) {
      resolve();
    } else {
      return BrowserTestUtils.waitForEvent(win, "activate");
    }
  });
}

function openAndWaitForContextMenu(popup, button, onShown) {
  return new Promise(resolve => {
    function onPopupShown() {
      info("onPopupShown");
      popup.removeEventListener("popupshown", onPopupShown);

      onShown && onShown();
      resolve(popup);
    }

    popup.addEventListener("popupshown", onPopupShown);

    info("wait for the context menu to open");

    button.scrollIntoView();
    const eventDetails = { type: "contextmenu", button: 2 };
    EventUtils.synthesizeMouseAtCenter(
      button,
      eventDetails,
      // eslint-disable-next-line mozilla/use-ownerGlobal
      button.ownerDocument.defaultView
    );
  });
}

function isActiveElement(el) {
  return el.getRootNode().activeElement == el;
}

async function toggleSidebarPanel(win, commandID) {
  const promiseFocused = BrowserTestUtils.waitForEvent(win, "SidebarFocused");
  win.SidebarController.toggle(commandID);
  await promiseFocused;
}

async function waitForTabstripOrientation(
  toOrientation = "vertical",
  win = window
) {
  await win.SidebarController.promiseInitialized;
  // We use the orient attribute on the tabstrip element as a reliable signal that
  // tabstrip orientation has changed/is settled into the given orientation
  info(
    `waitForTabstripOrientation: waiting for orient attribute to be "${toOrientation}"`
  );
  await BrowserTestUtils.waitForMutationCondition(
    win.gBrowser.tabContainer,
    { attributes: true, attributeFilter: ["orient"] },
    () => win.gBrowser.tabContainer.getAttribute("orient") == toOrientation
  );
  // This change is followed by a update/render step for the lit elements.
  // We need to wait for that too
  await win.SidebarController.sidebarMain?.updateComplete;
}

/**
 * Wait until Style and Layout information have been calculated and the paint
 * has occurred.
 *
 * @see https://firefox-source-docs.mozilla.org/performance/bestpractices.html
 */
async function waitForRepaint() {
  await SidebarController.waitUntilStable();
  return new Promise(resolve =>
    requestAnimationFrame(() => {
      Services.tm.dispatchToMainThread(resolve);
    })
  );
}

function cleanUpExtraTabs() {
  while (window.gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(window.gBrowser.tabs.at(-1));
  }
}

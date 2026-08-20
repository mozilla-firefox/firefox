/**
 * Given some nsIDOMWindow for a window running in the parent
 * process, return the nsIWebBrowserChrome chrome flags for
 * the associated XUL window.
 *
 * @param win (nsIDOMWindow)
 *        Some window in the parent process.
 * @returns int
 */
function getParentChromeFlags(win) {
  return win.docShell.treeOwner
    .QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIAppWindow).chromeFlags;
}

const WINDOW_OPEN_FEATURES_PATTERNS = [
  { features: "", popup: false },

  // If features isn't empty, the following should be true to open tab/window:
  //   * location or toolbar (defaults to false)
  //   * menubar (defaults to false)
  //   * resizable (defaults to true)
  //   * scrollbars (defaults to false)
  //   * status (defaults to false)
  { features: "location,menubar,resizable,scrollbars,status", popup: false },
  { features: "toolbar,menubar,resizable,scrollbars,status", popup: false },

  // resizable defaults to true.
  { features: "location,menubar,scrollbars,status", popup: false },

  // The following testcases use "location,menubar,scrollbars,status"
  // as the base non-popup case, and test the boundary between popup
  // vs non-popup.

  // If either location or toolbar is true, not popup.
  {
    features: "toolbar,menubar,resizable,scrollbars,status",
    popup: false,
  },
  {
    features: "location,menubar,resizable,scrollbars,status",
    popup: false,
  },

  // If both location and toolbar are false, popup.
  { features: "menubar,scrollbars,status", popup: true },

  // If menubar is false, popup.
  { features: "location,resizable,scrollbars,status", popup: true },

  // If resizable is true, not popup.
  {
    features: "location,menubar,resizable=yes,scrollbars,status",
    popup: false,
  },

  // If resizable is false, popup.
  { features: "location,menubar,resizable=0,scrollbars,status", popup: true },

  // If scrollbars is false, popup.
  { features: "location,menubar,resizable,status", popup: true },

  // If status is false, popup.
  { features: "location,menubar,resizable,scrollbars", popup: true },

  // position and size have no effect.
  {
    features:
      "location,menubar,scrollbars,status," +
      "left=100,screenX=100,top=100,screenY=100," +
      "width=100,innerWidth=100,outerWidth=100," +
      "height=100,innerHeight=100,outerHeight=100",
    popup: false,
  },

  // Most feature defaults to false if the feature is not empty.
  // Specifying only some of them opens a popup.
  { features: "location,toolbar,menubar", popup: true },
  { features: "resizable,scrollbars,status", popup: true },

  // Specifying unknown feature makes the feature not empty.
  { features: "someunknownfeature", popup: true },

  // noopener and noreferrer are removed before testing if feature is empty.
  { features: "noopener,noreferrer", popup: false },
];

const WINDOW_CHROME_FLAGS = {
  CHROME_WINDOW_RESIZE: true,
  CHROME_SCROLLBARS: true,
  CHROME_TITLEBAR: true,

  CHROME_MENUBAR: true,
  CHROME_TOOLBAR: true,
  CHROME_PERSONAL_TOOLBAR: true,
};

const POPUP_CHROME_FLAGS = {
  CHROME_WINDOW_RESIZE: true,
  CHROME_SCROLLBARS: true,
  CHROME_TITLEBAR: true,

  CHROME_MENUBAR: false,
  CHROME_TOOLBAR: false,
  CHROME_PERSONAL_TOOLBAR: false,
};

async function testPopupPatterns(nonPopup) {
  const TEST_PATH = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  );

  for (const { features, popup } of WINDOW_OPEN_FEATURES_PATTERNS) {
    const BLANK_PAGE = "data:text/html,";
    const OPEN_PAGE = TEST_PATH + "file_open_page.html";
    const SCRIPT_PAGE = `data:text/html,<script>window.open("${OPEN_PAGE}", "", "${features}");</script>`;

    async function testNewWindow(flags) {
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: BLANK_PAGE,
        },
        async function () {
          const newWinPromise = BrowserTestUtils.waitForNewWindow();
          BrowserTestUtils.startLoadingURIString(gBrowser, SCRIPT_PAGE);

          const win = await newWinPromise;
          const parentChromeFlags = getParentChromeFlags(win);

          for (const [name, visible] of Object.entries(flags)) {
            if (visible) {
              Assert.equal(
                !!(parentChromeFlags & Ci.nsIWebBrowserChrome[name]),
                true,
                `${name} should be present for features "${features}"`
              );
            } else {
              Assert.equal(
                !!(parentChromeFlags & Ci.nsIWebBrowserChrome[name]),
                false,
                `${name} should not be present for features "${features}"`
              );
            }
          }

          await BrowserTestUtils.closeWindow(win);
        }
      );
    }

    async function testNewTab() {
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: BLANK_PAGE,
        },
        async function () {
          const newTabPromise = BrowserTestUtils.waitForNewTab(
            gBrowser,
            OPEN_PAGE
          );
          BrowserTestUtils.startLoadingURIString(gBrowser, SCRIPT_PAGE);

          let tab = await newTabPromise;
          BrowserTestUtils.removeTab(tab);
        }
      );
    }

    async function testCurrentTab() {
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url: BLANK_PAGE,
        },
        async function (browser) {
          const pagePromise = BrowserTestUtils.browserLoaded(
            browser,
            false,
            OPEN_PAGE
          );
          BrowserTestUtils.startLoadingURIString(gBrowser, SCRIPT_PAGE);

          await pagePromise;
        }
      );
    }

    if (!popup) {
      if (nonPopup == "window") {
        await testNewWindow(WINDOW_CHROME_FLAGS);
      } else if (nonPopup == "tab") {
        await testNewTab();
      } else {
        // current tab
        await testCurrentTab();
      }
    } else {
      await testNewWindow(POPUP_CHROME_FLAGS);
    }
  }
}

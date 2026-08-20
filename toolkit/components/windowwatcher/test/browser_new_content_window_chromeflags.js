/**
 * Tests that chromeFlags are set properly on windows that are
 * being opened from content.
 */

// The following are not allowed from web content.
//
// <feature string>: {
//   flag: <associated nsIWebBrowserChrome flag>,
//   defaults_to: <what this feature defaults to normally>
// }
const DISALLOWED = {
  chrome: {
    flag: Ci.nsIWebBrowserChrome.CHROME_OPENAS_CHROME,
    defaults_to: false,
  },
  dialog: {
    flag: Ci.nsIWebBrowserChrome.CHROME_OPENAS_DIALOG,
    defaults_to: false,
  },
  private: {
    flag: Ci.nsIWebBrowserChrome.CHROME_PRIVATE_WINDOW,
    defaults_to: false,
  },
  "non-private": {
    flag: Ci.nsIWebBrowserChrome.CHROME_NON_PRIVATE_WINDOW,
    defaults_to: false,
  },
  // "all":
  //   checked manually, since this is an aggregate
  //   flag.
  //
  // "remote":
  //   checked manually, since its default value will
  //   depend on whether or not e10s is enabled by default.
  alwaysOnTop: {
    flag: Ci.nsIWebBrowserChrome.CHROME_ALWAYS_ON_TOP,
    defaults_to: false,
  },
  suppressanimation: {
    flag: Ci.nsIWebBrowserChrome.CHROME_SUPPRESS_ANIMATION,
    defaults_to: false,
  },
  suppressinitialfullscreen: {
    flag: Ci.nsIWebBrowserChrome.CHROME_SUPPRESS_INITIAL_FULLSCREEN,
    defaults_to: false,
  },
  extrachrome: {
    flag: Ci.nsIWebBrowserChrome.CHROME_EXTRA,
    defaults_to: false,
  },
  centerscreen: {
    flag: Ci.nsIWebBrowserChrome.CHROME_CENTER_SCREEN,
    defaults_to: false,
  },
  dependent: {
    flag: Ci.nsIWebBrowserChrome.CHROME_DEPENDENT,
    defaults_to: false,
  },
  modal: {
    flag: Ci.nsIWebBrowserChrome.CHROME_MODAL,
    defaults_to: false,
  },
  titlebar: {
    flag: Ci.nsIWebBrowserChrome.CHROME_TITLEBAR,
    defaults_to: true,
  },
  resizable: {
    flag: Ci.nsIWebBrowserChrome.CHROME_WINDOW_RESIZE,
    defaults_to: true,
  },
};

// This magic value of 2 means that by default, when content tries
// to open a new window, it'll actually open in a new window instead
// of a new tab.
Services.prefs.setIntPref("browser.link.open_newwindow", 2);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.link.open_newwindow");
});

/**
 * Given some nsIDOMWindow for a window running in the parent process,
 * asynchronously return the nsIWebBrowserChrome chrome flags for the
 * associated content window.
 *
 * @param win (nsIDOMWindow)
 * @returns int
 */
function getContentChromeFlags(win) {
  let b = win.gBrowser.selectedBrowser;
  return SpecialPowers.spawn(b, [], async function () {
    // Content scripts provide docShell as a global.
    /* global docShell */
    docShell.QueryInterface(Ci.nsIInterfaceRequestor);
    try {
      // This will throw if we're not a remote browser.
      return docShell
        .getInterface(Ci.nsIBrowserChild)
        .QueryInterface(Ci.nsIWebBrowserChrome).chromeFlags;
    } catch (e) {
      // This must be a non-remote browser...
      return docShell.treeOwner.QueryInterface(Ci.nsIWebBrowserChrome)
        .chromeFlags;
    }
  });
}

/**
 * For some chromeFlags, ensures that flags in the DISALLOWED
 * group were not modified.
 *
 * @param chromeFlags (int)
 *        Some chromeFlags to check.
 */
function assertContentFlags(chromeFlags) {
  for (let feature in DISALLOWED) {
    let flag = DISALLOWED[feature].flag;
    Assert.ok(flag, "Expected flag to be a non-zeroish value");
    if (DISALLOWED[feature].defaults_to) {
      // The feature is supposed to default to true, so it should
      // stay true.
      Assert.ok(
        chromeFlags & flag,
        `Expected feature ${feature} to be unchanged`
      );
    } else {
      // The feature is supposed to default to false, so it should
      // stay false.
      Assert.ok(
        !(chromeFlags & flag),
        `Expected feature ${feature} to be unchanged`
      );
    }
  }
}

/**
 * Opens a window from content using window.open with the
 * features computed from DISALLOWED. The computed feature string attempts to
 * flip every feature away from their default.
 */
add_task(async function test_disallowed_flags() {
  // Construct a features string that flips all DISALLOWED features
  // to not be their defaults.
  const DISALLOWED_STRING = Object.keys(DISALLOWED)
    .map(feature => {
      let toValue = DISALLOWED[feature].defaults_to ? "no" : "yes";
      return `${feature}=${toValue}`;
    })
    .join(",");

  const FEATURES = [DISALLOWED_STRING].join(",");

  const SCRIPT_PAGE = `data:text/html,<script>window.open("about:blank", "_blank", "${FEATURES}");</script>`;
  const SCRIPT_PAGE_FOR_CHROME_ALL = `data:text/html,<script>window.open("about:blank", "_blank", "all");</script>`;

  let newWinPromise = BrowserTestUtils.waitForNewWindow();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: SCRIPT_PAGE,
    },
    async function () {
      let win = await newWinPromise;
      let parentChromeFlags = getParentChromeFlags(win);
      assertContentFlags(parentChromeFlags);

      if (win.gMultiProcessBrowser) {
        Assert.ok(
          parentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_REMOTE_WINDOW,
          "Should be remote by default"
        );
      } else {
        Assert.ok(
          !(parentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_REMOTE_WINDOW),
          "Should not be remote by default"
        );
      }

      // Confusingly, chromeFlags also exist in the content process
      // as part of the BrowserChild, so we have to check those too.
      let contentChromeFlags = await getContentChromeFlags(win);
      assertContentFlags(contentChromeFlags);

      Assert.equal(
        parentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_REMOTE_WINDOW,
        contentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_REMOTE_WINDOW,
        "Should have matching remote value in parent and content"
      );

      Assert.equal(
        parentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_FISSION_WINDOW,
        contentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_FISSION_WINDOW,
        "Should have matching fission value in parent and content"
      );

      await BrowserTestUtils.closeWindow(win);
    }
  );

  // We check "all" manually, since that's an aggregate flag
  // and doesn't fit nicely into the ALLOWED / DISALLOWED scheme
  newWinPromise = BrowserTestUtils.waitForNewWindow();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: SCRIPT_PAGE_FOR_CHROME_ALL,
    },
    async function () {
      let win = await newWinPromise;
      let parentChromeFlags = getParentChromeFlags(win);
      Assert.notEqual(
        parentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_ALL,
        Ci.nsIWebBrowserChrome.CHROME_ALL,
        "Should not have been able to set CHROME_ALL"
      );
      await BrowserTestUtils.closeWindow(win);
    }
  );
});

/**
 * Opens a window with some chrome flags specified, which should not affect
 * scrollbars flag which defaults to true when not disabled explicitly.
 */
add_task(async function test_scrollbars_flag() {
  const SCRIPT = 'window.open("about:blank", "_blank", "toolbar=0");';
  const SCRIPT_PAGE = `data:text/html,<script>${SCRIPT}</script>`;

  let newWinPromise = BrowserTestUtils.waitForNewWindow();
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: SCRIPT_PAGE,
    },
    async function () {
      let win = await newWinPromise;

      let parentChromeFlags = getParentChromeFlags(win);
      Assert.ok(
        parentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_SCROLLBARS,
        "Should have scrollbars when not disabled explicitly"
      );

      let contentChromeFlags = await getContentChromeFlags(win);
      Assert.ok(
        contentChromeFlags & Ci.nsIWebBrowserChrome.CHROME_SCROLLBARS,
        "Should have scrollbars when not disabled explicitly"
      );

      await BrowserTestUtils.closeWindow(win);
    }
  );
});

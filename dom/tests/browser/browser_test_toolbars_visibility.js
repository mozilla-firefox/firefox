// Tests that toolbars have proper visibility when opening a new window
// in either content or chrome context.

const ROOT = "http://www.example.com/browser/dom/tests/browser/";
const CONTENT_PAGE = ROOT + "test_new_window_from_content_child.html";
const TARGET_PAGE = ROOT + "dummy.html";

/**
 * This function retrieves the visibility state of the toolbars of a
 * window within the content context.
 *
 * @param aBrowser (<xul:browser>)
 *        The browser to query for toolbar visibility states
 * @returns Promise
 *        A promise that resolves when the toolbar state is retrieved
 *        within the content context, which value is an object that holds
 *        the visibility state of the toolbars
 */
function getToolbarsFromBrowserContent(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], async function () {
    // This is still chrome context.
    // Inject a script that runs on content context, and gather the result.

    let script = content.document.createElement("script");
    script.textContent = `
let bars = [
  "toolbar",
  "menubar",
  "personalbar",
  "statusbar",
  "scrollbars",
  "locationbar",
];

for (let bar of bars) {
  let node = document.createElement("span");
  node.id = bar;
  node.textContent = window[bar].visible;
  document.body.appendChild(node);
}
`;
    content.document.body.appendChild(script);

    let result = {};

    let bars = [
      "toolbar",
      "menubar",
      "personalbar",
      "statusbar",
      "scrollbars",
      "locationbar",
    ];

    for (let bar of bars) {
      let node = content.document.getElementById(bar);
      let value = node.textContent;
      if (value !== "true" && value !== "false") {
        throw new Error("bar visibility isn't set");
      }
      result[bar] = value === "true";
      node.remove();
    }

    return result;
  });
}

/**
 * This function retrieves the visibility state of the toolbars of a
 * window within the chrome context.
 *
 * @param win
 *        the chrome privileged window
 * @returns object
 *        an object that holds the visibility state of the toolbars
 */
function getToolbarsFromWindowChrome(win) {
  return {
    toolbar: win.toolbar.visible,
    menubar: win.menubar.visible,
    personalbar: win.personalbar.visible,
    statusbar: win.statusbar.visible,
    scrollbars: win.scrollbars.visible,
    locationbar: win.locationbar.visible,
  };
}

/**
 * Tests toolbar visibility when opening a window with default parameters.
 *
 * @param toolbars
 *        the visibility state of the toolbar elements
 */
function testDefaultToolbars(toolbars) {
  ok(
    toolbars.locationbar,
    "locationbar should be visible on default window.open()"
  );
  ok(toolbars.menubar, "menubar be visible on default window.open()");
  ok(
    toolbars.personalbar,
    "personalbar should be visible on default window.open()"
  );
  ok(
    toolbars.statusbar,
    "statusbar should be visible on default window.open()"
  );
  ok(
    toolbars.scrollbars,
    "scrollbars should be visible on default window.open()"
  );
  ok(toolbars.toolbar, "toolbar should be visible on default window.open()");
}

/**
 * Tests toolbar visibility when opening a popup window on the content context,
 * and reading the value from context context.
 *
 * @param toolbars
 *        the visibility state of the toolbar elements
 */
function testNonDefaultContentToolbarsFromContent(toolbars) {
  // Accessing BarProp.visible from content context should return false for
  // popup, regardless of the each feature value in window.open parameter.
  ok(!toolbars.locationbar, "locationbar.visible should be false for popup");
  ok(!toolbars.menubar, "menubar.visible should be false for popup");
  ok(!toolbars.personalbar, "personalbar.visible should be false for popup");
  ok(!toolbars.statusbar, "statusbar.visible should be false for popup");
  ok(!toolbars.scrollbars, "scrollbars.visible should be false for popup");
  ok(!toolbars.toolbar, "toolbar.visible should be false for popup");
}

/**
 * Tests toolbar visibility when opening a popup window on the content context,
 * and reading the value from chrome context.
 *
 * @param toolbars
 *        the visibility state of the toolbar elements
 */
function testNonDefaultContentToolbarsFromChrome(toolbars) {
  // Accessing BarProp.visible from chrome context should return the
  // actual visibility.

  // Locationbar should always be visible on content context
  ok(
    toolbars.locationbar,
    "locationbar should be visible even with location=no"
  );
  ok(!toolbars.menubar, "menubar shouldn't be visible when menubar=no");
  ok(
    !toolbars.personalbar,
    "personalbar shouldn't be visible when personalbar=no"
  );
  // statusbar will report visible=true always on chrome.
  todo(!toolbars.statusbar, "statusbar shouldn't be visible when status=no");
  ok(
    toolbars.scrollbars,
    "scrollbars should be visible even with scrollbars=no"
  );
  ok(!toolbars.toolbar, "toolbar shouldn't be visible when toolbar=no");
}

/**
 * Tests toolbar visibility when opening a window with non default parameters
 * on the chrome context.
 *
 * @param toolbars
 *        the visibility state of the toolbar elements
 */
function testNonDefaultChromeToolbars(toolbars) {
  ok(!toolbars.menubar, "menubar should not be visible with menubar=no");
  ok(
    !toolbars.personalbar,
    "personalbar should not be visible with personalbar=no"
  );
  // See above.
  todo(!toolbars.statusbar, "statusbar shouldn't be visible when status=no");
  ok(!toolbars.toolbar, "toolbar should not be visible with toolbar=no");
  ok(
    toolbars.locationbar,
    "locationbar should be visible even with location=no"
  );
  ok(
    toolbars.scrollbars,
    "scrollbars should be visible even with scrollbars=no"
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });
});

/**
 * Ensure that toolbars of a window opened in the content context have the
 * correct visibility.
 *
 * A window opened with default parameters should have all toolbars visible.
 *
 * A window opened with "location=no, personalbar=no, toolbar=no, scrollbars=no,
 * menubar=no, status=no", should only have location visible.
 */
add_task(async function () {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: CONTENT_PAGE,
    },
    async function (browser) {
      // First, call the default window.open() which will open a new tab
      let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
      await BrowserTestUtils.synthesizeMouseAtCenter(
        "#winOpenDefault",
        {},
        browser
      );
      let tab = await newTabPromise;

      // Check that all toolbars are visible
      let toolbars = await getToolbarsFromBrowserContent(
        gBrowser.selectedBrowser
      );
      testDefaultToolbars(toolbars);

      // Cleanup
      BrowserTestUtils.removeTab(tab);

      // Now let's open a window with toolbars hidden
      let winPromise = BrowserTestUtils.waitForNewWindow({ url: TARGET_PAGE });
      await BrowserTestUtils.synthesizeMouseAtCenter(
        "#winOpenNonDefault",
        {},
        browser
      );
      let popupWindow = await winPromise;

      let popupBrowser = popupWindow.gBrowser.selectedBrowser;

      // Test toolbars visibility value from content.
      let popupToolbars = await getToolbarsFromBrowserContent(popupBrowser);
      testNonDefaultContentToolbarsFromContent(popupToolbars);

      // Test toolbars visibility value from chrome.
      let chromeToolbars = getToolbarsFromWindowChrome(popupWindow);
      testNonDefaultContentToolbarsFromChrome(chromeToolbars);

      // Close the new window
      await BrowserTestUtils.closeWindow(popupWindow);
    }
  );
});

/**
 * Ensure that toolbars of a window opened to about:blank in the content context
 * have the correct visibility.
 *
 * A window opened with "location=no, personalbar=no, toolbar=no, scrollbars=no,
 * menubar=no, status=no", should only have location visible.
 */
add_task(async function () {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: CONTENT_PAGE,
    },
    async function (browser) {
      // Open a blank window with toolbars hidden
      let winPromise = BrowserTestUtils.waitForNewWindow();
      await BrowserTestUtils.synthesizeMouseAtCenter(
        "#winOpenNoURLNonDefault",
        {},
        browser
      );
      let popupWindow = await winPromise;

      // No need to wait for this window to load, since it's loading about:blank
      let popupBrowser = popupWindow.gBrowser.selectedBrowser;
      let popupToolbars = await getToolbarsFromBrowserContent(popupBrowser);
      testNonDefaultContentToolbarsFromContent(popupToolbars);

      let chromeToolbars = getToolbarsFromWindowChrome(popupWindow);
      testNonDefaultContentToolbarsFromChrome(chromeToolbars);

      // Close the new window
      await BrowserTestUtils.closeWindow(popupWindow);
    }
  );
});

/**
 * Ensure that toolbars of a window opened in the chrome context have the
 * correct visibility.
 *
 * A window opened with default parameters should have all toolbars visible.
 *
 * A window opened with "location=no, personalbar=no, toolbar=no, scrollbars=no,
 * menubar=no, status=no", should not have any toolbar visible.
 */
add_task(async function () {
  // First open a default window from this chrome context
  let defaultWindowPromise = BrowserTestUtils.waitForNewWindow({
    url: TARGET_PAGE,
  });
  window.open(TARGET_PAGE, "_blank", "noopener");
  let defaultWindow = await defaultWindowPromise;

  // Check that all toolbars are visible
  let toolbars = getToolbarsFromWindowChrome(defaultWindow);
  testDefaultToolbars(toolbars);

  // Now lets open a window with toolbars hidden from this chrome context
  let features =
    "location=no, personalbar=no, toolbar=no, scrollbars=no, menubar=no, status=no, noopener";
  let popupWindowPromise = BrowserTestUtils.waitForNewWindow({
    url: TARGET_PAGE,
  });
  window.open(TARGET_PAGE, "_blank", features);
  let popupWindow = await popupWindowPromise;

  // Test none of the tooolbars are visible
  let hiddenToolbars = getToolbarsFromWindowChrome(popupWindow);
  testNonDefaultChromeToolbars(hiddenToolbars);

  // Cleanup
  await BrowserTestUtils.closeWindow(defaultWindow);
  await BrowserTestUtils.closeWindow(popupWindow);
});

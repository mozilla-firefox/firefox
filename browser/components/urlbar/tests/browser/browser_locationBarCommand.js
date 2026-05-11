/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This test is designed to ensure that the correct command/operation happens
 * when pressing Enter, or clicking the Go button, with various key
 * combinations in the urlbar.
 */

const TEST_VALUE = "http://example.com";
const START_VALUE = "http://example.org/";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.altClickSave", true],
      ["browser.urlbar.autoFill", false],
    ],
  });
});

add_task(async function alt_left_click_test() {
  info("Running test: Alt left click");

  // Monkey patch saveURL() to avoid dealing with file save code paths.
  let oldSaveURL = saveURL;
  let saveURLPromise = new Promise(resolve => {
    saveURL = () => {
      // Restore old saveURL() value.
      saveURL = oldSaveURL;
      resolve();
    };
  });

  await typeAndCommand("click", { altKey: true });

  await saveURLPromise;
  ok(true, "SaveURL was called");
  is(gURLBar.value, "", "Urlbar reverted to original value");
});

add_task(async function shift_left_click_test() {
  info("Running test: Shift left click");

  let destinationURL = TEST_VALUE + "/";
  let newWindowPromise = BrowserTestUtils.waitForNewWindow({
    url: destinationURL,
  });
  await typeAndCommand("click", { shiftKey: true });
  let win = await newWindowPromise;

  info("URL should be loaded in a new window");
  is(gURLBar.value, "", "Urlbar reverted to original value");
  await promiseCheckChildNoFocusedElement(gBrowser.selectedBrowser);
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Content window should be focused"
  );
  is(
    win.gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "New URL is loaded in new window"
  );

  // Cleanup.
  let ourWindowRefocusedPromise = Promise.all([
    BrowserTestUtils.waitForEvent(window, "activate"),
    BrowserTestUtils.waitForEvent(window, "focus", true),
  ]);
  await BrowserTestUtils.closeWindow(win);
  await ourWindowRefocusedPromise;
});

add_task(async function right_click_test() {
  info("Running test: Right click on go button");

  // Add a new tab.
  await promiseOpenNewTab();

  await typeAndCommand("click", { button: 2 });

  // Right click should do nothing (context menu will be shown).
  is(gURLBar.value, TEST_VALUE, "Urlbar still has the value we entered");

  // Cleanup.
  gBrowser.removeCurrentTab();
});

add_task(async function shift_accel_left_click_test() {
  info("Running test: Shift+Ctrl/Cmd left click on go button");

  // Add a new tab.
  let tab = await promiseOpenNewTab();

  let loadStartedPromise = promiseLoadStarted();
  await typeAndCommand("click", { accelKey: true, shiftKey: true });
  await loadStartedPromise;

  // Check the load occurred in a new background tab.
  info("URL should be loaded in a new background tab");
  is(gURLBar.value, "", "Urlbar reverted to original value");
  ok(!gURLBar.focused, "Urlbar is no longer focused after urlbar command");
  is(gBrowser.selectedTab, tab, "Focus did not change to the new tab");

  // Select the new background tab
  gBrowser.selectedTab = gBrowser.selectedTab.nextElementSibling;
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "New URL is loaded in new tab"
  );

  // Cleanup.
  gBrowser.removeCurrentTab();
  gBrowser.removeCurrentTab();
});

add_task(async function load_in_current_tab_test() {
  let tests = [
    {
      desc: "Simple return keypress",
      type: "keypress",
    },
    {
      desc: "Left click on go button",
      type: "click",
    },
    {
      desc: "Ctrl/Cmd+Return keypress",
      type: "keypress",
      details: { accelKey: true },
    },
    {
      desc: "Alt+Return keypress in a blank tab",
      type: "keypress",
      details: { altKey: true },
    },
    {
      desc: "AltGr+Return keypress in a blank tab",
      type: "keypress",
      details: { altGraphKey: true },
    },
  ];

  for (let { desc, type, details } of tests) {
    info(`Running test: ${desc}`);

    // Add a new tab.
    let tab = await promiseOpenNewTab();

    // Trigger a load and check it occurs in the current tab.
    let browserLoadedPromise = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      { wantLoad: TEST_VALUE + "/" }
    );
    await typeAndCommand(type, details);
    await browserLoadedPromise;

    info("URL should be loaded in the current tab");
    is(
      gURLBar.value,
      UrlbarTestUtils.trimURL(TEST_VALUE),
      "Urlbar still has the value we entered"
    );
    await promiseCheckChildNoFocusedElement(gBrowser.selectedBrowser);
    is(
      document.activeElement,
      gBrowser.selectedBrowser,
      "Content window should be focused"
    );
    is(gBrowser.selectedTab, tab, "New URL was loaded in the current tab");

    // Cleanup.
    gBrowser.removeCurrentTab();
  }
});

add_task(async function load_in_new_tab_test() {
  let tests = [
    {
      desc: "Ctrl/Cmd left click on go button",
      type: "click",
      details: { accelKey: true },
      url: "about:blank",
    },
    {
      desc: "Middleclick on go button",
      type: "click",
      details: { button: 1 },
      url: "about:blank",
    },
    {
      desc: "Alt+Return keypress in a dirty tab",
      type: "keypress",
      details: { altKey: true },
      url: START_VALUE,
    },
    {
      desc: "AltGr+Return keypress in a dirty tab",
      type: "keypress",
      details: { altGraphKey: true },
      url: START_VALUE,
    },
  ];

  for (let { desc, type, details, url } of tests) {
    info(`Running test: ${desc}`);

    // Add a new tab.
    let tab = await promiseOpenNewTab(url);

    // Trigger a load and check it occurs in a new tab.
    let newTabPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      TEST_VALUE + "/",
      true
    );
    await typeAndCommand(type, details);
    let newTab = await newTabPromise;

    // Check the load occurred in a new tab.
    info("URL should be loaded in a new focused tab");
    is(
      gURLBar.value,
      UrlbarTestUtils.trimURL(TEST_VALUE),
      "Urlbar still has the value we entered"
    );
    await promiseCheckChildNoFocusedElement(newTab.linkedBrowser);
    is(
      document.activeElement,
      newTab.linkedBrowser,
      "Content window should be focused"
    );
    isnot(gBrowser.selectedTab, tab, "New URL was loaded in a new tab");

    // Cleanup.
    gBrowser.removeCurrentTab();
    gBrowser.removeCurrentTab();
  }
});

add_task(async function go_button_after_tab_switch() {
  // Add a new tab.
  let tab = await promiseOpenNewTab();

  await UrlbarTestUtils.inputIntoURLBar(window, TEST_VALUE);
  await BrowserTestUtils.switchTab(gBrowser, gBrowser.visibleTabs[0]);
  isnot(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "Urlbar does not have the entered value after switching to a different tab"
  );
  await BrowserTestUtils.switchTab(gBrowser, tab);
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "Urlbar still has the entered value restored after switching back to the new tab"
  );

  // Trigger a load and check it occurs in the current tab.
  let browserLoadedPromise = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    { wantLoad: TEST_VALUE + "/" }
  );
  await triggerCommand("click");
  await browserLoadedPromise;

  info("URL should be loaded in the current tab");
  is(
    gURLBar.value,
    UrlbarTestUtils.trimURL(TEST_VALUE),
    "Urlbar still has the value we entered"
  );
  await promiseCheckChildNoFocusedElement(gBrowser.selectedBrowser);
  is(
    document.activeElement,
    gBrowser.selectedBrowser,
    "Content window should be focused"
  );
  is(gBrowser.selectedTab, tab, "New URL was loaded in the current tab");

  // Cleanup.
  gBrowser.removeCurrentTab();
});

add_task(async function changing_ref_does_not_reload() {
  // Load a page with ref, change the ref and confirm again, it should not
  // cause a reload of the page.
  for (let protocol of ["http://", "https://"]) {
    let url = protocol + "example.com/#ref";
    await BrowserTestUtils.withNewTab({ gBrowser, url }, async function () {
      await SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
        let link = content.document.createElement("a");
        link.textContent = "Click me";
        link.name = "refmod";
        link.setAttribute("name", "refmod");
        content.document.body.append(link);
      });

      await UrlbarTestUtils.promisePopupOpen(window, () => {
        EventUtils.synthesizeKey("l", { accelKey: true });
      });
      Assert.equal(
        document.activeElement,
        gURLBar.inputField,
        "urlbar is focused"
      );

      EventUtils.synthesizeKey("KEY_ArrowRight", {});
      EventUtils.sendString("mod");

      let promise = promiseHashChangeLoad(url + "mod");
      EventUtils.synthesizeKey("VK_RETURN");
      await promise;
    });
  }
});

async function typeAndCommand(eventType, details = {}) {
  await UrlbarTestUtils.inputIntoURLBar(window, TEST_VALUE);
  await triggerCommand(eventType, details);
}

async function triggerCommand(eventType, details = {}) {
  Assert.equal(
    await UrlbarTestUtils.promiseUserContextId(window),
    gBrowser.selectedTab.getAttribute("usercontextid") || "",
    "userContextId must be the same as the originating tab"
  );

  switch (eventType) {
    case "click":
      ok(
        gURLBar.hasAttribute("usertyping"),
        "usertyping attribute must be set for the go button to be visible"
      );
      EventUtils.synthesizeMouseAtCenter(gURLBar.goButton, details);
      break;
    case "keypress":
      EventUtils.synthesizeKey("KEY_Enter", details);
      break;
    default:
      throw new Error("Unsupported event type");
  }
}

function promiseLoadStarted() {
  return new Promise(resolve => {
    gBrowser.addTabsProgressListener({
      onStateChange(browser, webProgress, req, flags) {
        if (flags & Ci.nsIWebProgressListener.STATE_START) {
          gBrowser.removeTabsProgressListener(this);
          resolve();
        }
      },
    });
  });
}

async function promiseHashChangeLoad(url) {
  let { flags } = await BrowserTestUtils.waitForLocationChange(gBrowser, url);
  Assert.ok(
    flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_HASHCHANGE,
    "Only ref fragment was changed"
  );
}

let gUserContextIdSerial = 1;
async function promiseOpenNewTab(url = "about:blank") {
  let tab = BrowserTestUtils.addTab(gBrowser, url, {
    userContextId: gUserContextIdSerial++,
  });
  let tabSwitchPromise = BrowserTestUtils.switchTab(gBrowser, tab);
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser, {
    wantLoad: url,
  });
  await tabSwitchPromise;
  return tab;
}

function promiseCheckChildNoFocusedElement(browser) {
  if (!gMultiProcessBrowser) {
    Assert.equal(
      Services.focus.focusedElement,
      null,
      "There should be no focused element"
    );
    return null;
  }

  return SpecialPowers.spawn(browser, [], async function () {
    Assert.equal(
      Services.focus.focusedElement,
      null,
      "There should be no focused element"
    );
  });
}

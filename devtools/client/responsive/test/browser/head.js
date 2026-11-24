/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* eslint no-unused-vars: [2, {"vars": "local"}] */

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/shared-head.js",
  this
);

// Import helpers for the inspector that are also shared with others
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/inspector/test/shared-head.js",
  this
);

// Load APZ test utils so we properly wait after resize
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_utils.js",
  this
);
Services.scriptloader.loadSubScript(
  "chrome://mochikit/content/tests/SimpleTest/paint_listener.js",
  this
);

const {
  _loadPreferredDevices,
} = require("resource://devtools/client/responsive/actions/devices.js");
const {
  getStr,
} = require("resource://devtools/client/responsive/utils/l10n.js");
const {
  getTopLevelWindow,
} = require("resource://devtools/client/responsive/utils/window.js");
const {
  addDevice,
  getDeviceString,
  removeDevice,
  removeLocalDevices,
} = require("resource://devtools/client/shared/devices.js");
const { KeyCodes } = require("resource://devtools/client/shared/keycodes.js");
const asyncStorage = require("resource://devtools/shared/async-storage.js");
const localTypes = require("resource://devtools/client/responsive/types.js");

loader.lazyRequireGetter(
  this,
  "ResponsiveUIManager",
  "resource://devtools/client/responsive/manager.js"
);
loader.lazyRequireGetter(
  this,
  "message",
  "resource://devtools/client/responsive/utils/message.js"
);

const E10S_MULTI_ENABLED =
  Services.prefs.getIntPref("dom.ipc.processCount") > 1;
const TEST_URI_ROOT =
  "http://example.com/browser/devtools/client/responsive/test/browser/";
const RELOAD_CONDITION_PREF_PREFIX = "devtools.responsive.reloadConditions.";
const DEFAULT_UA = Cc["@mozilla.org/network/protocol;1?name=http"].getService(
  Ci.nsIHttpProtocolHandler
).userAgent;

SimpleTest.requestCompleteLog();
SimpleTest.waitForExplicitFinish();

// Toggling the RDM UI involves several docShell swap operations, which are somewhat slow
// on debug builds. Usually we are just barely over the limit, so a blanket factor of 2
// should be enough.
requestLongerTimeout(2);

registerCleanupFunction(async () => {
  await asyncStorage.removeItem("devtools.responsive.deviceState");
  await removeLocalDevices();

  delete window.waitForAllPaintsFlushed;
  delete window.waitForAllPaints;
  delete window.promiseAllPaintsDone;
});

/**
 * Adds a new test task that adds a tab with the given URL, awaits the
 * preTask (if provided), opens responsive design mode, awaits the task,
 * closes responsive design mode, awaits the postTask (if provided), and
 * removes the tab. The final argument is an options object, with these
 * optional properties:
 *
 * onlyPrefAndTask: if truthy, only the pref will be set and the task
 *   will be called, with none of the tab creation/teardown or open/close
 *   of RDM (default false).
 * waitForDeviceList: if truthy, the function will wait until the device
 *   list is loaded before calling the task (default false).
 *
 * Example usage:
 *
 *   addRDMTaskWithPreAndPost(
 *     TEST_URL,
 *     async function preTask({ message, browser }) {
 *       // Your pre-task goes here...
 *     },
 *     async function task({ ui, manager, message, browser, preTaskValue, tab }) {
 *       // Your task goes here...
 *     },
 *     async function postTask({ message, browser, preTaskValue, taskValue }) {
 *       // Your post-task goes here...
 *     },
 *     { waitForDeviceList: true }
 *   );
 */
function addRDMTaskWithPreAndPost(url, preTask, task, postTask, options) {
  let onlyPrefAndTask = false;
  let waitForDeviceList = false;
  if (typeof options == "object") {
    onlyPrefAndTask = !!options.onlyPrefAndTask;
    waitForDeviceList = !!options.waitForDeviceList;
  }

  add_task(async function () {
    let tab;
    let browser;
    let preTaskValue = null;
    let taskValue = null;
    let ui;
    let manager;

    if (!onlyPrefAndTask) {
      tab = await addTab(url);
      browser = tab.linkedBrowser;

      if (preTask) {
        preTaskValue = await preTask({ message, browser });
      }

      const rdmValues = await openRDM(tab, { waitForDeviceList });
      ui = rdmValues.ui;
      manager = rdmValues.manager;
    }

    try {
      taskValue = await task({
        ui,
        manager,
        message,
        browser,
        preTaskValue,
        tab,
      });
    } catch (err) {
      ok(false, "Got an error: " + DevToolsUtils.safeErrorString(err));
    }

    if (!onlyPrefAndTask) {
      // Close the toolbox first, as closing RDM might trigger a reload if
      // touch simulation was enabled, which will trigger RDP requests.
      await closeToolboxIfOpen();

      await closeRDM(tab);

      if (postTask) {
        await postTask({
          message,
          browser,
          preTaskValue,
          taskValue,
        });
      }
      await removeTab(tab);
    }

    // Flush prefs to not only undo our earlier change, but also undo
    // any changes made by the tasks.
    await SpecialPowers.flushPrefEnv();
  });
}

/**
 * This is a simplified version of addRDMTaskWithPreAndPost. Adds a new test
 * task that adds a tab with the given URL, opens responsive design mode,
 * closes responsive design mode, and removes the tab.
 *
 * Example usage:
 *
 *   addRDMTask(
 *     TEST_URL,
 *     async function task({ ui, manager, message, browser }) {
 *       // Your task goes here...
 *     },
 *     { waitForDeviceList: true }
 *   );
 */
function addRDMTask(rdmURL, rdmTask, options) {
  addRDMTaskWithPreAndPost(rdmURL, undefined, rdmTask, undefined, options);
}

async function spawnViewportTask(ui, args, task) {
  // Await a reflow after the task.
  const result = await SpecialPowers.spawn(
    ui.getViewportBrowser(),
    [args],
    task
  );
  await promiseContentReflow(ui);
  return result;
}

function waitForFrameLoad(ui, targetURL) {
  return spawnViewportTask(ui, { targetURL }, async function (args) {
    if (
      (content.document.readyState == "complete" ||
        content.document.readyState == "interactive") &&
      content.location.href == args.targetURL
    ) {
      return;
    }
    await ContentTaskUtils.waitForEvent(this, "DOMContentLoaded");
  });
}

function waitForViewportResizeTo(ui, width, height) {
  return new Promise(function (resolve) {
    const isSizeMatching = data => data.width == width && data.height == height;

    // If the viewport has already the expected size, we resolve the promise immediately.
    const size = ui.getViewportSize();
    if (isSizeMatching(size)) {
      info(`Viewport already resized to ${width} x ${height}`);
      resolve();
      return;
    }

    // Otherwise, we'll listen to the viewport's resize event, and the
    // browser's load end; since a racing condition can happen, where the
    // viewport's listener is added after the resize, because the viewport's
    // document was reloaded; therefore the test would hang forever.
    // See bug 1302879.
    const browser = ui.getViewportBrowser();

    const onContentResize = data => {
      if (!isSizeMatching(data)) {
        return;
      }
      ui.off("content-resize", onContentResize);
      browser.removeEventListener("mozbrowserloadend", onBrowserLoadEnd);
      info(`Got content-resize to ${width} x ${height}`);
      resolve();
    };

    const onBrowserLoadEnd = async function () {
      const data = ui.getViewportSize(ui);
      onContentResize(data);
    };

    info(`Waiting for viewport-resize to ${width} x ${height}`);
    // We're changing the viewport size, which may also change the content
    // size. We wait on the viewport resize event, and check for the
    // desired size.
    ui.on("content-resize", onContentResize);
    browser.addEventListener("mozbrowserloadend", onBrowserLoadEnd, {
      once: true,
    });
  });
}

var setViewportSize = async function (ui, manager, width, height) {
  const size = ui.getViewportSize();
  info(
    `Current size: ${size.width} x ${size.height}, ` +
      `set to: ${width} x ${height}`
  );
  if (size.width != width || size.height != height) {
    const resized = waitForViewportResizeTo(ui, width, height);
    ui.setViewportSize({ width, height });
    await resized;
    // content-resize is emitted before the content has the new size.
    await waitFor(async () => {
      const hasNewSize = await SpecialPowers.spawn(
        ui.getViewportBrowser(),
        [width, height],
        async (w, h) => {
          const viewerWidth = {};
          const viewerHeight = {};
          content.windowUtils.getDocumentViewerSize(viewerWidth, viewerHeight);
          const ratio = content.devicePixelRatio;
          return (
            viewerWidth.value == Math.round(w * ratio) &&
            viewerHeight.value == Math.round(h * ratio)
          );
        }
      );
      return hasNewSize;
    });
  }
};

// This performs the same function as setViewportSize, but additionally
// ensures that reflow of the viewport has completed.
var setViewportSizeAndAwaitReflow = async function (
  ui,
  manager,
  width,
  height
) {
  await setViewportSize(ui, manager, width, height);
  await promiseContentReflow(ui);
  await promiseApzFlushedRepaints();
};

function getViewportDevicePixelRatio(ui) {
  return SpecialPowers.spawn(ui.getViewportBrowser(), [], async function () {
    // Note that devicePixelRatio doesn't return the override to privileged
    // code, see bug 1759962.
    return content.browsingContext.overrideDPPX || content.devicePixelRatio;
  });
}

function getElRect(selector, win) {
  const el = win.document.querySelector(selector);
  return el.getBoundingClientRect();
}

/**
 * Drag an element identified by 'selector' by [x,y] amount. Returns
 * the rect of the dragged element as it was before drag.
 */
function dragElementBy(selector, x, y, ui) {
  const browserWindow = ui.getBrowserWindow();
  const rect = getElRect(selector, browserWindow);
  const startPoint = {
    clientX: Math.floor(rect.left + rect.width / 2),
    clientY: Math.floor(rect.top + rect.height / 2),
  };
  const endPoint = [startPoint.clientX + x, startPoint.clientY + y];

  EventUtils.synthesizeMouseAtPoint(
    startPoint.clientX,
    startPoint.clientY,
    { type: "mousedown" },
    browserWindow
  );

  // mousemove and mouseup are regular DOM listeners
  EventUtils.synthesizeMouseAtPoint(
    ...endPoint,
    { type: "mousemove" },
    browserWindow
  );
  EventUtils.synthesizeMouseAtPoint(
    ...endPoint,
    { type: "mouseup" },
    browserWindow
  );

  return rect;
}

/**
 * Resize the viewport and check that the resize happened as expected.
 *
 * @param {ResponsiveUI} ui
 *        The ResponsiveUI instance.
 * @param {string} selector
 *        The css selector of the resize handler, eg .viewport-horizontal-resize-handle.
 * @param {Array<number>} moveBy
 *        Array of 2 integers representing the x,y distance of the resize action.
 * @param {Array<number>} moveBy
 *        Array of 2 integers representing the actual resize performed.
 * @param {object} options
 * @param {boolean} options.hasDevice
 *        Whether a device is currently set and will be overridden by the resize
 */
async function testViewportResize(
  ui,
  selector,
  moveBy,
  expectedHandleMove,
  { hasDevice } = {}
) {
  // If a device was defined, wait for the device-associaton-removed event.
  const deviceRemoved = hasDevice
    ? once(ui, "device-association-removed")
    : null;

  const resized = ui.once("viewport-resize-dragend");
  const startRect = dragElementBy(selector, ...moveBy, ui);
  await resized;

  const endRect = getElRect(selector, ui.getBrowserWindow());
  is(
    endRect.left - startRect.left,
    expectedHandleMove[0],
    `The x move of ${selector} is as expected`
  );
  is(
    endRect.top - startRect.top,
    expectedHandleMove[1],
    `The y move of ${selector} is as expected`
  );

  await deviceRemoved;
}

async function openDeviceModal(ui) {
  const { document, store } = ui.toolWindow;

  info("Opening device modal through device selector.");
  const onModalOpen = waitUntilState(store, state => state.devices.isModalOpen);
  await selectMenuItem(
    ui,
    "#device-selector",
    getStr("responsive.editDeviceList2")
  );
  await onModalOpen;

  const modal = document.getElementById("device-modal-wrapper");
  ok(
    modal.classList.contains("opened") && !modal.classList.contains("closed"),
    "The device modal is displayed."
  );
}

async function selectMenuItem({ toolWindow }, selector, value) {
  const { document } = toolWindow;

  const button = document.querySelector(selector);
  isnot(
    button,
    null,
    `Selector "${selector}" should match an existing element.`
  );

  info(`Selecting ${value} in ${selector}.`);

  await testMenuItems(toolWindow, button, items => {
    const menuItem = findMenuItem(items, value);
    isnot(
      menuItem,
      undefined,
      `Value "${value}" should match an existing menu item.`
    );
    menuItem.click();
  });
}

/**
 * Runs the menu items from the button's context menu against a test function.
 *
 * @param  {Window} toolWindow
 *         A window reference.
 * @param  {Element} button
 *         The button that will show a context menu when clicked.
 * @param  {Function} testFn
 *         A test function that will be ran with the found menu item in the context menu
 *         as an argument.
 */
async function testMenuItems(toolWindow, button, testFn) {
  // The context menu appears only in the top level window, which is different from
  // the inner toolWindow.
  const win = getTopLevelWindow(toolWindow);

  await new Promise(resolve => {
    win.document.addEventListener(
      "popupshown",
      async () => {
        // Handle MenuButton popups (device selector and network throttling).
        if (
          button.id === "device-selector" ||
          button.id == "user-agent-selector" ||
          button.id === "network-throttling"
        ) {
          let popupId;
          if (button.id === "device-selector") {
            popupId = "#device-selector-menu";
          } else if (button.id === "user-agent-selector") {
            popupId = "#user-agent-selector-menu";
          } else {
            popupId = "#network-throttling-menu";
          }

          const popup = toolWindow.document.querySelector(popupId);
          const menuItems = [...popup.querySelectorAll(".menuitem > .command")];

          testFn(menuItems);

          if (popup.classList.contains("tooltip-visible")) {
            // Close the tooltip explicitly.
            button.click();
            await waitUntil(() => !popup.classList.contains("tooltip-visible"));
          }
        } else {
          const popup = win.document.querySelector(
            'menupopup[menu-api="true"]'
          );
          const menuItems = [...popup.children];

          testFn(menuItems);

          popup.hidePopup();
        }

        resolve();
      },
      { once: true }
    );

    button.click();
  });
}

const selectDevice = async (ui, value) => {
  const browser = ui.getViewportBrowser();
  const waitForDevToolsReload = await watchForDevToolsReload(browser);

  const onDeviceChanged = once(ui, "device-changed");
  await selectMenuItem(ui, "#device-selector", value);
  const { reloadTriggered } = await onDeviceChanged;
  if (reloadTriggered) {
    await waitForDevToolsReload();
  }
};

const selectDevicePixelRatio = (ui, value) =>
  selectMenuItem(ui, "#device-pixel-ratio-menu", `DPR: ${value}`);

const selectNetworkThrottling = (ui, value) =>
  Promise.all([
    once(ui, "network-throttling-changed"),
    selectMenuItem(ui, "#network-throttling", value),
  ]);

async function getSessionHistory(browser) {
  const browsingContext = browser.browsingContext;
  const uri = browsingContext.currentWindowGlobal.documentURI.displaySpec;
  const history = browsingContext.sessionHistory;
  const documentHasChildNodes = await SpecialPowers.spawn(
    browser,
    [],
    function () {
      return !!content.document.body;
    }
  );
  const { SessionHistory } = ChromeUtils.importESModule(
    "resource://gre/modules/sessionstore/SessionHistory.sys.mjs"
  );
  return SessionHistory.collectFromParent(uri, documentHasChildNodes, history);
}

function getContentSize(ui) {
  return spawnViewportTask(ui, {}, () => ({
    width: content.screen.width,
    height: content.screen.height,
  }));
}

function getViewportScroll(ui) {
  return spawnViewportTask(ui, {}, () => ({
    x: content.scrollX,
    y: content.scrollY,
  }));
}

async function waitForPageShow(browser) {
  const tab = gBrowser.getTabForBrowser(browser);
  const ui = ResponsiveUIManager.getResponsiveUIForTab(tab);
  if (ui) {
    browser = ui.getViewportBrowser();
  }
  info(
    "Waiting for pageshow from " + (ui ? "responsive" : "regular") + " browser"
  );
  // Need to wait an extra tick after pageshow to ensure everyone is up-to-date,
  // hence the waitForTick.
  await BrowserTestUtils.waitForContentEvent(browser, "pageshow");
  return waitForTick();
}

function waitForViewportScroll(ui) {
  return BrowserTestUtils.waitForContentEvent(
    ui.getViewportBrowser(),
    "scroll",
    true
  );
}

async function back(browser) {
  const waitForDevToolsReload = await watchForDevToolsReload(browser);
  const onPageShow = waitForPageShow(browser);

  browser.goBack();

  await onPageShow;
  await waitForDevToolsReload();
}

async function forward(browser) {
  const waitForDevToolsReload = await watchForDevToolsReload(browser);
  const onPageShow = waitForPageShow(browser);

  browser.goForward();

  await onPageShow;
  await waitForDevToolsReload();
}

function addDeviceForTest(device) {
  info(`Adding Test Device "${device.name}" to the list.`);
  addDevice(device);

  registerCleanupFunction(() => {
    // Note that assertions in cleanup functions are not displayed unless they failed.
    ok(
      removeDevice(device),
      `Removed Test Device "${device.name}" from the list.`
    );
  });
}

async function waitForClientClose(ui) {
  info("Waiting for RDM devtools client to close");
  await ui.commands.client.once("closed");
  info("RDM's devtools client is now closed");
}

async function testDevicePixelRatio(ui, expected) {
  const dppx = await getViewportDevicePixelRatio(ui);
  is(dppx, expected, `devicePixelRatio should be set to ${expected}`);
}

function testTouchEventsOverride(ui, expected) {
  const { document } = ui.toolWindow;
  const touchButton = document.getElementById("touch-simulation-button");

  const flag = gBrowser.selectedBrowser.browsingContext.touchEventsOverride;

  is(
    flag === "enabled",
    expected,
    `Touch events override should be ${expected ? "enabled" : "disabled"}`
  );
  is(
    touchButton.classList.contains("checked"),
    expected,
    `Touch simulation button should be ${expected ? "" : "in"}active.`
  );
}

function testViewportDeviceMenuLabel(ui, expectedDeviceName) {
  info("Test viewport's device select label");

  const button = ui.toolWindow.document.querySelector("#device-selector");
  ok(
    button.textContent.includes(expectedDeviceName),
    `Device Select value ${button.textContent} should be: ${expectedDeviceName}`
  );
}

async function toggleTouchSimulation(ui) {
  const { document } = ui.toolWindow;
  const browser = ui.getViewportBrowser();

  const touchButton = document.getElementById("touch-simulation-button");
  const wasChecked = touchButton.classList.contains("checked");
  const onTouchSimulationChanged = once(ui, "touch-simulation-changed");
  const waitForDevToolsReload = await watchForDevToolsReload(browser);
  const onTouchButtonStateChanged = waitFor(
    () => touchButton.classList.contains("checked") !== wasChecked
  );

  touchButton.click();
  await Promise.all([
    onTouchSimulationChanged,
    onTouchButtonStateChanged,
    waitForDevToolsReload(),
  ]);
}

async function testUserAgent(ui, expected) {
  const { document } = ui.toolWindow;
  const userAgentInput = document.getElementById("user-agent-input");

  if (expected === DEFAULT_UA) {
    is(userAgentInput.value, "", "UA input should be empty");
  } else {
    is(userAgentInput.value, expected, `UA input should be set to ${expected}`);
  }

  await testUserAgentFromBrowser(ui.getViewportBrowser(), expected);
}

async function testUserAgentFromBrowser(browser, expected) {
  const ua = await SpecialPowers.spawn(browser, [], async function () {
    return content.navigator.userAgent;
  });
  is(ua, expected, `UA should be set to ${expected}`);
}

function testViewportDimensions(ui, w, h) {
  const viewport = ui.viewportElement;

  is(
    ui.toolWindow.getComputedStyle(viewport).getPropertyValue("width"),
    `${w}px`,
    `Viewport should have width of ${w}px`
  );
  is(
    ui.toolWindow.getComputedStyle(viewport).getPropertyValue("height"),
    `${h}px`,
    `Viewport should have height of ${h}px`
  );
}

async function changeUserAgentInput(
  ui,
  value,
  keyPressedAfterChange = "VK_RETURN"
) {
  const { Simulate } = ui.toolWindow.require(
    "resource://devtools/client/shared/vendor/react-dom-test-utils.js"
  );
  const { document, store } = ui.toolWindow;
  const browser = ui.getViewportBrowser();

  const userAgentInput = document.getElementById("user-agent-input");
  userAgentInput.value = value;
  userAgentInput.focus();
  Simulate.change(userAgentInput);

  function pressKey() {
    EventUtils.synthesizeKey(keyPressedAfterChange, {}, ui.toolWindow);
  }

  if (keyPressedAfterChange === "VK_ESCAPE") {
    pressKey();
  } else {
    const userAgentChanged = waitUntilState(
      store,
      state => state.ui.userAgent === value
    );
    const changed = once(ui, "user-agent-changed");
    const waitForDevToolsReload = await watchForDevToolsReload(browser);

    pressKey();

    await Promise.all([changed, waitForDevToolsReload(), userAgentChanged]);
  }
}

/**
 * Assuming the device modal is open and the device adder form is shown, this helper
 * function adds `device` via the form, saves it, and waits for it to appear in the store.
 */
function addDeviceInModal(ui, device) {
  const { Simulate } = ui.toolWindow.require(
    "resource://devtools/client/shared/vendor/react-dom-test-utils.js"
  );
  const { document, store } = ui.toolWindow;

  const nameInput = document.querySelector("#device-form-name input");
  const [widthInput, heightInput] = document.querySelectorAll(
    "#device-form-size input"
  );
  const pixelRatioInput = document.querySelector(
    "#device-form-pixel-ratio input"
  );
  const userAgentInput = document.querySelector(
    "#device-form-user-agent input"
  );
  const touchInput = document.querySelector("#device-form-touch input");

  nameInput.value = device.name;
  Simulate.change(nameInput);
  widthInput.value = device.width;
  Simulate.change(widthInput);
  Simulate.blur(widthInput);
  heightInput.value = device.height;
  Simulate.change(heightInput);
  Simulate.blur(heightInput);
  pixelRatioInput.value = device.pixelRatio;
  Simulate.change(pixelRatioInput);
  userAgentInput.value = device.userAgent;
  Simulate.change(userAgentInput);
  touchInput.checked = device.touch;
  Simulate.change(touchInput);

  const existingCustomDevices = store.getState().devices.custom.length;
  const adderSave = document.querySelector("#device-form-save");
  const saved = waitUntilState(
    store,
    state => state.devices.custom.length == existingCustomDevices + 1
  );
  Simulate.click(adderSave);
  return saved;
}

async function editDeviceInModal(ui, device, newDevice) {
  const { Simulate } = ui.toolWindow.require(
    "resource://devtools/client/shared/vendor/react-dom-test-utils.js"
  );
  const { document, store } = ui.toolWindow;

  const nameInput = document.querySelector("#device-form-name input");
  const [widthInput, heightInput] = document.querySelectorAll(
    "#device-form-size input"
  );
  const pixelRatioInput = document.querySelector(
    "#device-form-pixel-ratio input"
  );
  const userAgentInput = document.querySelector(
    "#device-form-user-agent input"
  );
  const touchInput = document.querySelector("#device-form-touch input");

  nameInput.value = newDevice.name;
  Simulate.change(nameInput);
  widthInput.value = newDevice.width;
  Simulate.change(widthInput);
  Simulate.blur(widthInput);
  heightInput.value = newDevice.height;
  Simulate.change(heightInput);
  Simulate.blur(heightInput);
  pixelRatioInput.value = newDevice.pixelRatio;
  Simulate.change(pixelRatioInput);
  userAgentInput.value = newDevice.userAgent;
  Simulate.change(userAgentInput);
  touchInput.checked = newDevice.touch;
  Simulate.change(touchInput);

  const existingCustomDevices = store.getState().devices.custom.length;
  const formSave = document.querySelector("#device-form-save");

  const saved = waitUntilState(
    store,
    state =>
      state.devices.custom.length == existingCustomDevices &&
      state.devices.custom.find(({ name }) => name == newDevice.name) &&
      !state.devices.custom.find(({ name }) => name == device.name)
  );

  // Editing a custom device triggers a "device-change" message.
  // Wait for the `device-changed` event to avoid unfinished requests during the
  // tests.
  const onDeviceChanged = ui.once("device-changed");

  Simulate.click(formSave);

  await onDeviceChanged;
  return saved;
}

function findMenuItem(menuItems, name) {
  return menuItems.find(menuItem => menuItem.textContent.includes(name));
}

function reloadOnUAChange(enabled) {
  const pref = RELOAD_CONDITION_PREF_PREFIX + "userAgent";
  Services.prefs.setBoolPref(pref, enabled);
}

function reloadOnTouchChange(enabled) {
  const pref = RELOAD_CONDITION_PREF_PREFIX + "touchSimulation";
  Services.prefs.setBoolPref(pref, enabled);
}

function rotateViewport(ui) {
  const { document } = ui.toolWindow;
  const rotateButton = document.getElementById("rotate-button");
  rotateButton.click();
}

// Call this to switch between on/off support for meta viewports.
async function setTouchAndMetaViewportSupport(ui, value) {
  await ui.updateTouchSimulation(value);
  info("Reload so the new configuration applies cleanly to the page");
  await reloadSelectedTab();

  await promiseContentReflow(ui);
}

// This function checks that zoom, the initial containing block width and height
// are all as expected.
async function testViewportZoomWidthAndHeight(msg, ui, zoom, width, height) {
  if (typeof zoom !== "undefined") {
    const resolution = await spawnViewportTask(ui, {}, function () {
      return content.windowUtils.getResolution();
    });
    is(resolution, zoom, msg + " should have expected zoom.");
  }

  if (typeof width !== "undefined" || typeof height !== "undefined") {
    const innerSize = await spawnViewportTask(ui, {}, function () {
      return {
        width: content.document.documentElement.clientWidth,
        height: content.document.documentElement.clientHeight,
      };
    });
    if (typeof width !== "undefined") {
      is(innerSize.width, width, msg + " should have expected inner width.");
    }
    if (typeof height !== "undefined") {
      is(innerSize.height, height, msg + " should have expected inner height.");
    }
  }
}

function promiseContentReflow(ui) {
  return SpecialPowers.spawn(ui.getViewportBrowser(), [], async function () {
    return new Promise(resolve => {
      content.window.requestAnimationFrame(() => {
        content.window.requestAnimationFrame(resolve);
      });
    });
  });
}

// This function returns a promise that will be resolved when the
// RDM zoom has been set and the content has finished rescaling
// to the new size.
async function promiseRDMZoom(ui, browser, zoom) {
  const currentZoom = ZoomManager.getZoomForBrowser(browser);
  if (currentZoom.toFixed(2) == zoom.toFixed(2)) {
    return;
  }

  const width = browser.getBoundingClientRect().width;

  ZoomManager.setZoomForBrowser(browser, zoom);

  // RDM resizes the browser as a result of a zoom change, so we wait for that.
  //
  // This also has the side effect of updating layout which ensures that any
  // remote frame dimension update message gets there in time.
  await TestUtils.waitForCondition(function () {
    return browser.getBoundingClientRect().width != width;
  });
}

async function waitForDeviceAndViewportState(ui) {
  const { store } = ui.toolWindow;

  // Wait until the viewport has been added and the device list has been loaded
  await waitUntilState(
    store,
    state =>
      state.viewports.length == 1 &&
      state.devices.listState == localTypes.loadableState.LOADED
  );
}

/**
 * Wait for the content page to be rendered with the expected pixel ratio.
 *
 * @param {ResponsiveUI} ui
 *        The ResponsiveUI instance.
 * @param {Integer} expected
 *        The expected dpr for the content page.
 * @param {object} options
 * @param {boolean} options.waitForTargetConfiguration
 *        If set to true, the function will wait for the targetConfigurationCommand configuration
 *        to reflect the ratio that was set. This can be used to prevent pending requests
 *        to the actor.
 */
async function waitForDevicePixelRatio(
  ui,
  expected,
  { waitForTargetConfiguration } = {}
) {
  const dpx = await SpecialPowers.spawn(
    ui.getViewportBrowser(),
    [{ expected }],
    function (args) {
      const getDpr = function () {
        return content.browsingContext.overrideDPPX || content.devicePixelRatio;
      };
      const initial = getDpr();
      info(
        `Listening for pixel ratio change ` +
          `(current: ${initial}, expected: ${args.expected})`
      );
      return new Promise(resolve => {
        const mql = content.matchMedia(`(resolution: ${args.expected}dppx)`);
        if (mql.matches) {
          info(`Ratio already changed to ${args.expected}dppx`);
          resolve(getDpr());
          return;
        }
        mql.addListener(function listener() {
          info(`Ratio changed to ${args.expected}dppx`);
          mql.removeListener(listener);
          resolve(getDpr());
        });
      });
    }
  );

  if (waitForTargetConfiguration) {
    // Ensure the configuration was updated so we limit the risk of the client closing before
    // the server sent back the result of the updateConfiguration call.
    await waitFor(() => {
      return (
        ui.commands.targetConfigurationCommand.configuration.overrideDPPX ===
        expected
      );
    });
  }

  return dpx;
}

const { TabGroupTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TabGroupTestUtils.sys.mjs"
);

async function expectFocusAfterKey(expectedActiveElement, keyName, keyOptions) {
  let focused = BrowserTestUtils.waitForEvent(expectedActiveElement, "focus");
  EventUtils.synthesizeKey(keyName, keyOptions);
  await focused;
  Assert.equal(
    document.activeElement,
    expectedActiveElement,
    `After ${keyName}${keyOptions?.shiftKey ? " (Shift)" : ""}, the expected element has focus`
  );
}

function updateTabContextMenu(tab) {
  let menu = document.getElementById("tabContextMenu");
  if (!tab) {
    tab = gBrowser.selectedTab;
  }
  var evt = new Event("");
  tab.dispatchEvent(evt);
  menu.openPopup(tab, "end_after", 0, 0, true, false, evt);
  is(
    window.TabContextMenu.contextTab,
    tab,
    "TabContextMenu context is the expected tab"
  );
  menu.hidePopup();
}

// Open the tab context menu for `tab` and leave it open, resolving to the
// #tabContextMenu element once shown. Pair with closeTabContextMenu().
async function openTabContextMenu(tab = gBrowser.selectedTab) {
  const tabContextMenu = document.getElementById("tabContextMenu");
  const contextMenuShown = BrowserTestUtils.waitForPopupEvent(
    tabContextMenu,
    "shown"
  );
  tab.scrollIntoView({ behavior: "instant" });
  EventUtils.synthesizeMouseAtCenter(
    tab,
    { type: "contextmenu", button: 2 },
    window
  );
  await contextMenuShown;
  return tabContextMenu;
}

async function closeTabContextMenu(
  menu = document.getElementById("tabContextMenu")
) {
  const contextMenuHidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.hidePopup();
  await contextMenuHidden;
}

function triggerClickOn(target, options) {
  let promise = BrowserTestUtils.waitForEvent(target, "click");
  if (AppConstants.platform == "macosx") {
    options = {
      metaKey: options.ctrlKey,
      shiftKey: options.shiftKey,
    };
  }
  EventUtils.synthesizeMouseAtCenter(target, options);
  return promise;
}

function triggerMiddleClickOn(target) {
  let promise = BrowserTestUtils.waitForEvent(target, "click");
  EventUtils.synthesizeMouseAtCenter(target, { button: 1 });
  return promise;
}

async function addTab(url = "http://mochi.test:8888/", params) {
  return addTabTo(gBrowser, url, params);
}

async function addTabTo(
  targetBrowser,
  url = "http://mochi.test:8888/",
  params = {}
) {
  params.skipAnimation = true;
  const tab = BrowserTestUtils.addTab(targetBrowser, url, params);
  const browser = targetBrowser.getBrowserForTab(tab);
  await BrowserTestUtils.browserLoaded(browser, { wantLoad: url });
  return tab;
}

async function addMediaTab() {
  const PAGE =
    "https://example.com/browser/browser/components/tabbrowser/test/browser/tabs/file_mediaPlayback.html";
  const tab = BrowserTestUtils.addTab(gBrowser, PAGE, { skipAnimation: true });
  const browser = gBrowser.getBrowserForTab(tab);
  await BrowserTestUtils.browserLoaded(browser);
  return tab;
}

function muted(tab) {
  return tab.linkedBrowser.audioMuted;
}

function activeMediaBlocked(tab) {
  return tab.activeMediaBlocked;
}

async function toggleMuteAudio(tab, expectMuted) {
  let mutedPromise = get_wait_for_mute_promise(tab, expectMuted);
  tab.toggleMuteAudio();
  await mutedPromise;
}

async function pressIcon(icon) {
  let tooltip = document.getElementById("tabbrowser-tab-tooltip");
  await hover_icon(icon, tooltip);
  EventUtils.synthesizeMouseAtCenter(icon, { button: 0 });
  leave_icon(icon);
}

async function wait_for_tab_playing_event(tab, expectPlaying) {
  if (tab.soundPlaying == expectPlaying) {
    ok(true, "The tab should " + (expectPlaying ? "" : "not ") + "be playing");
    return true;
  }
  return BrowserTestUtils.waitForEvent(tab, "TabAttrModified", false, event => {
    if (event.detail.changed.includes("soundplaying")) {
      is(
        tab.hasAttribute("soundplaying"),
        expectPlaying,
        "The tab should " + (expectPlaying ? "" : "not ") + "be playing"
      );
      is(
        tab.soundPlaying,
        expectPlaying,
        "The tab should " + (expectPlaying ? "" : "not ") + "be playing"
      );
      return true;
    }
    return false;
  });
}

async function wait_for_tab_media_blocked_event(tab, expectMediaBlocked) {
  if (tab.activeMediaBlocked == expectMediaBlocked) {
    ok(
      true,
      "The tab should " +
        (expectMediaBlocked ? "" : "not ") +
        "be activemedia-blocked"
    );
    return true;
  }
  return BrowserTestUtils.waitForEvent(tab, "TabAttrModified", false, event => {
    if (event.detail.changed.includes("activemedia-blocked")) {
      is(
        tab.hasAttribute("activemedia-blocked"),
        expectMediaBlocked,
        "The tab should " +
          (expectMediaBlocked ? "" : "not ") +
          "be activemedia-blocked"
      );
      is(
        tab.activeMediaBlocked,
        expectMediaBlocked,
        "The tab should " +
          (expectMediaBlocked ? "" : "not ") +
          "be activemedia-blocked"
      );
      return true;
    }
    return false;
  });
}

async function is_audio_playing(tab) {
  let browser = tab.linkedBrowser;
  let isPlaying = await SpecialPowers.spawn(browser, [], async function () {
    let audio = content.document.querySelector("audio");
    return !audio.paused;
  });
  return isPlaying;
}

async function play(tab, expectPlaying = true) {
  let browser = tab.linkedBrowser;
  await SpecialPowers.spawn(browser, [], async function () {
    let audio = content.document.querySelector("audio");
    audio.play();
  });

  // If the tab has already been muted, it means the tab won't get soundplaying,
  // so we don't need to check this attribute.
  if (browser.audioMuted) {
    return;
  }

  if (expectPlaying) {
    await wait_for_tab_playing_event(tab, true);
  } else {
    await wait_for_tab_media_blocked_event(tab, true);
  }
}

function disable_non_test_mouse(disable) {
  let utils = window.windowUtils;
  utils.disableNonTestMouseEvents(disable);
}

function hover_icon(icon, tooltip) {
  disable_non_test_mouse(true);

  let popupShownPromise = BrowserTestUtils.waitForEvent(tooltip, "popupshown");
  EventUtils.synthesizeMouse(icon, 1, 1, { type: "mouseover" });
  EventUtils.synthesizeMouse(icon, 2, 2, { type: "mousemove" });
  EventUtils.synthesizeMouse(icon, 3, 3, { type: "mousemove" });
  EventUtils.synthesizeMouse(icon, 4, 4, { type: "mousemove" });
  return popupShownPromise;
}

function leave_icon(icon) {
  EventUtils.synthesizeMouse(icon, 0, 0, { type: "mouseout" });
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {
    type: "mousemove",
  });
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {
    type: "mousemove",
  });
  EventUtils.synthesizeMouseAtCenter(document.documentElement, {
    type: "mousemove",
  });

  disable_non_test_mouse(false);
}

// The set of tabs which have ever had their mute state changed.
// Used to determine whether the tab should have a muteReason value.
let everMutedTabs = new WeakSet();

function get_wait_for_mute_promise(tab, expectMuted) {
  return BrowserTestUtils.waitForEvent(tab, "TabAttrModified", false, event => {
    if (
      event.detail.changed.includes("muted") ||
      event.detail.changed.includes("activemedia-blocked")
    ) {
      is(
        tab.hasAttribute("muted"),
        expectMuted,
        "The tab should " + (expectMuted ? "" : "not ") + "be muted"
      );
      is(
        tab.muted,
        expectMuted,
        "The tab muted property " + (expectMuted ? "" : "not ") + "be true"
      );

      if (expectMuted || everMutedTabs.has(tab)) {
        everMutedTabs.add(tab);
        is(tab.muteReason, null, "The tab should have a null muteReason value");
      } else {
        is(
          tab.muteReason,
          undefined,
          "The tab should have an undefined muteReason value"
        );
      }
      return true;
    }
    return false;
  });
}

async function test_mute_tab(tab, icon, expectMuted) {
  let mutedPromise = get_wait_for_mute_promise(tab, expectMuted);

  let activeTab = gBrowser.selectedTab;

  let tooltip = document.getElementById("tabbrowser-tab-tooltip");

  await hover_icon(icon, tooltip);
  EventUtils.synthesizeMouseAtCenter(icon, { button: 0 });
  leave_icon(icon);

  is(
    gBrowser.selectedTab,
    activeTab,
    "Clicking on mute should not change the currently selected tab"
  );

  // If the audio is playing, we should check whether clicking on icon affects
  // the media element's playing state.
  let isAudioPlaying = await is_audio_playing(tab);
  if (isAudioPlaying) {
    await wait_for_tab_playing_event(tab, !expectMuted);
  }

  return mutedPromise;
}

async function dragAndDrop(
  tab1,
  tab2,
  copy = false,
  destWindow = window,
  afterTab = true,
  origWindow = window
) {
  let rect = tab2.getBoundingClientRect();
  let event = {
    ctrlKey: copy,
    altKey: copy,
    clientX: rect.left + rect.width / 2 + (afterTab ? 1 : -1),
    clientY: rect.top + rect.height / 2 + (afterTab ? 1 : -1),
  };

  if (destWindow != origWindow) {
    // Make sure that both tab1 and tab2 are visible
    await SimpleTest.promiseFocus(origWindow);
    origWindow.moveTo(rect.left, rect.top + rect.height * 3);
  }

  let originalIndex = tab1.elementIndex;
  EventUtils.synthesizeDrop(
    tab1,
    tab2,
    null,
    copy ? "copy" : "move",
    origWindow,
    destWindow,
    event
  );
  // Ensure dnd suppression is cleared.
  EventUtils.synthesizeMouseAtCenter(tab2, { type: "mouseup" }, destWindow);
  if (!copy && destWindow == origWindow) {
    await TestUtils.waitForCondition(() => {
      return tab1.elementIndex != originalIndex;
    }, "Waiting for tab position to be updated");
  } else if (destWindow != origWindow) {
    await TestUtils.waitForCondition(
      () => tab1.closing,
      "Waiting for tab closing"
    );
  }
}

function getUrl(tab) {
  return tab.linkedBrowser.currentURI.spec;
}

/**
 * Takes a xul:browser and makes sure that the remoteTypes for the browser in
 * both the parent and the child processes are the same.
 *
 * @param {xul:browser} browser
 *        A xul:browser.
 * @param {string} expectedRemoteType
 *        The expected remoteType value for the browser in both the parent
 *        and child processes.
 * @param {optional string} message
 *        If provided, shows this string as the message when remoteType values
 *        do not match. If not present, it uses the default message defined
 *        in the function parameters.
 */
function checkBrowserRemoteType(
  browser,
  expectedRemoteType,
  message = `Ensures that tab runs in the ${expectedRemoteType} content process.`
) {
  // Check both parent and child to ensure that they have the correct remoteType.
  if (expectedRemoteType == E10SUtils.WEB_REMOTE_TYPE) {
    ok(E10SUtils.isWebRemoteType(browser.remoteType), message);
    ok(
      E10SUtils.isWebRemoteType(browser.messageManager.remoteType),
      "Parent and child process should agree on the remote type."
    );
  } else {
    is(browser.remoteType, expectedRemoteType, message);
    is(
      browser.messageManager.remoteType,
      expectedRemoteType,
      "Parent and child process should agree on the remote type."
    );
  }
}

function test_url_for_process_types({
  url,
  chromeResult,
  webContentResult,
  privilegedAboutContentResult,
  privilegedMozillaContentResult,
  extensionProcessResult,
}) {
  const PROCESSES = [
    [E10SUtils.NOT_REMOTE, chromeResult, "chrome process"],
    [E10SUtils.WEB_REMOTE_TYPE, webContentResult, "web content process"],
    [
      E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE,
      privilegedAboutContentResult,
      "privileged about content process",
    ],
    [
      E10SUtils.PRIVILEGEDMOZILLA_REMOTE_TYPE,
      privilegedMozillaContentResult,
      "privileged mozilla content process",
    ],
    [
      E10SUtils.EXTENSION_REMOTE_TYPE,
      extensionProcessResult,
      "extension process",
    ],
  ];
  const EXTRAS = [
    ["", "URL"],
    ["#foo", "URL with ref"],
    ["?foo", "URL with query"],
    ["?foo#bar", "URL with query and ref"],
  ];

  for (let [extra, extraDesc] of EXTRAS) {
    for (let [remoteType, canLoad, remoteTypeDesc] of PROCESSES) {
      let description = `Check ${extraDesc} in ${remoteTypeDesc}.`;

      // Run the predictRemoteTypeForURI algorithm with preferredRemoteType set
      // to each process type. This is a rough approximation of whether or not
      // it is possible for a load started in that process to finish in that
      // process according to the predictor.
      let prediction = ChromeUtils.predictRemoteTypeForURI(url + extra, {
        useRemoteSubframes: false,
        preferredRemoteType: remoteType,
      });
      if (canLoad) {
        is(prediction, remoteType, description);
      } else {
        isnot(prediction, remoteType, description);
      }
    }
  }
}

/*
 * Get a file URL for the local file name.
 */
function fileURL(filename) {
  let ifile = getChromeDir(getResolvedURI(gTestPath));
  ifile.append(filename);
  return Services.io.newFileURI(ifile).spec;
}

/*
 * Get a http URL for the local file name.
 */
function httpURL(filename, host = "https://example.com/") {
  let root = getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content/",
    host
  );
  return root + filename;
}

function loadTestSubscript(filePath) {
  Services.scriptloader.loadSubScript(new URL(filePath, gTestPath).href, this);
}

/**
 * Removes a tab group (along with its tabs). Resolves when the tab group
 * is gone.
 *
 * @param {MozTabbrowserTabGroup} group
 * @returns {Promise<void>}
 */
async function removeTabGroup(group) {
  return TabGroupTestUtils.removeTabGroup(group);
}

/**
 * @param {Node} triggerNode
 * @param {string} contextMenuId
 * @returns {Promise<XULMenuElement|XULPopupElement>}
 */
async function getContextMenu(triggerNode, contextMenuId) {
  let win = triggerNode.documentGlobal;
  triggerNode.scrollIntoView({ behavior: "instant" });
  const contextMenu = win.document.getElementById(contextMenuId);
  const contextMenuShown = BrowserTestUtils.waitForPopupEvent(
    contextMenu,
    "shown"
  );

  EventUtils.synthesizeMouseAtCenter(
    triggerNode,
    { type: "contextmenu", button: 2 },
    win
  );
  await contextMenuShown;
  return contextMenu;
}

/**
 * @param {XULMenuElement|XULPopupElement} contextMenu
 * @returns {Promise<void>}
 */
async function closeContextMenu(contextMenu) {
  let menuHidden = BrowserTestUtils.waitForPopupEvent(contextMenu, "hidden");
  contextMenu.hidePopup();
  await menuHidden;
}

/**
 * Loads two tabs in a split view, executes a task, then closes the split view.
 *
 * @param {MozTabbrowserTab} tab1
 *   The first tab in the split view. It will be selected.
 * @param {MozTabbrowserTab} tab2
 *   The second tab in the split view.
 * @param {Function} taskFn
 */
async function withSplitView(tab1, tab2, taskFn) {
  await BrowserTestUtils.switchTab(gBrowser, tab1);
  const splitView = gBrowser.addTabSplitView([tab1, tab2], {
    insertBefore: tab1,
  });
  const splitter = gBrowser.tabpanels.splitViewSplitter;
  await BrowserTestUtils.waitForMutationCondition(
    splitter,
    { attributes: true },
    () => !splitter.hidden
  );
  await taskFn({ tab1, tab2, splitter, splitView });
  // Closes both tabs.
  splitView.close();
}

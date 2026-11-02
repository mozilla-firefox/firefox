/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * @import { SettingControl } from "chrome://browser/content/preferences/widgets/setting-control.mjs"
 * @import { Setting } from "chrome://global/content/preferences/Setting.mjs"
 */

const { EnterprisePolicyTesting, PoliciesPrefTracker } =
  ChromeUtils.importESModule(
    "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
  );

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);
const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

ChromeUtils.defineLazyGetter(this, "QuickSuggestTestUtils", () => {
  const { QuickSuggestTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/QuickSuggestTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineESModuleGetters(this, {
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

NimbusTestUtils.init(this);

const kDefaultWait = 2000;

function is_element_visible(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  ok(!BrowserTestUtils.isHidden(aElement), aMsg);
}

function is_element_hidden(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  ok(BrowserTestUtils.isHidden(aElement), aMsg);
}

/**
 * Asserts that an element exposes the given accessible role and a non-empty
 * accessible name.
 *
 * @param {Element} el
 * @param {number} role Expected `Ci.nsIAccessibleRole` value.
 */
async function assertAccessibleRoleAndName(el, role) {
  ok(el.getAttribute("aria-label"), "Element has a non-empty aria-label");

  let accService = Cc["@mozilla.org/accessibilityService;1"].getService(
    Ci.nsIAccessibilityService
  );
  let accessible;
  await TestUtils.waitForCondition(() => {
    accessible = accService.getAccessibleFor(el);
    return accessible && accessible.role == role;
  }, "Accessible reflects the expected role");
  is(accessible.role, role, "Accessible has the expected role");
  ok(accessible.name, "Accessible has a non-empty name");
}

async function assertRadioGroupAccessibleName(radioGroup) {
  ok(radioGroup, "Radio group exists");

  let doc = radioGroup.ownerDocument;
  await doc.l10n.ready;

  await doc.l10n.translateFragment(radioGroup);
  await radioGroup.updateComplete;
  await radioGroup.fieldset.updateComplete;

  let innerFieldset = radioGroup.fieldset.shadowRoot.querySelector("fieldset");
  ok(innerFieldset, "Radio group has an inner fieldset");
  is(
    innerFieldset.getAttribute("role"),
    "radiogroup",
    "Inner fieldset has the radiogroup role"
  );
  await assertAccessibleRoleAndName(
    innerFieldset,
    Ci.nsIAccessibleRole.ROLE_RADIO_GROUP
  );
}

/**
 * Asserts that a moz-box-group exposes a listbox with a localized
 * accessible name.
 *
 * @param {Element} boxGroup
 */
async function assertBoxGroupHasListboxAccessibleName(boxGroup) {
  ok(boxGroup, "Box group exists");

  let doc = boxGroup.ownerDocument;
  await doc.l10n.ready;

  await doc.l10n.translateFragment(boxGroup);
  await boxGroup.updateComplete;

  let listbox = boxGroup.shadowRoot.querySelector("[role='listbox']");
  ok(listbox, "Box group has an inner listbox");
  await assertAccessibleRoleAndName(listbox, Ci.nsIAccessibleRole.ROLE_LISTBOX);
}

async function assertReorderableListAccessibleName(boxGroup) {
  Assert.ok(boxGroup, "Production box group exists");
  Assert.ok(
    BrowserTestUtils.isVisible(boxGroup),
    "Production box group is visible"
  );

  await boxGroup.updateComplete;
  Assert.equal(boxGroup.type, "reorderable-list", "Group is reorderable");
  await boxGroup.reorderableList.updateComplete;

  let list = boxGroup.shadowRoot.querySelector("ol");
  Assert.ok(list, "Production group renders an inner ordered list");
  Assert.ok(BrowserTestUtils.isVisible(list), "Inner list is visible");

  let accService = Cc["@mozilla.org/accessibilityService;1"].getService(
    Ci.nsIAccessibilityService
  );
  let accessible;
  await TestUtils.waitForCondition(() => {
    accessible = accService.getAccessibleFor(list);
    return !!accessible;
  }, `${boxGroup.id}: list accessible exists`);

  Assert.ok(
    list.getAttribute("aria-label")?.trim(),
    `${boxGroup.id}: inner list has a nonempty aria-label`
  );
  Assert.ok(
    accessible.name?.trim(),
    `${boxGroup.id}: accessible has a nonempty name`
  );
}

/**
 * Opens a fresh preferences tab at the given pane and waits for it to
 * fully initialize before returning. Pre-registers the "Initialized" listener
 * before navigation starts to avoid the race where the event fires before
 * the listener is attached.
 *
 * @param {string} [pane] Fragment to append (e.g. "appearance"). Omit or
 *   pass "" to open the default pane.
 * @returns {Promise<MozTabbrowserTab>} The opened tab.
 */
async function openPrefsTab(pane) {
  let url = "about:preferences" + (pane ? "#" + pane : "");
  let tab = BrowserTestUtils.addTab(gBrowser, url);
  let initialized = BrowserTestUtils.waitForEvent(
    tab.linkedBrowser,
    "Initialized",
    true
  );
  gBrowser.selectedTab = tab;
  await initialized;
  return tab;
}

function open_preferences(aCallback) {
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, "about:preferences");
  let newTabBrowser = gBrowser.getBrowserForTab(gBrowser.selectedTab);
  newTabBrowser.addEventListener(
    "Initialized",
    function () {
      aCallback(gBrowser.contentWindow);
    },
    { capture: true, once: true }
  );
}

function openAndLoadSubDialog(
  aURL,
  aFeatures = null,
  aParams = null,
  aClosingCallback = null
) {
  let promise = promiseLoadSubDialog(aURL);
  content.gSubDialog.open(
    aURL,
    { features: aFeatures, closingCallback: aClosingCallback },
    aParams
  );
  return promise;
}

function promiseLoadSubDialog(aURL) {
  return new Promise(resolve => {
    content.gSubDialog._dialogStack.addEventListener(
      "dialogopen",
      function dialogopen(aEvent) {
        if (
          aEvent.detail.dialog._frame.contentWindow.location == "about:blank"
        ) {
          return;
        }
        content.gSubDialog._dialogStack.removeEventListener(
          "dialogopen",
          dialogopen
        );

        is(
          aEvent.detail.dialog._frame.contentWindow.location.toString(),
          aURL,
          "Check the proper URL is loaded"
        );

        // Check visibility
        is_element_visible(aEvent.detail.dialog._overlay, "Overlay is visible");

        // Check that stylesheets were injected
        let expectedStyleSheetURLs =
          aEvent.detail.dialog._injectedStyleSheets.slice(0);
        for (let styleSheet of aEvent.detail.dialog._frame.contentDocument
          .styleSheets) {
          let i = expectedStyleSheetURLs.indexOf(styleSheet.href);
          if (i >= 0) {
            info("found " + styleSheet.href);
            expectedStyleSheetURLs.splice(i, 1);
          }
        }
        is(
          expectedStyleSheetURLs.length,
          0,
          "All expectedStyleSheetURLs should have been found"
        );

        // Wait for the next event tick to make sure the remaining part of the
        // testcase runs after the dialog gets ready for input.
        executeSoon(() => resolve(aEvent.detail.dialog._frame.contentWindow));
      }
    );
  });
}

async function openPreferencesViaOpenPreferencesAPI(aPane, aOptions) {
  let finalPaneEvent = Services.prefs.getBoolPref("identity.fxaccounts.enabled")
    ? "sync-pane-loaded"
    : "privacy-pane-loaded";
  let finalPrefPaneLoaded = TestUtils.topicObserved(finalPaneEvent, () => true);
  gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    allowInheritPrincipal: true,
  });
  openPreferences(aPane, aOptions);
  let newTabBrowser = gBrowser.selectedBrowser;

  if (!newTabBrowser.contentWindow) {
    await BrowserTestUtils.waitForEvent(newTabBrowser, "Initialized", true);
    if (newTabBrowser.contentDocument.readyState != "complete") {
      await BrowserTestUtils.waitForEvent(newTabBrowser.contentWindow, "load");
    }
    await finalPrefPaneLoaded;
  }

  let win = gBrowser.contentWindow;
  let selectedPane = win.gLastCategory?.category;
  if (!aOptions || !aOptions.leaveOpen) {
    gBrowser.removeCurrentTab();
  }
  return { selectedPane };
}

async function runSearchInput(input) {
  let searchInput = gBrowser.contentDocument.getElementById("searchInput");
  searchInput.focus();
  let searchCompletedPromise = BrowserTestUtils.waitForEvent(
    gBrowser.contentWindow,
    "PreferencesSearchCompleted",
    evt => evt.detail == input
  );
  EventUtils.sendString(input);
  await searchCompletedPromise;
}

async function clearSearch(doc) {
  let searchInput = doc.getElementById("searchInput");
  searchInput.focus();
  let searchCompletedPromise = BrowserTestUtils.waitForEvent(
    gBrowser.contentWindow,
    "PreferencesSearchCompleted",
    evt => evt.detail == ""
  );
  let count = searchInput.value.length;
  while (count--) {
    EventUtils.sendKey("BACK_SPACE");
  }
  await searchCompletedPromise;
}

async function evaluateSearchResults(
  keyword,
  searchResults,
  includeExperiments = false
) {
  searchResults = Array.isArray(searchResults)
    ? searchResults
    : [searchResults];
  searchResults.push("header-searchResults");

  await runSearchInput(keyword);

  let mainPrefTag = gBrowser.contentDocument.getElementById("mainPrefPane");
  for (let i = 0; i < mainPrefTag.childElementCount; i++) {
    let child = mainPrefTag.children[i];
    if (!includeExperiments && child.id?.startsWith("pane-experimental")) {
      continue;
    }
    if (child.localName == "setting-group") {
      // Skip migrated setting-groups to avoid interference with legacy tests
      if (child.hasAttribute("data-srd-migrated")) {
        continue;
      }
      if (searchResults.includes(child.groupId)) {
        is_element_visible(
          child,
          `${child.groupId} should be in search results`
        );
      } else {
        is_element_hidden(
          child,
          `${child.groupId} should not be in search results`
        );
      }
    } else if (searchResults.includes(child.id)) {
      is_element_visible(child, `${child.id} should be in search results`);
    } else if (child.id) {
      is_element_hidden(child, `${child.id} should not be in search results`);
    }
  }
}

function waitForMutation(target, opts, cb) {
  return new Promise(resolve => {
    let observer = new MutationObserver(() => {
      if (!cb || cb(target)) {
        observer.disconnect();
        resolve();
      }
    });
    observer.observe(target, opts);
  });
}

/**
 * Creates observer that waits for and then compares all perm-changes with the observances in order.
 *
 * @param {Array} observances permission changes to observe (order is important)
 * @returns {Promise} Promise object that resolves once all permission changes have been observed
 */
function createObserveAllPromise(observances) {
  // Create new promise that resolves once all items
  // in observances array have been observed.
  return new Promise(resolve => {
    let permObserver = {
      observe(aSubject, aTopic, aData) {
        if (aTopic != "perm-changed") {
          return;
        }

        if (!observances.length) {
          // See bug 1063410
          return;
        }

        let permission = aSubject.QueryInterface(Ci.nsIPermission);
        let expected = observances.shift();

        info(
          `observed perm-changed for ${permission.principal.origin} (remaining ${observances.length})`
        );

        is(aData, expected.data, "type of message should be the same");
        for (let prop of ["type", "capability", "expireType"]) {
          if (expected[prop]) {
            is(
              permission[prop],
              expected[prop],
              `property: "${prop}" should be equal (${permission.principal.origin})`
            );
          }
        }

        if (expected.origin) {
          is(
            permission.principal.origin,
            expected.origin,
            `property: "origin" should be equal (${permission.principal.origin})`
          );
        }

        if (!observances.length) {
          Services.obs.removeObserver(permObserver, "perm-changed");
          executeSoon(resolve);
        }
      },
    };
    Services.obs.addObserver(permObserver, "perm-changed");
  });
}

/**
 * Waits for preference to be set and asserts the value.
 *
 * @param {string} pref - Preference key.
 * @param {*} expectedValue - Expected value of the preference.
 * @param {string} message - Assertion message.
 */
async function waitForAndAssertPrefState(pref, expectedValue, message) {
  await TestUtils.waitForPrefChange(pref, value => {
    if (value != expectedValue) {
      return false;
    }
    is(value, expectedValue, message);
    return true;
  });
}

/**
 * Select the given history mode via dropdown in the privacy pane.
 *
 * @param {Window} win - The preferences window which contains the
 * dropdown.
 * @param {string} value - The history mode to select.
 */
async function selectHistoryMode(win, value) {
  let historyMode = win.document.querySelector(
    "setting-group[groupid='history2'] #historyMode"
  );
  let updated = waitForSettingControlChange(historyMode);

  let optionItems = Array.from(historyMode.children);
  let targetItem = optionItems.find(option => option.value == value);
  if (!targetItem) {
    throw new Error(
      "Could not find history mode popup item for value: " + value
    );
  }

  if (historyMode.value == value) {
    return;
  }

  targetItem.click();
  await updated;
}

async function updateCheckBoxElement(checkbox, value) {
  ok(checkbox, "the " + checkbox.id + " checkbox should exist");
  is_element_visible(
    checkbox,
    "the " + checkbox.id + " checkbox should be visible"
  );

  // No need to click if we're already in the desired state.
  if (checkbox.checked === value) {
    return;
  }

  // Scroll into view for click to succeed.
  checkbox.scrollIntoView();

  // Toggle the state.
  EventUtils.synthesizeMouseAtCenter(checkbox, {}, checkbox.documentGlobal);
}

async function updateCheckBox(win, id, value) {
  let checkbox = win.document.getElementById(id);
  ok(checkbox, "the " + id + " checkbox should exist");
  is_element_visible(checkbox, "the " + id + " checkbox should be visible");

  // No need to click if we're already in the desired state.
  if (checkbox.checked === value) {
    return;
  }

  // Scroll into view for click to succeed.
  checkbox.scrollIntoView();

  // Toggle the state.
  EventUtils.synthesizeMouseAtCenter(checkbox, {}, checkbox.documentGlobal);
}

/**
 * @param {Setting} setting The setting to wait on.
 * @param {() => any} [triggerFn]
 * An optional function to call that will trigger the change.
 */
function waitForSettingChange(setting, triggerFn) {
  let changePromise = new Promise(resolve => {
    setting.on("change", function handler() {
      setting.off("change", handler);
      resolve();
    });
  });
  if (triggerFn) {
    triggerFn();
  }
  return changePromise;
}

async function waitForSettingControlChange(control) {
  await waitForSettingChange(control.setting);
  await new Promise(resolve => requestAnimationFrame(resolve));
}

/**
 * Wait for the current setting pane to change.
 *
 * @param {string} paneId
 * @param {Window} [win] The window to check, defaults to current window.
 */
async function waitForPaneChange(
  paneId,
  win = gBrowser.selectedBrowser.contentWindow
) {
  let event = await BrowserTestUtils.waitForEvent(win.document, "paneshown");
  let expectId = paneId.startsWith("pane")
    ? paneId
    : `pane${paneId[0].toUpperCase()}${paneId.substring(1)}`;
  is(event.detail.category, expectId, "Loaded the correct pane");
}

/**
 * Navigate an already-open preferences window to the named pane via
 * `location.hash`. No-op if the pane is already the current pane (e.g.
 * legacy chrome already on paneGeneral).
 *
 * @param {string} paneName - Unprefixed pane name (e.g. "tabsBrowsing")
 * @param {Window} [win] - Window to navigate (defaults to selected tab)
 */
async function maybeNavigateToPane(
  paneName,
  win = gBrowser.selectedBrowser.contentWindow
) {
  if (win.history.state?.category === paneName) {
    return;
  }
  const paneShown = waitForPaneChange(paneName, win);
  win.location.hash = `#${paneName}`;
  await paneShown;
}

/**
 * Get a reference to the setting-control for a specific setting ID.
 *
 * @param {string} settingId The setting ID
 * @param {Window} [win] The window to check, defaults to current window.
 * @returns {SettingControl}
 */
function getSettingControl(
  settingId,
  win = gBrowser.selectedBrowser.contentWindow
) {
  return win.document.getElementById(`setting-control-${settingId}`);
}

/**
 * Waits for a setting control to render and complete any async updates.
 *
 * @param {string} settingId - The setting identifier.
 * @param {Window} [win] - Optional window, defaults to current browser window.
 * @returns {Promise<Element>} The rendered setting control element.
 */
async function settingControlRenders(settingId, win) {
  await BrowserTestUtils.waitForMutationCondition(
    win.document.documentElement,
    { childList: true, subtree: true },
    () => !!getSettingControl(settingId, win)
  );
  let control = getSettingControl(settingId, win);
  if (control?.updateComplete) {
    await control.updateComplete;
  }
  return control;
}

function synthesizeClick(el) {
  let target = el.buttonEl ?? el.inputEl ?? el;
  target.scrollIntoView({ block: "center" });
  EventUtils.synthesizeMouseAtCenter(target, {}, target.documentGlobal);
}

async function changeMozSelectValue(selectEl, value) {
  let control = selectEl.control;
  let changePromise = waitForSettingControlChange(control);
  selectEl.value = value;
  selectEl.dispatchEvent(new Event("change", { bubbles: true }));
  await changePromise;
}

// Ensure each test leaves the sidebar in its initial state when it completes
const initialSidebarState = { ...SidebarController.getUIState(), command: "" };
registerCleanupFunction(async function () {
  const { ObjectUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/ObjectUtils.sys.mjs"
  );
  if (
    !ObjectUtils.deepEqual(SidebarController.getUIState(), initialSidebarState)
  ) {
    info("Restoring to initial sidebar state");
    await SidebarController.updateUIState(initialSidebarState);
  }
});
/**
 * Waits for a boolean preference to change to the expected value.
 *
 * @param {string} prefName - The preference name.
 * @param {boolean} expectedValue - The expected boolean value.
 * @returns {Promise} Promise that resolves when the pref reaches the expected value.
 */
async function waitForPrefChange(prefName, expectedValue) {
  return TestUtils.waitForCondition(
    () => Services.prefs.getBoolPref(prefName) === expectedValue,
    `Waiting for ${prefName} to be ${expectedValue}`
  );
}

/**
 * Opens preferences and registers a `testTopLevel` pane with a `testSubPane`
 * sub-pane. The default top-level pane has a single `moz-box-button` that
 * navigates to (and is wired to load via `loadPane`) the sub-pane. Callers
 * can override either group's `items` to add searchkeywords, swap the
 * control, etc.
 *
 * @param {object} [options]
 * @param {object[]} [options.parentItems]
 *   `items` for the top-level setting-group. Each item.id is registered as a
 *   basic Setting (with `testLoadSubPane` getting an onUserClick that
 *   navigates to the sub-pane).
 * @param {object[]} [options.subPaneItems]
 *   `items` for the sub-pane setting-group. Each item.id is registered as a
 *   basic Setting.
 * @returns {Promise<{ doc: Document, win: Window }>}
 */
async function setupTestSubPane({
  parentItems = [
    {
      id: "testLoadSubPane",
      control: "moz-box-button",
      loadPane: "testSubPane",
      controlAttrs: { label: "Top level setting" },
    },
  ],
  subPaneItems = [
    {
      id: "testSetting",
      controlAttrs: { label: "Test setting" },
    },
  ],
} = {}) {
  await openPreferencesViaOpenPreferencesAPI("sync", { leaveOpen: true });
  let doc = gBrowser.selectedBrowser.contentDocument;
  let win = doc.documentGlobal;

  win.Preferences.addSetting({
    id: "testLoadSubPane",
    onUserClick: () => win.gotoPref("paneTestSubPane"),
  });
  for (let item of [...parentItems, ...subPaneItems]) {
    if (item.id !== "testLoadSubPane") {
      win.Preferences.addSetting({ id: item.id, get: () => true });
    }
  }

  win.SettingGroupManager.registerGroup("testTopLevelGroup", {
    l10nId: "home-default-browser-title",
    headingLevel: 2,
    items: parentItems,
  });
  win.SettingPaneManager.registerPane("testTopLevel", {
    l10nId: "home-section",
    groupIds: ["testTopLevelGroup"],
  });
  let syncCategory = doc.getElementById("category-sync");
  let testTopLevelCategory = syncCategory.cloneNode(true);
  testTopLevelCategory.setAttribute("view", "paneTestTopLevel");
  syncCategory.insertAdjacentElement("afterend", testTopLevelCategory);

  win.SettingGroupManager.registerGroup("testSubGroup", {
    headingLevel: 2,
    items: subPaneItems,
  });
  win.SettingPaneManager.registerPane("testSubPane", {
    parent: "testTopLevel",
    l10nId: "containers-section-header2",
    groupIds: ["testSubGroup"],
  });

  let viewChanged = waitForPaneChange("paneTestTopLevel");
  win.gotoPref("paneTestTopLevel");
  await viewChanged;

  return { doc, win };
}

function performDragAndDrop({ contentWindow, dragItem, targetItem, position }) {
  dragItem.scrollIntoView({ behavior: "instant", block: "center" });
  EventUtils.startDragSession(contentWindow, "move");
  try {
    let [result, dataTransfer] = EventUtils.synthesizeDragOver(
      dragItem,
      targetItem,
      null,
      "move",
      contentWindow,
      contentWindow
    );

    let rect = targetItem.getBoundingClientRect();
    let threshold = rect.top + rect.height * 0.5;
    let dragEvent = {
      clientY: position === "before" ? threshold - 10 : threshold + 10,
    };

    EventUtils.synthesizeDropAfterDragOver(
      result,
      dataTransfer,
      targetItem,
      contentWindow,
      dragEvent
    );
  } finally {
    EventUtils._getDOMWindowUtils(contentWindow).dragSession?.endDragSession(
      true
    );
  }
}

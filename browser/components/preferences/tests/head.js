/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
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
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
});

const kDefaultWait = 2000;

function is_element_visible(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  ok(!BrowserTestUtils.isHidden(aElement), aMsg);
}

function is_element_hidden(aElement, aMsg) {
  isnot(aElement, null, "Element should not be null, when checking visibility");
  ok(BrowserTestUtils.isHidden(aElement), aMsg);
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
    await BrowserTestUtils.waitForEvent(newTabBrowser.contentWindow, "load");
    await finalPrefPaneLoaded;
  }

  let win = gBrowser.contentWindow;
  let selectedPane = win.history.state;
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
    if (searchResults.includes(child.id)) {
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
 * The Relay promo is not shown for distributions with a custom FxA instance,
 * since Relay requires an account on our own server. These prefs are set to a
 * dummy address by the test harness, filling the prefs with a "user value."
 * This temporarily sets the default value equal to the dummy value, so that
 * Firefox thinks we've configured the correct FxA server.
 * @returns {Promise<MockFxAUtilityFunctions>} { mock, unmock }
 */
async function mockDefaultFxAInstance() {
  /**
   * @typedef {Object} MockFxAUtilityFunctions
   * @property {function():void} mock - Makes the dummy values default, creating
   *                             the illusion of a production FxA instance.
   * @property {function():void} unmock - Restores the true defaults, creating
   *                             the illusion of a custom FxA instance.
   */

  const defaultPrefs = Services.prefs.getDefaultBranch("");
  const userPrefs = Services.prefs.getBranch("");
  const realAuth = defaultPrefs.getCharPref("identity.fxaccounts.auth.uri");
  const realRoot = defaultPrefs.getCharPref("identity.fxaccounts.remote.root");
  const mockAuth = userPrefs.getCharPref("identity.fxaccounts.auth.uri");
  const mockRoot = userPrefs.getCharPref("identity.fxaccounts.remote.root");
  const mock = () => {
    defaultPrefs.setCharPref("identity.fxaccounts.auth.uri", mockAuth);
    defaultPrefs.setCharPref("identity.fxaccounts.remote.root", mockRoot);
    userPrefs.clearUserPref("identity.fxaccounts.auth.uri");
    userPrefs.clearUserPref("identity.fxaccounts.remote.root");
  };
  const unmock = () => {
    defaultPrefs.setCharPref("identity.fxaccounts.auth.uri", realAuth);
    defaultPrefs.setCharPref("identity.fxaccounts.remote.root", realRoot);
    userPrefs.setCharPref("identity.fxaccounts.auth.uri", mockAuth);
    userPrefs.setCharPref("identity.fxaccounts.remote.root", mockRoot);
  };

  mock();
  registerCleanupFunction(unmock);

  return { mock, unmock };
}

/**
 * Runs a test that checks the visibility of the Firefox Suggest preferences UI.
 * An initial Suggest enabled status is set and visibility is checked. Then a
 * Nimbus experiment is installed that enables or disables Suggest and
 * visibility is checked again. Finally the page is reopened and visibility is
 * checked again.
 *
 * @param {boolean} initialSuggestEnabled
 *   Whether Suggest should be enabled initially.
 * @param {object} initialExpected
 *   The expected visibility after setting the initial enabled status. It should
 *   be an object that can be passed to `assertSuggestVisibility()`.
 * @param {object} nimbusVariables
 *   An object mapping Nimbus variable names to values.
 * @param {object} newExpected
 *   The expected visibility after installing the Nimbus experiment. It should
 *   be an object that can be passed to `assertSuggestVisibility()`.
 * @param {string} pane
 *   The pref pane to open.
 */
async function doSuggestVisibilityTest({
  initialSuggestEnabled,
  initialExpected,
  nimbusVariables,
  newExpected = initialExpected,
  pane = "search",
}) {
  info(
    "Running Suggest visibility test: " +
      JSON.stringify(
        {
          initialSuggestEnabled,
          initialExpected,
          nimbusVariables,
          newExpected,
        },
        null,
        2
      )
  );

  // Set the initial enabled status.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.quicksuggest.enabled", initialSuggestEnabled]],
  });

  // Open prefs and check the initial visibility.
  await openPreferencesViaOpenPreferencesAPI(pane, { leaveOpen: true });
  await assertSuggestVisibility(initialExpected);

  // Install a Nimbus experiment.
  await QuickSuggestTestUtils.withExperiment({
    valueOverrides: nimbusVariables,
    callback: async () => {
      // Check visibility again.
      await assertSuggestVisibility(newExpected);

      // To make sure visibility is properly updated on load, close the tab,
      // open the prefs again, and check visibility.
      gBrowser.removeCurrentTab();
      await openPreferencesViaOpenPreferencesAPI(pane, { leaveOpen: true });
      await assertSuggestVisibility(newExpected);
    },
  });

  gBrowser.removeCurrentTab();
  await SpecialPowers.popPrefEnv();
}

/**
 * Checks the visibility of the Suggest UI.
 *
 * @param {object} expectedByElementId
 *   An object that maps IDs of elements in the current tab to objects with the
 *   following properties:
 *
 *   {bool} isVisible
 *     Whether the element is expected to be visible.
 *   {string} l10nId
 *     The expected l10n ID of the element. Optional.
 */
async function assertSuggestVisibility(expectedByElementId) {
  let doc = gBrowser.selectedBrowser.contentDocument;
  for (let [elementId, { isVisible, l10nId }] of Object.entries(
    expectedByElementId
  )) {
    let element = doc.getElementById(elementId);
    await TestUtils.waitForCondition(
      () => BrowserTestUtils.isVisible(element) == isVisible,
      "Waiting for element visbility: " +
        JSON.stringify({ elementId, isVisible })
    );
    Assert.strictEqual(
      BrowserTestUtils.isVisible(element),
      isVisible,
      "Element should have expected visibility: " + elementId
    );
    if (l10nId) {
      Assert.equal(
        element.dataset.l10nId,
        l10nId,
        "The l10n ID should be correct for element: " + elementId
      );
    }
  }
}

const DEFAULT_LABS_RECIPES = [
  NimbusTestUtils.factories.recipe("nimbus-qa-1", {
    targeting: "true",
    isRollout: true,
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "experimental-features-auto-pip",
    firefoxLabsDescription: "experimental-features-auto-pip-description",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "experimental-features-group-customize-browsing",
    requiresRestart: false,
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "nimbus-qa-1",
            value: {
              value: "recipe-value-1",
            },
          },
        ],
      },
    ],
  }),

  NimbusTestUtils.factories.recipe("nimbus-qa-2", {
    targeting: "true",
    isRollout: true,
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "experimental-features-media-jxl",
    firefoxLabsDescription: "experimental-features-media-jxl-description",
    firefoxLabsDescriptionLinks: {
      bugzilla: "https://example.com",
    },
    firefoxLabsGroup: "experimental-features-group-webpage-display",
    branches: [
      {
        slug: "control",
        ratio: 1,
        features: [
          {
            featureId: "nimbus-qa-2",
            value: {
              value: "recipe-value-2",
            },
          },
        ],
      },
    ],
  }),

  NimbusTestUtils.factories.recipe("targeting-false", {
    targeting: "false",
    isRollout: true,
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "experimental-features-ime-search",
    firefoxLabsDescription: "experimental-features-ime-search-description",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "experimental-features-group-developer-tools",
    requiresRestart: false,
  }),

  NimbusTestUtils.factories.recipe("bucketing-false", {
    bucketConfig: {
      ...NimbusTestUtils.factories.recipe.bucketConfig,
      count: 0,
    },
    isRollout: true,
    targeting: "true",
    isFirefoxLabsOptIn: true,
    firefoxLabsTitle: "experimental-features-ime-search",
    firefoxLabsDescription: "experimental-features-ime-search-description",
    firefoxLabsDescriptionLinks: null,
    firefoxLabsGroup: "experimental-features-group-developer-tools",
    requiresRestart: false,
  }),
];

async function setupLabsTest(recipes) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["app.normandy.run_interval_seconds", 0],
      ["app.shield.optoutstudies.enabled", true],
      ["datareporting.healthreport.uploadEnabled", true],
      ["messaging-system.log", "debug"],
    ],
    clear: [
      ["browser.preferences.experimental"],
      ["browser.preferences.experimental.hidden"],
    ],
  });
  // Initialize Nimbus and wait for the RemoteSettingsExperimentLoader to finish
  // updating (with no recipes).
  await ExperimentAPI.ready();
  await ExperimentAPI._rsLoader.finishedUpdating();

  // Inject some recipes into the Remote Settings client and call
  // updateRecipes() so that we have available opt-ins.
  await ExperimentAPI._rsLoader.remoteSettingsClients.experiments.db.importChanges(
    {},
    Date.now(),
    recipes ?? DEFAULT_LABS_RECIPES,
    { clear: true }
  );

  await ExperimentAPI._rsLoader.updateRecipes("test");

  return async function cleanup() {
    const store = ExperimentAPI._manager.store;

    store._store._saver.disarm();
    if (store._store._saver.isRunning) {
      await store._store._saver._runningPromise;
    }

    await IOUtils.remove(store._store.path);

    await SpecialPowers.popPrefEnv();
  };
}

function promiseNimbusStoreUpdate(wantedSlug, wantedActive) {
  const deferred = Promise.withResolvers();
  const listener = (_event, { slug, active }) => {
    info(
      `promiseNimbusStoreUpdate: received update for ${slug} active=${active}`
    );
    if (slug === wantedSlug && active === wantedActive) {
      ExperimentAPI._manager.store.off("update", listener);
      deferred.resolve();
    }
  };

  ExperimentAPI._manager.store.on("update", listener);
  return deferred.promise;
}

function enrollByClick(el, wantedActive) {
  const slug = el.dataset.nimbusSlug;

  info(`Enrolling in ${slug}:${el.dataset.nimbusBranchSlug}...`);

  const promise = promiseNimbusStoreUpdate(slug, wantedActive);
  EventUtils.synthesizeMouseAtCenter(el.inputEl, {}, gBrowser.contentWindow);
  return promise;
}

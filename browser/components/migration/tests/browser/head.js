/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head-common.js */

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/migration/tests/browser/head-common.js",
  this
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { InternalTestingProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/InternalTestingProfileMigrator.sys.mjs"
);
const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const DIALOG_URL =
  "chrome://browser/content/migration/migration-dialog-window.html";

/**
 * We'll have this be our magic number of quantities of various imports.
 * We will use Sinon to prepare MigrationUtils to presume that this was
 * how many of each quantity-supported resource type was imported.
 */
const EXPECTED_QUANTITY = 123;

/**
 * These are the resource types that currently display their import success
 * message with a quantity.
 */
const RESOURCE_TYPES_WITH_QUANTITIES = [
  MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.BOOKMARKS,
  MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.HISTORY,
  MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.PASSWORDS,
  MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.FORMDATA,
  MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.PAYMENT_METHODS,
  MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.EXTENSIONS,
];

/**
 * The withMigrationWizardDialog callback, called after the
 * dialog has loaded and the wizard is ready.
 *
 * @callback withMigrationWizardDialogCallback
 * @param {DOMWindow} window
 *   The content window of the migration wizard subdialog frame.
 * @returns {Promise<undefined>}
 */

/**
 * Opens the migration wizard HTML5 dialog in about:preferences in the
 * current window's selected tab, runs an async taskFn, and then
 * cleans up by loading about:blank in the tab before resolving.
 *
 * @param {withMigrationWizardDialogCallback} taskFn
 *   An async test function to be called while the migration wizard
 *   dialog is open.
 * @returns {Promise<undefined>}
 */
async function withMigrationWizardDialog(taskFn) {
  let migrationDialogPromise = waitForMigrationWizardDialogTab();
  await MigrationUtils.showMigrationWizard(window, {});
  let prefsBrowser = await migrationDialogPromise;

  try {
    await taskFn(prefsBrowser.contentWindow);
  } finally {
    if (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.getTabForBrowser(prefsBrowser));
    } else {
      BrowserTestUtils.startLoadingURIString(prefsBrowser, "about:blank");
      await BrowserTestUtils.browserLoaded(prefsBrowser, {
        wantLoad: "about:blank",
      });
    }
  }
}

/**
 * Returns a Promise that resolves when an about:preferences tab opens
 * in the current window which loads the migration wizard dialog.
 * The Promise will wait until the migration wizard reports that it
 * is ready with the "MigrationWizard:Ready" event.
 *
 * @returns {Promise<browser>}
 *   Resolves with the about:preferences browser element.
 */
async function waitForMigrationWizardDialogTab() {
  let wizardReady = BrowserTestUtils.waitForEvent(
    window,
    "MigrationWizard:Ready"
  );

  let tab;
  if (gBrowser.selectedTab.isEmpty) {
    tab = gBrowser.selectedTab;
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, url => {
      return url.startsWith("about:preferences");
    });
  } else {
    tab = await BrowserTestUtils.waitForNewTab(gBrowser, url => {
      return url.startsWith("about:preferences");
    });
  }

  await wizardReady;
  info("Done waiting - migration subdialog loaded and ready.");

  return tab.linkedBrowser;
}

/**
 * A helper function that prepares the InternalTestingProfileMigrator
 * with some set of fake available resources, and resolves a Promise
 * when the InternalTestingProfileMigrator is used for a migration.
 *
 * @param {number[]} availableResourceTypes
 *   An array of resource types from MigrationUtils.resourcesTypes.
 *   A single MigrationResource will be created per type, with a
 *   no-op migrate function.
 * @param {number[]} expectedResourceTypes
 *   An array of resource types from MigrationUtils.resourceTypes.
 *   These are the resource types that are expected to be passed
 *   to the InternalTestingProfileMigrator.migrate function.
 * @param {object|string} expectedProfile
 *   The profile object or string that is expected to be passed
 *   to the InternalTestingProfileMigrator.migrate function.
 * @param {number[]} [errorResourceTypes=[]]
 *   Resource types that we should pretend have failed to complete
 *   their migration properly.
 * @param {number} [totalExtensions=1]
 *   If migrating extensions, the total that should be reported to
 *   have been found from the source browser.
 * @param {number} [matchedExtensions=1]
 *   If migrating extensions, the number of extensions that should
 *   be reported as having equivalent matches for this browser.
 * @returns {Promise<undefined>}
 */
async function waitForTestMigration(
  availableResourceTypes,
  expectedResourceTypes,
  expectedProfile,
  errorResourceTypes = [],
  totalExtensions = 1,
  matchedExtensions = 1
) {
  let sandbox = sinon.createSandbox();
  let sourceHistogram = TelemetryTestUtils.getAndClearHistogram(
    "FX_MIGRATION_SOURCE_BROWSER"
  );
  let usageHistogram =
    TelemetryTestUtils.getAndClearKeyedHistogram("FX_MIGRATION_USAGE");
  let errorHistogram = TelemetryTestUtils.getAndClearKeyedHistogram(
    "FX_MIGRATION_ERRORS"
  );

  // Fake out the getResources method of the migrator so that we return
  // a single fake MigratorResource per availableResourceType.
  sandbox
    .stub(InternalTestingProfileMigrator.prototype, "getResources")
    .callsFake(aProfile => {
      Assert.deepEqual(
        aProfile,
        expectedProfile,
        "Should have gotten the expected profile."
      );
      return Promise.resolve(
        availableResourceTypes.map(resourceType => {
          return {
            type: resourceType,
            migrate: () => {},
          };
        })
      );
    });

  sandbox.stub(MigrationUtils, "_importQuantities").value({
    bookmarks: EXPECTED_QUANTITY,
    history: EXPECTED_QUANTITY,
    logins: EXPECTED_QUANTITY,
    cards: EXPECTED_QUANTITY,
    extensions: EXPECTED_QUANTITY,
    formdata: EXPECTED_QUANTITY,
  });

  sandbox
    .stub(MigrationUtils, "getSourceIdForTelemetry")
    .withArgs(InternalTestingProfileMigrator.key)
    .returns(InternalTestingProfileMigrator.sourceID);

  // Fake out the migrate method of the migrator and assert that the
  // next time it's called, its arguments match our expectations.
  return new Promise(resolve => {
    sandbox
      .stub(InternalTestingProfileMigrator.prototype, "migrate")
      .callsFake((aResourceTypes, aStartup, aProfile, aProgressCallback) => {
        Assert.ok(
          !aStartup,
          "Migrator should not have been called as a startup migration."
        );

        let bitMask = 0;
        for (let resourceType of expectedResourceTypes) {
          bitMask |= resourceType;
        }

        Assert.deepEqual(
          aResourceTypes,
          bitMask,
          "Got the expected resource types"
        );
        Assert.deepEqual(
          aProfile,
          expectedProfile,
          "Got the expected profile object"
        );

        for (let resourceType of expectedResourceTypes) {
          let shouldError = errorResourceTypes.includes(resourceType);
          if (
            resourceType == MigrationUtils.resourceTypes.EXTENSIONS &&
            !shouldError
          ) {
            let progressValue;
            if (totalExtensions == matchedExtensions) {
              progressValue = MigrationWizardConstants.PROGRESS_VALUE.SUCCESS;
            } else if (
              totalExtensions > matchedExtensions &&
              matchedExtensions
            ) {
              progressValue = MigrationWizardConstants.PROGRESS_VALUE.INFO;
            } else {
              Assert.ok(
                false,
                "Total and matched extensions should be greater than 0 on success." +
                  `Total: ${totalExtensions}, Matched: ${matchedExtensions}`
              );
            }
            aProgressCallback(resourceType, !shouldError, {
              totalExtensions: Array(totalExtensions),
              importedExtensions: Array(matchedExtensions),
              progressValue,
            });
          } else {
            aProgressCallback(resourceType, !shouldError);
          }
        }

        let usageHistogramSnapshot =
          usageHistogram.snapshot()[InternalTestingProfileMigrator.key];

        let errorHistogramSnapshot =
          errorHistogram.snapshot()[InternalTestingProfileMigrator.key];

        for (let resourceTypeName in MigrationUtils.resourceTypes) {
          let resourceType = MigrationUtils.resourceTypes[resourceTypeName];
          if (resourceType == MigrationUtils.resourceTypes.ALL) {
            continue;
          }

          if (expectedResourceTypes.includes(resourceType)) {
            Assert.equal(
              usageHistogramSnapshot.values[Math.log2(resourceType)],
              1,
              `Should have set resource type ${resourceTypeName} on the FX_MIGRATION_USAGE keyed histogram.`
            );

            if (errorResourceTypes.includes(resourceType)) {
              Assert.equal(
                errorHistogramSnapshot.values[Math.log2(resourceType)],
                1,
                `Should have set resource type ${resourceTypeName} on the FX_MIGRATION_ERRORS keyed histogram.`
              );
            }
          } else {
            let value = usageHistogramSnapshot.values[Math.log2(resourceType)];
            Assert.ok(
              value === 0 || value === undefined,
              `Should not have set resource type ${resourceTypeName} on the FX_MIGRATION_USAGE keyed histogram.`
            );
          }
        }

        Services.obs.notifyObservers(null, "Migration:Ended");

        TelemetryTestUtils.assertHistogram(
          sourceHistogram,
          InternalTestingProfileMigrator.sourceID,
          1
        );

        resolve();
      });
  }).finally(async () => {
    sandbox.restore();

    // MigratorBase caches resources fetched by the getResources method
    // as a performance optimization. In order to allow different tests
    // to have different available resources, we call into a special
    // method of InternalTestingProfileMigrator that clears that
    // cache.
    let migrator = await MigrationUtils.getMigrator(
      InternalTestingProfileMigrator.key
    );
    migrator.flushResourceCache();
  });
}

/**
 * Wait for a panel-list to be ready to interact with. In chrome the panel-list
 * is parented to a XUL <panel> that it opens asynchronously, so its own "shown"
 * event can fire before that panel is open and its items are interactive.
 *
 * Bug 2063011 will fix this in panel-list itself, and remove this helper.
 *
 * @param {Element} panelList
 */
async function waitForPanelListShown(panelList) {
  await new Promise(resolve => {
    panelList.addEventListener("shown", resolve, { once: true });
  });

  let panel = panelList.parentElement;
  if (panel?.localName == "panel") {
    await BrowserTestUtils.waitForPopupEvent(panel, "shown");
  }
}

/**
 * Takes a MigrationWizard element and chooses the
 * InternalTestingProfileMigrator as the browser to migrate from. Then, it
 * checks the checkboxes associated with the selectedResourceTypes and
 * unchecks the rest before clicking the "Import" button.
 *
 * @param {Element} wizard
 *   The MigrationWizard element.
 * @param {string[]} selectedResourceTypes
 *   An array of resource type strings from
 *   MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.
 * @param {string} [migratorKey=InternalTestingProfileMigrator.key]
 *   The key for the migrator to use. Defaults to the
 *   InternalTestingProfileMigrator.
 */
async function selectResourceTypesAndStartMigration(
  wizard,
  selectedResourceTypes,
  migratorKey = InternalTestingProfileMigrator.key
) {
  let shadow = wizard.openOrClosedShadowRoot;
  let panelList = shadow.querySelector("panel-list");
  Assert.ok(!panelList.open, "Panel should not yet be open.");

  // First, select the InternalTestingProfileMigrator browser.
  let selector = shadow.querySelector("#browser-profile-selector");
  EventUtils.synthesizeMouseAtCenter(selector, {}, wizard.documentGlobal);

  await waitForPanelListShown(panelList);

  let panelItem = shadow.querySelector(`panel-item[key="${migratorKey}"]`);
  Assert.ok(
    panelItem.openOrClosedShadowRoot.querySelector("button"),
    "The panel-list button exists."
  );
  EventUtils.synthesizeMouseAtCenter(panelItem, {}, wizard.documentGlobal);

  await new Promise(resolve => {
    panelList.addEventListener("hidden", resolve, { once: true });
  });

  // And then check the right checkboxes for the resource types.
  let resourceTypeList = shadow.querySelector("#resource-type-list");
  for (let resourceType of getChoosableResourceTypes()) {
    let node = resourceTypeList.querySelector(
      `label[data-resource-type="${resourceType}"]`
    );
    node.control.checked = selectedResourceTypes.includes(resourceType);
  }

  let importButton = shadow.querySelector("#import");
  importButton.click();
}

/**
 * Assert that the resource types passed in expectedResourceTypes are
 * showing a success state after a migration, and if they are part of
 * the RESOURCE_TYPES_WITH_QUANTITIES group, that they're showing the
 * EXPECTED_QUANTITY magic number in their success message. Otherwise,
 * we (currently) check that they show the empty string.
 *
 * @param {Element} wizard
 *   The MigrationWizard element.
 * @param {string[]} expectedResourceTypes
 *   An array of resource type strings from
 *   MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.
 * @param {string[]} [warningResourceTypes=[]]
 *   An array of resource type strings from
 *   MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES. These
 *   are the resources that should be showing a warning message.
 */
function assertQuantitiesShown(
  wizard,
  expectedResourceTypes,
  warningResourceTypes = []
) {
  let shadow = wizard.openOrClosedShadowRoot;

  // Make sure that we're showing the progress page first.
  let deck = shadow.querySelector("#wizard-deck");
  Assert.equal(
    deck.selectedViewName,
    `page-${MigrationWizardConstants.PAGES.PROGRESS}`
  );

  let headerL10nID = shadow.querySelector("#progress-header").dataset.l10nId;
  if (warningResourceTypes.length) {
    Assert.equal(
      headerL10nID,
      "migration-wizard-progress-done-with-warnings-header"
    );
  } else {
    Assert.equal(headerL10nID, "migration-wizard-progress-done-header");
  }

  // Go through each displayed resource and make sure that only the
  // ones that are expected are shown, and are showing the right
  // success message.

  let progressGroups = shadow.querySelectorAll(".resource-progress-group");
  for (let progressGroup of progressGroups) {
    if (expectedResourceTypes.includes(progressGroup.dataset.resourceType)) {
      let progressIcon = progressGroup.querySelector(".progress-icon");
      let messageText =
        progressGroup.querySelector(".message-text").textContent;

      if (warningResourceTypes.includes(progressGroup.dataset.resourceType)) {
        Assert.equal(
          progressIcon.getAttribute("state"),
          "warning",
          "Should be showing the warning icon state."
        );
      } else {
        Assert.equal(
          progressIcon.getAttribute("state"),
          "success",
          "Should be showing the success icon state."
        );
      }

      if (
        RESOURCE_TYPES_WITH_QUANTITIES.includes(
          progressGroup.dataset.resourceType
        )
      ) {
        if (
          progressGroup.dataset.resourceType ==
          MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.HISTORY
        ) {
          // HISTORY is a special case that doesn't show the number of imported
          // history entries, but instead shows the maximum number of days of history
          // that might have been imported.
          Assert.notEqual(
            messageText.indexOf(MigrationUtils.HISTORY_MAX_AGE_IN_DAYS),
            -1,
            `Found expected maximum number of days of history: ${messageText}`
          );
        } else if (
          progressGroup.dataset.resourceType ==
          MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.FORMDATA
        ) {
          // FORMDATA is another special case, because we simply show "Form history" as
          // the message string, rather than a particular quantity.
          Assert.equal(
            messageText,
            "Form history",
            `Found expected form data string: ${messageText}`
          );
        } else if (
          progressGroup.dataset.resourceType ==
          MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.EXTENSIONS
        ) {
          // waitForTestMigration by default sets up a "successful" migration of 1
          // extension.
          Assert.stringMatches(messageText, "1 extension");
        } else {
          Assert.notEqual(
            messageText.indexOf(EXPECTED_QUANTITY),
            -1,
            `Found expected quantity in message string: ${messageText}`
          );
        }
      } else {
        // If you've found yourself here, and this is failing, it's probably because you've
        // updated MigrationWizardParent.#getStringForImportQuantity to return a string for
        // a resource type that's not in RESOURCE_TYPES_WITH_QUANTITIES, and you'll need
        // to modify this function to check for that string.
        Assert.equal(
          messageText,
          "",
          "Expected the empty string if the resource type " +
            "isn't in RESOURCE_TYPES_WITH_QUANTITIES"
        );
      }
    } else {
      Assert.ok(
        BrowserTestUtils.isHidden(progressGroup),
        `Resource progress group for ${progressGroup.dataset.resourceType}` +
          ` should be hidden.`
      );
    }
  }
}

/**
 * Translates an entrypoint string into the proper numeric value for the
 * FX_MIGRATION_ENTRY_POINT_CATEGORICAL histogram.
 *
 * @param {string} entrypoint
 *   The entrypoint to translate from MIGRATION_ENTRYPOINTS.
 * @returns {number}
 *   The numeric index value for the FX_MIGRATION_ENTRY_POINT_CATEGORICAL
 *   histogram.
 */
function getEntrypointHistogramIndex(entrypoint) {
  switch (entrypoint) {
    case MigrationUtils.MIGRATION_ENTRYPOINTS.FIRSTRUN: {
      return 1;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.FXREFRESH: {
      return 2;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.PLACES: {
      return 3;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.PASSWORDS: {
      return 4;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.NEWTAB: {
      return 5;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.FILE_MENU: {
      return 6;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.HELP_MENU: {
      return 7;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.BOOKMARKS_TOOLBAR: {
      return 8;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.PREFERENCES: {
      return 9;
    }
    case MigrationUtils.MIGRATION_ENTRYPOINTS.UNKNOWN:
    // Intentional fall-through
    default: {
      return 0; // Unknown
    }
  }
}

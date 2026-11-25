/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ChromeProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeProfileMigrator.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.migrate.chrome.get_permissions.enabled", true]],
  });
});

/**
 * Tests that the migration wizard can request permission from
 * the user to read from other browser data directories when
 * explicit permission needs to be granted.
 *
 * This can occur when, for example, Firefox is installed as a
 * Snap on Ubuntu Linux. In this state, Firefox does not have
 * direct read access to other browser's data directories (although)
 * it can tell if they exist. For Chromium-based browsers, this
 * means we cannot tell what profiles nor resources are available
 * for Chromium-based browsers without read permissions.
 *
 * Note that the Safari migrator is not tested here, as it has
 * its own special permission flow. This is because we can
 * determine what resources Safari has before requiring permissions,
 * and (as of this writing) Safari does not support multiple
 * user profiles.
 */
add_task(async function test_permissions() {
  Services.telemetry.clearEvents();
  Services.fog.testResetFOG();

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  // Browsers installed on the machine running the test could otherwise be
  // selected first.
  sandbox.stub(MigrationUtils, "availableMigratorKeys").get(() => {
    return [InternalTestingProfileMigrator.key];
  });

  sandbox
    .stub(InternalTestingProfileMigrator.prototype, "canGetPermissions")
    .resolves("/some/path");

  let hasPermissionsStub = sandbox
    .stub(InternalTestingProfileMigrator.prototype, "hasPermissions")
    .resolves(false);

  let testingMigrator = await MigrationUtils.getMigrator(
    InternalTestingProfileMigrator.key
  );
  Assert.ok(
    testingMigrator,
    "Got migrator, even though we don't yet have permission to read its resources."
  );

  // The first request doesn't get access, as if the user cancelled the file
  // picker. The second one does, after a folder that didn't give access.
  let { promise: firstRequestDone, resolve: firstRequestResolve } =
    Promise.withResolvers();
  let getPermissionsStub = sandbox.stub(testingMigrator, "getPermissions");
  getPermissionsStub.onFirstCall().callsFake(async (win, options) => {
    options.onFilePickerShown();
    firstRequestResolve();
    return false;
  });
  getPermissionsStub.callsFake(async (win, options) => {
    options.onFilePickerShown();
    options.onFilePickerShown();
    testingMigrator.flushResourceCache();
    hasPermissionsStub.resolves(true);
    return true;
  });

  let getResourcesStub = sandbox
    .stub(testingMigrator, "getResources")
    .resolves([]);

  let migration = waitForTestMigration(
    [MigrationUtils.resourceTypes.BOOKMARKS],
    [MigrationUtils.resourceTypes.BOOKMARKS],
    InternalTestingProfileMigrator.testProfile
  );

  await withMigrationWizardDialog(async prefsWin => {
    let dialogBody = prefsWin.document.body;
    let wizard = dialogBody.querySelector("migration-wizard");
    let shadow = wizard.openOrClosedShadowRoot;

    // Clear out any pre-existing events events have been logged
    Services.telemetry.clearEvents();
    TelemetryTestUtils.assertNumberOfEvents(0);

    let panelItem = shadow.querySelector(
      `panel-item[key="${InternalTestingProfileMigrator.key}"]`
    );
    panelItem.click();

    let resourceList = shadow.querySelector(".resource-selection-details");
    Assert.ok(
      BrowserTestUtils.isHidden(resourceList),
      "Resources list is hidden."
    );
    let importButton = shadow.querySelector("#import");
    Assert.ok(BrowserTestUtils.isHidden(importButton), "Import button hidden.");
    let noPermissionsMessage = shadow.querySelector(".no-permissions-message");
    Assert.ok(
      BrowserTestUtils.isVisible(noPermissionsMessage),
      "No permissions message shown."
    );
    let step2 = shadow.querySelector(
      ".migration-no-permissions-instructions-step2"
    );
    // The instructions are first translated without the path, and again once
    // the selected browser's path is set.
    let isMacOS = AppConstants.platform == "macosx";
    await BrowserTestUtils.waitForMutationCondition(
      step2,
      { childList: true, subtree: true, characterData: true },
      () =>
        isMacOS
          ? step2.textContent.includes("file picker")
          : step2.textContent.includes("/some/path")
    );
    if (isMacOS) {
      Assert.equal(
        step2.textContent.trim(),
        "In the file picker, choose “Open”",
        "Tells macOS users to choose Open."
      );
    } else {
      Assert.ok(
        step2.textContent.includes("/some/path"),
        "Shows the path to navigate to."
      );
    }
    let getPermissionButton = shadow.querySelector("#get-permissions");
    Assert.ok(
      BrowserTestUtils.isVisible(getPermissionButton),
      "Get permissions button shown."
    );

    getPermissionButton.click();
    await firstRequestDone;
    // Nothing in the wizard changes when access isn't granted.
    await TestUtils.waitForCondition(
      () => Glean.browserMigration.getPermissionsWizard.testGetValue(),
      "Waiting for the request to be recorded."
    );
    Assert.deepEqual(
      Glean.browserMigration.getPermissionsWizard
        .testGetValue()
        .map(event => event.extra),
      [
        {
          migrator_key: InternalTestingProfileMigrator.key,
          granted: "false",
          attempts: "1",
        },
      ],
      "Recorded that access was not granted."
    );
    Assert.ok(
      BrowserTestUtils.isVisible(noPermissionsMessage),
      "Still asks for access."
    );

    // Now put the permissions functions back into their default
    // state - which is the "permission granted" state.
    getResourcesStub.restore();
    hasPermissionsStub.restore();
    Services.fog.testResetFOG();

    let refreshDone = BrowserTestUtils.waitForEvent(
      wizard,
      "MigrationWizard:Ready"
    );

    getPermissionButton.click();

    await refreshDone;
    Assert.ok(true, "Refreshed migrator list.");
    Assert.deepEqual(
      Glean.browserMigration.getPermissionsWizard
        .testGetValue()
        .map(event => event.extra),
      [
        {
          migrator_key: InternalTestingProfileMigrator.key,
          granted: "true",
          attempts: "2",
        },
      ],
      "Recorded that access was granted the second time."
    );

    let wizardDone = BrowserTestUtils.waitForEvent(
      wizard,
      "MigrationWizard:DoneMigration"
    );
    selectResourceTypesAndStartMigration(wizard, [
      MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.BOOKMARKS,
    ]);
    await migration;
    await wizardDone;

    assertQuantitiesShown(wizard, [
      MigrationWizardConstants.DISPLAYED_RESOURCE_TYPES.BOOKMARKS,
    ]);

    let dialog = prefsWin.document.querySelector("#migrationWizardDialog");
    let doneButton = shadow.querySelector(
      "div[name='page-progress'] .done-button"
    );

    await new Promise(resolve => prefsWin.requestAnimationFrame(resolve));
    Assert.equal(
      shadow.activeElement,
      doneButton,
      "Done button should be focused."
    );

    let dialogClosed = BrowserTestUtils.waitForEvent(dialog, "close");
    doneButton.click();
    await dialogClosed;
  });

  TelemetryTestUtils.assertEvents(
    [
      {
        category: "browser.migration",
        method: "linux_perms",
        object: "wizard",
        value: null,
        extra: {
          migrator_key: InternalTestingProfileMigrator.key,
        },
      },
    ],
    {
      category: "browser.migration",
      method: "linux_perms",
      object: "wizard",
    }
  );

  sandbox.restore();
});

/**
 * Tests that the browser that permission was granted for stays selected
 * after the list of browsers is refreshed, even if another browser comes
 * first in that list.
 */
add_task(async function test_selection_kept_after_permissions() {
  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  sandbox.stub(MigrationUtils, "availableMigratorKeys").get(() => {
    return [InternalTestingProfileMigrator.key, ChromeProfileMigrator.key];
  });

  // Make Chrome the most recently used browser, so that it comes first.
  sandbox
    .stub(ChromeProfileMigrator.prototype, "getSourceProfiles")
    .resolves([{ id: "chrome-test", name: "Chrome test profile" }]);
  sandbox
    .stub(ChromeProfileMigrator.prototype, "hasPermissions")
    .resolves(true);
  sandbox
    .stub(ChromeProfileMigrator.prototype, "getResources")
    .resolves([
      { type: MigrationUtils.resourceTypes.BOOKMARKS, migrate: () => {} },
    ]);
  sandbox
    .stub(ChromeProfileMigrator.prototype, "getLastUsedDate")
    .resolves(new Date());

  sandbox
    .stub(InternalTestingProfileMigrator.prototype, "canGetPermissions")
    .resolves("/some/path");
  let hasPermissionsStub = sandbox
    .stub(InternalTestingProfileMigrator.prototype, "hasPermissions")
    .resolves(false);
  let testingMigrator = await MigrationUtils.getMigrator(
    InternalTestingProfileMigrator.key
  );
  sandbox.stub(testingMigrator, "getPermissions").callsFake(async () => {
    testingMigrator.flushResourceCache();
    hasPermissionsStub.resolves(true);
    return true;
  });
  let getResourcesStub = sandbox
    .stub(testingMigrator, "getResources")
    .resolves([]);

  await withMigrationWizardDialog(async prefsWin => {
    let wizard = prefsWin.document.body.querySelector("migration-wizard");
    let shadow = wizard.openOrClosedShadowRoot;
    let selector = shadow.querySelector("#browser-profile-selector");
    Assert.equal(
      selector.selectedPanelItem.getAttribute("key"),
      ChromeProfileMigrator.key,
      "Chrome is selected first."
    );

    shadow
      .querySelector(`panel-item[key="${InternalTestingProfileMigrator.key}"]`)
      .click();

    getResourcesStub.restore();
    let refreshDone = BrowserTestUtils.waitForEvent(
      wizard,
      "MigrationWizard:Ready"
    );
    shadow.querySelector("#get-permissions").click();
    await refreshDone;

    Assert.equal(
      selector.selectedPanelItem.getAttribute("key"),
      InternalTestingProfileMigrator.key,
      "The browser that permission was granted for is still selected."
    );
  });

  sandbox.restore();
});

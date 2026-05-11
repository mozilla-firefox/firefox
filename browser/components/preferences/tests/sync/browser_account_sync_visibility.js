/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);

/**
 * Verifies that a link element has an href containing the expected URL fragment.
 *
 * @param {Element} linkElement - The link element to check
 * @param {string} expectedUrlFragment - URL fragment to look for in the href
 * @param {string} message - Assertion message
 * @returns {Promise<void>}
 */
async function assertLinkHasHref(linkElement, expectedUrlFragment, message) {
  await BrowserTestUtils.waitForMutationCondition(
    linkElement,
    { attributes: true, attributeFilter: ["href"] },
    () => linkElement.href
  );
  ok(linkElement.href.includes(expectedUrlFragment), message);
}

/**
 * Verifies that the specified account groups are hidden.
 *
 * @param {Document} doc - The document to query
 * @param {Array<string>} groupIds - Array of group IDs to check
 * @returns {void}
 */
function assertAccountGroupsHidden(doc, groupIds) {
  groupIds.forEach(groupId => {
    let group = doc.getElementById(groupId);
    ok(BrowserTestUtils.isHidden(group), `${groupId} is hidden`);
  });
}

// Account group visibility tests
add_task(async function testAccountGroupSignedOut() {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.enabled", true]],
  });

  await runSyncPaneTest(
    {
      status: UIState.STATUS_NOT_CONFIGURED,
    },
    async doc => {
      let accountGroup = doc.querySelector('setting-group[groupid="account"]');
      ok(
        BrowserTestUtils.isVisible(accountGroup),
        "Account setting group is displayed when accounts are enabled."
      );

      let noFxaAccount = accountGroup.querySelector("#noFxaAccount");
      ok(
        BrowserTestUtils.isVisible(noFxaAccount),
        "No account placeholder is displayed when signed out."
      );

      let noFxaSignIn = accountGroup.querySelector("#noFxaSignIn");
      ok(
        BrowserTestUtils.isVisible(noFxaSignIn),
        "Sign in button is displayed when signed out."
      );

      // Other account groups should be hidden
      assertAccountGroupsHidden(doc, [
        "fxaSignedInGroup",
        "fxaUnverifiedGroup",
        "fxaLoginRejectedGroup",
      ]);
    }
  );
});

add_task(async function testAccountGroupSignedIn() {
  await runSyncPaneTest(
    {
      status: UIState.STATUS_SIGNED_IN,
      email: "test@example.com",
      displayName: "Test User",
      avatarURL: null,
      syncEnabled: true,
    },
    async doc => {
      let fxaSignedInGroup = doc.getElementById("fxaSignedInGroup");
      ok(
        BrowserTestUtils.isVisible(fxaSignedInGroup),
        "Signed in group is displayed when user is signed in."
      );

      let fxaLoginVerified =
        fxaSignedInGroup.querySelector("#fxaLoginVerified");
      ok(
        BrowserTestUtils.isVisible(fxaLoginVerified),
        "Verified account info is displayed."
      );

      // Verify that the display name and email are shown in the UI
      ok(
        fxaLoginVerified.label.includes("Test User"),
        "Display name is shown in the account info."
      );
      ok(
        fxaLoginVerified.description.includes("test@example.com"),
        "Email is shown in the account info."
      );

      let verifiedManage = fxaSignedInGroup.querySelector("#verifiedManage");
      ok(
        BrowserTestUtils.isVisible(verifiedManage),
        "Manage account link is displayed."
      );

      // Verify the manage account link has the correct href
      await assertLinkHasHref(
        verifiedManage,
        "accounts.firefox.com",
        "Manage account link points to Firefox Accounts settings"
      );

      let fxaUnlinkButton = fxaSignedInGroup.querySelector("#fxaUnlinkButton");
      ok(
        BrowserTestUtils.isVisible(fxaUnlinkButton),
        "Unlink account button is displayed."
      );

      // Other account groups should be hidden
      assertAccountGroupsHidden(doc, [
        "noFxaAccountGroup",
        "fxaUnverifiedGroup",
        "fxaLoginRejectedGroup",
      ]);
    }
  );
});

add_task(async function testAccountGroupUnverified() {
  await runSyncPaneTest(
    {
      status: UIState.STATUS_NOT_VERIFIED,
      email: "unverified@example.com",
    },
    async doc => {
      let fxaUnverifiedGroup = doc.getElementById("fxaUnverifiedGroup");
      ok(
        BrowserTestUtils.isVisible(fxaUnverifiedGroup),
        "Unverified group is displayed when account is not verified."
      );

      let fxaLoginUnverified = fxaUnverifiedGroup.querySelector(
        "#fxaLoginUnverified"
      );
      ok(
        BrowserTestUtils.isVisible(fxaLoginUnverified),
        "Unverified message is displayed."
      );

      // Verify that the email is shown in the unverified message
      ok(
        fxaLoginUnverified.label.includes("unverified@example.com"),
        "Email is shown in the unverified message."
      );

      let verifyFxaAccount =
        fxaUnverifiedGroup.querySelector("#verifyFxaAccount");
      ok(
        BrowserTestUtils.isVisible(verifyFxaAccount),
        "Verify account link is displayed."
      );

      let unverifiedUnlinkFxaAccount = fxaUnverifiedGroup.querySelector(
        "#unverifiedUnlinkFxaAccount"
      );
      ok(
        BrowserTestUtils.isVisible(unverifiedUnlinkFxaAccount),
        "Unlink account button is displayed for unverified account."
      );

      // Other account groups should be hidden
      assertAccountGroupsHidden(doc, [
        "noFxaAccountGroup",
        "fxaSignedInGroup",
        "fxaLoginRejectedGroup",
      ]);
    }
  );
});

add_task(async function testAccountGroupLoginFailed() {
  await runSyncPaneTest(
    {
      status: UIState.STATUS_LOGIN_FAILED,
      email: "failed@example.com",
    },
    async doc => {
      let fxaLoginRejectedGroup = doc.getElementById("fxaLoginRejectedGroup");
      ok(
        BrowserTestUtils.isVisible(fxaLoginRejectedGroup),
        "Login rejected group is displayed when credentials are rejected."
      );

      let fxaLoginRejected =
        fxaLoginRejectedGroup.querySelector("#fxaLoginRejected");
      ok(
        BrowserTestUtils.isVisible(fxaLoginRejected),
        "Login rejected message is displayed."
      );

      // Verify that the email is shown in the error message
      ok(
        fxaLoginRejected.label.includes("failed@example.com"),
        "Email is shown in the login rejected message."
      );

      let rejectReSignIn =
        fxaLoginRejectedGroup.querySelector("#rejectReSignIn");
      ok(
        BrowserTestUtils.isVisible(rejectReSignIn),
        "Re-sign in link is displayed."
      );

      let rejectUnlinkFxaAccount = fxaLoginRejectedGroup.querySelector(
        "#rejectUnlinkFxaAccount"
      );
      ok(
        BrowserTestUtils.isVisible(rejectUnlinkFxaAccount),
        "Unlink account button is displayed for rejected login."
      );

      // Other account groups should be hidden
      assertAccountGroupsHidden(doc, [
        "noFxaAccountGroup",
        "fxaSignedInGroup",
        "fxaUnverifiedGroup",
      ]);
    }
  );
});

// Test when Firefox Accounts is disabled
add_task(async function testAccountsDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.enabled", false]],
  });

  await runSyncPaneTest(
    {
      status: UIState.STATUS_NOT_CONFIGURED,
    },
    async doc => {
      let accountGroup = doc.querySelector('setting-group[groupid="account"]');
      ok(
        BrowserTestUtils.isHidden(accountGroup),
        "Account section is hidden when Firefox Accounts is disabled."
      );

      let syncGroup = doc.querySelector('setting-group[groupid="sync"]');
      ok(
        BrowserTestUtils.isHidden(syncGroup),
        "Sync section is hidden when Firefox Accounts is disabled."
      );

      // Category nav button should still be visible
      let categoryButton = doc.getElementById("category-sync");
      ok(
        BrowserTestUtils.isVisible(categoryButton),
        "Category nav button is still visible even when accounts disabled."
      );
    }
  );
});

// Import section visibility tests
add_task(async function testImportSectionVisibleWhenAllowed() {
  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  ok(
    Services.policies.isAllowed("profileImport"),
    "profileImport is allowed by default"
  );

  let importGroup = doc.querySelector(
    'setting-group[groupid="importBrowserData"]'
  );
  ok(
    BrowserTestUtils.isVisible(importGroup),
    "Import section is visible when policy allows it."
  );

  let dataMigration = doc.getElementById("data-migration");
  ok(
    BrowserTestUtils.isVisible(dataMigration),
    "Data migration button is displayed."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testImportSectionHiddenWhenBlocked() {
  await setupPolicyEngineWithJson({
    policies: {
      DisableProfileImport: true,
    },
  });

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  ok(
    !Services.policies.isAllowed("profileImport"),
    "profileImport is blocked by DisableProfileImport policy"
  );

  let dataMigration = doc.getElementById("data-migration");
  ok(
    BrowserTestUtils.isHidden(dataMigration),
    "Data migration button is hidden when policy blocks profileImport."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Profiles section visibility test. Additional visibility tests in
// browser/components/profiles/tests/browser/browser_preferences.js
add_task(async function testProfilesSettingsHiddenWhenDisabled() {
  let { SelectableProfileService } = ChromeUtils.importESModule(
    "resource:///modules/profiles/SelectableProfileService.sys.mjs"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.enabled", false]],
  });

  await SelectableProfileService.uninit();
  await SelectableProfileService.init();

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  ok(
    !SelectableProfileService.isEnabled,
    "SelectableProfileService should not be enabled"
  );

  let profilesSettings = doc.getElementById("profilesSettings");
  ok(
    BrowserTestUtils.isHidden(profilesSettings),
    "Profiles settings button is hidden when SelectableProfileService is disabled."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Backup section visibility tests
add_task(async function testBackupSettingsHiddenWhenDisabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.backup.archive.enabled", false],
      ["browser.backup.restore.enabled", false],
    ],
  });

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  let backupSettings = doc.getElementById("backupSettings");
  ok(
    BrowserTestUtils.isHidden(backupSettings),
    "Backup settings is hidden when both archive and restore are disabled."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testBackupSettingsVisibleWhenArchiveEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.backup.archive.enabled", true],
      ["browser.backup.restore.enabled", false],
    ],
  });

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  let backupSettings = doc.getElementById("backupSettings");
  ok(
    BrowserTestUtils.isVisible(backupSettings),
    "Backup settings is visible when archive is enabled."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testBackupSettingsVisibleWhenRestoreEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.backup.archive.enabled", false],
      ["browser.backup.restore.enabled", true],
    ],
  });

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });
  let doc = gBrowser.contentDocument;

  let backupSettings = doc.getElementById("backupSettings");
  ok(
    BrowserTestUtils.isVisible(backupSettings),
    "Backup settings is visible when restore is enabled."
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

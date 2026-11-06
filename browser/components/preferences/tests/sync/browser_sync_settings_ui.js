/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);

add_task(async function testSyncNoFxaSignIn() {
  await runSyncPaneTest(
    {
      status: UIState.STATUS_NOT_CONFIGURED,
      email: "foo@bar.com",
    },
    async doc => {
      let syncSettingGroup = doc.querySelector('setting-group[groupid="sync"]');
      ok(
        !BrowserTestUtils.isHidden(syncSettingGroup),
        "Sync setting group is displayed."
      );

      let syncNoFxaSignIn = syncSettingGroup.querySelector("#noFxaSignIn");
      ok(
        !BrowserTestUtils.isHidden(syncNoFxaSignIn),
        "Link to sign in is displayed when status is not confirmed."
      );

      let syncConfigured = syncSettingGroup.querySelector("#syncConfigured");
      let syncNotConfigured =
        syncSettingGroup.querySelector("#syncNotConfigured");
      let fxaDeviceNameSection = syncSettingGroup.querySelector(
        "#fxaDeviceNameSection"
      );
      ok(
        BrowserTestUtils.isHidden(syncConfigured) &&
          BrowserTestUtils.isHidden(syncNotConfigured) &&
          BrowserTestUtils.isHidden(fxaDeviceNameSection),
        "All other sync sections are hidden."
      );
    }
  );
});

add_task(async function testSyncFxaNotVerified() {
  await runSyncPaneTest(
    {
      status: UIState.STATUS_NOT_VERIFIED,
      email: "foo@bar.com",
    },
    async doc => {
      let syncSettingGroup = doc.querySelector('setting-group[groupid="sync"]');
      ok(
        !BrowserTestUtils.isHidden(syncSettingGroup),
        "Sync setting group is displayed."
      );

      let fxaDeviceNameSection = syncSettingGroup.querySelector(
        "#fxaDeviceNameSection"
      );
      ok(
        !BrowserTestUtils.isHidden(fxaDeviceNameSection),
        "Device name section is displayed when status is not confirmed."
      );

      let fxaDeviceName = fxaDeviceNameSection.querySelector("#fxaDeviceName");
      ok(
        fxaDeviceName.disabled,
        "Change device name is disabled when status is not confirmed."
      );

      let syncNoFxaSignIn = syncSettingGroup.querySelector("#noFxaSignIn");
      let syncConfigured = syncSettingGroup.querySelector("#syncConfigured");
      let syncNotConfigured =
        syncSettingGroup.querySelector("#syncNotConfigured");
      ok(
        BrowserTestUtils.isHidden(syncConfigured) &&
          BrowserTestUtils.isHidden(syncNotConfigured) &&
          BrowserTestUtils.isHidden(syncNoFxaSignIn),
        "All other sync sections are hidden."
      );
    }
  );
});

add_task(async function testSyncFxaLoginFailed() {
  await runSyncPaneTest(
    {
      status: UIState.STATUS_LOGIN_FAILED,
      email: "foo@bar.com",
    },
    async doc => {
      let syncSettingGroup = doc.querySelector('setting-group[groupid="sync"]');
      ok(
        !BrowserTestUtils.isHidden(syncSettingGroup),
        "Sync setting group is displayed."
      );

      let fxaDeviceNameSection = syncSettingGroup.querySelector(
        "#fxaDeviceNameSection"
      );
      ok(
        !BrowserTestUtils.isHidden(fxaDeviceNameSection),
        "Device name section is displayed when login failed."
      );

      let fxaDeviceName = fxaDeviceNameSection.querySelector("#fxaDeviceName");
      ok(
        fxaDeviceName.disabled,
        "Change device name is disabled when login failed."
      );

      let syncNoFxaSignIn = syncSettingGroup.querySelector("#noFxaSignIn");
      let syncConfigured = syncSettingGroup.querySelector("#syncConfigured");
      let syncNotConfigured =
        syncSettingGroup.querySelector("#syncNotConfigured");
      ok(
        BrowserTestUtils.isHidden(syncConfigured) &&
          BrowserTestUtils.isHidden(syncNotConfigured) &&
          BrowserTestUtils.isHidden(syncNoFxaSignIn),
        "All other sync sections are hidden."
      );
    }
  );
});

add_task(async function testSyncFxaSignedInSyncingOff() {
  const testCases = [
    {
      isPerDeviceSyncEnabled: false,
      dataL10nId: "prefs-syncing-off-2",
      assertMessage: "Sync not configured text is displayed for legacy sync",
    },
    {
      isPerDeviceSyncEnabled: true,
      dataL10nId: "prefs-syncing-off-3",
      assertMessage:
        "Sync not configured text is displayed for per-device sync",
    },
  ];

  for (const testCase of testCases) {
    await runSyncPaneTest(
      {
        status: UIState.STATUS_SIGNED_IN,
        email: "foo@bar.com",
        syncEnabled: false,
      },
      async doc => {
        let syncSettingGroup = doc.querySelector(
          'setting-group[groupid="sync"]'
        );
        ok(
          !BrowserTestUtils.isHidden(syncSettingGroup),
          "Sync setting group is displayed."
        );

        let fxaDeviceNameSection = syncSettingGroup.querySelector(
          "#fxaDeviceNameSection"
        );
        ok(
          !BrowserTestUtils.isHidden(fxaDeviceNameSection),
          "Device name section is displayed when user is signed in."
        );

        let fxaDeviceName =
          fxaDeviceNameSection.querySelector("#fxaDeviceName");
        ok(
          !fxaDeviceName.disabled,
          "Change device name is enabled when user is signed in."
        );

        let syncNotConfigured =
          syncSettingGroup.querySelector("#syncNotConfigured");
        ok(
          !BrowserTestUtils.isHidden(syncNotConfigured),
          "Syncing is off section is displayed when user is signed in but sync is disabled."
        );

        let notConfiguredText = syncNotConfigured.getAttribute("data-l10n-id");
        Assert.equal(
          notConfiguredText,
          testCase.dataL10nId,
          testCase.assertMessage
        );

        let syncNoFxaSignIn = syncSettingGroup.querySelector("#noFxaSignIn");
        let syncConfigured = syncSettingGroup.querySelector("#syncConfigured");
        ok(
          BrowserTestUtils.isHidden(syncConfigured) &&
            BrowserTestUtils.isHidden(syncNoFxaSignIn),
          "All other sync sections are hidden."
        );

        const cwtsMigrationNotice = doc.getElementById(
          "syncCwtsMigrationNotification"
        );
        if (testCase.isPerDeviceSyncEnabled) {
          ok(
            !!cwtsMigrationNotice && !cwtsMigrationNotice.hidden,
            "CWTS Migration Message should be shown"
          );
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeShown.testGetValue()?.length,
            1,
            "Should be 1 telemetry event for CWTS Migration message being shown"
          );
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeShown.testGetValue()[0].extra
              .surface,
            "settingsPage",
            "CWTS Migration message shown telemetry should describe the right surface"
          );

          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeLinkClicked.testGetValue(),
            null,
            "Should not be telemetry for CWTS Migration message link being clicked"
          );
          const learnMoreLink = cwtsMigrationNotice.shadowRoot.querySelector(
            "[is='moz-support-link']"
          );
          // We would rather this link not go anywhere so we don't have to deal
          // with the new tab
          learnMoreLink.href = "";
          learnMoreLink.target = "";
          learnMoreLink.click();
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeLinkClicked.testGetValue()
              ?.length,
            1,
            "Should be 1 telemetry event for CWTS Migration message link being clicked"
          );
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeLinkClicked.testGetValue()[0]
              .extra.surface,
            "settingsPage",
            "CWTS Migration message link clicked telemetry should describe the right surface"
          );

          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeDismissed.testGetValue(),
            null,
            "Should not be telemetry for CWTS Migration message being dismissed"
          );
          cwtsMigrationNotice.shadowRoot.querySelector(".close").click();
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeDismissed.testGetValue()
              ?.length,
            1,
            "Should be 1 telemetry event for CWTS Migration message being dismissed"
          );
        } else {
          ok(
            !cwtsMigrationNotice || cwtsMigrationNotice.hidden,
            "CWTS Migration Message shouldn't be shown"
          );
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeShown.testGetValue(),
            null,
            "Should not be telemetry for CWTS Migration message being shown"
          );
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeLinkClicked.testGetValue(),
            null,
            "Should not be telemetry for CWTS Migration message link being clicked"
          );
          Assert.equal(
            Glean.syncSettings.cwtsUpgradeNoticeDismissed.testGetValue(),
            null,
            "Should not be telemetry for CWTS Migration message being dismissed"
          );
        }
      },
      testCase.isPerDeviceSyncEnabled
    );
  }
});

add_task(async function testSyncFxaSignedInSyncingOn() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["services.sync.engine.bookmarks", true],
      ["services.sync.engine.history", true],
      ["services.sync.engine.tabs", true],
      ["services.sync.engine.addons", true],
      ["services.sync.engine.prefs", true],
      ["services.sync.engine.passwords", true],
      ["services.sync.engine.addresses", true],
      ["services.sync.engine.creditcards", true],
    ],
  });

  const testCases = [
    {
      isPerDeviceSyncEnabled: false,
      dataL10nId: "sync-syncing-across-devices-heading-2",
      assertMessage: "The synced engines header is set for legacy sync",
    },
    {
      isPerDeviceSyncEnabled: true,
      dataL10nId: "sync-syncing-across-devices-heading-3",
      assertMessage: "The synced engines header is set for per-device sync",
    },
  ];

  for (const testCase of testCases) {
    await runSyncPaneTest(
      {
        status: UIState.STATUS_SIGNED_IN,
        email: "foo@bar.com",
        syncEnabled: true,
      },
      async doc => {
        let syncSettingGroup = doc.querySelector(
          'setting-group[groupid="sync"]'
        );
        ok(
          !BrowserTestUtils.isHidden(syncSettingGroup),
          "Sync setting group is displayed."
        );

        let fxaDeviceNameSection = syncSettingGroup.querySelector(
          "#fxaDeviceNameSection"
        );
        ok(
          !BrowserTestUtils.isHidden(fxaDeviceNameSection),
          "Device name section is displayed when user is signed in."
        );

        let fxaDeviceName =
          fxaDeviceNameSection.querySelector("#fxaDeviceName");
        ok(
          !fxaDeviceName.disabled,
          "Change device name is enabled when user is signed in."
        );

        let syncConfigured = syncSettingGroup.querySelector("#syncConfigured");
        ok(
          !BrowserTestUtils.isHidden(syncConfigured),
          "Syncing is on section is displayed when user is signed in and sync is enabled."
        );

        let syncEnginesList = syncConfigured.querySelector("sync-engines-list");
        ok(syncEnginesList, "sync-engines-list component is displayed.");

        let engines = syncEnginesList.shadowRoot.querySelector(
          ".engines-list-wrapper"
        );
        ok(
          engines,
          "The list of synced engines is displayed when syncing is on."
        );

        let enginesHeader =
          syncEnginesList.shadowRoot.querySelector("#heading");
        ok(
          enginesHeader,
          "The synced engines header is displayed when syncing is on."
        );

        let enginesHeaderText = enginesHeader.getAttribute("data-l10n-id");
        Assert.equal(
          enginesHeaderText,
          testCase.dataL10nId,
          testCase.assertMessage
        );

        let syncNoFxaSignIn = syncSettingGroup.querySelector("#noFxaSignIn");
        let syncNotConfigured =
          syncSettingGroup.querySelector("#syncNotConfigured");
        ok(
          BrowserTestUtils.isHidden(syncNotConfigured) &&
            BrowserTestUtils.isHidden(syncNoFxaSignIn),
          "All other sync sections are hidden."
        );
      },
      testCase.isPerDeviceSyncEnabled
    );
  }
});

add_task(async function testSyncedEnginesEmptyState() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["services.sync.engine.bookmarks", false],
      ["services.sync.engine.history", false],
      ["services.sync.engine.tabs", false],
      ["services.sync.engine.addons", false],
      ["services.sync.engine.prefs", false],
      ["services.sync.engine.passwords", false],
      ["services.sync.engine.addresses", false],
      ["services.sync.engine.creditcards", false],
    ],
  });

  const testCases = [
    {
      isPerDeviceSyncEnabled: false,
      dataL10nId: "sync-syncing-across-devices-empty-state2",
      assertMessage: "Empty state text is displayed for legacy sync",
    },
    {
      isPerDeviceSyncEnabled: true,
      dataL10nId: "sync-syncing-across-devices-empty-state3",
      assertMessage: "Empty state text is displayed for per-device sync",
    },
  ];

  for (const testCase of testCases) {
    await runSyncPaneTest(
      {
        status: UIState.STATUS_SIGNED_IN,
        email: "foo@bar.com",
        syncEnabled: true,
      },
      async doc => {
        let syncSettingGroup = doc.querySelector(
          'setting-group[groupid="sync"]'
        );
        ok(
          !BrowserTestUtils.isHidden(syncSettingGroup),
          "Sync setting group is displayed."
        );

        let syncConfigured = syncSettingGroup.querySelector("#syncConfigured");
        ok(
          !BrowserTestUtils.isHidden(syncConfigured),
          "Syncing is on section is displayed when user is signed in and sync is enabled."
        );

        let syncEnginesList = syncConfigured.querySelector("sync-engines-list");
        ok(syncEnginesList, "sync-engines-list component is displayed.");

        let engineListEmptyState =
          syncEnginesList.shadowRoot.querySelector(".empty-state");
        ok(
          engineListEmptyState,
          "Empty state message is displayed when syncing is on but non of the engines is synced."
        );

        let text = engineListEmptyState.getAttribute("data-l10n-id");
        Assert.equal(text, testCase.dataL10nId, testCase.assertMessage);
      },
      testCase.isPerDeviceSyncEnabled
    );
  }
});

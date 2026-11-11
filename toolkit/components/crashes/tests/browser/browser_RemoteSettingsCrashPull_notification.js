/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteSettingsCrashPull } = ChromeUtils.importESModule(
  "resource://gre/modules/RemoteSettingsCrashPull.sys.mjs"
);

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const { UnsubmittedCrashHandler } = ChromeUtils.importESModule(
  "resource:///modules/ContentCrashHandlers.sys.mjs"
);

const kNotificationSelector =
  'notification-message[message-bar-type="infobar"]' +
  '[value="pending-crash-reports-req"]';

const kRemoteSettingsCollectionName = "crash-reports-ondemand";

let rscp = undefined;

add_setup(async function setup() {
  rscp = RemoteSettingsCrashPull;
  await SpecialPowers.pushPrefEnv({
    set: [["browser.crashReports.requestedNeverShowAgain", false]],
  });
});

async function getNotification(shouldBeNull = false) {
  await TestUtils.waitForCondition(() => {
    if (shouldBeNull) {
      return document.querySelector(kNotificationSelector) === null;
    }
    const node = document.querySelector(kNotificationSelector);
    const msg = node?.shadowRoot?.querySelector?.(".message");
    return msg !== null && msg.innerText !== "";
  }, "Trying to get a notification");
  return document.querySelector(kNotificationSelector);
}

function resetDontShowBefore() {
  Services.prefs.setIntPref("browser.crashReports.dontShowBefore", 0);
}

add_task(async function test_no_notification() {
  Assert.equal(
    null,
    document.querySelector(kNotificationSelector),
    "No existing notification"
  );
});

add_task(async function test_no_crash_no_notification() {
  const payload = {
    current: [],
    created: [
      {
        hashes: [
          "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
        ],
      },
    ],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload,
  });

  try {
    await TestUtils.waitForCondition(
      () => document.querySelector(kNotificationSelector) !== null,
      "Waiting for a notification to have been shown (not good)"
    );
  } catch (ex) {
    if (!ex.includes("timed out after 50 tries")) {
      throw ex;
    }
  }

  resetDontShowBefore();
});

add_task(async function test_one_crash_one_notification() {
  const _checkForInterestingUnsubmittedCrash =
    rscp.checkForInterestingUnsubmittedCrash;
  rscp.checkForInterestingUnsubmittedCrash = async _ => {
    return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
  };

  const payload = {
    current: [],
    created: [
      {
        hashes: [
          // value here is not important
          "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
        ],
      },
    ],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload,
  });

  const notification = await getNotification();
  const notificationMsg = notification.shadowRoot.querySelector(".message");

  rscp.checkForInterestingUnsubmittedCrash =
    _checkForInterestingUnsubmittedCrash;

  const msg = notificationMsg.getAttribute("data-l10n-id");
  Assert.equal(
    msg,
    "requested-crash-reports-message-new",
    "Shows requested crash msg"
  );

  const args = JSON.parse(notificationMsg.getAttribute("data-l10n-args"));
  Assert.equal(args.reportCount, 1, "Shows requested crash msg for one crash");

  const closeButton = notification.shadowRoot.querySelector(".close");
  closeButton.click();
  await TestUtils.waitForCondition(
    () => document.querySelector(kNotificationSelector) === null,
    "Waiting for notification to be removed"
  );

  resetDontShowBefore();
});

add_task(async function test_multiple_crashes_one_notification() {
  const _checkForInterestingUnsubmittedCrash =
    rscp.checkForInterestingUnsubmittedCrash;
  rscp.checkForInterestingUnsubmittedCrash = async _ => {
    return [
      "26df2a6c-ff31-477b-b1ad-b2dc10a0a664",
      "8c2c68f9-fdc3-4465-999c-3b775ee38dca",
      "a60929fd-1f3b-4edf-ae81-64afc25dcbfb",
    ];
  };

  const payload = {
    current: [],
    created: [
      {
        hashes: [
          // values here are not important
          "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
          "3a8721436b6585d43f8ecd96f9a336df564828285498f3f6c45848aae19d3477",
          "e1d015e337acf8e7ce097ca08429594ebb7a8bb01463d2d9f477492abcaa5f8e",
          "14d5d4e13e84f85f2553642bfd240f753bfe98b28e14bbe300533f9f25671c72",
          "ea5a4e8c59eab078df1e8b645d8637de89f8d9c2ebe5926fe1d1bc02f1f98d25",
          "6ab24ad7e2dd92501ccd8274ac4219780ef6628c861986e3e97b673a4fdb2691",
        ],
      },
    ],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload,
  });

  const notification = await getNotification();
  const notificationMsg = notification.shadowRoot.querySelector(".message");
  rscp.checkForInterestingUnsubmittedCrash =
    _checkForInterestingUnsubmittedCrash;

  const args = JSON.parse(notificationMsg.getAttribute("data-l10n-args"));
  Assert.equal(args.reportCount, 3, "Shows requested crash msg for one crash");

  const closeButton = notification.shadowRoot.querySelector(".close");
  closeButton.click();
  await TestUtils.waitForCondition(
    () => document.querySelector(kNotificationSelector) === null,
    "Waiting for notification to be removed"
  );

  resetDontShowBefore();
});

add_task(async function test_one_crash_notification_click_send_nothrottle() {
  const _checkForInterestingUnsubmittedCrash =
    rscp.checkForInterestingUnsubmittedCrash;
  rscp.checkForInterestingUnsubmittedCrash = async _ => {
    return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
  };

  const payload = {
    current: [],
    created: [
      {
        hashes: [
          // value here is not important
          "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
        ],
      },
    ],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload,
  });

  const notification = await getNotification();

  rscp.checkForInterestingUnsubmittedCrash =
    _checkForInterestingUnsubmittedCrash;

  const _submitReports = UnsubmittedCrashHandler.submitReports;

  let calledWithNoThrottle = false;
  UnsubmittedCrashHandler.submitReports = (reportIDs, from, obj) => {
    if ("noThrottle" in obj && obj.noThrottle) {
      calledWithNoThrottle = true;
    }
  };

  const dontShowBeforeValueOrig = Services.prefs.getIntPref(
    "browser.crashReports.dontShowBefore"
  );

  const sendButton = notification.querySelector(
    '.notification-button[data-l10n-id="pending-crash-reports-send"]'
  );
  sendButton.click();
  await TestUtils.waitForCondition(
    () => document.querySelector(kNotificationSelector) === null,
    "Waiting for notification to be removed"
  );

  await TestUtils.waitForCondition(
    () => calledWithNoThrottle,
    "Waiting for notification send to call submitReports(noThrottle)"
  );

  UnsubmittedCrashHandler.submitReports = _submitReports;

  // It should be incremente of 7 days, check we are between six and 8 to make sure
  // there is no bad intermittence
  const expectedDontShowBeforeValueUp =
    Math.trunc(Date.now() / 1000) + 6 * 86400;
  const expectedDontShowBeforeValueDown =
    Math.trunc(Date.now() / 1000) + 8 * 86400;
  const dontShowBeforeValueSubmit = Services.prefs.getIntPref(
    "browser.crashReports.dontShowBefore"
  );
  if (
    dontShowBeforeValueSubmit >= expectedDontShowBeforeValueDown ||
    dontShowBeforeValueSubmit <= expectedDontShowBeforeValueUp
  ) {
    Assert.equal(
      dontShowBeforeValueSubmit,
      expectedDontShowBeforeValueUp,
      `The dontShowBefore pref was updated from ${dontShowBeforeValueOrig} to ${dontShowBeforeValueSubmit}. Should be [${expectedDontShowBeforeValueDown};${expectedDontShowBeforeValueUp}]`
    );
  }

  resetDontShowBefore();
});

add_task(async function test_multiple_crashes_notification_merge() {
  const _checkForInterestingUnsubmittedCrash =
    rscp.checkForInterestingUnsubmittedCrash;
  rscp.checkForInterestingUnsubmittedCrash = async records => {
    if (records[0].hashes[0] === "1") {
      return ["32721fda-e22a-4a49-bee3-9d4c71f43605"];
    }
    return ["b36dc3a6-d5f0-4afa-acb4-29f6eccb8282"];
  };

  const payload1 = {
    current: [],
    created: [{ hashes: ["1"] }],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload1,
  });

  const notification1 = await getNotification();
  const notification1Msg = notification1.shadowRoot.querySelector(".message");

  const msg1 = notification1Msg.getAttribute("data-l10n-id");
  Assert.equal(
    msg1,
    "requested-crash-reports-message-new",
    "Shows requested crash msg"
  );

  const args1 = JSON.parse(notification1Msg.getAttribute("data-l10n-args"));
  Assert.equal(args1.reportCount, 1, "Shows requested crash msg for one crash");

  const payload2 = {
    current: [],
    created: [{ hashes: ["2"] }],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload2,
  });

  const notification2 = await getNotification();
  const notification2Msg = notification2.shadowRoot.querySelector(".message");

  const msg2 = notification2Msg.getAttribute("data-l10n-id");
  Assert.equal(
    msg2,
    "requested-crash-reports-message-new",
    "Shows requested crash msg"
  );

  const args2 = JSON.parse(notification2Msg.getAttribute("data-l10n-args"));
  Assert.equal(
    args2.reportCount,
    2,
    "Shows requested crash msg for two crashes"
  );

  rscp.checkForInterestingUnsubmittedCrash =
    _checkForInterestingUnsubmittedCrash;

  const _submitReports = UnsubmittedCrashHandler.submitReports;

  let calledWithTwoReports = false;
  UnsubmittedCrashHandler.submitReports = reportIDs => {
    if (reportIDs.length === 2) {
      calledWithTwoReports = true;
    }
  };

  const sendButton = notification2.querySelector(
    '.notification-button[data-l10n-id="pending-crash-reports-send"]'
  );
  sendButton.click();

  await TestUtils.waitForCondition(
    () => calledWithTwoReports,
    "Waiting for notification send to call calledWithTwoReports"
  );

  await TestUtils.waitForCondition(
    () => document.querySelector(kNotificationSelector) === null,
    "Waiting for notification to be removed"
  );

  UnsubmittedCrashHandler.submitReports = _submitReports;

  resetDontShowBefore();
});

add_task(
  async function test_one_crash_one_notification_no_pending_unsubmitted() {
    Assert.ok(
      UnsubmittedCrashHandler.shouldShowPendingSubmissionsNotification(),
      "Scheduled check would show an unsubmitted notification"
    );

    const _checkForInterestingUnsubmittedCrash =
      rscp.checkForInterestingUnsubmittedCrash;
    rscp.checkForInterestingUnsubmittedCrash = async _ => {
      return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
    };

    const payload = {
      current: [],
      created: [
        {
          hashes: [
            // value here is not important
            "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
          ],
        },
      ],
      updated: [],
      deleted: [],
    };

    await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
      data: payload,
    });

    const notification = await getNotification();
    const notificationMsg = notification.shadowRoot.querySelector(".message");

    rscp.checkForInterestingUnsubmittedCrash =
      _checkForInterestingUnsubmittedCrash;

    const msg = notificationMsg.getAttribute("data-l10n-id");
    Assert.equal(
      msg,
      "requested-crash-reports-message-new",
      "Shows requested crash msg"
    );

    const args = JSON.parse(notificationMsg.getAttribute("data-l10n-args"));
    Assert.equal(
      args.reportCount,
      1,
      "Shows requested crash msg for one crash"
    );

    Assert.ok(
      !UnsubmittedCrashHandler.shouldShowPendingSubmissionsNotification(),
      "Scheduled check would NOT show an unsubmitted notification when we show requested notification"
    );

    const closeButton = notification.shadowRoot.querySelector(".close");
    closeButton.click();

    await TestUtils.waitForCondition(
      () => document.querySelector(kNotificationSelector) === null,
      "Waiting for notification to be removed"
    );

    Assert.ok(
      UnsubmittedCrashHandler.shouldShowPendingSubmissionsNotification(),
      "Scheduled check would show an unsubmitted notification again"
    );

    resetDontShowBefore();
  }
);

add_task(
  async function test_one_crash_notification_click_never_show_again_set_pref() {
    Assert.equal(
      Services.prefs.getBoolPref(
        "browser.crashReports.requestedNeverShowAgain"
      ),
      false,
      "Pref is disabled"
    );

    const _checkForInterestingUnsubmittedCrash =
      rscp.checkForInterestingUnsubmittedCrash;
    rscp.checkForInterestingUnsubmittedCrash = async _ => {
      return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
    };

    const payload = {
      current: [],
      created: [
        {
          hashes: [
            // value here is not important
            "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
          ],
        },
      ],
      updated: [],
      deleted: [],
    };

    await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
      data: payload,
    });

    const notification = await getNotification();

    rscp.checkForInterestingUnsubmittedCrash =
      _checkForInterestingUnsubmittedCrash;

    const disableButton = notification.querySelector(
      '.notification-button[data-l10n-id="requested-crash-reports-dont-show-again"]'
    );
    disableButton.click();

    await TestUtils.waitForCondition(
      () => document.querySelector(kNotificationSelector) === null,
      "Waiting for notification to be removed"
    );

    Assert.equal(
      Services.prefs.getBoolPref(
        "browser.crashReports.requestedNeverShowAgain"
      ),
      true,
      "Pref is enabled"
    );

    resetDontShowBefore();
  }
);

add_task(async function test_one_crash_notification_never_show_again() {
  Services.prefs.setBoolPref(
    "browser.crashReports.requestedNeverShowAgain",
    true
  );

  const _checkForInterestingUnsubmittedCrash =
    rscp.checkForInterestingUnsubmittedCrash;
  rscp.checkForInterestingUnsubmittedCrash = async _ => {
    return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
  };

  const payload = {
    current: [],
    created: [
      {
        hashes: [
          // value here is not important
          "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
        ],
      },
    ],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload,
  });

  rscp.checkForInterestingUnsubmittedCrash =
    _checkForInterestingUnsubmittedCrash;

  try {
    await TestUtils.waitForCondition(
      () => document.querySelector(kNotificationSelector) !== null,
      "Waiting for notification to be shown (should not)"
    );
  } catch (ex) {
    if (!ex.includes("timed out after 50 tries")) {
      throw ex;
    }
  }

  resetDontShowBefore();
});

add_task(async function test_one_crash_notification_blocked_before_next_week() {
  const oneWeekAfter = Math.trunc(Date.now() / 1000) + 7 * 86400;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.crashReports.requestedNeverShowAgain", false],
      ["browser.crashReports.dontShowBefore", oneWeekAfter],
    ],
  });

  const _checkForInterestingUnsubmittedCrash =
    rscp.checkForInterestingUnsubmittedCrash;
  rscp.checkForInterestingUnsubmittedCrash = async _ => {
    return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
  };

  const payload = {
    current: [],
    created: [
      {
        hashes: [
          // value here is not important
          "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
        ],
      },
    ],
    updated: [],
    deleted: [],
  };

  await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
    data: payload,
  });

  rscp.checkForInterestingUnsubmittedCrash =
    _checkForInterestingUnsubmittedCrash;

  try {
    await TestUtils.waitForCondition(
      () => document.querySelector(kNotificationSelector) !== null,
      "Waiting for notification to be shown (should not)"
    );
  } catch (ex) {
    if (!ex.includes("timed out after 50 tries")) {
      throw ex;
    }
  }
});

add_task(
  async function test_one_crash_one_notification_not_blocked_one_week_after() {
    const oneWeekBefore = Math.trunc(Date.now() / 1000) - 7 * 86400;
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.crashReports.requestedNeverShowAgain", false],
        ["browser.crashReports.dontShowBefore", oneWeekBefore],
      ],
    });

    const _checkForInterestingUnsubmittedCrash =
      rscp.checkForInterestingUnsubmittedCrash;
    rscp.checkForInterestingUnsubmittedCrash = async _ => {
      return ["989df240-a40c-405a-9a22-f2fc4a31db6c"];
    };

    const payload = {
      current: [],
      created: [
        {
          hashes: [
            // value here is not important
            "2435191bfd64cf0c8cbf0397f1cb5654f778388a3be72cb01502196896f5a0e9",
          ],
        },
      ],
      updated: [],
      deleted: [],
    };

    await RemoteSettings(kRemoteSettingsCollectionName).emit("sync", {
      data: payload,
    });

    const notification = await getNotification();
    const notificationMsg = notification.shadowRoot.querySelector(".message");

    rscp.checkForInterestingUnsubmittedCrash =
      _checkForInterestingUnsubmittedCrash;

    const msg = notificationMsg.getAttribute("data-l10n-id");
    Assert.equal(
      msg,
      "requested-crash-reports-message-new",
      "Shows requested crash msg"
    );

    const args = JSON.parse(notificationMsg.getAttribute("data-l10n-args"));
    Assert.equal(
      args.reportCount,
      1,
      "Shows requested crash msg for one crash"
    );

    const closeButton = notification.shadowRoot.querySelector(".close");
    closeButton.click();
    await TestUtils.waitForCondition(
      () => document.querySelector(kNotificationSelector) === null,
      "Waiting for notification to be removed"
    );
  }
);

/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Verify that SpeechRecognition.install() shows a permission doorhanger and
// that accepting it resolves the promise with true, while dismissing (via the
// Not Now button or the Escape key) resolves with false and removes the
// notification. Also verifies the prompt is skipped entirely once the model
// is already installed.

const ORIGIN = "https://example.com";
const PAGE =
  getRootDirectory(gTestPath).replace("chrome://mochitests/content", ORIGIN) +
  "empty.html";
const MODEL_DOWNLOAD_NOTIFICATION_ID = "speech-recognition-model-download";
const MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID =
  "speech-recognition-model-download-progress";

async function triggerInstall(browser) {
  await SpecialPowers.spawn(browser, [], () => {
    // install() requires transient user activation (it can trigger a large
    // download); synthesize it since this call isn't from a real user gesture.
    content.document.notifyUserGestureActivation();
    // Do not await — we need to return immediately so the popup can appear.
    content.SpeechRecognition.install({
      langs: ["en-US"],
      processLocally: true,
    });
  });
}

async function triggerInstallWithLang(browser, lang) {
  await SpecialPowers.spawn(browser, [lang], selectedLang => {
    content.document.notifyUserGestureActivation();
    content.speechInstallResult = content.SpeechRecognition.install({
      langs: [selectedLang],
      processLocally: true,
    });
  });
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["media.webspeech.recognition.enable", true]],
  });
});

add_task(async function test_install_shows_doorhanger() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    let popupShown = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popupshown"
    );

    await triggerInstall(browser);
    await popupShown;

    let notification = PopupNotifications.getNotification(
      MODEL_DOWNLOAD_NOTIFICATION_ID,
      browser
    );
    ok(notification, "speech-recognition-model-download notification exists");

    // Dismiss without downloading.
    let popupHidden = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popuphidden"
    );
    notification.remove();
    await popupHidden;
  });
});

add_task(async function test_concurrent_downloads_in_two_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.modelHub.testing", false],
      ["media.webspeech.recognition.model-download.prompt.testing", false],
      ["media.navigator.permission.disabled", false],
    ],
  });

  const { MLModelHubService } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ml/MLModelHubService.sys.mjs"
  );
  const { DownloadUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/DownloadUtils.sys.mjs"
  );
  let originalStartDownload = MLModelHubService.prototype._startDownload;
  let originalGetDownloadStatus = DownloadUtils.getDownloadStatus;
  let downloads = [];
  MLModelHubService.prototype._startDownload = function (downloadInfo) {
    downloads.push({ service: this, ...downloadInfo });
    downloadInfo.progressCallback.onProgress(10, 10, 10, 100);
    return new Promise(() => {});
  };
  DownloadUtils.getDownloadStatus = (_loaded, _total, _speed, lastSec) => [
    lastSec == Infinity ? "fresh estimate" : "stale estimate",
    120,
  ];

  try {
    await BrowserTestUtils.withNewTab(PAGE, async browserA => {
      let popupShown = BrowserTestUtils.waitForEvent(
        PopupNotifications.panel,
        "popupshown"
      );
      await triggerInstall(browserA);
      await popupShown;

      let firstPopup = document.getElementById(
        "speech-recognition-model-download-notification"
      );
      await firstPopup.button.updateComplete;
      firstPopup.button.click();
      await TestUtils.waitForCondition(
        () =>
          downloads.length == 1 &&
          PopupNotifications.getNotification(
            MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID,
            browserA
          ),
        "Waiting for the first tab's download progress"
      );
      let status = document.getElementById(
        "speech-recognition-model-download-progress-status"
      );
      is(status.textContent, "fresh estimate", "The first estimate is fresh");

      await BrowserTestUtils.withNewTab(PAGE, async browserB => {
        is(gBrowser.selectedBrowser, browserB, "the second tab is selected");
        await triggerInstallWithLang(browserB, "fr-FR");
        await TestUtils.waitForCondition(
          () =>
            PopupNotifications.getNotification(
              MODEL_DOWNLOAD_NOTIFICATION_ID,
              browserB
            ),
          "Waiting for the second tab's install notification"
        );

        is(
          PopupNotifications.panel.firstElementChild?.notification?.browser,
          browserB,
          "the second tab's install prompt is visible"
        );
        ok(
          PopupNotifications.getNotification(
            MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID,
            browserA
          ),
          "The first tab's download progress is still shown"
        );
        ok(
          PopupNotifications.getNotification(
            MODEL_DOWNLOAD_NOTIFICATION_ID,
            browserB
          ),
          "The second tab gets its own install prompt"
        );

        let secondPopup = document.getElementById(
          "speech-recognition-model-download-notification"
        );
        if (PopupNotifications.panel.state != "open") {
          await BrowserTestUtils.waitForEvent(
            PopupNotifications.panel,
            "popupshown"
          );
        }
        await secondPopup.button.updateComplete;
        secondPopup.button.click();
        await TestUtils.waitForCondition(
          () => downloads.length == 2,
          "Waiting for two active model downloads"
        );
        is(downloads.length, 2, "Both tabs started a model download");

        await TestUtils.waitForCondition(() => {
          let progressNotification = PopupNotifications.getNotification(
            MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID,
            browserB
          );
          return (
            progressNotification &&
            PopupNotifications.panel.firstElementChild?.notification ==
              progressNotification
          );
        }, "Waiting for the second tab's download progress");
        let secondProgress = document.getElementById(
          "speech-recognition-model-download-progress"
        );
        is(secondProgress.value, 10, "The second download starts at 10%");
        downloads[0].progressCallback.onProgress(70, 70, 70, 100);
        await TestUtils.waitForTick();
        ok(
          downloads[1].service._activeDownloads.has(downloads[1].sessionId),
          "The second download is still active"
        );
        is(
          secondProgress.value,
          10,
          "The first download's progress does not change the second tab's progress"
        );

        await BrowserTestUtils.switchTab(
          gBrowser,
          gBrowser.getTabForBrowser(browserA)
        );
        await TestUtils.waitForCondition(
          () => secondProgress.value == 70,
          "The first tab restores its own progress"
        );
        is(
          status.textContent,
          "fresh estimate",
          "Restoring the first tab resets its time-left estimate"
        );
        await BrowserTestUtils.switchTab(
          gBrowser,
          gBrowser.getTabForBrowser(browserB)
        );
        await TestUtils.waitForCondition(
          () => secondProgress.value == 10,
          "The second tab restores its own progress"
        );

        downloads[0].service._completeDownload(downloads[0].sessionId, true);
        await TestUtils.waitForTick();
        is(
          secondProgress.value,
          10,
          "Completing the first download leaves the second tab's progress alone"
        );
        ok(
          !secondProgress
            .closest("popupnotification")
            .hasAttribute("model-download-succeeded"),
          "The second tab remains in progress"
        );
        await BrowserTestUtils.switchTab(
          gBrowser,
          gBrowser.getTabForBrowser(browserA)
        );
        await TestUtils.waitForCondition(
          () =>
            secondProgress
              .closest("popupnotification")
              .hasAttribute("model-download-succeeded"),
          "The first tab shows its completed download"
        );
        await BrowserTestUtils.switchTab(
          gBrowser,
          gBrowser.getTabForBrowser(browserB)
        );
        await TestUtils.waitForCondition(
          () =>
            secondProgress.value == 10 &&
            !secondProgress
              .closest("popupnotification")
              .hasAttribute("model-download-succeeded"),
          "The second tab still shows its active download"
        );

        for (let download of downloads) {
          if (download.service._activeDownloads.has(download.sessionId)) {
            download.service._completeDownload(download.sessionId, true);
          }
        }
        is(
          await SpecialPowers.spawn(
            browserB,
            [],
            () => content.speechInstallResult
          ),
          true,
          "The second tab's install completes after both downloads start"
        );
      });
    });
  } finally {
    for (let download of downloads) {
      if (download.service._activeDownloads.has(download.sessionId)) {
        download.service._completeDownload(download.sessionId, false);
      }
    }
    MLModelHubService.prototype._startDownload = originalStartDownload;
    DownloadUtils.getDownloadStatus = originalGetDownloadStatus;
    await SpecialPowers.popPrefEnv();
  }
});

// Asserts that a content-process install() call resolves with `false`.
// `installResultPromise` must be the raw promise returned from within a
// SpecialPowers.spawn task, so the settlement happens in the content process.
async function assertInstallResolvesFalse(installResultPromise, why) {
  let result;
  try {
    result = await installResultPromise;
  } catch (e) {
    ok(false, `install() unexpectedly rejected in content ${why}: ${e}`);
    throw e;
  }
  is(result, false, `install() resolves false in content ${why}`);
}

async function assertNotificationGone(browser, why) {
  ok(
    !PopupNotifications.getNotification(
      MODEL_DOWNLOAD_NOTIFICATION_ID,
      browser
    ),
    `notification is actually removed (not just dismissed) ${why}`
  );
}

add_task(async function test_install_not_now_resolves_false() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    let popupShown = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popupshown"
    );

    let installResult = SpecialPowers.spawn(browser, [], async () => {
      content.document.notifyUserGestureActivation();
      return content.SpeechRecognition.install({
        langs: ["en-US"],
        processLocally: true,
      });
    });

    await popupShown;

    let notification = PopupNotifications.getNotification(
      MODEL_DOWNLOAD_NOTIFICATION_ID,
      browser
    );
    ok(notification, "Notification present before dismissal");

    let popupHidden = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popuphidden"
    );
    let popup = document.getElementById(
      "speech-recognition-model-download-notification"
    );
    await popup.secondaryButton.updateComplete;
    popup.secondaryButton.click();
    await popupHidden;

    await assertNotificationGone(browser, "after clicking Not Now");
    await assertInstallResolvesFalse(installResult, "after clicking Not Now");
  });
});

// Verify that dismissing the prompt with the Escape key resolves install()
// with false and removes the notification, same as clicking Not Now.
add_task(async function test_install_escape_resolves_false() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    let popupShown = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popupshown"
    );

    let installResult = SpecialPowers.spawn(browser, [], async () => {
      content.document.notifyUserGestureActivation();
      return content.SpeechRecognition.install({
        langs: ["en-US"],
        processLocally: true,
      });
    });

    await popupShown;

    let notification = PopupNotifications.getNotification(
      MODEL_DOWNLOAD_NOTIFICATION_ID,
      browser
    );
    ok(notification, "Notification present before dismissal");

    let popupHidden = BrowserTestUtils.waitForEvent(
      PopupNotifications.panel,
      "popuphidden"
    );
    EventUtils.synthesizeKey("KEY_Escape");
    await popupHidden;

    // Let any pending _update() run before asserting the notification stays gone.
    await TestUtils.waitForTick();
    await assertNotificationGone(browser, "after pressing Escape");
    Assert.notEqual(
      PopupNotifications.panel.state,
      "open",
      "panel did not reopen after Escape"
    );

    await assertInstallResolvesFalse(installResult, "after pressing Escape");
  });
});

add_task(async function test_overlapping_installs_share_prompt_and_result() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.modelHub.testing", true],
      ["media.webspeech.recognition.model-download.prompt.testing", false],
      ["media.navigator.permission.disabled", false],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    let popupShownCount = 0;
    let onPopupShown = () => {
      popupShownCount++;
    };
    PopupNotifications.panel.addEventListener("popupshown", onPopupShown);

    try {
      let popupShown = BrowserTestUtils.waitForEvent(
        PopupNotifications.panel,
        "popupshown"
      );

      await SpecialPowers.spawn(browser, [], () => {
        content.document.notifyUserGestureActivation();
        content.overlappingSpeechInstalls = [
          content.SpeechRecognition.install({
            langs: ["fr-FR"],
            processLocally: true,
          }),
        ];
      });
      await popupShown;

      await SpecialPowers.spawn(browser, [], () => {
        for (let i = 0; i < 2; i++) {
          content.document.notifyUserGestureActivation();
          content.overlappingSpeechInstalls.push(
            content.SpeechRecognition.install({
              langs: ["fr-FR"],
              processLocally: true,
            })
          );
        }
      });

      await TestUtils.waitForTick();
      is(popupShownCount, 1, "overlapping install() calls share one prompt");

      let popup = document.getElementById(
        "speech-recognition-model-download-notification"
      );
      await popup.button.updateComplete;
      popup.button.click();

      let results = await SpecialPowers.spawn(browser, [], () =>
        Promise.all(content.overlappingSpeechInstalls)
      );
      is(
        results.join(","),
        "true,true,true",
        "all overlapping install() calls resolve with the same result"
      );

      let progressNotification = PopupNotifications.getNotification(
        MODEL_DOWNLOAD_PROGRESS_NOTIFICATION_ID,
        browser
      );
      if (progressNotification) {
        let popupHidden;
        if (PopupNotifications.panel.state != "closed") {
          popupHidden = BrowserTestUtils.waitForEvent(
            PopupNotifications.panel,
            "popuphidden"
          );
        }
        progressNotification.remove();
        if (popupHidden) {
          await popupHidden;
        }
      }
    } finally {
      PopupNotifications.panel.removeEventListener("popupshown", onPopupShown);
    }
  });

  await SpecialPowers.popPrefEnv();
});

// install() must skip the permission prompt entirely once the model is
// already installed: there is nothing to download, so nothing to consent to.
add_task(async function test_install_skips_prompt_when_already_installed() {
  // Mock backend; auto-allow the first install so the model becomes installed.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.modelHub.testing", true],
      ["media.webspeech.recognition.model-download.prompt.testing", true],
      ["media.navigator.permission.disabled", true],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await SpecialPowers.spawn(browser, [], () => {
      content.document.notifyUserGestureActivation();
      return content.SpeechRecognition.install({
        langs: ["en-US"],
        processLocally: true,
      });
    });
    let status = await SpecialPowers.spawn(browser, [], () =>
      content.SpeechRecognition.available({
        langs: ["en-US"],
        processLocally: true,
      })
    );
    is(status, "available", "model reports available after first install");
  });

  // Real prompt path. The model is now installed, so a second install() must
  // resolve true without ever showing the permission prompt.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["media.webspeech.recognition.model-download.prompt.testing", false],
      ["media.navigator.permission.disabled", false],
    ],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    let popupShown = false;
    let listener = () => {
      popupShown = true;
    };
    PopupNotifications.panel.addEventListener("popupshown", listener);

    let installed = await SpecialPowers.spawn(browser, [], () => {
      content.document.notifyUserGestureActivation();
      return content.SpeechRecognition.install({
        langs: ["en-US"],
        processLocally: true,
      });
    });

    PopupNotifications.panel.removeEventListener("popupshown", listener);

    ok(installed, "install() resolves true for an already-installed model");
    ok(
      !popupShown,
      "install() must not show the prompt when already installed"
    );
  });

  // One pop per pushPrefEnv() above, not a copy-paste duplicate.
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
});

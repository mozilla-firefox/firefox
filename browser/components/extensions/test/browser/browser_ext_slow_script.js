"use strict";

/* globals ProcessHangMonitor */

const DEFAULT_PROCESS_COUNT = Services.prefs
  .getDefaultBranch(null)
  .getIntPref("dom.ipc.processCount");

add_task(async function test_slow_content_script() {
  // Make sure we get a new process for our tab, or our reportProcessHangs
  // preferences value won't apply to it.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.ipc.processCount", 1],
      ["dom.ipc.keepProcessesAlive.web", 0],
    ],
  });
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.ipc.processCount", DEFAULT_PROCESS_COUNT * 2],
      ["dom.ipc.processPrelaunch.enabled", false],
      ["dom.ipc.reportProcessHangs", true],
      ["dom.max_script_run_time.require_critical_input", false],
    ],
  });

  // Registered before loading the extension so that it runs before the
  // extension is unloaded, and the content script does not keep running if
  // the test fails.
  registerCleanupFunction(() =>
    ProcessHangMonitor.stopAllHangs("test-cleanup")
  );

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",

    manifest: {
      name: "Slow Script Extension",

      content_scripts: [
        {
          matches: ["http://example.com/"],
          js: ["content.js"],
        },
      ],
    },

    files: {
      "content.js": function () {
        while (true) {
          // Busy wait.
        }
      },
    },
  });

  await extension.startup();

  let alert = BrowserTestUtils.waitForGlobalNotificationBar(
    window,
    "process-hang"
  );

  BrowserTestUtils.openNewForegroundTab(gBrowser, "http://example.com/");

  let notification = await alert;
  let text = notification.messageText.textContent;

  ok(text.includes("\u201cSlow Script Extension\u201d"), "Label is correct");

  let stopButton = notification.buttonContainer.querySelector(
    "[data-l10n-id='process-hang-stop-button']"
  );
  stopButton.click();

  BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await extension.unload();
});

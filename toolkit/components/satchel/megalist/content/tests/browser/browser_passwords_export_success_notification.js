/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockFilePicker } = SpecialPowers;
const tempDir = createTemporarySaveDirectory();
MockFilePicker.displayDirectory = tempDir;

function createTemporarySaveDirectory() {
  let saveDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  saveDir.append("testsavedir");
  saveDir.createUnique(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  return saveDir;
}

function waitForOpenFilePicker() {
  return new Promise(resolve => {
    MockFilePicker.showCallback = fp => {
      info("MockFilePicker showCallback");

      let fileName = fp.defaultString;
      let destFile = tempDir.clone();
      destFile.append(fileName);

      MockFilePicker.setFiles([destFile]);
      MockFilePicker.filterIndex = 1;

      resolve();
    };
  });
}

async function clickExportAllPasswords(megalist, megalistParent) {
  MockFilePicker.init(window.browsingContext);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  const getShadowBtn = (el, selector) =>
    el.querySelector(selector).shadowRoot.querySelector("button");
  const menu = megalist.querySelector("panel-list");
  const menuButton = megalist.querySelector("#more-options-menubutton");
  menuButton.click();
  await BrowserTestUtils.waitForEvent(menu, "shown");
  const exportMenuItem = getShadowBtn(menu, "[action='export-logins']");
  const authExpirationTime = megalistParent.authExpirationTime();
  let reauthObserved = Promise.resolve();

  if (OSKeyStore.canReauth() && Date.now() > authExpirationTime) {
    reauthObserved = OSKeyStoreTestUtils.waitForOSKeyStoreLogin(true);
  }
  exportMenuItem.click();

  await reauthObserved;

  async function waitForFilePicker() {
    let filePickerPromise = waitForOpenFilePicker();
    info("Waiting for export file picker to get opened");
    await filePickerPromise;
    Assert.ok(true, "Export file picker opened");
  }

  await waitForFilePicker();
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contextual-password-manager.enabled", true],
      ["signon.rememberSignons", true],
    ],
  });
  registerCleanupFunction(async () => {
    LoginTestUtils.clearData();
    MockFilePicker.cleanup();
    tempDir.remove(true);
  });
});

add_task(async function test_passwords_export_notification() {
  info("Check that notification is shown when user exports all passwords.");
  const canTestOSAuth = await resetTelemetryIfKeyStoreTestable();
  if (!canTestOSAuth) {
    return;
  }
  const megalist = await openPasswordsSidebar();
  await addMockPasswords();
  await checkAllLoginsRendered(megalist);
  await BrowserTestUtils.waitForCondition(
    () => megalist.querySelector(".second-row"),
    "Second row failed to render"
  );
  const originalPromptService = Services.prompt;
  Services.prompt = mockServicePrompt();

  await clickExportAllPasswords(megalist, getMegalistParent());
  ok(true, "Export menu clicked.");
  const notifMsgBar = await checkNotificationAndTelemetry(
    megalist,
    "export-passwords-success"
  );
  ok(true, "Notification for successful export of passwords is shown.");
  checkNotificationInteractionTelemetry(notifMsgBar, "primary-action", {
    notification_detail: "export_passwords_success",
    action_type: "dismiss",
  });

  info("Closing the sidebar");
  SidebarController.hide();
  Services.prompt = originalPromptService;
});

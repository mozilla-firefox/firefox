/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Tests the primary password prompt of the Rust storage backend and the
 * telemetry it records. Unlike NSS, the Rust key manager asks for a new
 * password through `getPrimaryPassword()` after a wrong one, so every dialog
 * is visible here and gets its own event.
 */

"use strict";

const { LoginManagerRustStorage } = ChromeUtils.importESModule(
  "resource://gre/modules/storage-rust.sys.mjs"
);

let rustStore;

/**
 * Answers every primary password dialog that opens until the returned function
 * is called. A single observer registered up front cannot race with the
 * re-prompt, which opens as soon as the previous dialog closes.
 *
 * @param {Array<string|null>} answers
 *        The password to type into each dialog, in order, or null to cancel it.
 * @returns {Function} Stops answering and returns the number of dialogs shown.
 */
function answerPrimaryPasswordDialogs(answers) {
  let shown = 0;
  let observer = subject => {
    let dialog = subject.Dialog;
    let answer = answers[shown++];
    if (answer == null) {
      dialog.ui.button1.click();
      return;
    }
    SpecialPowers.wrap(dialog.ui.password1Textbox).setUserInput(answer);
    dialog.ui.button0.click();
  };
  Services.obs.addObserver(observer, "common-dialog-loaded");
  return () => {
    Services.obs.removeObserver(observer, "common-dialog-loaded");
    return shown;
  };
}

function rustPromptEvents() {
  return (Glean.pwmgr.primaryPasswordPrompt.testGetValue() ?? [])
    .map(event => event.extra)
    .filter(extra => extra.source == "rust_storage");
}

add_setup(async function () {
  await Services.logins.initializationPromise;
  rustStore = new LoginManagerRustStorage();
  await rustStore.initialize();
  Services.fog.initializeFOG();

  // Added while the token is still unlocked, so that reading it back is the
  // only thing that needs the primary password. The store is emptied first so
  // that the count does not depend on what an earlier test file left in it.
  await rustStore.removeAllLoginsAsync();
  await rustStore.addLoginsAsync([LoginTestUtils.testData.formLogin({})]);

  registerCleanupFunction(async () => {
    await LoginTestUtils.primaryPassword.disable();
    await rustStore.removeAllLoginsAsync();
    Services.fog.testResetFOG();
  });
});

add_task(async function test_wrong_password_then_success() {
  await LoginTestUtils.primaryPassword.enable();
  Services.fog.testResetFOG();

  let stopAnswering = answerPrimaryPasswordDialogs([
    "not-the-primary-password",
    LoginTestUtils.primaryPassword.primaryPassword,
  ]);
  let logins = await rustStore.getAllLogins();
  let shown = stopAnswering();

  Assert.equal(shown, 2, "A wrong password is followed by another dialog");
  Assert.equal(logins.length, 1, "The store is readable once unlocked");
  Assert.deepEqual(
    rustPromptEvents(),
    [
      { source: "rust_storage", result: "wrong_password" },
      { source: "rust_storage", result: "success" },
    ],
    "Both dialogs are recorded with their own result"
  );

  await LoginTestUtils.primaryPassword.disable();
});

add_task(async function test_cancel() {
  await LoginTestUtils.primaryPassword.enable();
  Services.fog.testResetFOG();

  let stopAnswering = answerPrimaryPasswordDialogs([null]);
  await Assert.rejects(
    rustStore.getAllLogins(),
    /Primary password locked/,
    "Cancelling the prompt fails the store operation"
  );

  Assert.equal(stopAnswering(), 1, "One dialog was shown");
  Assert.deepEqual(
    rustPromptEvents(),
    [{ source: "rust_storage", result: "cancel" }],
    "Cancelling is recorded"
  );
  Assert.deepEqual(
    Glean.pwmgr.rustStorageCryptoError.testGetValue().map(event => event.extra),
    [{ operation: "list", error_kind: "canceled", direction: "decrypt" }],
    "The failed operation is recorded as cancelled"
  );

  await LoginTestUtils.primaryPassword.disable();
});

add_task(async function test_cancel_while_saving() {
  await LoginTestUtils.primaryPassword.enable();
  Services.fog.testResetFOG();

  let stopAnswering = answerPrimaryPasswordDialogs([null]);
  await Assert.rejects(
    rustStore.addLoginsAsync([
      LoginTestUtils.testData.formLogin({ origin: "https://save.example.com" }),
    ]),
    /Primary password locked/,
    "Cancelling the prompt fails the write"
  );

  Assert.equal(stopAnswering(), 1, "One dialog was shown");
  Assert.deepEqual(
    Glean.pwmgr.rustStorageCryptoError.testGetValue().map(event => event.extra),
    [{ operation: "add", error_kind: "canceled", direction: "encrypt" }],
    "The failed write is recorded as a cancelled encryption"
  );

  await LoginTestUtils.primaryPassword.disable();
});

add_task(async function test_count_does_not_prompt() {
  await LoginTestUtils.primaryPassword.enable();

  let stopAnswering = answerPrimaryPasswordDialogs([null]);
  let login = LoginTestUtils.testData.formLogin({});
  let count = await rustStore.countLoginsAsync(
    login.origin,
    login.formActionOrigin,
    ""
  );

  Assert.equal(stopAnswering(), 0, "No dialog was shown");
  Assert.equal(count, 1, "The matching login is counted while locked");

  await LoginTestUtils.primaryPassword.disable();
});

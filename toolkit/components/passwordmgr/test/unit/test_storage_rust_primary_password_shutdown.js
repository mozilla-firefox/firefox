/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that the Rust storage can be finalized while a store operation waits
 * on the primary password prompt.
 *
 * A window-modal prompt spins a nested event loop, and shutdown can run inside
 * that loop. The prompt then cannot return before shutdown is done, while the
 * store operation waiting for it holds the lock that finalizing needs.
 */

"use strict";

const { LoginManagerRustStorage } = ChromeUtils.importESModule(
  "resource://gre/modules/storage-rust.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

let dialogClosed = false;

const mockPromptService = {
  MODAL_TYPE_WINDOW: Ci.nsIPromptService.MODAL_TYPE_WINDOW,
  numPrompts: 0,

  // Behaves like a window-modal prompt: it only returns once the dialog is
  // closed, spinning the event loop meanwhile.
  asyncPromptPassword() {
    this.numPrompts++;
    Services.tm.spinEventLoopUntil(
      "test_storage_rust_primary_password_shutdown: modal prompt",
      () => dialogClosed
    );
    const result = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
      Ci.nsIWritablePropertyBag2
    );
    result.setPropertyAsBool("ok", false);
    return Promise.resolve(result);
  },
};

const originalPromptService = Services.prompt;
Services.prompt = mockPromptService;
registerCleanupFunction(() => {
  Services.prompt = originalPromptService;
});

add_task(async function test_finalize_while_prompt_is_open() {
  const storage = new LoginManagerRustStorage();
  await storage.initialize();
  await storage.addLoginsWithResultsAsync([
    TestData.formLogin({ origin: "https://example.com" }),
  ]);
  await LoginTestUtils.primaryPassword.enable();

  const listing = storage.getAllLogins();
  await TestUtils.waitForCondition(
    () => mockPromptService.numPrompts == 1,
    "the store operation prompts for the primary password"
  );

  const finalized = storage.finalize().then(() => "finalized");
  const timedOut = new Promise(resolve =>
    do_timeout(5000, () => resolve("timed out"))
  );
  const outcome = await Promise.race([finalized, timedOut]);
  dialogClosed = true;

  Assert.equal(
    outcome,
    "finalized",
    "finalize() does not wait for the prompt to be answered"
  );
  await Assert.rejects(
    listing,
    e => e.result == Cr.NS_ERROR_ABORT,
    "the store operation waiting on the prompt is cancelled"
  );
  await finalized;
});

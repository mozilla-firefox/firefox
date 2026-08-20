/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../file_ime_state_test_helper.js */
/* import-globals-from ../file_test_ime_request_commit.js */

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/widget/tests/browser/file_ime_state_test_helper.js",
  this
);

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/widget/tests/browser/file_test_ime_request_commit.js",
  this
);

add_task(async function () {
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    const tipWrapper = new TIPWrapper(window);
    Assert.ok(
      tipWrapper.isAvailable(),
      "TextInputProcessor should've been initialized"
    );
    await runTest(tipWrapper, browser, true);
  });
});

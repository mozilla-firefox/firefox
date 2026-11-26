/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check that actor gets added when enabled
 */
add_task(async function test_got_actor() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.provider", "http://localhost:8080"]],
  });
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    Assert.ok(
      browser.browsingContext.currentWindowContext.getActor("GenAI"),
      "Got the actor"
    );
  });
});

/**
 * Check that actor not found when disabled
 */
add_task(async function test_actor_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", ""],
      ["browser.ml.chat.page", false],
      ["browser.ml.chat.shortcuts", false],
      ["browser.ml.chat.shortcuts.smartwindow", false],
    ],
  });
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    Assert.throws(
      () => browser.browsingContext.currentWindowContext.getActor("GenAI"),
      /NotFoundError/,
      "Actor disabled"
    );
  });
  await SpecialPowers.popPrefEnv();
});

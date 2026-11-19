/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const emptyPage =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "empty.html";

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.gamepad.test.enabled", true]],
  });
});

async function promiseObserverNotification() {
  await TestUtils.topicObserved("user-characteristics-populating-data-done");

  await GleanPings.userCharacteristics.testSubmission(
    () => {
      let gamepads = Glean.characteristics.gamepads.testGetValue();

      is(gamepads.length, 2, "Two gamepads were reported");

      is(
        gamepads[0],
        `["test gamepad 1","",4,4,0,0,0]`,
        "The first gamepads metrics is expected."
      );

      is(
        gamepads[1],
        `["test gamepad 2","right",10,4,2,0,0]`,
        "The second gamepads metrics is expected."
      );
    },
    () => GleanPings.userCharacteristics.submit()
  );
}

add_task(async function test() {
  Services.fog.testResetFOG();

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: emptyPage },
    async browser => {
      await SpecialPowers.spawn(browser, [], async _ => {
        const GamepadService = content.navigator.requestGamepadServiceTest();

        // Call getGamepads() to start gamepad monitoring. Otherwise, the below
        // addGamepad function will stuck.
        content.navigator.getGamepads();

        info("Add two gamepads");
        await GamepadService.addGamepad(
          "test gamepad 1",
          GamepadService.standardMapping,
          GamepadService.noHand,
          4, // buttons
          4, // axes
          0, // haptics
          0, // lights
          0 // touches
        );

        await GamepadService.addGamepad(
          "test gamepad 2",
          GamepadService.standardMapping,
          GamepadService.rightHand,
          10, // buttons
          4, // axes
          2, // haptics
          0, // lights
          0 // touches
        );
      });

      let promise = promiseObserverNotification();

      Services.obs.notifyObservers(
        null,
        "user-characteristics-testing-please-populate-data"
      );

      await promise;
    }
  );
});

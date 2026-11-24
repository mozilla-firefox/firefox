/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that a video being emptied while it is no longer rendered, but still
 * has a src, is handled correctly based on the auto close pref
 */
add_task(async () => {
  for (let autoCloseEnabledPref of [true, false]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        [AUTO_CLOSE_ENABLED_PREF, autoCloseEnabledPref],
        [AUTO_CLOSE_TIMEOUT_PREF, 0],
      ],
    });
    for (let videoID of ["with-controls", "no-controls"]) {
      info(`Testing ${videoID} case.`);

      await BrowserTestUtils.withNewTab(
        {
          url: TEST_PAGE,
          gBrowser,
        },
        async browser => {
          let pipWin = await triggerPictureInPicture(browser, videoID);
          Assert.ok(pipWin, "Got PiP window.");

          Assert.ok(!pipWin.closed, "PiP window should be open.");

          await SpecialPowers.spawn(browser, [videoID], async videoID => {
            let doc = content.document;
            let video = doc.querySelector(`#${videoID}`);

            video.style.display = "none";
            video.load();
          });

          // Wait for after autoclose's hardcoded time limit to pass
          try {
            await TestUtils.waitForCondition(
              () => pipWin.closed,
              "Player window closed.",
              10,
              2
            );
          } catch {}

          if (autoCloseEnabledPref) {
            Assert.ok(pipWin.closed, "PiP window should be closed.");
          } else {
            Assert.ok(!pipWin.closed, "PiP window should still be open.");
            await BrowserTestUtils.closeWindow(pipWin);
          }
        }
      );
    }
  }
});

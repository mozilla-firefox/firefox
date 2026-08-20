/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function testResizePip(isRFP, src) {
  clearSavedPosition();

  await BrowserTestUtils.withNewTab(
    {
      url: TEST_PAGE,
      gBrowser,
    },
    async browser => {
      let videoID = "with-controls";

      let [width, height, pipWidth, pipHeight] = await SpecialPowers.spawn(
        browser,
        [videoID, src],
        async (videoID, src) => {
          content.window.resizeEverCalled = false;
          let video = content.wrappedJSObject.document.getElementById(videoID);
          if (src) {
            let { promise, resolve } = Promise.withResolvers();
            video.addEventListener("canplay", resolve);
            video.src = src;
            await promise;
          }
          content.document.notifyUserGestureActivation();
          let pip = await video.requestPictureInPicture();
          pip.addEventListener(
            "resize",
            () => (content.window.resizeEverCalled = true)
          );
          return [video.width, video.height, pip.width, pip.height];
        }
      );
      info(
        `Video size is ${width}x${height}. PiP size is ${pipWidth}x${pipHeight}`
      );

      const { PictureInPicture } = ChromeUtils.importESModule(
        "moz-src:///toolkit/components/pictureinpicture/PictureInPicture.sys.mjs"
      );
      let pipWindow = PictureInPicture.apiPipWindow?.get();
      Assert.ok(pipWindow, "We found the chrome PiP window.");

      let pipBrowser = pipWindow.document.getElementById("browser");
      await SpecialPowers.spawn(pipBrowser, [], async () => {
        let {
          promise: setupPromise,
          resolve: setupResolve,
          reject: setupReject,
        } = Promise.withResolvers();
        content.resizePromise = new Promise(resizeResolve => {
          let firstObserved = false;
          // Scope the observer to the content because we need to block on the
          // setup before resizing, therefore we need two separate promises and
          // the observer needs to outlive this spawn call.
          content.observer = new content.ResizeObserver(() => {
            // Attaching the element will trigger a first call to this callback.
            // Therefore, we need to ignore it.
            if (firstObserved) {
              resizeResolve();
            } else {
              firstObserved = true;
              setupResolve();
            }
          });
          let video = content.document.querySelector("video");
          if (video) {
            content.observer.observe(video);
          } else {
            setupReject(new Error("Video not found in the PiP window."));
          }
        });
        await setupPromise;
      });

      pipWindow.resizeTo(pipWidth * 2, pipHeight * 2);
      await SpecialPowers.spawn(pipBrowser, [], async () => {
        await content.resizePromise;
        content.observer.disconnect();
        await new Promise(resolve => content.requestAnimationFrame(resolve));
      });

      let [pipWidthAfter, pipHeightAfter] = await SpecialPowers.spawn(
        browser,
        [videoID],
        async videoID => {
          let video = content.wrappedJSObject.document.getElementById(videoID);
          // We already have the window, no need to simulate another user
          // interaction.
          let pip = await video.requestPictureInPicture();
          return [pip.width, pip.height];
        }
      );
      info(
        `PiP size after resizing ${isRFP ? "with" : "without"} RFP ` +
          `is ${pipWidthAfter}x${pipHeightAfter}`
      );

      if (isRFP) {
        Assert.equal(pipWidth, pipWidthAfter, "RFP spoofed PiP width.");
        Assert.equal(pipHeight, pipHeightAfter, "RFP spoofed PiP height.");
      } else {
        Assert.notEqual(
          pipWidth,
          pipWidthAfter,
          "After resizing the PiP window, PiP width was updated."
        );
        Assert.notEqual(
          pipHeight,
          pipHeightAfter,
          "After resizing the PiP window, PiP height was updated."
        );
      }

      let everResized = await SpecialPowers.spawn(browser, [], async () => {
        await content.document.exitPictureInPicture();
        return content.window.resizeEverCalled;
      });
      Assert.equal(
        everResized,
        !isRFP,
        `We expected the resize handler ${isRFP ? "not " : ""}to be ever called.`
      );
    }
  );
}

add_task(async function test_video_pip_size() {
  await testResizePip(false, "test-video.mp4");
});

add_task(async function test_video_pip_size_vertical() {
  await testResizePip(false, "test-video-vertical.mp4");
});

add_task(async function test_video_pip_size_rfp() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.resistFingerprinting", true]],
  });
  await testResizePip(true, "test-video.mp4");
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_video_pip_size_rfp_vertical() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.resistFingerprinting", true]],
  });
  await testResizePip(true, "test-video-vertical.mp4");
  await SpecialPowers.popPrefEnv();
});

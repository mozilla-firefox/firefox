"use strict";

const TEST_VIDEO =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "file_video.webm";

add_task(async function test_top_level_video_document() {
  let consoleListener = {
    observe(message) {
      if (message.message.includes("Connection-Allowlist")) {
        ok(false, `Unexpected console message: ${message.message}`);
      }
    },
  };
  Services.console.registerListener(consoleListener);
  registerCleanupFunction(() =>
    Services.console.unregisterListener(consoleListener)
  );

  await BrowserTestUtils.withNewTab(TEST_VIDEO, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      const video = content.document.querySelector("video");
      ok(video, "Loaded a VideoDocument");

      await ContentTaskUtils.waitForCondition(
        () =>
          content.getComputedStyle(content.document.documentElement)
            .backgroundColor == "rgb(0, 0, 0)",
        "TopLevelVideoDocument.css is applied"
      );
      await ContentTaskUtils.waitForCondition(
        () => content.document.activeElement == video,
        "TopLevelVideoDocument.js focused the video"
      );
    });
  });
});

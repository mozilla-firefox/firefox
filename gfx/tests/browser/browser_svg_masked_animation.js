/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);

add_task(async function test_svg_masked_animation() {
  registerCleanupFunction(async () => {
    if (Services.profiler.IsActive()) {
      await Services.profiler.StopProfiler();
    }
  });

  for (let { effect, hasNativeClip, expectedBlobs } of [
    { effect: "clip-path", hasNativeClip: false, expectedBlobs: 1 },
    { effect: "mask", hasNativeClip: false, expectedBlobs: 1 },
    { effect: "", hasNativeClip: false, expectedBlobs: 3 },
    { effect: "clip-path", hasNativeClip: true, expectedBlobs: 3 },
  ]) {
    let description = `${effect || "unmasked"}, native clip: ${hasNativeClip}`;
    info(description);
    await SpecialPowers.pushPrefEnv({
      set: [
        ["gfx.webrender.svg-simple-clips", hasNativeClip],
        ["gfx.webrender.svg-mask-propagates-activity", hasNativeClip],
      ],
    });

    await BrowserTestUtils.withNewTab("about:blank", async browser => {
      await ProfilerTestUtils.startProfiler({
        entries: 1024 * 1024,
        features: ["nostacksampling"],
      });

      let contentPid = await SpecialPowers.spawn(
        browser,
        [effect],
        async attribute => {
          content.document.body.innerHTML = `
          <svg width="300" height="200">
            <defs>
              <clipPath id="clip">
                <rect width="300" height="200"/>
              </clipPath>
              <mask id="mask">
                <rect width="300" height="200" fill="white"/>
              </mask>
            </defs>
            <g id="wrapper">
              ${[0, 1, 2]
                .map(
                  i => `<g class="animated" transform="translate(${i},0)">
                    <path d="M0 0 C0 100 100 0 100 100"
                          stroke="blue" fill="none"/>
                  </g>`
                )
                .join("")}
            </g>
          </svg>`;

          if (attribute) {
            let target = attribute == "mask" ? "mask" : "clip";
            content.document
              .getElementById("wrapper")
              .setAttribute(attribute, `url(#${target})`);
          }

          let groups = content.document.querySelectorAll(".animated");
          for (let i = 0; i < 6; i++) {
            await new Promise(resolve =>
              content.requestAnimationFrame(resolve)
            );
            for (let group of groups) {
              group.setAttribute("transform", `translate(${i + 10},0)`);
            }
          }
          await new Promise(resolve => content.requestAnimationFrame(resolve));
          await new Promise(resolve => content.requestAnimationFrame(resolve));
          return Services.appinfo.processID;
        }
      );

      let profile = await ProfilerTestUtils.stopNowAndGetProfile();
      let process = profile.processes.find(p =>
        p.threads.some(thread => thread.pid == contentPid)
      );
      let thread = process.threads.find(t => t.name == "GeckoMain");
      let markers = ProfilerTestUtils.getInflatedMarkerData(thread).filter(
        marker => marker.name == "WebRender blob images"
      );
      Assert.greater(
        markers.length,
        0,
        `${description}: recorded blob statistics`
      );
      let stats = thread.stringTable[markers.at(-1).data.name];
      info(stats);
      let blobCount = Number(/^group blobs: (\d+)/.exec(stats)[1]);
      Assert.equal(blobCount, expectedBlobs, `${description}: SVG blob count`);
    });

    await SpecialPowers.popPrefEnv();
  }
});

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that resolution is restored to its pre-RDM value after closing RDM.
// Do this by using a chrome-only method to force resolution before opening
// RDM, then letting RDM set its own preferred resolution due to the meta
// viewport settings. When we close RDM and check resolution, we check for
// something close to what we initially set, bracketed by these scaling
// factors:
const RESOLUTION_FACTOR_MIN = 0.96;
const RESOLUTION_FACTOR_MAX = 1.04;

info("--- Starting viewport test output ---");

registerCleanupFunction(() => {
  window.windowUtils.setResolutionAndScaleTo(1);
});

const WIDTH = 200;
const HEIGHT = 200;
const TESTS = [
  { content: "width=600" },
  { content: "width=600, initial-scale=1.0", res_restore: 0.782 },
  { content: "width=device-width", res_restore: 3.4 },
  { content: "width=device-width, initial-scale=2.0", res_restore: 1.1 },
];

for (const { content, res_restore } of TESTS) {
  const TEST_URL =
    `data:text/html;charset=utf-8,` +
    `<html><head><meta name="viewport" content="${content}"></head>` +
    `<body><div style="width:100%;background-color:green">${content}</div>` +
    `</body></html>`;

  addRDMTaskWithPreAndPost(
    TEST_URL,
    async function rdmPreTask({ browser }) {
      if (res_restore) {
        info(`Setting resolution to ${res_restore}.`);
        browser.documentGlobal.windowUtils.setResolutionAndScaleTo(res_restore);
      } else {
        info(`Not setting resolution.`);
      }
    },
    async function rdmTask({ ui, manager }) {
      info(`Resizing viewport and ensuring that meta viewport is on.`);
      await setViewportSize(ui, manager, WIDTH, HEIGHT);
      await setTouchAndMetaViewportSupport(ui, true);
    },
    async function rdmPostTask({ browser }) {
      const resolution = browser.documentGlobal.windowUtils.getResolution();
      const res_target = res_restore ? res_restore : 1.0;

      const res_min = res_target * RESOLUTION_FACTOR_MIN;
      const res_max = res_target * RESOLUTION_FACTOR_MAX;
      ok(
        res_min <= resolution && res_max >= resolution,
        `${content} resolution should be near ${res_target}, and we got ${resolution}.`
      );
    }
  );
}

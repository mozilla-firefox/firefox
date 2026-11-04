/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Use ASAP mode to reduce frame pacing delays during interactions.
//
// perfMetadata is parsed by esprima (ES2017): no optional chaining, nullish
// coalescing or object spread in this file.

const perfMetadata = {
  owner: "PDF.js Team",
  name: "browser_pdfjs_ui_interaction_perf.js",
  description:
    "Time for the PDF.js viewer to scroll, zoom and go to pages in PDFs with 100 and 1000 empty pages",
  options: {
    default: {
      extra_args: [
        "setpref=layout.frame_rate=0",
        "setpref=docshell.event_starvation_delay_hint=1",
      ],
      perfherder: true,
      perfherder_metrics: [
        { name: "PDFJS-UI-scroll-100p", unit: "ms", shouldAlert: true },
        { name: "PDFJS-UI-zoom-100p", unit: "ms", shouldAlert: true },
        { name: "PDFJS-UI-goto-1000p", unit: "ms", shouldAlert: true },
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

const PASSES = 5;
const GOTO_PASSES = 10;
const ZOOM_SCALES = [0.5, 1, 1.5, 2, 3, 4, 3, 2, 1.5, 1, 0.5, 1];
// Shift destinations between passes to avoid cached pages.
const GOTO_PAGES = [1000, 1, 500, 125, 875, 250, 750, 375];
const GOTO_SHIFT = 10;

requestLongerTimeout(5);

add_task(async function test_interactions() {
  const scroll = [];
  const zoom = [];
  const gotoTimes = [];

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      const measure = (type, steps) =>
        SpecialPowers.spawn(
          browser,
          [{ type, steps }],
          measureViewerInteraction
        );
      await openPdf(browser, "file_pdfjs_empty_100.pdf");
      for (let i = 0; i < PASSES; i++) {
        scroll.push(await measure("scroll"));
        zoom.push(await measure("zoom", ZOOM_SCALES));
      }
      await closePdf(browser);

      await openPdf(browser, "file_pdfjs_empty_1000.pdf");
      for (let i = 0; i < GOTO_PASSES; i++) {
        const shift = GOTO_SHIFT * i;
        const steps = GOTO_PAGES.map(page =>
          page > 500 ? page - shift : page + shift
        );
        gotoTimes.push(await measure("goto", steps));
      }
      await closePdf(browser);
    }
  );

  reportMetrics({
    "PDFJS-UI-scroll-100p": scroll,
    "PDFJS-UI-zoom-100p": zoom,
    "PDFJS-UI-goto-1000p": gotoTimes,
  });
});

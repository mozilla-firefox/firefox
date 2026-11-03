/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Measure opening times from the viewer's navigation start, reporting the
// first open separately.
//
// perfMetadata is parsed by esprima (ES2017): no optional chaining, nullish
// coalescing or object spread in this file.

const perfMetadata = {
  owner: "PDF.js Team",
  name: "browser_pdfjs_ui_open_perf.js",
  description:
    "Time for the PDF.js viewer to open PDFs with 1 and 1000 empty pages, from the navigation start",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        {
          name: "PDFJS-UI-first-open-pagesinit",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PDFJS-UI-first-open-pagerendered",
          unit: "ms",
          shouldAlert: false,
        },
        { name: "PDFJS-UI-open-1p-pagesinit", unit: "ms", shouldAlert: true },
        {
          name: "PDFJS-UI-open-1p-pagerendered",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "PDFJS-UI-open-1p-render-delay",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PDFJS-UI-open-1000p-pagesinit",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "PDFJS-UI-open-1000p-pagerendered",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "PDFJS-UI-open-1000p-pagesloaded",
          unit: "ms",
          shouldAlert: true,
        },
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

const OPENS = 15;
const MARKS = ["pdfjs:pagesinit", "pdfjs:pagerendered:1", "pdfjs:pagesloaded"];

requestLongerTimeout(5);

add_task(async function test_open() {
  recordViewerEventsAsMarks(["pagesinit", "pagerendered", "pagesloaded"]);
  const metrics = {};
  const record = (name, value) => {
    if (!metrics[name]) {
      metrics[name] = [];
    }
    metrics[name].push(value);
  };

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      const runs = [
        { label: "first-open", file: "file_pdfjs_empty_1.pdf", count: 1 },
        { label: "open-1p", file: "file_pdfjs_empty_1.pdf", count: OPENS },
        {
          label: "open-1000p",
          file: "file_pdfjs_empty_1000.pdf",
          count: OPENS,
        },
      ];
      for (const { label, file, count } of runs) {
        for (let i = 0; i < count; i++) {
          await openPdf(browser, file);
          const [pagesinit, pagerendered, pagesloaded] = await readMarks(
            browser,
            MARKS
          );
          record(`PDFJS-UI-${label}-pagesinit`, pagesinit);
          record(`PDFJS-UI-${label}-pagerendered`, pagerendered);
          if (label === "open-1p") {
            record("PDFJS-UI-open-1p-render-delay", pagerendered - pagesinit);
          } else if (label === "open-1000p") {
            record("PDFJS-UI-open-1000p-pagesloaded", pagesloaded);
          }
          await closePdf(browser);
        }
      }
    }
  );

  reportMetrics(metrics);
});

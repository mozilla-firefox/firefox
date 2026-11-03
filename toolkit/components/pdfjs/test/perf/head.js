/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TESTROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content/",
  "https://example.com/"
);

let gLoadCount = 0;

/**
 * Record viewer events as marks in the content document's performance timeline.
 *
 * @param {Array<string>} eventNames
 */
function recordViewerEventsAsMarks(eventNames) {
  for (const eventName of eventNames) {
    const remove = BrowserTestUtils.addContentEventListener(
      gBrowser.selectedBrowser,
      eventName,
      () => {},
      { wantUntrusted: true },
      event => {
        const detail = event.detail && Cu.waiveXrays(event.detail);
        let name = `pdfjs:${event.type}`;
        if (detail?.pageNumber) {
          name += `:${detail.pageNumber}`;
          if (detail.isDetailView) {
            name += ":detail";
          }
          if (detail.cssTransform) {
            name += ":css";
          }
        }
        event.target.defaultView?.performance.mark(name, {
          startTime: detail?.timestamp ?? event.timeStamp,
        });
        return false;
      }
    );
    registerCleanupFunction(remove);
  }
}

/**
 * Load a PDF at 100% zoom and wait for pagesloaded.
 *
 * @param {MozBrowser} browser
 * @param {string} file
 */
async function openPdf(browser, file) {
  const loaded = BrowserTestUtils.waitForContentEvent(
    browser,
    "pagesloaded",
    false,
    null,
    true
  );
  // Use a distinct URL for each navigation.
  BrowserTestUtils.startLoadingURIString(
    browser,
    `${TESTROOT}${file}?load=${++gLoadCount}#zoom=100`
  );
  await loaded;
}

async function closePdf(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    await content.wrappedJSObject.PDFViewerApplication.testingClose();
    Cu.forceGC();
    Cu.forceCC();
    await new Promise(resolve => content.requestIdleCallback(resolve));
  });
}

/**
 * @param {MozBrowser} browser
 * @param {Array<string>} names
 * @returns {Promise<Array<number>>} The time of the first mark with each name.
 */
async function readMarks(browser, names) {
  const times = await SpecialPowers.spawn(browser, [names], markNames =>
    markNames.map(
      name => content.performance.getEntriesByName(name, "mark")[0]?.startTime
    )
  );
  for (const [i, time] of times.entries()) {
    Assert.ok(Number.isFinite(time), `Got a time for ${names[i]}`);
  }
  return times;
}

/**
 * Time interactions, waiting for the viewer's page rendering queue to be idle.
 *
 * @param {object} interaction
 * @param {string} interaction.type "scroll", "zoom", or "goto".
 * @param {Array<number>} [interaction.steps] Zoom scales or page numbers.
 * @returns {Promise<number>} The total time of the interaction, in milliseconds.
 */
async function measureViewerInteraction({ type, steps }) {
  const app = content.wrappedJSObject.PDFViewerApplication;
  const viewer = app.pdfViewer;
  const { container } = viewer;

  const nextEvent = (eventType, predicate = () => true) =>
    new Promise(resolve => {
      const listener = () => {
        if (predicate()) {
          content.document.removeEventListener(eventType, listener);
          resolve();
        }
      };
      content.document.addEventListener(eventType, listener);
    });
  // Match PDFViewer.forceRendering's direction at document boundaries.
  const isIdle = () => {
    const visible = viewer._getVisiblePages();
    let scrollAhead = viewer.scroll.down;
    if (visible.first?.id === 1) {
      scrollAhead = true;
    } else if (visible.last?.id === viewer.pagesCount) {
      scrollAhead = false;
    }
    return (
      app.pdfRenderingQueue.getHighestPriority(
        visible,
        viewer._pages,
        scrollAhead
      ) === null
    );
  };
  const nextFrame = () =>
    new Promise(resolve => content.requestAnimationFrame(resolve));
  const settled = async () => {
    do {
      if (!isIdle()) {
        await nextEvent("pagerendered", isIdle);
      }
      await nextFrame();
    } while (!isIdle());
  };
  const scrollTo = async top => {
    if (container.scrollTop !== top) {
      const updated = nextEvent("updateviewarea");
      container.scrollTop = top;
      await updated;
    }
    await settled();
  };
  const time = async action => {
    const start = content.performance.now();
    await action();
    await settled();
    return content.performance.now() - start;
  };

  await scrollTo(0);
  if (type === "scroll") {
    const containerTop = container.getBoundingClientRect().top;
    const maxScroll = container.scrollHeight - container.clientHeight;
    const tops = [];
    for (const { div } of viewer._pages) {
      const top = Math.min(
        Math.round(div.getBoundingClientRect().top - containerTop),
        maxScroll
      );
      if (top > 0 && top !== tops.at(-1)) {
        tops.push(top);
      }
    }
    return time(async () => {
      for (const top of tops) {
        await scrollTo(top);
      }
    });
  }
  if (type !== "zoom" && type !== "goto") {
    throw new Error(`Unknown interaction: ${type}`);
  }

  let total = 0;
  for (const step of steps) {
    total += await time(async () => {
      // The first visible page may precede the target.
      const updated = nextEvent(
        "updateviewarea",
        () => type === "zoom" || viewer._getVisiblePages().ids.has(step)
      );
      if (type === "zoom") {
        viewer.currentScale = step;
      } else {
        viewer.currentPageNumber = step;
      }
      await updated;
    });
  }
  return total;
}

function median(values) {
  const sorted = values.toSorted((a, b) => a - b);
  const middle = sorted.length >> 1;
  return sorted.length % 2
    ? sorted[middle]
    : (sorted[middle - 1] + sorted[middle]) / 2;
}

/**
 * Report each metric's replicates and median to mozperftest.
 *
 * @param {{[name: string]: Array<number>}} metrics
 */
function reportMetrics(metrics) {
  const results = Object.entries(metrics).map(([name, values]) => {
    Assert.ok(
      values.length && values.every(Number.isFinite),
      `Got replicates for ${name}`
    );
    return { name, values, value: median(values) };
  });
  info(`perfMetrics | ${JSON.stringify(results)}`);
}

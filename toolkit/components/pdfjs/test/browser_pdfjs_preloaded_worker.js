/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const RELATIVE_DIR = "toolkit/components/pdfjs/test/";
const TESTROOT = "https://example.com/browser/" + RELATIVE_DIR;
const PDF_EMPTY_SERVER = `${TESTROOT}pdf_empty.sjs`;
const PDF_TRUNCATED_SERVER = `${TESTROOT}pdf_truncated.sjs`;
const PDF_URL = `${TESTROOT}file_pdfjs_test.pdf`;
const NON_PDF_URL =
  "https://example.com/document-builder.sjs?html=" +
  encodeURIComponent("<!DOCTYPE html>Not a PDF.");
const BLANK_URL = "about:blank";

async function test_pdf_empty(disableStream, browser) {
  info(`Loading an empty PDF response with disableStream=${disableStream}.`);

  await SpecialPowers.pushPrefEnv({
    set: [
      ["pdfjs.disableStream", disableStream],
      ["pdfjs.disableWorkerRendering", false],
      ["pdfjs.rendererWorkerCount", 2],
    ],
  });

  const loaded = BrowserTestUtils.browserLoaded(
    browser,
    false,
    PDF_EMPTY_SERVER
  );
  BrowserTestUtils.startLoadingURIString(browser, PDF_EMPTY_SERVER);
  await loaded;

  await SpecialPowers.spawn(browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.wrappedJSObject.pdfjsPreloadedWorker === null,
      "Waiting for the preloaded worker reference to be cleared"
    );
    Assert.strictEqual(
      content.wrappedJSObject.pdfjsPreloadedWorker,
      null,
      "The parser worker reference is cleared on an empty response"
    );
    Assert.strictEqual(
      content.wrappedJSObject.pdfjsPreloadedRendererWorkers,
      null,
      "The renderer worker references are cleared on an empty response"
    );
    Assert.equal(
      content.wrappedJSObject.PDFViewerApplicationOptions.get("rendererPorts")
        .length,
      0,
      "The viewer no longer holds the closed renderer workers"
    );
  });

  await SpecialPowers.popPrefEnv();
}

add_task(async function test_pdf_empty_no_stream_request() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    test_pdf_empty.bind(null, true)
  );
});

add_task(async function test_pdf_empty_with_stream_request() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    test_pdf_empty.bind(null, false)
  );
});

add_task(async function test_pdf_truncated_stream_keeps_worker() {
  await SpecialPowers.pushPrefEnv({
    // Report the short response as NS_ERROR_NET_PARTIAL_TRANSFER.
    set: [["network.http.enforce-framing.http1", true]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    async browser => {
      await waitForPdfJS(browser, PDF_TRUNCATED_SERVER);

      await SpecialPowers.spawn(browser, [], () => {
        const win = content.wrappedJSObject;
        Assert.strictEqual(
          win.PDFViewerApplicationOptions.get("workerPort"),
          win.pdfjsPreloadedWorker,
          "The viewer is still using the preloaded worker"
        );
      });

      await waitForPdfJSClose(browser);
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_pdf_load_uses_preloaded_worker() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    async browser => {
      await waitForPdfJS(browser, PDF_URL);

      await SpecialPowers.spawn(browser, [], () => {
        const win = content.wrappedJSObject;
        const worker = win.pdfjsPreloadedWorker;
        Assert.ok(worker, "The preloaded worker has not been destroyed");

        // AppOptions reads the preloaded worker during module evaluation.
        Assert.strictEqual(
          win.PDFViewerApplicationOptions.get("workerPort"),
          worker,
          "The document has been rendered with the preloaded worker"
        );
      });

      await waitForPdfJSClose(browser);
    }
  );
});

add_task(async function test_pdf_reopen_reuses_preloaded_worker() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    async browser => {
      await waitForPdfJS(browser, PDF_URL);

      await SpecialPowers.spawn(browser, [], async () => {
        const win = content.wrappedJSObject;
        const { PDFViewerApplication: app } = win;
        const worker = win.pdfjsPreloadedWorker;
        const data = await app.pdfDocument.getData();
        const pagesLoaded = new Promise(resolve => {
          app.eventBus.on("pagesloaded", resolve, { once: true });
        });

        await app.open(Cu.cloneInto({ data }, win));
        await pagesLoaded;

        Assert.strictEqual(
          win.PDFViewerApplicationOptions.get("workerPort"),
          worker,
          "The reopened document reuses the preloaded worker"
        );
      });

      await waitForPdfJSClose(browser);
    }
  );
});

add_task(async function test_pdf_uses_preloaded_renderer_workers() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["pdfjs.disableWorkerRendering", false],
      ["pdfjs.rendererWorkerCount", 2],
    ],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    async browser => {
      await waitForPdfJS(browser, PDF_URL);

      await SpecialPowers.spawn(browser, [], async () => {
        const win = content.wrappedJSObject;
        const { PDFViewerApplication: app } = win;
        const workers = win.pdfjsPreloadedRendererWorkers;
        Assert.equal(workers.length, 2, "Two renderer workers are preloaded");
        Assert.strictEqual(
          win.PDFViewerApplicationOptions.get("rendererPorts"),
          workers,
          "The viewer uses the preloaded renderer array"
        );

        const checkWorkerRendering = async () => {
          Assert.strictEqual(
            app.pdfLoadingTask._rendererWorker.port,
            workers[0],
            "The document uses the first preloaded renderer worker"
          );
          const renderTask = await new Promise(resolve => {
            app.eventBus.on(
              "pagerender",
              evt => resolve(Cu.waiveXrays(evt).source.renderTask),
              { once: true }
            );
            app.pdfViewer.currentScale *= 2;
          });
          await renderTask.promise;
          Assert.ok(
            renderTask.isWorkerRendering,
            "The page rendered in a worker"
          );
        };

        await checkWorkerRendering();

        const data = await app.pdfDocument.getData();
        // Wait for initial zoom restoration before changing the scale.
        const reopened = Promise.all(
          ["pagesloaded", "documentinit"].map(
            name =>
              new Promise(resolve => {
                app.eventBus.on(name, resolve, { once: true });
              })
          )
        );
        await app.open(Cu.cloneInto({ data }, win));
        await reopened;

        await checkWorkerRendering();
      });

      await waitForPdfJSClose(browser);
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_disabled_worker_rendering_skips_preloading() {
  await SpecialPowers.pushPrefEnv({
    set: [["pdfjs.disableWorkerRendering", true]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    async browser => {
      await waitForPdfJS(browser, PDF_URL);

      await SpecialPowers.spawn(browser, [], () => {
        Assert.strictEqual(
          content.wrappedJSObject.pdfjsPreloadedRendererWorkers,
          undefined,
          "Disabling worker rendering skips renderer preloading"
        );
      });

      await waitForPdfJSClose(browser);
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_disable_worker_skips_preloading() {
  await SpecialPowers.pushPrefEnv({
    set: [["pdfjs.pdfBugEnabled", true]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: BLANK_URL },
    async browser => {
      await waitForPdfJS(browser, `${PDF_URL}#disableworker=true`);

      await SpecialPowers.spawn(browser, [], () => {
        Assert.strictEqual(
          content.wrappedJSObject.pdfjsPreloadedWorker,
          undefined,
          "No worker is preloaded when workers are disabled"
        );
      });

      await waitForPdfJSClose(browser);
    }
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_no_worker_for_non_pdf_document() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: NON_PDF_URL },
    async browser => {
      await SpecialPowers.spawn(browser, [], () => {
        // Actor creation alone must not preload a worker.
        content.windowGlobalChild.getActor("PdfJs");
        Assert.strictEqual(
          content.wrappedJSObject.pdfjsPreloadedWorker,
          undefined,
          "No worker has been preloaded in a non-PDF document"
        );
      });
    }
  );
});

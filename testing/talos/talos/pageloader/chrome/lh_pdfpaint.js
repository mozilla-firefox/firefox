/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// OptionKind.API in pdf.js web/app_options.js.
const PDFJS_API_OPTIONS = 0x04;

// Render the PDF named by the placeholder's `file` parameter.
async function _pdfPaint() {
  const win = content.wrappedJSObject;
  const {
    PDFViewerApplication: app,
    PDFViewerApplicationOptions: options,
    pdfjsLib,
  } = win;

  // Release the placeholder's worker connections.
  await app.close();

  const file = new content.URLSearchParams(content.location.search).get("file");
  const url = new content.URL(file, content.location.href).href;
  // The viewer CSP blocks HTTP fetches.
  const sandbox = Cu.Sandbox(
    Services.scriptSecurityManager.getSystemPrincipal(),
    { wantGlobalProperties: ["fetch"] }
  );
  const response = await sandbox.fetch(url);
  if (!response.ok) {
    throw new Error(`Cannot fetch ${url}: ${response.status}`);
  }
  const params = options.getAll(PDFJS_API_OPTIONS);
  params.data = Cu.cloneInto(new Uint8Array(await response.arrayBuffer()), win);

  const loadingTask = pdfjsLib.getDocument(params);
  try {
    const pdfDocument = Cu.waiveXrays(await loadingTask.promise);
    const page = Cu.waiveXrays(await pdfDocument.getPage(1));
    const viewport = page.getViewport(
      Cu.cloneInto({ scale: pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS }, win)
    );
    const outputScale = new pdfjsLib.OutputScale();
    outputScale.limitCanvas(
      viewport.width,
      viewport.height,
      options.get("maxCanvasPixels"),
      options.get("maxCanvasDim")
    );
    const { sx, sy } = outputScale;
    const canvas = content.document.createElement("canvas");
    canvas.width = Math.floor(viewport.width * sx);
    canvas.height = Math.floor(viewport.height * sy);

    const renderParams = Cu.waiveXrays(
      Cu.cloneInto(
        {
          annotationMode: options.get("annotationMode"),
          transform: outputScale.scaled ? [sx, 0, 0, sy, 0, 0] : null,
          recordImages: options.get("imagesRightClickMinSize") !== -1,
          partialFrames: true,
        },
        win
      )
    );
    renderParams.canvas = canvas;
    renderParams.viewport = viewport;
    renderParams.optionalContentConfigPromise =
      pdfDocument.getOptionalContentConfig();

    // Exclude renderer worker startup from the measurement.
    await loadingTask._rendererWorker?.promise;
    const start = ChromeUtils.now();
    await page.render(renderParams).promise;
    return ChromeUtils.now() - start;
  } finally {
    await loadingTask.destroy();
  }
}

async function _pdfPaintHandler() {
  try {
    sendAsyncMessage("PageLoader:LoadEvent", {
      time: await _pdfPaint(),
      name: "pdfpaint",
    });
  } catch (error) {
    sendAsyncMessage("PageLoader:Error", {
      msg: `pdfpaint: ${error}`,
    });
  }
}

addEventListener(
  "DOMContentLoaded",
  () => {
    content.window.addEventListener("pagerendered", _pdfPaintHandler, {
      once: true,
    });
  },
  true
);

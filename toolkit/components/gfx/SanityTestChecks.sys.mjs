/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { setTimeout, clearTimeout } from "resource://gre/modules/Timer.sys.mjs";

const XHTML_NS = "http://www.w3.org/1999/xhtml";

const FRAME_SCRIPT_URL = "resource://gfxsanity/gfxFrameScript.js";
const PARENT_URL = "chrome://gfxsanity/content/sanityparent.html";

const PAGE_WIDTH = 160;
const PAGE_HEIGHT = 234;
const LEFT_EDGE = 8;
const TOP_EDGE = 8;
const CANVAS_WIDTH = 32;
const CANVAS_HEIGHT = 64;
// If those values are ever changed, make sure to update
// WMFVideoMFTManager::CanUseDXVA accordingly.
const DECODE_WIDTH = 132;
const DECODE_HEIGHT = 132;

const MEDIA_ENGINE_PREF = "media.wmf.media-engine.enabled";

const TIMEOUT_MS = 20000;

/** Glean.gfx.sanityTest values. */
export const SanityCheckResult = {
  Passed: 0,
  FailedRender: 1,
  FailedVideoDecode: 2,
  Crashed: 3,
  Timeout: 4,
  FailedToRun: 5,
  FailedVideoEncode: 6,
  PassedNoHardwareEncoder: 7, // internal
};

export const CodecTestResult = {
  Passed: "passed",
  Failed: "failed",
  Unsupported: "unsupported",
};

const ENCODE_WIDTH = 320;
const ENCODE_HEIGHT = 240;
const ENCODE_CODEC = "avc1.42001E";
const ENCODE_TIMEOUT_MS = 5000;
const ENCODE_REPLY_TIMEOUT_MS = 10000;

/** Error thrown for timeouts. */
class TimeoutError extends Error {}

function withTimeout(promise, timeoutMs, message) {
  let timer;
  const timeout = new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(new TimeoutError(message)), timeoutMs);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

function testPixel(ctx, x, y, r, g, b, a, fuzz) {
  var data = ctx.getImageData(x, y, 1, 1);

  if (
    Math.abs(data.data[0] - r) <= fuzz &&
    Math.abs(data.data[1] - g) <= fuzz &&
    Math.abs(data.data[2] - b) <= fuzz &&
    Math.abs(data.data[3] - a) <= fuzz
  ) {
    return true;
  }
  ChromeUtils.addProfilerMarker(
    "GfxSanityTest",
    { category: "Graphics" },
    `pixel (${x}, ${y}) is ${Array.from(data.data).join(",")}, expected ${r},${g},${b},${a}`
  );
  return false;
}

function takeWindowSnapshot(win, ctx) {
  // TODO: drawWindow reads back from the gpu's backbuffer, which won't catch issues with presenting
  // the front buffer via the window manager. Ideally we'd use an OS level API for reading back
  // from the desktop itself to get a more accurate test.
  var flags =
    ctx.DRAWWINDOW_DRAW_CARET |
    ctx.DRAWWINDOW_DRAW_VIEW |
    ctx.DRAWWINDOW_USE_WIDGET_LAYERS;
  ctx.drawWindow(win, 0, 0, PAGE_WIDTH, PAGE_HEIGHT, "rgb(255,255,255)", flags);
}

// Verify that all the 4 coloured squares of the video
// render as expected (with a tolerance of 64 to allow for
// yuv->rgb differences between platforms).
//
// The video is DECODE_WIDTH*DECODE_HEIGHT, and is split into quadrants of
// different colours. The top left of the video is LEFT_EDGE,TOP_EDGE+CANVAS_HEIGHT
// and we test a pixel into the middle of each quadrant to avoid
// blending differences at the edges.
//
// We allow massive amounts of fuzz for the colours since
// it can depend hugely on the yuv -> rgb conversion, and
// we don't want to fail unnecessarily.
function verifyDecodeRendering(ctx) {
  return (
    testPixel(
      ctx,
      LEFT_EDGE + DECODE_WIDTH / 4,
      TOP_EDGE + CANVAS_HEIGHT + DECODE_HEIGHT / 4,
      255,
      255,
      255,
      255,
      64
    ) &&
    testPixel(
      ctx,
      LEFT_EDGE + (3 * DECODE_WIDTH) / 4,
      TOP_EDGE + CANVAS_HEIGHT + DECODE_HEIGHT / 4,
      0,
      255,
      0,
      255,
      64
    ) &&
    testPixel(
      ctx,
      LEFT_EDGE + DECODE_WIDTH / 4,
      TOP_EDGE + CANVAS_HEIGHT + (3 * DECODE_HEIGHT) / 4,
      0,
      0,
      255,
      255,
      64
    ) &&
    testPixel(
      ctx,
      LEFT_EDGE + (3 * DECODE_WIDTH) / 4,
      TOP_EDGE + CANVAS_HEIGHT + (3 * DECODE_HEIGHT) / 4,
      255,
      0,
      0,
      255,
      64
    )
  );
}

// Verify that the middle of the layers test is the color we expect.
// It's a red CANVAS_WIDTHxCANVAS_HEIGHT square, test a pixel deep into the
// square to prevent fuzzing, and another outside the expected limit of the
// square to check that scaling occurred properly. The square is drawn LEFT_EDGE
// pixels from the window's left edge and TOP_EDGE from the window's top edge.
function verifyLayersRendering(ctx) {
  return (
    testPixel(
      ctx,
      LEFT_EDGE + CANVAS_WIDTH / 2,
      TOP_EDGE + CANVAS_HEIGHT / 2,
      255,
      0,
      0,
      255,
      64
    ) &&
    testPixel(
      ctx,
      LEFT_EDGE + CANVAS_WIDTH,
      TOP_EDGE + CANVAS_HEIGHT / 2,
      255,
      255,
      255,
      255,
      64
    )
  );
}

/**
 * Snapshot the test window and verify the compositor rendered it correctly.
 *
 * @param {Window} win The sanity test window.
 * @returns {number} A SanityCheckResult.
 */
function testCompositor(win) {
  const canvas = win.document.createElementNS(XHTML_NS, "canvas");
  canvas.setAttribute("width", PAGE_WIDTH);
  canvas.setAttribute("height", PAGE_HEIGHT);
  const ctx = canvas.getContext("2d");

  takeWindowSnapshot(win, ctx);

  if (!verifyLayersRendering(ctx)) {
    return SanityCheckResult.FailedRender;
  }

  if (!verifyDecodeRendering(ctx)) {
    return SanityCheckResult.FailedVideoDecode;
  }

  return SanityCheckResult.Passed;
}

function encodeConfig() {
  return {
    codec: ENCODE_CODEC,
    width: ENCODE_WIDTH,
    height: ENCODE_HEIGHT,
    hardwareAcceleration: "prefer-hardware",
    avc: { format: "annexb" },
  };
}

function makeEncodeCanvas(win, width, height) {
  const canvas = win.document.createElementNS(XHTML_NS, "canvas");
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "rgb(0, 0, 255)";
  ctx.fillRect(0, 0, width, height);
  ctx.fillStyle = "rgb(255, 255, 0)";
  ctx.fillRect(0, 0, width / 2, height / 2);
  ctx.fillStyle = "rgb(255, 0, 0)";
  ctx.fillRect(width / 2, height / 2, width / 2, height / 2);
  return canvas;
}

/**
 * Encode a single keyframe with a hardware-only H.264 encoder.
 *
 * @param {Window} win The window whose WebCodecs implementation to use.
 * @returns {Promise<{status: string, reason: string}>}
 */
export async function testVideoEncode(win) {
  if (!win.VideoEncoder || !win.VideoFrame) {
    return {
      status: CodecTestResult.Unsupported,
      reason: "WebCodecs is not available",
    };
  }

  const config = encodeConfig();

  let support;
  try {
    support = await withTimeout(
      win.VideoEncoder.isConfigSupported(config),
      ENCODE_TIMEOUT_MS,
      "isConfigSupported timed out"
    );
  } catch (e) {
    return {
      status: CodecTestResult.Unsupported,
      reason: `isConfigSupported failed: ${e}`,
    };
  }

  if (!support || !support.supported) {
    return {
      status: CodecTestResult.Unsupported,
      reason: "no hardware H.264 encoder for this configuration",
    };
  }

  const chunks = [];
  let encoderError = null;
  let encoder;

  try {
    encoder = new win.VideoEncoder({
      output: chunk => {
        chunks.push({ type: chunk.type, byteLength: chunk.byteLength });
      },
      error: e => {
        encoderError = e;
      },
    });

    encoder.configure(config);

    const canvas = makeEncodeCanvas(win, ENCODE_WIDTH, ENCODE_HEIGHT);
    const frame = new win.VideoFrame(canvas, { timestamp: 0 });
    try {
      encoder.encode(frame, { keyFrame: true });
    } finally {
      frame.close();
    }

    await withTimeout(
      encoder.flush(),
      ENCODE_TIMEOUT_MS,
      "encoder flush timed out"
    );
  } catch (e) {
    return {
      status: CodecTestResult.Failed,
      reason: `encode failed: ${encoderError ?? e}`,
    };
  } finally {
    try {
      encoder?.close();
    } catch (e) {
      // The encoder may already be closed after an error.
    }
  }

  if (encoderError) {
    return {
      status: CodecTestResult.Failed,
      reason: `encode errored: ${encoderError}`,
    };
  }

  if (!chunks.some(chunk => chunk.type === "key" && chunk.byteLength > 0)) {
    return {
      status: CodecTestResult.Failed,
      reason: `encode produced no keyframe (${chunks.length} chunks)`,
    };
  }

  return { status: CodecTestResult.Passed, reason: "" };
}

/**
 * Open the offscreen window the checks run against.
 *
 * @returns {Window}
 */
function openSanityTestWindow() {
  const win = Services.ww.openWindow(
    null,
    PARENT_URL,
    "Test Page",
    "width=" +
      PAGE_WIDTH +
      ",height=" +
      PAGE_HEIGHT +
      ",chrome,titlebar=0,dialog=1",
    null
  );

  const appWin = win.docShell.treeOwner
    .QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIAppWindow);

  // Request fast snapshot at RenderCompositor of WebRender.
  // Since readback of Windows DirectComposition is very slow.
  appWin.needFastSnaphot();

  // There's no clean way to have an invisible window and ensure it's always painted.
  // Instead, move the window far offscreen so it doesn't show up during launch.
  win.moveTo(100000000, 1000000000);
  // In multi-screens with different dpi setup, the window may have been
  // incorrectly resized.
  win.resizeTo(PAGE_WIDTH, PAGE_HEIGHT);

  return win;
}

function waitForWindowLoad(win) {
  const loaded = () =>
    win.document.readyState == "complete" &&
    win.document.documentURI == PARENT_URL;

  if (loaded()) {
    return Promise.resolve();
  }

  return new Promise(resolve => {
    win.addEventListener(
      "load",
      function onLoad() {
        if (!loaded()) {
          return;
        }
        win.removeEventListener("load", onLoad, true);
        resolve();
      },
      true
    );
  });
}

function attachTestBrowser(win) {
  const browser = win.document.createXULElement("browser");
  browser.setAttribute("type", "content");
  browser.setAttribute("disableglobalhistory", "true");
  browser.toggleAttribute(
    "remote",
    Services.appinfo.browserTabsRemoteAutostart
  );
  browser.style.width = PAGE_WIDTH + "px";
  browser.style.height = PAGE_HEIGHT + "px";

  win.document.documentElement.appendChild(browser);
  return browser;
}

/**
 * Open a test window, run every sanity check against it, and close it again.
 *
 * Returns plain data, so a content scoped test can drive the identical checks
 * through SpecialPowers without handling a chrome window itself.
 *
 * @returns {Promise<number>} A SanityCheckResult.
 */
export async function runSanityTest() {
  let win;
  try {
    win = openSanityTestWindow();
    return await runSanityChecks(win);
  } catch (e) {
    return SanityCheckResult.FailedToRun;
  } finally {
    win?.close();
  }
}

/**
 * Run every sanity check against an already open test window.
 *
 * The encoder is only probed when rendering and decoding are healthy as
 * gfxPlatform force disables hardware encoding alongside decoding.
 *
 * @param {Window} win A window from openSanityTestWindow().
 * @returns {Promise<number>} A SanityCheckResult.
 */
async function runSanityChecks(win) {
  let mediaEnginePrefVal = 0;
  const restoreMediaEngine = () => {
    if (mediaEnginePrefVal != 0) {
      Services.prefs.setIntPref(MEDIA_ENGINE_PREF, mediaEnginePrefVal);
      mediaEnginePrefVal = 0;
    }
  };

  try {
    return await withTimeout(
      (async () => {
        await waitForWindowLoad(win);

        const browser = attachTestBrowser(win);
        const mm = browser.messageManager;

        const contentLoaded = new Promise(resolve =>
          mm.addMessageListener("gfxSanity:ContentLoaded", function onLoaded() {
            mm.removeMessageListener("gfxSanity:ContentLoaded", onLoaded);
            resolve();
          })
        );

        // The media engine does not support capturing an image to a canvas,
        // which the compositor check relies on.
        mediaEnginePrefVal = Services.prefs.getIntPref(MEDIA_ENGINE_PREF, 0);
        if (mediaEnginePrefVal != 0) {
          Services.prefs.setIntPref(MEDIA_ENGINE_PREF, 0);
        }

        let result;
        try {
          mm.loadFrameScript(FRAME_SCRIPT_URL, false);
          await contentLoaded;

          result = testCompositor(win);
        } finally {
          restoreMediaEngine();
        }

        if (result != SanityCheckResult.Passed) {
          return result;
        }

        const encoderResult = new Promise(resolve =>
          mm.addMessageListener(
            "gfxSanity:EncoderResult",
            function onResult(message) {
              mm.removeMessageListener("gfxSanity:EncoderResult", onResult);
              resolve(message.data);
            }
          )
        );

        mm.sendAsyncMessage("gfxSanity:RunEncoderTest");

        let encoder;
        try {
          encoder = await withTimeout(
            encoderResult,
            ENCODE_REPLY_TIMEOUT_MS,
            "encode check did not reply"
          );
        } catch (e) {
          return SanityCheckResult.FailedVideoEncode;
        }

        switch (encoder.status) {
          case CodecTestResult.Failed:
            return SanityCheckResult.FailedVideoEncode;
          case CodecTestResult.Unsupported:
            return SanityCheckResult.PassedNoHardwareEncoder;
          default:
            return SanityCheckResult.Passed;
        }
      })(),
      TIMEOUT_MS,
      "sanity test timed out"
    );
  } catch (e) {
    return e instanceof TimeoutError
      ? SanityCheckResult.Timeout
      : SanityCheckResult.FailedToRun;
  } finally {
    // If we timeout, we may need to restore this here too.
    restoreMediaEngine();
  }
}

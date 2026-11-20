/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () => lazy.Log.get());

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "canvasMaxArea",
  "gfx.canvas.max-area"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "canvasMaxSize",
  "gfx.canvas.max-size"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "compositorReadback",
  "remote.screenshot.use_readback",
  false
);

const CONTEXT_2D = "2d";
const BG_COLOUR = "rgb(255,255,255)";
const XHTML_NS = "http://www.w3.org/1999/xhtml";

/**
 * Provides primitives to capture screenshots.
 *
 * @namespace
 */
export const capture = {};

capture.Format = {
  Base64: 0,
  Hash: 1,
};

/**
 * Determine whether capture.canvas will read back the compositor with
 * drawWindow for the given browsing context, rather than re-render it with
 * drawSnapshot.
 *
 * @param {BrowsingContext} browsingContext
 *     The BrowsingContext from which the snapshot would be taken.
 * @param {boolean} readback
 *     Whether the caller explicitly requested readback.
 *
 * @returns {boolean}
 *     True if capture.canvas will read back with drawWindow, false if it will
 *     use drawSnapshot.
 */
capture.usesReadback = function (browsingContext, readback) {
  // The remote.screenshot.use_readback pref (mirrored here in
  // lazy.compositorReadback) needs to be able to query the embedderElement to
  // find the content area; so the pref only actually turns on readback if we
  // have an embedderElement.
  let readbackEnabledViaPref =
    lazy.compositorReadback && !!browsingContext.top.embedderElement;

  if (!readback && !readbackEnabledViaPref) {
    return false;
  }
  if (AppConstants.platform === "macosx" && !Cu.isInAutomation) {
    // On macOS, the compositor runs in the GPU process, which honors
    // a window readback request *only if we're in automation* (see
    // NativeLayerRootRemoteMacParent::RecvRequestReadback) and otherwise
    // crashes the process. To avoid this crash, we fall back to the
    // drawSnapshot path for that configuration.
    return false;
  }
  return true;
};

/**
 * Draw a rectangle off the framebuffer.
 *
 * @param {DOMWindow} win
 *     The DOM window used for the framebuffer, and providing the interfaces
 *     for creating an HTMLCanvasElement.
 * @param {BrowsingContext} browsingContext
 *     The BrowsingContext from which the snapshot should be taken.
 * @param {number} left
 *     The left, X axis offset of the rectangle.
 * @param {number} top
 *     The top, Y axis offset of the rectangle.
 * @param {number} width
 *     The width dimension of the rectangle to paint.
 * @param {number} height
 *     The height dimension of the rectangle to paint.
 * @param {object=} options
 * @param {HTMLCanvasElement=} options.canvas
 *     Optional canvas to reuse for the screenshot.
 * @param {number=} options.flags
 *     Optional integer representing flags to pass to drawWindow; these
 *     are defined on CanvasRenderingContext2D.
 * @param {number=} options.maxHeight
 *     Maximum height of the resulting canvas in pixels.
 * @param {number=} options.maxWidth
 *     Maximum width of the resulting canvas in pixels.
 * @param {number=} options.dX
 *     Horizontal offset between the browser window and content area. Defaults to 0.
 * @param {number=} options.dY
 *     Vertical offset between the browser window and content area. Defaults to 0.
 * @param {boolean=} options.readback
 *     If true, read back a snapshot of the pixel data currently in the
 *     compositor/window. Defaults to false, unless the
 *     `remote.screenshot.use_readback` preference is set (checked here via
 *     lazy.compositorReadback).
 * @param {boolean=} options.drawView
 *     If true, the rectangle is relative to the visible viewport rather than to
 *     the page, and view level rendering such as the root scrollbars is
 *     included. Ignored when reading back. Defaults to false.
 *
 * @returns {HTMLCanvasElement}
 *     The canvas on which the selection from the window's framebuffer
 *     has been painted on.
 */
capture.canvas = async function (
  win,
  browsingContext,
  left,
  top,
  width,
  height,
  {
    canvas = null,
    flags = null,
    maxHeight = null,
    maxWidth = null,
    dX = 0,
    dY = 0,
    readback = false,
    drawView = false,
  } = {}
) {
  const readbackRequested = readback;
  readback = capture.usesReadback(browsingContext, readback);
  if (readbackRequested && !readback) {
    lazy.logger.warn(
      "Ignoring readback, which is unavailable on macOS outside of " +
        "automation; using drawSnapshot instead"
    );
  }
  const browser = readback && browsingContext.top.embedderElement;
  if (browser) {
    // Readback can only return the content area composited on screen, and its
    // coordinates are relative to the chrome window rather than the document,
    // so any requested region degrades to the whole content area. When the
    // browsing context has no embedder element (such as the dedicated window
    // used for WPT reftests), the caller-provided rectangle is used as-is.
    ({ left, top, width, height } = browser.getBoundingClientRect());
  }

  // FIXME(bug 1761032): This looks a bit sketchy, overrideDPPX doesn't
  // influence rendering...
  const devicePixelRatio = browsingContext.overrideDPPX || win.devicePixelRatio;
  let scale = devicePixelRatio;

  if (maxWidth !== null) {
    scale = Math.min(scale, maxWidth / width);
  }
  if (maxHeight !== null) {
    scale = Math.min(scale, maxHeight / height);
  }

  const isDownscaled = scale < devicePixelRatio;
  const canvasHeight = isDownscaled
    ? Math.max(1, Math.round(height * scale))
    : height * scale;
  const canvasWidth = isDownscaled
    ? Math.max(1, Math.round(width * scale))
    : width * scale;
  const canvasArea = canvasWidth * canvasHeight;

  if (canvasWidth > lazy.canvasMaxSize) {
    throw new lazy.error.UnsupportedOperationError(
      `${canvasWidth} exceeds the maximum allowed screenshot width of ${lazy.canvasMaxSize} pixels`
    );
  }

  if (canvasHeight > lazy.canvasMaxSize) {
    throw new lazy.error.UnsupportedOperationError(
      `${canvasHeight} exceeds the maximum allowed screenshot height of ${lazy.canvasMaxSize} pixels`
    );
  }

  if (canvasArea > lazy.canvasMaxArea) {
    throw new lazy.error.UnsupportedOperationError(
      `${canvasArea} exceeds the maximum allowed screenshot area of ${lazy.canvasMaxArea} square pixels`
    );
  }

  try {
    if (canvas === null) {
      canvas = win.document.createElementNS(XHTML_NS, "canvas");
      canvas.width = canvasWidth;
      canvas.height = canvasHeight;
    }

    const ctx = canvas.getContext(CONTEXT_2D);

    if (readback) {
      if (flags === null) {
        flags =
          ctx.DRAWWINDOW_DRAW_CARET |
          ctx.DRAWWINDOW_DRAW_VIEW |
          ctx.DRAWWINDOW_USE_WIDGET_LAYERS;
      }

      // drawWindow doesn't take scaling into account.
      if (isDownscaled) {
        ctx.scale(canvasWidth / width, canvasHeight / height);
      } else {
        ctx.scale(scale, scale);
      }
      ctx.drawWindow(win, left + dX, top + dY, width, height, BG_COLOUR, flags);
    } else {
      let rect = new DOMRect(left, top, width, height);
      let snapshot = await browsingContext.currentWindowGlobal.drawSnapshot(
        rect,
        scale,
        BG_COLOUR,
        { drawView }
      );

      if (isDownscaled) {
        ctx.drawImage(snapshot, 0, 0, canvasWidth, canvasHeight);
      } else {
        ctx.drawImage(snapshot, 0, 0);
      }

      // Bug 1574935 - Huge dimensions can trigger an OOM because multiple copies
      // of the bitmap will exist in memory. Force the removal of the snapshot
      // because it is no longer needed.
      snapshot.close();
    }
  } catch (error) {
    throw new lazy.error.UnknownError(
      `Unable to capture screenshot: ${error.message}`
    );
  }
  return canvas;
};

/**
 * Encode the contents of an HTMLCanvasElement to a Base64 encoded string.
 *
 * @param {HTMLCanvasElement} canvas
 *     The canvas to encode.
 * @param {string} type
 *     The image format to output such as `image/png`.
 * @param {number} encoderOptions
 *     A number between 0 and 1 representing the image quality. Defaults to `1.0`.
 *
 * @returns {string}
 *     A Base64 encoded string.
 */
capture.toBase64 = function (canvas, type, encoderOptions = 1.0) {
  let u = canvas.toDataURL(type, encoderOptions);
  return u.substring(u.indexOf(",") + 1);
};

/**
 * Hash the contents of an HTMLCanvasElement to a SHA-256 hex digest.
 *
 * @param {HTMLCanvasElement} canvas
 *     The canvas to encode.
 *
 * @returns {string}
 *     A hex digest of the SHA-256 hash of the base64 encoded string.
 */
capture.toHash = function (canvas) {
  let u = capture.toBase64(canvas, "image/png");
  let buffer = new TextEncoder().encode(u);
  return crypto.subtle.digest("SHA-256", buffer).then(hash => hex(hash));
};

/**
 * Convert buffer into to hex.
 *
 * @param {ArrayBuffer} buffer
 *     The buffer containing the data to convert to hex.
 *
 * @returns {string}
 *     A hex digest of the input buffer.
 */
function hex(buffer) {
  let hexCodes = [];
  let view = new DataView(buffer);
  for (let i = 0; i < view.byteLength; i += 4) {
    let value = view.getUint32(i);
    let stringValue = value.toString(16);
    let padding = "00000000";
    let paddedValue = (padding + stringValue).slice(-padding.length);
    hexCodes.push(paddedValue);
  }
  return hexCodes.join("");
}

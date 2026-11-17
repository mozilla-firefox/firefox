/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Eye-dropper tool. This is implemented as a highlighter so it can be displayed in the
// content page.
// It basically displays a magnifier that tracks mouse moves and shows a magnified version
// of the page. On click, it samples the color at the pixel being hovered.

const {
  CanvasFrameAnonymousContentHelper,
} = require("resource://devtools/server/actors/highlighters/utils/markup.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const { rgbToHsl } =
  require("resource://devtools/shared/css/color.js").colorUtils;
const {
  getCurrentZoom,
  getFrameOffsets,
} = require("resource://devtools/shared/layout/utils.js");
const { debounce } = require("resource://devtools/shared/debounce.js");

loader.lazyGetter(this, "clipboardHelper", () =>
  Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper)
);
loader.lazyGetter(
  this,
  "EyeDropperMagnifier",
  () =>
    ChromeUtils.importESModule(
      "moz-src:///toolkit/modules/EyeDropperMagnifier.sys.mjs",
      { global: "contextual" }
    ).EyeDropperMagnifier
);
loader.lazyGetter(this, "l10n", () =>
  Services.strings.createBundle(
    "chrome://devtools-shared/locale/eyedropper.properties"
  )
);

const ZOOM_LEVEL_PREF = "devtools.eyedropper.zoom";
const FORMAT_PREF = "devtools.defaultColorUnit";
// Width of the canvas.
const MAGNIFIER_WIDTH = 96;
// Height of the canvas.
const MAGNIFIER_HEIGHT = 96;
// Start position, when the tool is first shown. This should match the top/left position
// defined in CSS.
const DEFAULT_START_POS_X = 100;
const DEFAULT_START_POS_Y = 100;
// How long to wait before closing after copy.
const CLOSE_DELAY = 750;

/**
 * The EyeDropper allows the user to select a color of a pixel within the content page,
 * showing a magnified circle and color preview while the user hover the page.
 */
class EyeDropper extends EventEmitter {
  #pageEventListenersAbortController;
  #debouncedUpdateScreenshot;
  #scrolling = false;
  /** @type {EyeDropperMagnifier} */
  #magnifier;
  constructor(highlighterEnv) {
    super();

    this.highlighterEnv = highlighterEnv;

    // Get a couple of settings from prefs.
    this.format = Services.prefs.getCharPref(FORMAT_PREF);
    this.eyeDropperZoomLevel = Services.prefs.getIntPref(ZOOM_LEVEL_PREF);

    this.markup = new CanvasFrameAnonymousContentHelper(
      this.highlighterEnv,
      this._buildMarkup.bind(this),
      {
        contentRootHostClassName: "devtools-highlighter-eye-dropper",
      }
    );
    this.isReady = this.markup.initialize();

    this.#debouncedUpdateScreenshot = debounce(
      this.updateScreenshot.bind(this),
      200,
      this
    );
  }

  get win() {
    return this.highlighterEnv.window;
  }

  _buildMarkup() {
    // Highlighter main container.
    const container = this.markup.createNode({
      attributes: {
        id: "eye-dropper-container",
        class: "highlighter-container",
      },
    });
    this.#magnifier = new EyeDropperMagnifier(container, {
      zoomLevel: this.eyeDropperZoomLevel,
      width: MAGNIFIER_WIDTH,
      height: MAGNIFIER_HEIGHT,
      colorFormatter: rgb => toColorString(rgb, this.format),
    });
    return container;
  }

  destroy() {
    this.hide();
    this.markup.destroy();
  }

  getElement(id) {
    return this.markup.getElement(id);
  }

  /**
   * Show the eye-dropper highlighter.
   *
   * @param {DOMNode} node The node which document the highlighter should be inserted in.
   * @param {object} options The options object may contain the following properties:
   * - {Boolean} copyOnSelect: Whether selecting a color should copy it to the clipboard.
   * - {String|null} screenshot: a dataURL representation of the page screenshot. If null,
   *                 the eyedropper will use `drawWindow` to get the the screenshot
   *                 (⚠️ but it won't handle remote frames).
   */
  show(node, options = {}) {
    if (this.highlighterEnv.isXUL) {
      return false;
    }

    this.options = options;

    // Get the page's current zoom level.
    this.pageZoom = getCurrentZoom(this.win);

    // Take a screenshot of the viewport.
    // Once the screenshot is ready, the magnified area will be drawn.
    this.updateScreenshot(options.screenshot);

    // Start listening for user events.
    const { pageListenerTarget } = this.highlighterEnv;
    this.#pageEventListenersAbortController = new AbortController();
    const signal = this.#pageEventListenersAbortController.signal;
    pageListenerTarget.addEventListener("mousemove", this, { signal });
    pageListenerTarget.addEventListener("click", this, {
      signal,
      useCapture: true,
    });
    pageListenerTarget.addEventListener("keydown", this, { signal });
    pageListenerTarget.addEventListener("scroll", this, {
      signal,
      useCapture: true,
    });
    pageListenerTarget.addEventListener("scrollend", this, { signal });
    pageListenerTarget.addEventListener("FullZoomChange", this, { signal });
    pageListenerTarget.addEventListener("resize", this, { signal });

    this.magnifiedArea = {
      x: DEFAULT_START_POS_X,
      y: DEFAULT_START_POS_Y,
    };

    this.moveTo(DEFAULT_START_POS_X, DEFAULT_START_POS_Y);

    // Focus the content so the keyboard can be used.
    this.win.focus();

    // Make sure we receive mouse events when the debugger has paused execution
    // in the page.
    this.win.document.setSuppressedEventListener(this);

    return true;
  }

  /**
   * Hide the eye-dropper highlighter.
   */
  hide() {
    this.pageImage = null;

    if (this.#pageEventListenersAbortController) {
      this.#pageEventListenersAbortController.abort();
      this.#pageEventListenersAbortController = null;

      this.#magnifier.hide();

      this.getElement("eye-dropper-container").removeAttribute("drawn");
      this.#scrolling = false;

      this.emit("hidden");

      this.win.document.setSuppressedEventListener(null);
    }
  }

  /**
   * Convert a base64 png data-uri to raw binary data.
   */
  #dataURItoBlob(dataURI) {
    const byteString = atob(dataURI.split(",")[1]);

    // write the bytes of the string to an ArrayBuffer
    const buffer = new ArrayBuffer(byteString.length);
    // Update the buffer through a typed array.
    const typedArray = new Uint8Array(buffer);
    for (let i = 0; i < byteString.length; i++) {
      typedArray[i] = byteString.charCodeAt(i);
    }

    return new Blob([buffer], { type: "image/png" });
  }

  /**
   * Create an image bitmap from the page screenshot, draw the eyedropper and set the
   * "drawn" attribute on the container element once it's done.
   *
   * @param {string | null} screenshot
   *   A dataURL representation of the page screenshot.
   *   If null, we'll use `drawWindow` to get the the page screenshot
   *   (⚠️ but it won't handle remote frames).
   */
  async updateScreenshot(screenshot) {
    let imageSource;
    if (screenshot) {
      imageSource = this.#dataURItoBlob(screenshot);
    } else {
      // Hide the eyedropper while we take the screenshot.
      this.#magnifier.hide();
      imageSource = getWindowAsImageData(this.win);
    }

    // We need to transform the blob/imageData to something drawWindow will consume.
    // An ImageBitmap works well. We could have used an Image, but doing so results
    // in errors if the page defines CSP headers.
    const image = await this.win.createImageBitmap(imageSource);

    this.pageImage = image;
    // We likely haven't drawn anything yet (no mousemove events yet), so start now.
    this.draw();

    // Set an attribute on the container element to be able to run tests after the first
    // draw was done.
    this.getElement("eye-dropper-container").setAttribute("drawn", "true");

    this.#magnifier.show();
  }

  draw() {
    if (this.#scrolling) {
      this.#magnifier.draw(null);
      return;
    }

    // If the image of the page isn't ready yet, bail out, we'll draw later on mousemove.
    if (!this.pageImage) {
      return;
    }

    const { x, y } = this.magnifiedArea;
    this.#magnifier.draw(this.pageImage, x, y);
  }

  handleEvent(e) {
    switch (e.type) {
      case "mousemove": {
        // We might be getting an event from a child frame, so account for the offset.
        const [xOffset, yOffset] = getFrameOffsets(this.win, e.target);
        const x = xOffset + e.pageX - this.win.scrollX;
        const y = yOffset + e.pageY - this.win.scrollY;
        // Update the zoom area.
        this.magnifiedArea.x = x * this.pageZoom;
        this.magnifiedArea.y = y * this.pageZoom;
        // Redraw the portion of the screenshot that is now under the mouse.
        this.draw();
        // And move the eye-dropper's UI so it follows the mouse.
        this.moveTo(x, y);
        break;
      }
      // Note: when events are suppressed we will only get mousedown/mouseup and
      // not any click events.
      case "click":
      case "mouseup":
        this.selectColor();
        break;
      case "keydown":
        this.handleKeyDown(e);
        break;
      case "scroll":
        if (!this.#scrolling) {
          this.#scrolling = true;
          this.pageImage = null;
          this.draw();
        }
        break;
      case "scrollend":
        this.#scrolling = false;
        this.getElement("eye-dropper-container").removeAttribute("drawn");
        this.updateScreenshot();
        break;
      case "FullZoomChange":
        this.hide();
        this.show();
        break;
      case "resize":
        this.getElement("eye-dropper-container").removeAttribute("drawn");
        this.#debouncedUpdateScreenshot();
        break;
    }
  }

  moveTo(x, y) {
    this.#magnifier.moveTo(x, y);

    const positions = [];
    // Move the label container to the top if the magnifier is close to the bottom edge.
    if (y >= this.win.innerHeight - MAGNIFIER_HEIGHT) {
      positions.push("top");
    }

    // Also offset the label container to the right or left if the magnifier is close to
    // the edge.
    if (x <= MAGNIFIER_WIDTH) {
      positions.push("right");
    } else if (x >= this.win.innerWidth - MAGNIFIER_WIDTH) {
      positions.push("left");
    }

    this.#magnifier.setLabelPosition(positions);
  }

  /**
   * Select the current color that's being previewed. Depending on the current options,
   * selecting might mean copying to the clipboard and closing the
   */
  selectColor() {
    if (this.#scrolling) {
      return;
    }

    if (!this.#magnifier.colorString) {
      return;
    }

    let onColorSelected = Promise.resolve();
    if (this.options.copyOnSelect) {
      onColorSelected = this.copyColor();
    }

    this.emit("selected", this.#magnifier.colorString);
    onColorSelected.then(() => this.hide(), console.error);
  }

  /**
   * Handler for the keydown event. Either select the color or move the panel in a
   * direction depending on the key pressed.
   */
  handleKeyDown(e) {
    // Bail out early if any unsupported modifier is used, so that we let
    // keyboard shortcuts through.
    if (e.metaKey || e.ctrlKey || e.altKey) {
      return;
    }
    if (e.keyCode === e.DOM_VK_RETURN) {
      this.selectColor();
      e.preventDefault();
      return;
    }

    if (e.keyCode === e.DOM_VK_ESCAPE) {
      this.emit("canceled");
      this.hide();
      e.preventDefault();
      return;
    }

    let offsetX = 0;
    let offsetY = 0;
    let modifier = 1;

    if (e.keyCode === e.DOM_VK_LEFT) {
      offsetX = -1;
    } else if (e.keyCode === e.DOM_VK_RIGHT) {
      offsetX = 1;
    } else if (e.keyCode === e.DOM_VK_UP) {
      offsetY = -1;
    } else if (e.keyCode === e.DOM_VK_DOWN) {
      offsetY = 1;
    }

    if (e.shiftKey) {
      modifier = 10;
    }

    offsetY *= modifier;
    offsetX *= modifier;

    if (offsetX !== 0 || offsetY !== 0) {
      this.magnifiedArea.x = cap(
        this.magnifiedArea.x + offsetX,
        0,
        this.win.innerWidth * this.pageZoom
      );
      this.magnifiedArea.y = cap(
        this.magnifiedArea.y + offsetY,
        0,
        this.win.innerHeight * this.pageZoom
      );

      this.draw();

      this.moveTo(
        this.magnifiedArea.x / this.pageZoom,
        this.magnifiedArea.y / this.pageZoom
      );

      e.preventDefault();
    }
  }

  /**
   * Copy the currently inspected color to the clipboard.
   *
   * @return {Promise} Resolves when the copy has been done (after a delay that is used to
   * let users know that something was copied).
   */
  copyColor() {
    // Copy to the clipboard.
    clipboardHelper.copyString(this.#magnifier.colorString);

    // Provide some feedback.
    this.getElement("eye-dropper-color-value").setTextContent(
      "✓ " + l10n.GetStringFromName("colorValue.copied")
    );

    // Hide the tool after a delay.
    clearTimeout(this._copyTimeout);
    return new Promise(resolve => {
      this._copyTimeout = setTimeout(resolve, CLOSE_DELAY);
    });
  }
}

exports.EyeDropper = EyeDropper;

/**
 * Draw the visible portion of the window on a canvas and get the resulting ImageData.
 *
 * @param {Window} win
 * @return {ImageData} The image data for the window.
 */
function getWindowAsImageData(win) {
  const canvas = win.document.createElementNS(
    "http://www.w3.org/1999/xhtml",
    "canvas"
  );
  const scale = getCurrentZoom(win);
  const width = win.innerWidth;
  const height = win.innerHeight;
  canvas.width = width * scale;
  canvas.height = height * scale;
  canvas.mozOpaque = true;

  const ctx = canvas.getContext("2d");

  ctx.scale(scale, scale);
  ctx.drawWindow(win, win.scrollX, win.scrollY, width, height, "#fff");

  return ctx.getImageData(0, 0, canvas.width, canvas.height);
}

/**
 * Get a formatted CSS color string from a color value.
 *
 * @param {{r: number, g: number, b: number}} rgb Rgb values of a color to format.
 * @param {string} format Format of string. One of "hex", "rgb", "hsl", "name".
 * @return {string} Formatted color value, e.g. "#FFF" or "hsl(20, 10%, 10%)".
 */
function toColorString(rgb, format) {
  const { r, g, b } = rgb;

  switch (format) {
    case "hex":
      return hexString(rgb);
    case "rgb":
      return "rgb(" + r + ", " + g + ", " + b + ")";
    case "hsl": {
      const [h, s, l] = rgbToHsl([r, g, b]);
      return "hsl(" + h + ", " + s + "%, " + l + "%)";
    }
    case "name":
      return InspectorUtils.rgbToColorName(r, g, b) || hexString(rgb);
    default:
      return hexString(rgb);
  }
}

/**
 * Produce a hex-formatted color string from rgb values.
 *
 * @param {{r: number, g: number, b: number}} rgb Rgb values of color to stringify.
 * @return {string} Hex formatted string for color, e.g. "#FFEE00".
 */
function hexString({ r, g, b }) {
  const val = (1 << 24) + (r << 16) + (g << 8) + (b << 0);
  return "#" + val.toString(16).substr(-6);
}

function cap(value, min, max) {
  return Math.max(min, Math.min(value, max));
}

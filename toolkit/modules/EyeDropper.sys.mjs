/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { EyeDropperMagnifier } from "./EyeDropperMagnifier.sys.mjs";

const DEBUG = false;
function debug(aStr) {
  if (DEBUG) {
    dump("-*- EyeDropper: " + aStr + "\n");
  }
}

// How many times the page is magnified.
const ZOOM_LEVEL = 6;
// Width of the canvas.
const MAGNIFIER_WIDTH = 96;
// Height of the canvas.
const MAGNIFIER_HEIGHT = 96;

// There is at most one eyedropper per browser window, the one opened last
// replacing any other.
const gActiveEyeDroppers = new WeakMap();

/**
 * The eyedropper currently open in the given browser window, if any. Tests use
 * it to check whether an eyedropper is open.
 *
 * @param {Window} aWindow A browser window.
 * @returns {EyeDropper | undefined} Current active EyeDropper if any.
 */
export function getActiveEyeDropperForTests(aWindow) {
  return gActiveEyeDroppers.get(aWindow);
}

/**
 * An eyedropper shown in the browser window's document, over the page shown in
 * a <browser>. It works on a screenshot of the page taken in the parent
 * process, which includes out-of-process iframes and never leaves the parent
 * process: only the picked color is reported.
 */
export class EyeDropper {
  /** @type {XULBrowserElement} */
  #browser;
  /**
   * The popover the eyedropper is built in, in the browser window's document.
   * It covers the page, so mouse events over the page target it.
   *
   * @type {HTMLDivElement}
   */
  #host = null;
  /** @type {ImageBitmap} */
  #screenshot = null;
  /** The position of the page in the window, in CSS pixels. */
  #pageRect = null;
  /** The position of the cursor in the page, in CSS pixels. */
  #cursorX = 0;
  #cursorY = 0;

  /**
   * Removes the input listeners. Created by the first call to open() and kept
   * afterwards, so that an eyedropper can only be shown once.
   *
   * @type {AbortController}
   */
  #inputAbortController = null;
  #resolvePick = null;

  /** @type {EyeDropperMagnifier} */
  #magnifier = null;

  /**
   * @param {XULBrowserElement} aBrowser
   */
  constructor(aBrowser) {
    debug("construct");
    this.#browser = aBrowser;
  }

  get #window() {
    return this.#browser.documentGlobal;
  }

  /**
   * Shows the eyedropper over the page shown in the browser and resolves with
   * the picked color, or with null if the user dismissed it without picking.
   * While it is shown, the page does not receive any mouse or keyboard input.
   *
   * @param {object} [options]
   * @param {AbortSignal} [options.signal] Dismisses the eyedropper, which then
   *        resolves with null.
   * @returns {Promise<{r: number, g: number, b: number} | null>} The picked
   *          color, each component in the 0..255 range, or null.
   */
  async open({ signal } = {}) {
    // Outside of the try block, so that the eyedropper already being shown is
    // left alone.
    if (this.#inputAbortController) {
      throw new Error("EyeDropper.open() can only be called once");
    }
    this.#inputAbortController = new AbortController();

    if (signal?.aborted) {
      return null;
    }

    signal?.addEventListener("abort", () => this.#finish(null), {
      once: true,
    });

    let result = null;
    try {
      result = await this.#openInternal();
    } finally {
      this.#cleanup();
    }
    return result;
  }

  async #openInternal() {
    const { promise, resolve } = Promise.withResolvers();
    this.#resolvePick = resolve;

    const oldEyeDropper = gActiveEyeDroppers.get(this.#window);
    if (oldEyeDropper) {
      debug("Dismiss existing EyeDropper");
      oldEyeDropper.#finish(null);
    }
    gActiveEyeDroppers.set(this.#window, this);

    this.#pageRect = this.#browser.getBoundingClientRect();
    const browsingContext = this.#browser.browsingContext;
    // Rendered at the full zoom so that one pixel of the snapshot is one CSS
    // pixel of the browser window.
    this.#screenshot = await browsingContext.currentWindowGlobal.drawSnapshot(
      null,
      browsingContext.fullZoom,
      "rgb(255, 255, 255)"
    );
    if (!this.#resolvePick) {
      return promise;
    }

    await this.#buildMagnifier();
    if (!this.#resolvePick) {
      return promise;
    }

    const { x, y } = this.#initialPosition;
    this.#moveTo(x, y);
    this.#addInputListeners();
    this.#host.showPopover();

    return promise;
  }

  /**
   * Where the cursor last was over the page, or the middle of the page if it
   * is elsewhere, e.g. when the eyedropper was opened from the keyboard.
   *
   * @returns {{x: number, y: number}} In CSS pixels, relative to the window.
   */
  get #initialPosition() {
    const x = {};
    const y = {};
    this.#window.windowUtils.getLastOverWindowPointerLocationInCSSPixels(x, y);
    if (this.#isOverPage(x.value, y.value)) {
      return { x: x.value, y: y.value };
    }
    const { left, top, width, height } = this.#pageRect;
    return { x: left + width / 2, y: top + height / 2 };
  }

  #cleanup() {
    debug("cleanup");
    if (gActiveEyeDroppers.get(this.#window) === this) {
      gActiveEyeDroppers.delete(this.#window);
    }
    // The controller is deliberately not cleared, as it is what keeps open()
    // from being called again, which is not supported.
    this.#inputAbortController.abort();
    this.#host?.hidePopover();
    this.#host?.remove();
    this.#host = null;
    this.#screenshot?.close();
    this.#screenshot = null;
  }

  #finish(color) {
    debug(`finish with ${JSON.stringify(color)}`);
    const resolvePick = this.#resolvePick;
    this.#resolvePick = null;
    resolvePick?.(color);
  }

  async #buildMagnifier() {
    const doc = this.#browser.ownerDocument;
    const createElement = (tagName, className, parent) => {
      const element = doc.createElementNS(
        "http://www.w3.org/1999/xhtml",
        tagName
      );
      if (className) {
        element.className = className;
      }
      parent.appendChild(element);
      return element;
    };

    // A popover, so that it is shown in the top layer, above the <browser> even
    // when the page is in fullscreen, and with a shadow root, so that its
    // styles and the browser UI's do not affect each other. It covers the page
    // and clips the eyedropper to it, so that it is not drawn over the browser
    // UI.
    this.#host = createElement(
      "div",
      "eyedropper-host",
      doc.body ?? doc.documentElement
    );
    this.#host.popover = "manual";
    for (const name of ["left", "top", "width", "height"]) {
      this.#host.style.setProperty(
        `--eyedropper-page-${name}`,
        `${this.#pageRect[name]}px`
      );
    }
    const shadowRoot = this.#host.attachShadow({ mode: "closed" });
    const link = createElement("link", null, shadowRoot);
    link.rel = "stylesheet";
    link.href = "chrome://global/skin/eyedropper.css";
    const stylesheetLoaded = new Promise(resolve => {
      link.addEventListener("load", resolve, { once: true });
      link.addEventListener("error", resolve, { once: true });
    });

    this.#magnifier = new EyeDropperMagnifier(shadowRoot, {
      zoomLevel: ZOOM_LEVEL,
      width: MAGNIFIER_WIDTH,
      height: MAGNIFIER_HEIGHT,
      colorFormatter: ({ r, g, b }) => {
        const val = (1 << 24) + (r << 16) + (g << 8) + (b << 0);
        return `#${val.toString(16).substr(-6)}`;
      },
    });

    await stylesheetLoaded;
  }

  /**
   * Moves the magnifier to the given position, clamped to the page.
   *
   * @param {number} x In CSS pixels, relative to the window.
   * @param {number} y In CSS pixels, relative to the window.
   */
  #moveTo(x, y) {
    const { left, top, width, height } = this.#pageRect;
    const clamp = (value, max) => Math.min(Math.max(Math.floor(value), 0), max);
    this.#cursorX = clamp(x - left, Math.ceil(width) - 1);
    this.#cursorY = clamp(y - top, Math.ceil(height) - 1);

    this.#magnifier.moveTo(this.#cursorX, this.#cursorY);
    this.#magnifier.draw(this.#screenshot, this.#cursorX, this.#cursorY);

    const positions = [];
    // Move the label container to the top if the magnifier is close to the bottom edge.
    if (this.#cursorY >= height - MAGNIFIER_HEIGHT) {
      positions.push("top");
    }
    // Also offset the label container to the right or left if the magnifier is close to
    // the edge.
    if (this.#cursorX <= MAGNIFIER_WIDTH) {
      positions.push("right");
    } else if (this.#cursorX >= width - MAGNIFIER_WIDTH) {
      positions.push("left");
    }
    this.#magnifier.setLabelPosition(positions);
    this.#magnifier.show();
  }

  /**
   * @param {number} x In CSS pixels, relative to the window.
   * @param {number} y In CSS pixels, relative to the window.
   */
  #isOverPage(x, y) {
    const { left, top, right, bottom } = this.#pageRect;
    return x >= left && x < right && y >= top && y < bottom;
  }

  /**
   * Mouse events over the page target the host, which covers it, rather
   * than the <browser>, so they never reach the page. They are still stopped
   * propagation, so that the browser UI does not react to them either.
   * Keys go to the focused <browser>, so they are taken from the window,
   * before they reach it, and stopped propagation. Preventing the default from
   * chrome also stops them from being forwarded to the content process.
   */
  #addInputListeners() {
    const { signal } = this.#inputAbortController;
    // A private method cannot be used as a listener directly, as it would be
    // called with the event target as `this`.
    const listener = event => this.#eventHandler(event);
    for (const type of [
      "mousemove",
      "mouseup",
      "click",
      "dblclick",
      "auxclick",
      "contextmenu",
      "wheel",
    ]) {
      this.#host.addEventListener(type, listener, { signal });
    }

    const options = { capture: true, signal };
    for (const type of [
      "mousedown",
      "keydown",
      "keyup",
      "keypress",
      "TabSelect",
      "deactivate",
    ]) {
      this.#window.addEventListener(type, listener, options);
    }
    this.#browser.addEventListener("FullZoomChange", listener, options);

    // The screenshot no longer matches what is shown once the page is resized,
    // be it with the window or not, e.g. when a sidebar is opened.
    const resizeObserver = new this.#window.ResizeObserver(([entry]) => {
      const [{ inlineSize, blockSize }] = entry.borderBoxSize;
      // Also called once when starting to observe.
      if (
        inlineSize !== this.#pageRect.width ||
        blockSize !== this.#pageRect.height
      ) {
        this.#finish(null);
      }
    });
    resizeObserver.observe(this.#browser);
    signal.addEventListener("abort", () => resizeObserver.disconnect(), {
      once: true,
    });
  }

  /**
   * Handles the events listened to by #addInputListeners().
   *
   * @param {Event} event
   */
  #eventHandler(event) {
    const blockEvent = event => {
      event.preventDefault();
      event.stopPropagation();
    };
    switch (event.type) {
      case "mousedown":
        // Pressing a mouse button over the browser UI dismisses the eyedropper
        // and lets the UI handle it. Over the page, it is left to the host.
        if (!this.#isOverPage(event.clientX, event.clientY)) {
          this.#finish(null);
          break;
        }
      // fall through
      case "mousemove":
      case "mouseup":
      case "click":
      case "dblclick":
      case "auxclick":
      case "contextmenu":
      case "wheel":
        blockEvent(event);
        if (event.type === "mousemove") {
          this.#moveTo(event.clientX, event.clientY);
        } else if (event.type === "click" && event.button === 0) {
          this.#moveTo(event.clientX, event.clientY);
          this.#finish(this.#magnifier.color);
        }
        break;
      // Keys go to the focused <browser> but are seen by the window first.
      // Shortcuts with modifiers dismiss the eyedropper, so that they apply to
      // the page as it is.
      case "keydown":
        if (event.metaKey || event.ctrlKey || event.altKey) {
          this.#finish(null);
          break;
        }
        blockEvent(event);
        if (event.key === "Escape") {
          this.#finish(null);
        }
        break;
      case "keyup":
      case "keypress":
        blockEvent(event);
        break;
      // The screenshot no longer matches what is shown.
      case "FullZoomChange":
      case "TabSelect":
      case "deactivate":
        this.#finish(null);
        break;
    }
  }
}

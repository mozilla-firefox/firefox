/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Scrolling fires continuously; report each direction at most this often.
const SCROLL_REPORT_MAX_INTERVAL_MS = 250;

/**
 * Content-side actor for Mini Window.
 */
export class MiniWindowChild extends JSWindowActorChild {
  #lastScrollY = 0;
  #lastReportUp = null;
  #lastReportTime = 0;
  /**
   * Start reporting the user's scrolling to show and hide the toolbar.
   */
  #enableScrollReveal() {
    this.#lastScrollY = this.#scrollY();
    this.contentWindow?.addEventListener("mozvisualscroll", this, {
      mozSystemGroup: true,
      wantUntrusted: false,
    });
  }

  #disableScrollReveal() {
    this.contentWindow?.removeEventListener("mozvisualscroll", this, {
      mozSystemGroup: true,
      wantUntrusted: false,
    });
  }

  handleEvent(event) {
    if (event.type === "mozvisualscroll") {
      this.#onScroll(event);
    }
  }

  /**
   * @returns {number} The visual viewport's vertical offset in the page.
   */
  #scrollY() {
    let utils = this.contentWindow?.windowUtils;
    if (!utils) {
      return 0;
    }
    let x = {},
      y = {};
    utils.getVisualViewportOffset(x, y);
    return y.value;
  }

  /**
   * @param {Event} event
   */
  #onScroll(event) {
    let y = this.#scrollY();
    if (y === this.#lastScrollY) {
      return;
    }
    let up = y < this.#lastScrollY;
    this.#lastScrollY = y;
    if (
      up === this.#lastReportUp &&
      event.timeStamp - this.#lastReportTime < SCROLL_REPORT_MAX_INTERVAL_MS
    ) {
      return;
    }
    this.#lastReportUp = up;
    this.#lastReportTime = event.timeStamp;
    this.sendAsyncMessage("Scrolled", { up });
  }

  receiveMessage(message) {
    switch (message.name) {
      case "EnableScrollReveal":
        this.#enableScrollReveal();
        break;
      case "ApzFrame":
        this.#apzFrame(message.data);
        break;
      case "ResetApzFrame":
        this.#resetApzFrame();
        break;
      case "DisableScrollReveal":
        this.#disableScrollReveal();
        break;
    }
    return undefined;
  }

  /**
   * Hold the page at the layout viewport size the crop was captured against,
   * and zoom its visual viewport onto the crop.
   *
   * @param {object} data
   * @param {object} data.crop - crop rect, content CSS px {left, top, width, height}.
   * @param {number} data.layoutWidth - the layout viewport width to hold.
   * @param {number} data.layoutHeight - the layout viewport height to hold.
   */
  #apzFrame({ crop, layoutWidth, layoutHeight }) {
    let win = this.contentWindow;
    let utils = win?.windowUtils;
    if (!utils) {
      return;
    }
    win.document.setViewportSizeOverrideForMiniWindow(
      layoutWidth,
      layoutHeight
    );

    let current = utils.getResolution();
    utils.setResolutionAndScaleTo(
      (win.visualViewport.width * current) / crop.width
    );

    utils.scrollToVisual(
      crop.left,
      crop.top,
      utils.UPDATE_TYPE_MAIN_THREAD,
      utils.SCROLL_MODE_INSTANT
    );

    // Framing isn't the user scrolling, so don't report it.
    this.#lastScrollY = Math.round(crop.top);
  }

  /**
   * Lift the layout viewport override, which also resets the zoom, so the tab
   * goes back to its origin window unframed.
   */
  #resetApzFrame() {
    this.contentWindow?.document.clearViewportSizeOverrideForMiniWindow();
  }
}

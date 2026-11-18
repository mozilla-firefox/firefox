/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The magnifier of an eyedropper, styled by eyedropper-common.css, so that it
 * can be shared between eyedroppers. It shows the part of an image around a
 * pixel, magnified, with a grid showing the pixel boundaries and a box around
 * that pixel, and a label with the color of that pixel.
 *
 * Where the magnifier is, where its label goes and whether it is shown are up
 * to the eyedropper.
 */
export class EyeDropperMagnifier {
  /** How many times the image is magnified. */
  #zoomLevel;
  /** @type {{width: number, height: number}} The size of the canvas. */
  #canvasSize = null;
  /** @type {{r: number, g: number, b: number} | null} */
  #centerColor = null;
  /** Formats the color shown in the label. */
  #colorFormatter;

  /** The .eye-dropper-root element, holding the canvas and the label. */
  #root = null;
  /** @type {CanvasRenderingContext2D} */
  #canvasCtx = null;
  /** The element showing the color as text. */
  #colorText = null;
  /** The element showing the color as a swatch. */
  #colorPreview = null;

  /**
   * @param {Element | ShadowRoot} aContainer - The node to build the magnifier
   *        in.
   * @param {object} aOptions
   * @param {number} aOptions.zoomLevel - How many times the image is magnified.
   * @param {number} aOptions.width - Width of the canvas.
   * @param {number} aOptions.height - Height of the canvas.
   * @param {(rgb: {r: number, g: number, b: number}) => string} aOptions.colorFormatter -
   *        Formats the color shown in the label, given its red, green and blue
   *        components.
   */
  constructor(aContainer, { zoomLevel, width, height, colorFormatter }) {
    this.#zoomLevel = zoomLevel;
    this.#colorFormatter = colorFormatter;
    this.#canvasSize = {
      width,
      height,
    };
    this.#buildMarkup(aContainer);
  }

  /**
   * Builds the magnifier, keeping the ids and classes of the DevTools
   * eyedropper, which its code and tests rely on.
   *
   * @param {Element | ShadowRoot} aContainer
   */
  #buildMarkup(aContainer) {
    const doc = aContainer.ownerDocument;
    const createElement = (aTag, aParent, aAttributes) => {
      const element = doc.createElementNS("http://www.w3.org/1999/xhtml", aTag);
      for (const [name, value] of Object.entries(aAttributes)) {
        element.setAttribute(name, value);
      }
      aParent.appendChild(element);
      return element;
    };

    this.#root = createElement("div", aContainer, {
      id: "eye-dropper-root",
      class: "eye-dropper-root",
      hidden: "true",
    });

    // Prepare the canvas context on which we're drawing the magnified page portion.
    const canvas = createElement("canvas", this.#root, {
      id: "eye-dropper-canvas",
      class: "eye-dropper-canvas",
      width: this.#canvasSize.width,
      height: this.#canvasSize.height,
    });
    this.#canvasCtx = canvas.getContext("2d");
    this.#canvasCtx.imageSmoothingEnabled = false;

    // Prepare the label below the magnifier, showing the color as a swatch and as text.
    const label = createElement("div", this.#root, {
      class: "eye-dropper-color-container",
    });
    this.#colorPreview = createElement("div", label, {
      id: "eye-dropper-color-preview",
      class: "eye-dropper-color-preview",
    });
    this.#colorText = createElement("div", label, {
      id: "eye-dropper-color-value",
      class: "eye-dropper-color-value",
    });
  }

  /**
   * Get the number of cells (blown-up pixels) per direction in the grid.
   */
  get #cellsWide() {
    // Canvas will render whole "pixels" (cells) only, and an even number at that. Round
    // up to the nearest even number of pixels.
    let cellsWide = Math.ceil(this.#canvasSize.width / this.#zoomLevel);
    cellsWide += cellsWide % 2;

    return cellsWide;
  }

  /**
   * Get the size of each cell (blown-up pixel) in the grid.
   */
  get #cellSize() {
    return this.#canvasSize.width / this.#cellsWide;
  }

  /**
   * Get index of cell in the center of the grid.
   */
  get #centerCell() {
    return Math.floor(this.#cellsWide / 2);
  }

  /**
   * Get color of center cell in the grid.
   *
   * @returns {{r: number, g: number, b: number} | null}
   */
  get color() {
    return this.#centerColor;
  }

  /**
   * Draw a grid on the canvas representing pixel boundaries.
   */
  #drawGrid() {
    // Draw the grid on top, but only at 3x or more, otherwise it's too busy.
    if (this.#zoomLevel <= 2) {
      return;
    }

    const { width, height } = this.#canvasSize;

    this.#canvasCtx.lineWidth = 1;
    this.#canvasCtx.strokeStyle = "rgba(143, 143, 143, 0.2)";

    for (let i = 0; i < width; i += this.#cellSize) {
      this.#canvasCtx.beginPath();
      this.#canvasCtx.moveTo(i - 0.5, 0);
      this.#canvasCtx.lineTo(i - 0.5, height);
      this.#canvasCtx.stroke();

      this.#canvasCtx.beginPath();
      this.#canvasCtx.moveTo(0, i - 0.5);
      this.#canvasCtx.lineTo(width, i - 0.5);
      this.#canvasCtx.stroke();
    }
  }

  /**
   * Draw a box on the canvas to highlight the center cell.
   */
  #drawCrosshair() {
    const pos = this.#centerCell * this.#cellSize;

    this.#canvasCtx.lineWidth = 1;
    this.#canvasCtx.lineJoin = "miter";
    this.#canvasCtx.strokeStyle = "rgba(0, 0, 0, 1)";
    this.#canvasCtx.strokeRect(
      pos - 1.5,
      pos - 1.5,
      this.#cellSize + 2,
      this.#cellSize + 2
    );

    this.#canvasCtx.strokeStyle = "rgba(255, 255, 255, 1)";
    this.#canvasCtx.strokeRect(
      pos - 0.5,
      pos - 0.5,
      this.#cellSize,
      this.#cellSize
    );
  }

  /**
   * Hides the magnifier and its label.
   */
  hide() {
    this.#root.setAttribute("hidden", "true");
  }

  /**
   * Shows the magnifier and its label.
   */
  show() {
    this.#root.removeAttribute("hidden");
  }

  /**
   * Draws the part of the image around the given pixel, magnified, and updates
   * the label with the color of that pixel.
   *
   * @param {CanvasImageSource | null} aSourceImage - Null while there is no
   *        available image, in which case the magnifier is disabled: it only
   *        shows the crosshair, and its label is hidden, until it is drawn
   *        with an image again.
   * @param {number} aX - In pixels of the image, unused without an image.
   * @param {number} aY - In pixels of the image, unused without an image.
   */
  draw(aSourceImage, aX, aY) {
    const { width, height } = this.#canvasSize;

    if (!aSourceImage) {
      // The magnifier is disabled: it neither shows nor provides a color, e.g.
      // in the DevTools eyedropper while the page scrolls (as its screenshot
      // is outdated until a new one is taken). Only the crosshair is kept, so
      // that it is still clear where the color will be picked from once the
      // magnifier is enabled again
      this.#canvasCtx.clearRect(0, 0, width, height);
      this.#drawCrosshair();
      this.#root.setAttribute("disabled", "");
      this.#centerColor = null;
      return;
    }

    const zoomedWidth = width / this.#zoomLevel;
    const zoomedHeight = height / this.#zoomLevel;
    const sx = aX - zoomedWidth / 2;
    const sy = aY - zoomedHeight / 2;
    const sw = zoomedWidth;
    const sh = zoomedHeight;

    this.#canvasCtx.drawImage(
      aSourceImage,
      sx,
      sy,
      sw,
      sh,
      0,
      0,
      width,
      height
    );

    this.#drawGrid();
    this.#drawCrosshair();

    // Update the color preview and value.
    const pos = this.#centerCell * this.#cellSize + this.#cellSize / 2;
    const [r, g, b] = this.#canvasCtx.getImageData(pos, pos, 1, 1).data;
    this.#centerColor = { r, g, b };
    this.#colorPreview.setAttribute(
      "style",
      `background-color:rgb(${r}, ${g}, ${b});`
    );
    this.#colorText.textContent = this.#colorFormatter({ r, g, b });
    this.#root.removeAttribute("disabled");
  }

  /**
   * Centers the magnifier on the given position, in CSS pixels, relative to the
   * element it is positioned in.
   *
   * @param {number} x
   * @param {number} y
   */
  moveTo(x, y) {
    this.#root.setAttribute("style", `top:${y}px;left:${x}px;`);
  }

  /**
   * Sets where the label goes, e.g. so that it stays visible near an edge. By
   * default, it is centered below the magnifier.
   *
   * @param {Array<"top" | "left" | "right">} aPositions "top" puts it above the
   *        magnifier, and "left" or "right" offsets it to that side, "right"
   *        taking precedence.
   */
  setLabelPosition(aPositions = []) {
    if (aPositions.includes("top")) {
      this.#root.setAttribute("top", "");
    } else {
      this.#root.removeAttribute("top");
    }

    this.#root.removeAttribute("left");
    this.#root.removeAttribute("right");
    if (aPositions.includes("right")) {
      this.#root.setAttribute("right", "");
    } else if (aPositions.includes("left")) {
      this.#root.setAttribute("left", "");
    }
  }

  /**
   * The color of the pixel at the center of the magnifier, as last drawn,
   * formatted by the color formatter, or an empty string if it is disabled.
   *
   * @returns {string}
   */
  get colorString() {
    return this.#centerColor ? this.#colorFormatter(this.#centerColor) : "";
  }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  AutoRefreshHighlighter,
} = require("resource://devtools/server/actors/highlighters/auto-refresh.js");
const {
  CANVAS_SIZE,
  DEFAULT_COLOR,
  drawBubbleRect,
  drawLine,
  drawRect,
  drawRoundedRect,
  getBoundsFromPoints,
  getCurrentMatrix,
  getPathDescriptionFromPoints,
  getPointsFromDiagonal,
  updateCanvasElement,
  updateCanvasPosition,
} = require("resource://devtools/server/actors/highlighters/utils/canvas.js");
const {
  CanvasFrameAnonymousContentHelper,
  getComputedStyle,
  moveInfobar,
} = require("resource://devtools/server/actors/highlighters/utils/markup.js");
const { apply } = require("resource://devtools/shared/layout/dom-matrix-2d.js");
const {
  getCurrentZoom,
  getDisplayPixelRatio,
  getWindowDimensions,
  setIgnoreLayoutChanges,
} = require("resource://devtools/shared/layout/utils.js");
loader.lazyGetter(this, "HighlightersBundle", () => {
  return new Localization(["devtools/shared/highlighters.ftl"], true);
});

const COLUMNS = "cols";
const ROWS = "rows";

const GRID_FONT_SIZE = 10;
const GRID_FONT_FAMILY = "sans-serif";
const GRID_AREA_NAME_FONT_SIZE = "20";

const GRID_LINES_PROPERTIES = {
  edge: {
    lineDash: [0, 0],
    alpha: 1,
  },
  explicit: {
    lineDash: [5, 3],
    alpha: 0.75,
  },
  implicit: {
    lineDash: [2, 2],
    alpha: 0.5,
  },
  areaEdge: {
    lineDash: [0, 0],
    alpha: 1,
    lineWidth: 3,
  },
};

const GRID_GAP_PATTERN_WIDTH = 14; // px
const GRID_GAP_PATTERN_HEIGHT = 14; // px
const GRID_GAP_PATTERN_LINE_DASH = [5, 3]; // px
const GRID_GAP_ALPHA = 0.5;

// This is the minimum distance a line can be to the edge of the document under which we
// push the line number arrow to be inside the grid. This offset is enough to fit the
// entire arrow + a stacked arrow behind it.
const OFFSET_FROM_EDGE = 32;
// This is how much inside the grid we push the arrow. This a factor of the arrow size.
// The goal here is for a row and a column arrow that have both been pushed inside the
// grid, in a corner, not to overlap.
const FLIP_ARROW_INSIDE_FACTOR = 2.5;

/**
 * Given an `edge` of a box, return the name of the edge one move to the right.
 */
function rotateEdgeRight(edge) {
  switch (edge) {
    case "top":
      return "right";
    case "right":
      return "bottom";
    case "bottom":
      return "left";
    case "left":
      return "top";
    default:
      return edge;
  }
}

/**
 * Given an `edge` of a box, return the name of the edge one move to the left.
 */
function rotateEdgeLeft(edge) {
  switch (edge) {
    case "top":
      return "left";
    case "right":
      return "top";
    case "bottom":
      return "right";
    case "left":
      return "bottom";
    default:
      return edge;
  }
}

/**
 * Given an `edge` of a box, return the name of the opposite edge.
 */
function reflectEdge(edge) {
  switch (edge) {
    case "top":
      return "bottom";
    case "right":
      return "left";
    case "bottom":
      return "top";
    case "left":
      return "right";
    default:
      return edge;
  }
}

/**
 * Cached used by `CssGridHighlighter.getGridGapPattern`.
 */
const gCachedGridPattern = new Map();

/**
 * The CssGridHighlighter is the class that overlays a visual grid on top of
 * display:[inline-]grid elements.
 *
 * Usage example:
 * let h = new CssGridHighlighter(env);
 * h.show(node, options);
 * h.hide();
 * h.destroy();
 *
 * @param {String} options.color
 *        The color that should be used to draw the highlighter for this grid.
 * @param {Number} options.globalAlpha
 *        The alpha (transparency) value that should be used to draw the highlighter for
 *        this grid.
 * @param {Boolean} options.showAllGridAreas
 *        Shows all the grid area highlights for the current grid if isShown is
 *        true.
 * @param {String} options.showGridArea
 *        Shows the grid area highlight for the given area name.
 * @param {Boolean} options.showGridAreasOverlay
 *        Displays an overlay of all the grid areas for the current grid
 *        container if isShown is true.
 * @param {Object} options.showGridCell
 *        An object containing the grid fragment index, row and column numbers
 *        to the corresponding grid cell to highlight for the current grid.
 * @param {Number} options.showGridCell.gridFragmentIndex
 *        Index of the grid fragment to render the grid cell highlight.
 * @param {Number} options.showGridCell.rowNumber
 *        Row number of the grid cell to highlight.
 * @param {Number} options.showGridCell.columnNumber
 *        Column number of the grid cell to highlight.
 * @param {Object} options.showGridLineNames
 *        An object containing the grid fragment index and line number to the
 *        corresponding grid line to highlight for the current grid.
 * @param {Number} options.showGridLineNames.gridFragmentIndex
 *        Index of the grid fragment to render the grid line highlight.
 * @param {Number} options.showGridLineNames.lineNumber
 *        Line number of the grid line to highlight.
 * @param {String} options.showGridLineNames.type
 *        The dimension type of the grid line.
 * @param {Boolean} options.showGridLineNumbers
 *        Displays the grid line numbers on the grid lines if isShown is true.
 * @param {Boolean} options.showInfiniteLines
 *        Displays an infinite line to represent the grid lines if isShown is
 *        true.
 * @param {Number} options.isParent
 *        Set to true if this is a "parent" grid, i.e. a grid with a subgrid.
 * @param {Number} options.zIndex
 *        The z-index to decide the displaying order.
 *
 * Structure:
 * <div class="highlighter-container">
 *   <canvas id="css-grid-canvas" class="css-grid-canvas">
 *   <svg class="css-grid-elements" hidden="true">
 *     <g class="css-grid-regions">
 *       <path class="css-grid-areas" points="..." />
 *       <path class="css-grid-cells" points="..." />
 *     </g>
 *   </svg>
 *   <div class="css-grid-area-infobar-container">
 *     <div class="css-grid-infobar">
 *       <div class="css-grid-infobar-text">
 *         <span class="css-grid-area-infobar-name">Grid Area Name</span>
 *         <span class="css-grid-area-infobar-dimensions">Grid Area Dimensions></span>
 *       </div>
 *     </div>
 *   </div>
 *   <div class="css-grid-cell-infobar-container">
 *     <div class="css-grid-infobar">
 *       <div class="css-grid-infobar-text">
 *         <span class="css-grid-cell-infobar-position">Grid Cell Position</span>
 *         <span class="css-grid-cell-infobar-dimensions">Grid Cell Dimensions></span>
 *       </div>
 *     </div>
 *   <div class="css-grid-line-infobar-container">
 *     <div class="css-grid-infobar">
 *       <div class="css-grid-infobar-text">
 *         <span class="css-grid-line-infobar-number">Grid Line Number</span>
 *         <span class="css-grid-line-infobar-names">Grid Line Names></span>
 *       </div>
 *     </div>
 *   </div>
 * </div>
 */

class CssGridHighlighter extends AutoRefreshHighlighter {
  constructor(highlighterEnv) {
    super(highlighterEnv);

    this.ID_CLASS_PREFIX = "css-grid-";

    this.markup = new CanvasFrameAnonymousContentHelper(
      this.highlighterEnv,
      this._buildMarkup.bind(this)
    );
    this.isReady = this.markup.initialize();

    this.onPageHide = this.onPageHide.bind(this);
    this.onWillNavigate = this.onWillNavigate.bind(this);

    this.highlighterEnv.on("will-navigate", this.onWillNavigate);

    const { pageListenerTarget } = highlighterEnv;
    pageListenerTarget.addEventListener("pagehide", this.onPageHide);

    // Initialize the <canvas> position to the top left corner of the page.
    this._canvasPosition = {
      x: 0,
      y: 0,
    };

    // Calling `updateCanvasPosition` anyway since the highlighter could be initialized
    // on a page that has scrolled already.
    updateCanvasPosition(
      this._canvasPosition,
      this._scroll,
      this.win,
      this._winDimensions
    );
  }

  _buildMarkup() {
    const container = this.markup.createNode({
      attributes: {
        class: "highlighter-container",
      },
    });

    const root = this.markup.createNode({
      parent: container,
      attributes: {
        id: "root",
        class: "root",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    // We use a <canvas> element so that we can draw an arbitrary number of lines
    // which wouldn't be possible with HTML or SVG without having to insert and remove
    // the whole markup on every update.
    this.markup.createNode({
      parent: root,
      nodeType: "canvas",
      attributes: {
        id: "canvas",
        class: "canvas",
        hidden: "true",
        width: CANVAS_SIZE,
        height: CANVAS_SIZE,
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    // Build the SVG element.
    const svg = this.markup.createSVGNode({
      nodeType: "svg",
      parent: root,
      attributes: {
        id: "elements",
        width: "100%",
        height: "100%",
        hidden: "true",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const regions = this.markup.createSVGNode({
      nodeType: "g",
      parent: svg,
      attributes: {
        class: "regions",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    this.markup.createSVGNode({
      nodeType: "path",
      parent: regions,
      attributes: {
        class: "areas",
        id: "areas",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    this.markup.createSVGNode({
      nodeType: "path",
      parent: regions,
      attributes: {
        class: "cells",
        id: "cells",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    // Build the grid area infobar markup.
    const areaInfobarContainer = this.markup.createNode({
      parent: container,
      attributes: {
        class: "area-infobar-container",
        id: "area-infobar-container",
        position: "top",
        hidden: "true",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const areaInfobar = this.markup.createNode({
      parent: areaInfobarContainer,
      attributes: {
        class: "infobar",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const areaTextbox = this.markup.createNode({
      parent: areaInfobar,
      attributes: {
        class: "infobar-text",
      },
      prefix: this.ID_CLASS_PREFIX,
    });
    this.markup.createNode({
      nodeType: "span",
      parent: areaTextbox,
      attributes: {
        class: "area-infobar-name",
        id: "area-infobar-name",
      },
      prefix: this.ID_CLASS_PREFIX,
    });
    this.markup.createNode({
      nodeType: "span",
      parent: areaTextbox,
      attributes: {
        class: "area-infobar-dimensions",
        id: "area-infobar-dimensions",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    // Build the grid cell infobar markup.
    const cellInfobarContainer = this.markup.createNode({
      parent: container,
      attributes: {
        class: "cell-infobar-container",
        id: "cell-infobar-container",
        position: "top",
        hidden: "true",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const cellInfobar = this.markup.createNode({
      parent: cellInfobarContainer,
      attributes: {
        class: "infobar",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const cellTextbox = this.markup.createNode({
      parent: cellInfobar,
      attributes: {
        class: "infobar-text",
      },
      prefix: this.ID_CLASS_PREFIX,
    });
    this.markup.createNode({
      nodeType: "span",
      parent: cellTextbox,
      attributes: {
        class: "cell-infobar-position",
        id: "cell-infobar-position",
      },
      prefix: this.ID_CLASS_PREFIX,
    });
    this.markup.createNode({
      nodeType: "span",
      parent: cellTextbox,
      attributes: {
        class: "cell-infobar-dimensions",
        id: "cell-infobar-dimensions",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    // Build the grid line infobar markup.
    const lineInfobarContainer = this.markup.createNode({
      parent: container,
      attributes: {
        class: "line-infobar-container",
        id: "line-infobar-container",
        position: "top",
        hidden: "true",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const lineInfobar = this.markup.createNode({
      parent: lineInfobarContainer,
      attributes: {
        class: "infobar",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    const lineTextbox = this.markup.createNode({
      parent: lineInfobar,
      attributes: {
        class: "infobar-text",
      },
      prefix: this.ID_CLASS_PREFIX,
    });
    this.markup.createNode({
      nodeType: "span",
      parent: lineTextbox,
      attributes: {
        class: "line-infobar-number",
        id: "line-infobar-number",
      },
      prefix: this.ID_CLASS_PREFIX,
    });
    this.markup.createNode({
      nodeType: "span",
      parent: lineTextbox,
      attributes: {
        class: "line-infobar-names",
        id: "line-infobar-names",
      },
      prefix: this.ID_CLASS_PREFIX,
    });

    return container;
  }

  clearCache() {
    gCachedGridPattern.clear();
  }

  /**
   * Clear the grid area highlights.
   */
  clearGridAreas() {
    const areas = this.getElement("areas");
    areas.setAttribute("d", "");
  }

  /**
   * Clear the grid cell highlights.
   */
  clearGridCell() {
    const cells = this.getElement("cells");
    cells.setAttribute("d", "");
  }

  destroy() {
    const { highlighterEnv } = this;
    highlighterEnv.off("will-navigate", this.onWillNavigate);

    const { pageListenerTarget } = highlighterEnv;
    if (pageListenerTarget) {
      pageListenerTarget.removeEventListener("pagehide", this.onPageHide);
    }

    this.markup.destroy();

    // Clear the pattern cache to avoid dead object exceptions (Bug 1342051).
    this.clearCache();
    AutoRefreshHighlighter.prototype.destroy.call(this);
  }

  get canvas() {
    return this.getElement("canvas");
  }

  get color() {
    return this.options.color || DEFAULT_COLOR;
  }

  get ctx() {
    return this.canvas.getCanvasContext("2d");
  }

  get globalAlpha() {
    return this.options.globalAlpha || 1;
  }

  getElement(id) {
    return this.markup.getElement(this.ID_CLASS_PREFIX + id);
  }

  getFirstColLinePos(fragment) {
    return fragment.cols.lines[0].start;
  }

  getFirstRowLinePos(fragment) {
    return fragment.rows.lines[0].start;
  }

  /**
   * Gets the grid gap pattern used to render the gap regions based on the device
   * pixel ratio given.
   *
   * @param  {Number} devicePixelRatio
   *         The device pixel ratio we want the pattern for.
   * @param  {Object} dimension
   *         Refers to the Map key for the grid dimension type which is either the
   *         constant COLUMNS or ROWS.
   * @return {CanvasPattern} grid gap pattern.
   */
  getGridGapPattern(devicePixelRatio, dimension) {
    let gridPatternMap = null;

    if (gCachedGridPattern.has(devicePixelRatio)) {
      gridPatternMap = gCachedGridPattern.get(devicePixelRatio);
    } else {
      gridPatternMap = new Map();
    }

    if (gridPatternMap.has(dimension)) {
      return gridPatternMap.get(dimension);
    }

    // Create the diagonal lines pattern for the rendering the grid gaps.
    const canvas = this.markup.createNode({ nodeType: "canvas" });
    const width = (canvas.width = GRID_GAP_PATTERN_WIDTH * devicePixelRatio);
    const height = (canvas.height = GRID_GAP_PATTERN_HEIGHT * devicePixelRatio);

    const ctx = canvas.getContext("2d");
    ctx.save();
    ctx.setLineDash(GRID_GAP_PATTERN_LINE_DASH);
    ctx.beginPath();
    ctx.translate(0.5, 0.5);

    if (dimension === COLUMNS) {
      ctx.moveTo(0, 0);
      ctx.lineTo(width, height);
    } else {
      ctx.moveTo(width, 0);
      ctx.lineTo(0, height);
    }

    ctx.strokeStyle = this.color;
    ctx.globalAlpha = GRID_GAP_ALPHA * this.globalAlpha;
    ctx.stroke();
    ctx.restore();

    const pattern = ctx.createPattern(canvas, "repeat");

    gridPatternMap.set(dimension, pattern);
    gCachedGridPattern.set(devicePixelRatio, gridPatternMap);

    return pattern;
  }

  getLastColLinePos(fragment) {
    return fragment.cols.lines[fragment.cols.lines.length - 1].start;
  }

  /**
   * Get the GridLine index of the last edge of the explicit grid for a grid dimension.
   *
   * @param  {GridTracks} tracks
   *         The grid track of a given grid dimension.
   * @return {Number} index of the last edge of the explicit grid for a grid dimension.
   */
  getLastEdgeLineIndex(tracks) {
    let trackIndex = tracks.length - 1;

    // Traverse the grid track backwards until we find an explicit track.
    while (trackIndex >= 0 && tracks[trackIndex].type != "explicit") {
      trackIndex--;
    }

    // The grid line index is the grid track index + 1.
    return trackIndex + 1;
  }

  getLastRowLinePos(fragment) {
    return fragment.rows.lines[fragment.rows.lines.length - 1].start;
  }

  getNode(id) {
    return this.markup.content.root.getElementById(this.ID_CLASS_PREFIX + id);
  }

  /**
   * The AutoRefreshHighlighter's _hasMoved method returns true only if the
   * element's quads have changed. Override it so it also returns true if the
   * element's grid has changed (which can happen when you change the
   * grid-template-* CSS properties with the highlighter displayed). This
   * check is prone to false positives, because it does a direct object
   * comparison of the first grid fragment structure. This structure is
   * generated by the first call to getGridFragments, and on any subsequent
   * calls where a reflow is needed. Since a reflow is needed when the CSS
   * changes, this will correctly detect that the grid structure has changed.
   * However, it's possible that the reflow could generate a novel grid
   * fragment object containing information that is unchanged -- a false
   * positive.
   */
  _hasMoved() {
    const hasMoved = AutoRefreshHighlighter.prototype._hasMoved.call(this);

    const oldFirstGridFragment = this.gridData?.[0];
    this.gridData = this.currentNode.getGridFragments();
    const newFirstGridFragment = this.gridData[0];

    return hasMoved || oldFirstGridFragment !== newFirstGridFragment;
  }

  /**
   * Hide the highlighter, the canvas and the infobars.
   */
  _hide() {
    setIgnoreLayoutChanges(true);
    this._hideGrid();
    this._hideGridElements();
    this._hideGridAreaInfoBar();
    this._hideGridCellInfoBar();
    this._hideGridLineInfoBar();
    setIgnoreLayoutChanges(false, this.highlighterEnv.document.documentElement);
  }

  _hideGrid() {
    this.getElement("canvas").setAttribute("hidden", "true");
  }

  _hideGridAreaInfoBar() {
    this.getElement("area-infobar-container").setAttribute("hidden", "true");
  }

  _hideGridCellInfoBar() {
    this.getElement("cell-infobar-container").setAttribute("hidden", "true");
  }

  _hideGridElements() {
    this.getElement("elements").setAttribute("hidden", "true");
  }

  _hideGridLineInfoBar() {
    this.getElement("line-infobar-container").setAttribute("hidden", "true");
  }

  /**
   * Checks if the current node has a CSS Grid layout.
   *
   * @return {Boolean} true if the current node has a CSS grid layout, false otherwise.
   */
  isGrid() {
    return this.currentNode.hasGridFragments();
  }

  /**
   * Is a given grid fragment valid? i.e. does it actually have tracks? In some cases, we
   * may have a fragment that defines column tracks but doesn't have any rows (or vice
   * versa). In which case we do not want to draw anything for that fragment.
   *
   * @param  {Object} fragment
   * @return {Boolean}
   */
  isValidFragment(fragment) {
    return fragment.cols.tracks.length && fragment.rows.tracks.length;
  }

  /**
   * The <canvas>'s position needs to be updated if the page scrolls too much, in order
   * to give the illusion that it always covers the viewport.
   */
  _scrollUpdate() {
    const hasUpdated = updateCanvasPosition(
      this._canvasPosition,
      this._scroll,
      this.win,
      this._winDimensions
    );

    if (hasUpdated) {
      this._update();
    }
  }

  _show() {
    if (!this.isGrid()) {
      this.hide();
      return false;
    }

    // The grid pattern cache should be cleared in case the color changed.
    this.clearCache();

    // Hide the canvas, grid element highlights and infobar.
    this._hide();

    this.getElement("root").setAttribute(
      "data-is-parent-grid",
      !!this.options.isParent
    );

    // Set z-index.
    this.markup.content.root.firstElementChild.style.setProperty(
      "z-index",
      this.options.zIndex
    );

    // Update the grid color
    this.markup.content.root.firstElementChild.style.setProperty(
      "--grid-color",
      this.color
    );

    return this._update();
  }

  _showGrid() {
    this.getElement("canvas").removeAttribute("hidden");
  }

  _showGridAreaInfoBar() {
    this.getElement("area-infobar-container").removeAttribute("hidden");
  }

  _showGridCellInfoBar() {
    this.getElement("cell-infobar-container").removeAttribute("hidden");
  }

  _showGridElements() {
    this.getElement("elements").removeAttribute("hidden");
  }

  _showGridLineInfoBar() {
    this.getElement("line-infobar-container").removeAttribute("hidden");
  }

  /**
   * Shows all the grid area highlights for the current grid.
   */
  showAllGridAreas() {
    this.renderGridArea();
  }

  /**
   * Shows the grid area highlight for the given area name.
   *
   * @param  {String} areaName
   *         Grid area name.
   */
  showGridArea(areaName) {
    this.renderGridArea(areaName);
  }

  /**
   * Shows the grid cell highlight for the given grid cell options.
   *
   * @param  {Number} options.gridFragmentIndex
   *         Index of the grid fragment to render the grid cell highlight.
   * @param  {Number} options.rowNumber
   *         Row number of the grid cell to highlight.
   * @param  {Number} options.columnNumber
   *         Column number of the grid cell to highlight.
   */
  showGridCell({ gridFragmentIndex, rowNumber, columnNumber }) {
    this.renderGridCell(gridFragmentIndex, rowNumber, columnNumber);
  }

  /**
   * Shows the grid line highlight for the given grid line options.
   *
   * @param  {Number} options.gridFragmentIndex
   *         Index of the grid fragment to render the grid line highlight.
   * @param  {Number} options.lineNumber
   *         Line number of the grid line to highlight.
   * @param  {String} options.type
   *         The dimension type of the grid line.
   */
  showGridLineNames({ gridFragmentIndex, lineNumber, type }) {
    this.renderGridLineNames(gridFragmentIndex, lineNumber, type);
  }

  /**
   * If a page hide event is triggered for current window's highlighter, hide the
   * highlighter.
   */
  onPageHide({ target }) {
    if (target.defaultView === this.win) {
      this.hide();
    }
  }

  /**
   * Called when the page will-navigate. Used to hide the grid highlighter and clear
   * the cached gap patterns and avoid using DeadWrapper obejcts as gap patterns the
   * next time.
   */
  onWillNavigate({ isTopLevel }) {
    this.clearCache();

    if (isTopLevel) {
      this.hide();
    }
  }

  renderFragment(fragment) {
    if (!this.isValidFragment(fragment)) {
      return;
    }

    this.renderLines(
      fragment.cols,
      COLUMNS,
      this.getFirstRowLinePos(fragment),
      this.getLastRowLinePos(fragment)
    );
    this.renderLines(
      fragment.rows,
      ROWS,
      this.getFirstColLinePos(fragment),
      this.getLastColLinePos(fragment)
    );

    if (this.options.showGridAreasOverlay) {
      this.renderGridAreaOverlay();
    }

    // Line numbers are rendered in a 2nd step to avoid overlapping with existing lines.
    if (this.options.showGridLineNumbers) {
      this.renderLineNumbers(
        fragment.cols,
        COLUMNS,
        this.getFirstRowLinePos(fragment)
      );
      this.renderLineNumbers(
        fragment.rows,
        ROWS,
        this.getFirstColLinePos(fragment)
      );
      this.renderNegativeLineNumbers(
        fragment.cols,
        COLUMNS,
        this.getLastRowLinePos(fragment)
      );
      this.renderNegativeLineNumbers(
        fragment.rows,
        ROWS,
        this.getLastColLinePos(fragment)
      );
    }
  }

  /**
   * Render the grid area highlight for the given area name or for all the grid areas.
   *
   * @param  {String} areaName
   *         Name of the grid area to be highlighted. If no area name is provided, all
   *         the grid areas should be highlighted.
   */
  renderGridArea(areaName) {
    const { devicePixelRatio } = this.win;
    const displayPixelRatio = getDisplayPixelRatio(this.win);
    const paths = [];

    for (let i = 0; i < this.gridData.length; i++) {
      const fragment = this.gridData[i];

      for (const area of fragment.areas) {
        if (areaName && areaName != area.name) {
          continue;
        }

        const rowStart = fragment.rows.lines[area.rowStart - 1];
        const rowEnd = fragment.rows.lines[area.rowEnd - 1];
        const columnStart = fragment.cols.lines[area.columnStart - 1];
        const columnEnd = fragment.cols.lines[area.columnEnd - 1];

        const x1 = columnStart.start + columnStart.breadth;
        const y1 = rowStart.start + rowStart.breadth;
        const x2 = columnEnd.start;
        const y2 = rowEnd.start;

        const points = getPointsFromDiagonal(
          x1,
          y1,
          x2,
          y2,
          this.currentMatrix
        );

        // Scale down by `devicePixelRatio` since SVG element already take them into
        // account.
        const svgPoints = points.map(point => ({
          x: Math.round(point.x / devicePixelRatio),
          y: Math.round(point.y / devicePixelRatio),
        }));

        // Scale down by `displayPixelRatio` since infobar's HTML elements already take it
        // into account; and the zoom scaling is handled by `moveInfobar`.
        const bounds = getBoundsFromPoints(
          points.map(point => ({
            x: Math.round(point.x / displayPixelRatio),
            y: Math.round(point.y / displayPixelRatio),
          }))
        );

        paths.push(getPathDescriptionFromPoints(svgPoints));

        // Update and show the info bar when only displaying a single grid area.
        if (areaName) {
          this._showGridAreaInfoBar();
          this._updateGridAreaInfobar(area, bounds);
        }
      }
    }

    const areas = this.getElement("areas");
    areas.setAttribute("d", paths.join(" "));
  }

  /**
   * Render grid area name on the containing grid area cell.
   *
   * @param  {Object} fragment
   *         The grid fragment of the grid container.
   * @param  {Object} area
   *         The area overlay to render on the CSS highlighter canvas.
   */
  renderGridAreaName(fragment, area) {
    const { rowStart, rowEnd, columnStart, columnEnd } = area;
    const { devicePixelRatio } = this.win;
    const displayPixelRatio = getDisplayPixelRatio(this.win);
    const offset = (displayPixelRatio / 2) % 1;
    let fontSize = GRID_AREA_NAME_FONT_SIZE * displayPixelRatio;
    const canvasX = Math.round(this._canvasPosition.x * devicePixelRatio);
    const canvasY = Math.round(this._canvasPosition.y * devicePixelRatio);

    this.ctx.save();
    this.ctx.translate(offset - canvasX, offset - canvasY);
    this.ctx.font = fontSize + "px " + GRID_FONT_FAMILY;
    this.ctx.globalAlpha = this.globalAlpha;
    this.ctx.strokeStyle = this.color;
    this.ctx.textAlign = "center";
    this.ctx.textBaseline = "middle";

    // Draw the text for the grid area name.
    for (let rowNumber = rowStart; rowNumber < rowEnd; rowNumber++) {
      for (
        let columnNumber = columnStart;
        columnNumber < columnEnd;
        columnNumber++
      ) {
        const row = fragment.rows.tracks[rowNumber - 1];
        const column = fragment.cols.tracks[columnNumber - 1];

        // If the font size exceeds the bounds of the containing grid cell, size it its
        // row or column dimension, whichever is smallest.
        if (
          fontSize > column.breadth * displayPixelRatio ||
          fontSize > row.breadth * displayPixelRatio
        ) {
          fontSize = Math.min([column.breadth, row.breadth]);
          this.ctx.font = fontSize + "px " + GRID_FONT_FAMILY;
        }

        const textWidth = this.ctx.measureText(area.name).width;
        // The width of the character 'm' approximates the height of the text.
        const textHeight = this.ctx.measureText("m").width;
        // Padding in pixels for the line number text inside of the line number container.
        const padding = 3 * displayPixelRatio;

        const boxWidth = textWidth + 2 * padding;
        const boxHeight = textHeight + 2 * padding;

        let x = column.start + column.breadth / 2;
        let y = row.start + row.breadth / 2;

        [x, y] = apply(this.currentMatrix, [x, y]);

        const rectXPos = x - boxWidth / 2;
        const rectYPos = y - boxHeight / 2;

        // Draw a rounded rectangle with a border width of 1 pixel,
        // a border color matching the grid color, and a white background.
        this.ctx.lineWidth = 1 * displayPixelRatio;
        this.ctx.strokeStyle = this.color;
        this.ctx.fillStyle = "white";
        const radius = 2 * displayPixelRatio;
        drawRoundedRect(
          this.ctx,
          rectXPos,
          rectYPos,
          boxWidth,
          boxHeight,
          radius
        );

        this.ctx.fillStyle = this.color;
        this.ctx.fillText(area.name, x, y + padding);
      }
    }

    this.ctx.restore();
  }

  /**
   * Renders the grid area overlay on the css grid highlighter canvas.
   */
  renderGridAreaOverlay() {
    const padding = 1;

    for (let i = 0; i < this.gridData.length; i++) {
      const fragment = this.gridData[i];

      for (const area of fragment.areas) {
        const { rowStart, rowEnd, columnStart, columnEnd, type } = area;

        if (type === "implicit") {
          continue;
        }

        // Draw the line edges for the grid area.
        const areaColStart = fragment.cols.lines[columnStart - 1];
        const areaColEnd = fragment.cols.lines[columnEnd - 1];

        const areaRowStart = fragment.rows.lines[rowStart - 1];
        const areaRowEnd = fragment.rows.lines[rowEnd - 1];

        const areaColStartLinePos = areaColStart.start + areaColStart.breadth;
        const areaRowStartLinePos = areaRowStart.start + areaRowStart.breadth;

        this.renderLine(
          areaColStartLinePos + padding,
          areaRowStartLinePos,
          areaRowEnd.start,
          COLUMNS,
          "areaEdge"
        );
        this.renderLine(
          areaColEnd.start - padding,
          areaRowStartLinePos,
          areaRowEnd.start,
          COLUMNS,
          "areaEdge"
        );

        this.renderLine(
          areaRowStartLinePos + padding,
          areaColStartLinePos,
          areaColEnd.start,
          ROWS,
          "areaEdge"
        );
        this.renderLine(
          areaRowEnd.start - padding,
          areaColStartLinePos,
          areaColEnd.start,
          ROWS,
          "areaEdge"
        );

        this.renderGridAreaName(fragment, area);
      }
    }
  }

  /**
   * Render the grid cell highlight for the given grid fragment index, row and column
   * number.
   *
   * @param  {Number} gridFragmentIndex
   *         Index of the grid fragment to render the grid cell highlight.
   * @param  {Number} rowNumber
   *         Row number of the grid cell to highlight.
   * @param  {Number} columnNumber
   *         Column number of the grid cell to highlight.
   */
  renderGridCell(gridFragmentIndex, rowNumber, columnNumber) {
    const fragment = this.gridData[gridFragmentIndex];

    if (!fragment) {
      return;
    }

    const row = fragment.rows.tracks[rowNumber - 1];
    const column = fragment.cols.tracks[columnNumber - 1];

    if (!row || !column) {
      return;
    }

    const x1 = column.start;
    const y1 = row.start;
    const x2 = column.start + column.breadth;
    const y2 = row.start + row.breadth;

    const { devicePixelRatio } = this.win;
    const displayPixelRatio = getDisplayPixelRatio(this.win);
    const points = getPointsFromDiagonal(x1, y1, x2, y2, this.currentMatrix);

    // Scale down by `devicePixelRatio` since SVG element already take them into account.
    const svgPoints = points.map(point => ({
      x: Math.round(point.x / devicePixelRatio),
      y: Math.round(point.y / devicePixelRatio),
    }));

    // Scale down by `displayPixelRatio` since infobar's HTML elements already take it
    // into account, and the zoom scaling is handled by `moveInfobar`.
    const bounds = getBoundsFromPoints(
      points.map(point => ({
        x: Math.round(point.x / displayPixelRatio),
        y: Math.round(point.y / displayPixelRatio),
      }))
    );

    const cells = this.getElement("cells");
    cells.setAttribute("d", getPathDescriptionFromPoints(svgPoints));

    this._showGridCellInfoBar();
    this._updateGridCellInfobar(rowNumber, columnNumber, bounds);
  }

  /**
   * Render the grid gap area on the css grid highlighter canvas.
   *
   * @param  {Number} linePos
   *         The line position along the x-axis for a column grid line and
   *         y-axis for a row grid line.
   * @param  {Number} startPos
   *         The start position of the cross side of the grid line.
   * @param  {Number} endPos
   *         The end position of the cross side of the grid line.
   * @param  {Number} breadth
   *         The grid line breadth value.
   * @param  {String} dimensionType
   *         The grid dimension type which is either the constant COLUMNS or ROWS.
   */
  renderGridGap(linePos, startPos, endPos, breadth, dimensionType) {
    const { devicePixelRatio } = this.win;
    const displayPixelRatio = getDisplayPixelRatio(this.win);
    const offset = (displayPixelRatio / 2) % 1;
    const canvasX = Math.round(this._canvasPosition.x * devicePixelRatio);
    const canvasY = Math.round(this._canvasPosition.y * devicePixelRatio);

    linePos = Math.round(linePos);
    startPos = Math.round(startPos);
    breadth = Math.round(breadth);

    this.ctx.save();
    this.ctx.fillStyle = this.getGridGapPattern(
      devicePixelRatio,
      dimensionType
    );
    this.ctx.translate(offset - canvasX, offset - canvasY);

    if (dimensionType === COLUMNS) {
      if (isFinite(endPos)) {
        endPos = Math.round(endPos);
      } else {
        endPos = this._winDimensions.height;
        startPos = -endPos;
      }
      drawRect(
        this.ctx,
        linePos,
        startPos,
        linePos + breadth,
        endPos,
        this.currentMatrix
      );
    } else {
      if (isFinite(endPos)) {
        endPos = Math.round(endPos);
      } else {
        endPos = this._winDimensions.width;
        startPos = -endPos;
      }
      drawRect(
        this.ctx,
        startPos,
        linePos,
        endPos,
        linePos + breadth,
        this.currentMatrix
      );
    }

    // Find current angle of grid by measuring the angle of two arbitrary points,
    // then rotate canvas, so the hash pattern stays 45deg to the gridlines.
    const p1 = apply(this.currentMatrix, [0, 0]);
    const p2 = apply(this.currentMatrix, [1, 0]);
    const angleRad = Math.atan2(p2[1] - p1[1], p2[0] - p1[0]);
    this.ctx.rotate(angleRad);

    this.ctx.fill();
    this.ctx.restore();
  }

  /**
   * Render the grid line name highlight for the given grid fragment index, lineNumber,
   * and dimensionType.
   *
   * @param  {Number} gridFragmentIndex
   *         Index of the grid fragment to render the grid line highlight.
   * @param  {Number} lineNumber
   *         Line number of the grid line to highlight.
   * @param  {String} dimensionType
   *         The dimension type of the grid line.
   */
  renderGridLineNames(gridFragmentIndex, lineNumber, dimensionType) {
    const fragment = this.gridData[gridFragmentIndex];

    if (!fragment || !lineNumber || !dimensionType) {
      return;
    }

    const { names } = fragment[dimensionType].lines[lineNumber - 1];
    let linePos;

    if (dimensionType === ROWS) {
      linePos = fragment.rows.lines[lineNumber - 1];
    } else if (dimensionType === COLUMNS) {
      linePos = fragment.cols.lines[lineNumber - 1];
    }

    if (!linePos) {
      return;
    }

    const currentZoom = getCurrentZoom(this.win);
    const { bounds } = this.currentQuads.content[gridFragmentIndex];

    const rowYPosition = fragment.rows.lines[0];
    const colXPosition = fragment.rows.lines[0];

    const x =
      dimensionType === COLUMNS
        ? linePos.start + bounds.left / currentZoom
        : colXPosition.start + bounds.left / currentZoom;

    const y =
      dimensionType === ROWS
        ? linePos.start + bounds.top / currentZoom
        : rowYPosition.start + bounds.top / currentZoom;

    this._showGridLineInfoBar();
    this._updateGridLineInfobar(names.join(", "), lineNumber, x, y);
  }

  /**
   * Render the grid line number on the css grid highlighter canvas.
   *
   * @param  {Number} lineNumber
   *         The grid line number.
   * @param  {Number} linePos
   *         The line position along the x-axis for a column grid line and
   *         y-axis for a row grid line.
   * @param  {Number} startPos
   *         The start position of the cross side of the grid line.
   * @param  {Number} breadth
   *         The grid line breadth value.
   * @param  {String} dimensionType
   *         The grid dimension type which is either the constant COLUMNS or ROWS.
   * @param  {Boolean||undefined} isStackedLine
   *         Boolean indicating if the line is stacked.
   */
  // eslint-disable-next-line complexity
  renderGridLineNumber(
    lineNumber,
    linePos,
    startPos,
    breadth,
    dimensionType,
    isStackedLine
  ) {
    const displayPixelRatio = getDisplayPixelRatio(this.win);
    const { devicePixelRatio } = this.win;
    const offset = (displayPixelRatio / 2) % 1;
    const fontSize = GRID_FONT_SIZE * devicePixelRatio;
    const canvasX = Math.round(this._canvasPosition.x * devicePixelRatio);
    const canvasY = Math.round(this._canvasPosition.y * devicePixelRatio);

    linePos = Math.round(linePos);
    startPos = Math.round(startPos);
    breadth = Math.round(breadth);

    if (linePos + breadth < 0) {
      // Don't render the line number since the line is not visible on screen.
      return;
    }

    this.ctx.save();
    this.ctx.translate(offset - canvasX, offset - canvasY);
    this.ctx.font = fontSize + "px " + GRID_FONT_FAMILY;

    // For a general grid box, the height of the character "m" will be its minimum width
    // and height. If line number's text width is greater, then use the grid box's text
    // width instead.
    const textHeight = this.ctx.measureText("m").width;
    const textWidth = Math.max(
      textHeight,
      this.ctx.measureText(lineNumber).width
    );

    // Padding in pixels for the line number text inside of the line number container.
    const padding = 3 * devicePixelRatio;
    const offsetFromEdge = 2 * devicePixelRatio;

    let boxWidth = textWidth + 2 * padding;
    let boxHeight = textHeight + 2 * padding;

    // Calculate the x & y coordinates for the line number container, so that its arrow
    // tip is centered on the line (or the gap if there is one), and is offset by the
    // calculated padding value from the grid container edge.
    let x, y;

    if (dimensionType === COLUMNS) {
      x = linePos + breadth / 2;
      y =
        lineNumber > 0 ? startPos - offsetFromEdge : startPos + offsetFromEdge;
    } else if (dimensionType === ROWS) {
      y = linePos + breadth / 2;
      x =
        lineNumber > 0 ? startPos - offsetFromEdge : startPos + offsetFromEdge;
    }

    [x, y] = apply(this.currentMatrix, [x, y]);

    // Draw a bubble rectangular arrow with a border width of 2 pixels, a border color
    // matching the grid color and a white background (the line number will be written in
    // black).
    this.ctx.lineWidth = 2 * displayPixelRatio;
    this.ctx.strokeStyle = this.color;
    this.ctx.fillStyle = "white";
    this.ctx.globalAlpha = this.globalAlpha;

    // See param definitions of drawBubbleRect.
    const radius = 2 * displayPixelRatio;
    const margin = 2 * displayPixelRatio;
    const arrowSize = 8 * displayPixelRatio;

    const minBoxSize = arrowSize * 2 + padding;
    boxWidth = Math.max(boxWidth, minBoxSize);
    boxHeight = Math.max(boxHeight, minBoxSize);

    // Determine which edge of the box to aim the line number arrow at.
    const boxEdge = this.getBoxEdge(dimensionType, lineNumber);

    let { width, height } = this._winDimensions;
    width *= displayPixelRatio;
    height *= displayPixelRatio;

    // Don't draw if the line is out of the viewport.
    if (
      (dimensionType === ROWS && (y < 0 || y > height)) ||
      (dimensionType === COLUMNS && (x < 0 || x > width))
    ) {
      this.ctx.restore();
      return;
    }

    // If the arrow's edge (the one perpendicular to the line direction) is too close to
    // the edge of the viewport. Push the arrow inside the grid.
    const minOffsetFromEdge = OFFSET_FROM_EDGE * displayPixelRatio;
    switch (boxEdge) {
      case "left":
        if (x < minOffsetFromEdge) {
          x += FLIP_ARROW_INSIDE_FACTOR * boxWidth;
        }
        break;
      case "right":
        if (width - x < minOffsetFromEdge) {
          x -= FLIP_ARROW_INSIDE_FACTOR * boxWidth;
        }
        break;
      case "top":
        if (y < minOffsetFromEdge) {
          y += FLIP_ARROW_INSIDE_FACTOR * boxHeight;
        }
        break;
      case "bottom":
        if (height - y < minOffsetFromEdge) {
          y -= FLIP_ARROW_INSIDE_FACTOR * boxHeight;
        }
        break;
    }

    // Offset stacked line numbers by a quarter of the box's width/height, so a part of
    // them remains visible behind the number that sits at the top of the stack.
    if (isStackedLine) {
      const xOffset = boxWidth / 4;
      const yOffset = boxHeight / 4;

      if (lineNumber > 0) {
        x -= xOffset;
        y -= yOffset;
      } else {
        x += xOffset;
        y += yOffset;
      }
    }

    // If one the edges of the arrow that's parallel to the line is too close to the edge
    // of the viewport (and therefore partly hidden), grow the arrow's size in the
    // opposite direction.
    // The goal is for the part that's not hidden to be exactly the size of a normal
    // arrow and for the arrow to keep pointing at the line (keep being centered on it).
    let grewBox = false;
    const boxWidthBeforeGrowth = boxWidth;
    const boxHeightBeforeGrowth = boxHeight;

    if (dimensionType === ROWS && y <= boxHeight / 2) {
      grewBox = true;
      boxHeight = 2 * (boxHeight - y);
    } else if (dimensionType === ROWS && y >= height - boxHeight / 2) {
      grewBox = true;
      boxHeight = 2 * (y - height + boxHeight);
    } else if (dimensionType === COLUMNS && x <= boxWidth / 2) {
      grewBox = true;
      boxWidth = 2 * (boxWidth - x);
    } else if (dimensionType === COLUMNS && x >= width - boxWidth / 2) {
      grewBox = true;
      boxWidth = 2 * (x - width + boxWidth);
    }

    // Draw the arrow box itself
    drawBubbleRect(
      this.ctx,
      x,
      y,
      boxWidth,
      boxHeight,
      radius,
      margin,
      arrowSize,
      boxEdge
    );

    // Determine the text position for it to be centered nicely inside the arrow box.
    switch (boxEdge) {
      case "left":
        x -= boxWidth + arrowSize + radius - boxWidth / 2;
        break;
      case "right":
        x += boxWidth + arrowSize + radius - boxWidth / 2;
        break;
      case "top":
        y -= boxHeight + arrowSize + radius - boxHeight / 2;
        break;
      case "bottom":
        y += boxHeight + arrowSize + radius - boxHeight / 2;
        break;
    }

    // Do a second pass to adjust the position, along the other axis, if the box grew
    // during the previous step, so the text is also centered on that axis.
    if (grewBox) {
      if (dimensionType === ROWS && y <= boxHeightBeforeGrowth / 2) {
        y = boxHeightBeforeGrowth / 2;
      } else if (
        dimensionType === ROWS &&
        y >= height - boxHeightBeforeGrowth / 2
      ) {
        y = height - boxHeightBeforeGrowth / 2;
      } else if (dimensionType === COLUMNS && x <= boxWidthBeforeGrowth / 2) {
        x = boxWidthBeforeGrowth / 2;
      } else if (
        dimensionType === COLUMNS &&
        x >= width - boxWidthBeforeGrowth / 2
      ) {
        x = width - boxWidthBeforeGrowth / 2;
      }
    }

    // Write the line number inside of the rectangle.
    this.ctx.textAlign = "center";
    this.ctx.textBaseline = "middle";
    this.ctx.fillStyle = "black";
    const numberText = isStackedLine ? "" : lineNumber;
    this.ctx.fillText(numberText, x, y);
    this.ctx.restore();
  }

  /**
   * Determine which edge of a line number box to aim the line number arrow at.
   *
   * @param  {String} dimensionType
   *         The grid line dimension type which is either the constant COLUMNS or ROWS.
   * @param  {Number} lineNumber
   *         The grid line number.
   * @return {String} The edge of the box: top, right, bottom or left.
   */
  getBoxEdge(dimensionType, lineNumber) {
    let boxEdge;

    if (dimensionType === COLUMNS) {
      boxEdge = lineNumber > 0 ? "top" : "bottom";
    } else if (dimensionType === ROWS) {
      boxEdge = lineNumber > 0 ? "left" : "right";
    }

    // Rotate box edge as needed for writing mode and text direction.
    const { direction, writingMode } = getComputedStyle(this.currentNode);

    switch (writingMode) {
      case "horizontal-tb":
        // This is the initial value.  No further adjustment needed.
        break;
      case "vertical-rl":
        boxEdge = rotateEdgeRight(boxEdge);
        break;
      case "vertical-lr":
        if (dimensionType === COLUMNS) {
          boxEdge = rotateEdgeLeft(boxEdge);
        } else {
          boxEdge = rotateEdgeRight(boxEdge);
        }
        break;
      case "sideways-rl":
        boxEdge = rotateEdgeRight(boxEdge);
        break;
      case "sideways-lr":
        boxEdge = rotateEdgeLeft(boxEdge);
        break;
      default:
        console.error(`Unexpected writing-mode: ${writingMode}`);
    }

    switch (direction) {
      case "ltr":
        // This is the initial value.  No further adjustment needed.
        break;
      case "rtl":
        if (dimensionType === ROWS) {
          boxEdge = reflectEdge(boxEdge);
        }
        break;
      default:
        console.error(`Unexpected direction: ${direction}`);
    }

    return boxEdge;
  }

  /**
   * Render the grid line on the css grid highlighter canvas.
   *
   * @param  {Number} linePos
   *         The line position along the x-axis for a column grid line and
   *         y-axis for a row grid line.
   * @param  {Number} startPos
   *         The start position of the cross side of the grid line.
   * @param  {Number} endPos
   *         The end position of the cross side of the grid line.
   * @param  {String} dimensionType
   *         The grid dimension type which is either the constant COLUMNS or ROWS.
   * @param  {String} lineType
   *         The grid line type - "edge", "explicit", or "implicit".
   */
  renderLine(linePos, startPos, endPos, dimensionType, lineType) {
    const { devicePixelRatio } = this.win;
    const lineWidth = getDisplayPixelRatio(this.win);
    const offset = (lineWidth / 2) % 1;
    const canvasX = Math.round(this._canvasPosition.x * devicePixelRatio);
    const canvasY = Math.round(this._canvasPosition.y * devicePixelRatio);

    linePos = Math.round(linePos);
    startPos = Math.round(startPos);
    endPos = Math.round(endPos);

    this.ctx.save();
    this.ctx.setLineDash(GRID_LINES_PROPERTIES[lineType].lineDash);
    this.ctx.translate(offset - canvasX, offset - canvasY);

    const lineOptions = {
      matrix: this.currentMatrix,
    };

    if (this.options.showInfiniteLines) {
      lineOptions.extendToBoundaries = [
        canvasX,
        canvasY,
        canvasX + CANVAS_SIZE,
        canvasY + CANVAS_SIZE,
      ];
    }

    if (dimensionType === COLUMNS) {
      drawLine(this.ctx, linePos, startPos, linePos, endPos, lineOptions);
    } else {
      drawLine(this.ctx, startPos, linePos, endPos, linePos, lineOptions);
    }

    this.ctx.strokeStyle = this.color;
    this.ctx.globalAlpha =
      GRID_LINES_PROPERTIES[lineType].alpha * this.globalAlpha;

    if (GRID_LINES_PROPERTIES[lineType].lineWidth) {
      this.ctx.lineWidth =
        GRID_LINES_PROPERTIES[lineType].lineWidth * devicePixelRatio;
    } else {
      this.ctx.lineWidth = lineWidth;
    }

    this.ctx.stroke();
    this.ctx.restore();
  }

  /**
   * Render the grid lines given the grid dimension information of the
   * column or row lines.
   *
   * @param  {GridDimension} gridDimension
   *         Column or row grid dimension object.
   * @param  {Object} quad.bounds
   *         The content bounds of the box model region quads.
   * @param  {String} dimensionType
   *         The grid dimension type which is either the constant COLUMNS or ROWS.
   * @param  {Number} startPos
   *         The start position of the cross side ("left" for ROWS and "top" for COLUMNS)
   *         of the grid dimension.
   * @param  {Number} endPos
   *         The end position of the cross side ("left" for ROWS and "top" for COLUMNS)
   *         of the grid dimension.
   */
  renderLines(gridDimension, dimensionType, startPos, endPos) {
    const { lines, tracks } = gridDimension;
    const lastEdgeLineIndex = this.getLastEdgeLineIndex(tracks);

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      const linePos = line.start;

      if (i == 0 || i == lastEdgeLineIndex) {
        this.renderLine(linePos, startPos, endPos, dimensionType, "edge");
      } else {
        this.renderLine(
          linePos,
          startPos,
          endPos,
          dimensionType,
          tracks[i - 1].type
        );
      }

      // Render a second line to illustrate the gutter for non-zero breadth.
      if (line.breadth > 0) {
        this.renderGridGap(
          linePos,
          startPos,
          endPos,
          line.breadth,
          dimensionType
        );
        this.renderLine(
          linePos + line.breadth,
          startPos,
          endPos,
          dimensionType,
          tracks[i].type
        );
      }
    }
  }

  /**
   * Render the grid lines given the grid dimension information of the
   * column or row lines.
   *
   * @param  {GridDimension} gridDimension
   *         Column or row grid dimension object.
   * @param  {String} dimensionType
   *         The grid dimension type which is either the constant COLUMNS or ROWS.
   * @param  {Number} startPos
   *         The start position of the cross side ("left" for ROWS and "top" for COLUMNS)
   *         of the grid dimension.
   */
  renderLineNumbers(gridDimension, dimensionType, startPos) {
    const { lines, tracks } = gridDimension;

    for (let i = 0, line; (line = lines[i++]); ) {
      // If you place something using negative numbers, you can trigger some implicit
      // grid creation above and to the left of the explicit grid (assuming a
      // horizontal-tb writing mode).
      //
      // The first explicit grid line gets the number of 1, and any implicit grid lines
      // before 1 get negative numbers. Since here we're rendering only the positive line
      // numbers, we have to skip any implicit grid lines before the first one that is
      // explicit. The API returns a 0 as the line's number for these implicit lines that
      // occurs before the first explicit line.
      if (line.number === 0) {
        continue;
      }

      // Check for overlapping lines by measuring the track width between them.
      // We render a second box beneath the last overlapping
      // line number to indicate there are lines beneath it.
      const gridTrack = tracks[i - 1];

      if (gridTrack) {
        const { breadth } = gridTrack;

        if (breadth === 0) {
          this.renderGridLineNumber(
            line.number,
            line.start,
            startPos,
            line.breadth,
            dimensionType,
            true
          );
          continue;
        }
      }

      this.renderGridLineNumber(
        line.number,
        line.start,
        startPos,
        line.breadth,
        dimensionType
      );
    }
  }

  /**
   * Render the negative grid lines given the grid dimension information of the
   * column or row lines.
   *
   * @param  {GridDimension} gridDimension
   *         Column or row grid dimension object.
   * @param  {String} dimensionType
   *         The grid dimension type which is either the constant COLUMNS or ROWS.
   * @param  {Number} startPos
   *         The start position of the cross side ("left" for ROWS and "top" for COLUMNS)
   *         of the grid dimension.
   */
  renderNegativeLineNumbers(gridDimension, dimensionType, startPos) {
    const { lines, tracks } = gridDimension;

    for (let i = 0, line; (line = lines[i++]); ) {
      const linePos = line.start;
      const negativeLineNumber = line.negativeNumber;

      // Don't render any negative line number greater than -1.
      if (negativeLineNumber == 0) {
        break;
      }

      // Check for overlapping lines by measuring the track width between them.
      // We render a second box beneath the last overlapping
      // line number to indicate there are lines beneath it.
      const gridTrack = tracks[i - 1];
      if (gridTrack) {
        const { breadth } = gridTrack;

        // Ensure "-1" is always visible, since it is always the largest number.
        if (breadth === 0 && negativeLineNumber != -1) {
          this.renderGridLineNumber(
            negativeLineNumber,
            linePos,
            startPos,
            line.breadth,
            dimensionType,
            true
          );
          continue;
        }
      }

      this.renderGridLineNumber(
        negativeLineNumber,
        linePos,
        startPos,
        line.breadth,
        dimensionType
      );
    }
  }

  /**
   * Update the highlighter on the current highlighted node (the one that was
   * passed as an argument to show(node)). Should be called whenever node's geometry
   * or grid changes.
   */
  _update() {
    setIgnoreLayoutChanges(true);

    const root = this.getNode("root");
    this._winDimensions = getWindowDimensions(this.win);
    const { width, height } = this._winDimensions;

    // Updates the <canvas> element's position and size.
    // It also clear the <canvas>'s drawing context.
    updateCanvasElement(
      this.canvas,
      this._canvasPosition,
      this.win.devicePixelRatio
    );

    // Clear the grid area highlights.
    this.clearGridAreas();
    this.clearGridCell();

    // Update the current matrix used in our canvas' rendering.
    const { currentMatrix, hasNodeTransformations } = getCurrentMatrix(
      this.currentNode,
      this.win
    );
    this.currentMatrix = currentMatrix;
    this.hasNodeTransformations = hasNodeTransformations;

    // Start drawing the grid fragments.
    for (let i = 0; i < this.gridData.length; i++) {
      this.renderFragment(this.gridData[i]);
    }

    // Display the grid area highlights if needed.
    if (this.options.showAllGridAreas) {
      this.showAllGridAreas();
    } else if (this.options.showGridArea) {
      this.showGridArea(this.options.showGridArea);
    }

    // Display the grid cell highlights if needed.
    if (this.options.showGridCell) {
      this.showGridCell(this.options.showGridCell);
    }

    // Display the grid line names if needed.
    if (this.options.showGridLineNames) {
      this.showGridLineNames(this.options.showGridLineNames);
    }

    this._showGrid();
    this._showGridElements();

    root.style.setProperty("width", `${width}px`);
    root.style.setProperty("height", `${height}px`);

    setIgnoreLayoutChanges(false, this.highlighterEnv.document.documentElement);
    return true;
  }

  /**
   * Update the grid information displayed in the grid area info bar.
   *
   * @param  {GridArea} area
   *         The grid area object.
   * @param  {Object} bounds
   *         A DOMRect-like object represent the grid area rectangle.
   */
  _updateGridAreaInfobar(area, bounds) {
    const { width, height } = bounds;
    const dim =
      parseFloat(width.toPrecision(6)) +
      " \u00D7 " +
      parseFloat(height.toPrecision(6));

    this.getElement("area-infobar-name").setTextContent(area.name);
    this.getElement("area-infobar-dimensions").setTextContent(dim);

    const container = this.getElement("area-infobar-container");
    moveInfobar(container, bounds, this.win, {
      position: "bottom",
    });
  }

  /**
   * Update the grid information displayed in the grid cell info bar.
   *
   * @param  {Number} rowNumber
   *         The grid cell's row number.
   * @param  {Number} columnNumber
   *         The grid cell's column number.
   * @param  {Object} bounds
   *         A DOMRect-like object represent the grid cell rectangle.
   */
  _updateGridCellInfobar(rowNumber, columnNumber, bounds) {
    const { width, height } = bounds;
    const dim =
      parseFloat(width.toPrecision(6)) +
      " \u00D7 " +
      parseFloat(height.toPrecision(6));
    const position = HighlightersBundle.formatValueSync(
      "grid-row-column-positions",
      { row: rowNumber, column: columnNumber }
    );

    this.getElement("cell-infobar-position").setTextContent(position);
    this.getElement("cell-infobar-dimensions").setTextContent(dim);

    const container = this.getElement("cell-infobar-container");
    moveInfobar(container, bounds, this.win, {
      position: "top",
    });
  }

  /**
   * Update the grid information displayed in the grid line info bar.
   *
   * @param  {String} gridLineNames
   *         Comma-separated string of names for the grid line.
   * @param  {Number} gridLineNumber
   *         The grid line number.
   * @param  {Number} x
   *         The x-coordinate of the grid line.
   * @param  {Number} y
   *         The y-coordinate of the grid line.
   */
  _updateGridLineInfobar(gridLineNames, gridLineNumber, x, y) {
    this.getElement("line-infobar-number").setTextContent(gridLineNumber);
    this.getElement("line-infobar-names").setTextContent(gridLineNames);

    const container = this.getElement("line-infobar-container");
    moveInfobar(
      container,
      getBoundsFromPoints([
        { x, y },
        { x, y },
        { x, y },
        { x, y },
      ]),
      this.win
    );
  }
}

exports.CssGridHighlighter = CssGridHighlighter;

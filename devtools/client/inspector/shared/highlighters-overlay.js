/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  safeAsyncMethod,
} = require("resource://devtools/shared/async-utils.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const WalkerEventListener = require("resource://devtools/client/inspector/shared/walker-event-listener.js");
const {
  VIEW_NODE_VALUE_TYPE,
  VIEW_NODE_SHAPE_POINT_TYPE,
} = require("resource://devtools/client/inspector/shared/node-types.js");

const { TYPES } = ChromeUtils.importESModule(
  "resource://devtools/shared/highlighters.mjs"
);

loader.lazyRequireGetter(
  this,
  "parseURL",
  "resource://devtools/client/shared/source-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "asyncStorage",
  "resource://devtools/shared/async-storage.js"
);
loader.lazyRequireGetter(
  this,
  "gridsReducer",
  "resource://devtools/client/inspector/grids/reducers/grids.js"
);
loader.lazyRequireGetter(
  this,
  "highlighterSettingsReducer",
  "resource://devtools/client/inspector/grids/reducers/highlighter-settings.js"
);
loader.lazyRequireGetter(
  this,
  "flexboxReducer",
  "resource://devtools/client/inspector/flexbox/reducers/flexbox.js"
);
loader.lazyRequireGetter(
  this,
  "deepEqual",
  "resource://devtools/shared/DevToolsUtils.js",
  true
);
loader.lazyGetter(this, "HighlightersBundle", () => {
  return new Localization(["devtools/shared/highlighters.ftl"], true);
});

const DEFAULT_HIGHLIGHTER_COLOR = "#9400FF";
const SUBGRID_PARENT_ALPHA = 0.5;

/**
 * While refactoring to an abstracted way to show and hide highlighters,
 * we did not update all tests and code paths which listen for exact events.
 *
 * When we show or hide highlighters we reference this mapping to
 * emit events that consumers may be listening to.
 *
 * This list should go away as we incrementally rewrite tests to use
 * abstract event names with data payloads indicating the highlighter.
 *
 * DO NOT OPTIMIZE THIS MAPPING AS CONCATENATED SUBSTRINGS!
 * It makes it difficult to do project-wide searches for exact matches.
 */
const HIGHLIGHTER_EVENTS = {
  [TYPES.GRID]: {
    shown: "grid-highlighter-shown",
    hidden: "grid-highlighter-hidden",
  },
  [TYPES.GEOMETRY]: {
    shown: "geometry-editor-highlighter-shown",
    hidden: "geometry-editor-highlighter-hidden",
  },
  [TYPES.SHAPES]: {
    shown: "shapes-highlighter-shown",
    hidden: "shapes-highlighter-hidden",
  },
  [TYPES.TRANSFORM]: {
    shown: "css-transform-highlighter-shown",
    hidden: "css-transform-highlighter-hidden",
  },
};

// Tool IDs mapped by highlighter type. Used to log telemetry for opening & closing tools.
const GLEAN_TOOL_IDS = {
  [TYPES.FLEXBOX]: "flexbox_highlighter",
  [TYPES.GRID]: "grid_highlighter",
};

// Glean counter names mapped by highlighter type. Used to log telemetry about highlighter triggers.
const GLEAN_COUNTER_NAMES = {
  [TYPES.FLEXBOX]: {
    layout: "devtoolsLayoutFlexboxhighlighter",
    markup: "devtoolsMarkupFlexboxhighlighter",
    rule: "devtoolsRulesFlexboxhighlighter",
  },

  [TYPES.GRID]: {
    grid: "devtoolsGridGridinspector",
    markup: "devtoolsMarkupGridinspector",
    rule: "devtoolsRulesGridinspector",
  },
};

/**
 * HighlightersOverlay manages the visibility of highlighters in the Inspector.
 */
class HighlightersOverlay {
  /**
   * @param  {Inspector} inspector
   *         Inspector toolbox panel.
   */
  constructor(inspector) {
    this.inspector = inspector;
    this.store = this.inspector.store;

    this.telemetry = this.inspector.telemetry;
    this.maxGridHighlighters = Services.prefs.getIntPref(
      "devtools.gridinspector.maxHighlighters"
    );

    // Collection of instantiated highlighter actors like FlexboxHighlighter,
    // ShapesHighlighter and GeometryEditorHighlighter.
    this.highlighters = {};
    // Map of grid container node to an object with the grid highlighter instance
    // and, if the node is a subgrid, the parent grid node and parent grid highlighter.
    // Ex: {NodeFront} => {
    //  highlighter: {CustomHighlighterFront},
    //  parentGridNode: {NodeFront|null},
    //  parentGridHighlighter: {CustomHighlighterFront|null}
    // }
    this.gridHighlighters = new Map();
    // Collection of instantiated in-context editors, like ShapesInContextEditor, which
    // behave like highlighters but with added editing capabilities that need to map value
    // changes to properties in the Rule view.
    this.editors = {};

    // Highlighter state.
    this.state = {
      // Map of grid container NodeFront to the their stored grid options
      // Used to restore grid highlighters on reload (should be migrated to
      // #restorableHighlighters in Bug 1572652).
      grids: new Map(),
      // Shape Path Editor highlighter options.
      // Used as a cache for the latest configuration when showing the highlighter.
      // It is reused and augmented when hovering coordinates in the Rules view which
      // mark the corresponding points in the highlighter overlay.
      shapes: {},
    };

    // NodeFront of element that is highlighted by the geometry editor.
    this.geometryEditorHighlighterShown = null;
    // Name of the highlighter shown on mouse hover.
    this.hoveredHighlighterShown = null;
    // NodeFront of the shape that is highlighted
    this.shapesHighlighterShown = null;

    this.onClick = this.onClick.bind(this);
    this.onDisplayChange = this.onDisplayChange.bind(this);
    this.onMarkupMutation = this.onMarkupMutation.bind(this);

    this.onMouseMove = this.onMouseMove.bind(this);
    this.onMouseOut = this.onMouseOut.bind(this);
    this.hideAllHighlighters = this.hideAllHighlighters.bind(this);
    this.hideFlexboxHighlighter = this.hideFlexboxHighlighter.bind(this);
    this.hideGridHighlighter = this.hideGridHighlighter.bind(this);
    this.hideShapesHighlighter = this.hideShapesHighlighter.bind(this);
    this.showFlexboxHighlighter = this.showFlexboxHighlighter.bind(this);
    this.showGridHighlighter = this.showGridHighlighter.bind(this);
    this.showShapesHighlighter = this.showShapesHighlighter.bind(this);
    this.onShapesHighlighterShown = this.onShapesHighlighterShown.bind(this);
    this.onShapesHighlighterHidden = this.onShapesHighlighterHidden.bind(this);

    // Catch unexpected errors from async functions if the manager has been destroyed.
    this.hideHighlighterType = safeAsyncMethod(
      this.hideHighlighterType.bind(this),
      () => this.destroyed
    );
    this.showHighlighterTypeForNode = safeAsyncMethod(
      this.showHighlighterTypeForNode.bind(this),
      () => this.destroyed
    );
    this.showGridHighlighter = safeAsyncMethod(
      this.showGridHighlighter.bind(this),
      () => this.destroyed
    );
    this.restoreState = safeAsyncMethod(
      this.restoreState.bind(this),
      () => this.destroyed
    );

    // Add inspector events, not specific to a given view.
    this.inspector.on("markupmutation", this.onMarkupMutation);

    this.resourceCommand = this.inspector.toolbox.resourceCommand;
    this.resourceCommand.watchResources(
      [this.resourceCommand.TYPES.ROOT_NODE],
      { onAvailable: this.#onResourceAvailable }
    );

    this.walkerEventListener = new WalkerEventListener(this.inspector, {
      "display-change": this.onDisplayChange,
    });

    if (this.toolbox.win.matchMedia("(prefers-reduced-motion)").matches) {
      this.#showSimpleHighlightersMessage();
    }

    EventEmitter.decorate(this);
  }

  // Map of active highlighter types to objects with the highlighted nodeFront and the
  // highlighter instance. Ex: "BoxModelHighlighter" => { nodeFront, highlighter }
  // It will fully replace this.highlighters when all highlighter consumers are updated
  // to use it as the single source of truth for which highlighters are visible.
  #activeHighlighters = new Map();
  // Map of highlighter types to symbols. Showing highlighters is an async operation,
  // until it doesn't complete, this map will be populated with the requested type and
  // a unique symbol identifying that request. Once completed, the entry is removed.
  #pendingHighlighters = new Map();
  // Map of highlighter types to objects with metadata used to restore active
  // highlighters after a page reload.
  #restorableHighlighters = new Map();

  #lastHovered = null;

  get inspectorFront() {
    return this.inspector.inspectorFront;
  }

  get target() {
    return this.inspector.currentTarget;
  }

  get toolbox() {
    return this.inspector.toolbox;
  }

  /**
   * Optionally run some operations right after showing a highlighter of a given type,
   * but before notifying consumers by emitting the "highlighter-shown" event.
   *
   * This is a chance to run some non-essential operations like: logging telemetry data,
   * storing metadata about the highlighter to enable restoring it after refresh, etc.
   *
   * @param  {String} type
   *          Highlighter type shown.
   * @param  {NodeFront} nodeFront
   *          Node front of the element that was highlighted.
   * @param  {Options} options
   *          Optional object with options passed to the highlighter.
   */
  #afterShowHighlighterTypeForNode(type, nodeFront, options) {
    switch (type) {
      // Log telemetry for showing the flexbox and grid highlighters.
      case TYPES.FLEXBOX:
      case TYPES.GRID:
        const toolID = GLEAN_TOOL_IDS[type];
        if (toolID) {
          this.telemetry.toolOpened(toolID, this);
        }

        const counterName = GLEAN_COUNTER_NAMES[type]?.[options?.trigger];
        if (counterName) {
          Glean[counterName].opened.add(1);
        }

        break;
    }

    // Set metadata necessary to restore the active highlighter upon page refresh.
    if (type === TYPES.FLEXBOX) {
      const { url } = this.target;
      const selectors = [...this.inspector.selectionCssSelectors];

      this.#restorableHighlighters.set(type, {
        options,
        selectors,
        type,
        url,
      });
    }
  }

  /**
   * Optionally run some operations before showing a highlighter of a given type.
   *
   * Depending its type, before showing a new instance of a highlighter, we may do extra
   * operations, like hiding another visible highlighter, or preventing the show
   * operation, for example due to a duplicate call with the same arguments.
   *
   * Returns a promise that resovles with a boolean indicating whether to skip showing
   * the highlighter with these arguments.
   *
   * @param  {String} type
   *          Highlighter type to show.
   * @param  {NodeFront} nodeFront
   *          Node front of the element to be highlighted.
   * @param  {Options} options
   *          Optional object with options to pass to the highlighter.
   * @return {Promise}
   */
  async #beforeShowHighlighterTypeForNode(type, nodeFront, options) {
    // Get the data associated with the visible highlighter of this type, if any.
    const {
      highlighter: activeHighlighter,
      nodeFront: activeNodeFront,
      options: activeOptions,
      timer: activeTimer,
    } = this.getDataForActiveHighlighter(type);

    // There isn't an active highlighter of this type. Early return, proceed with showing.
    if (!activeHighlighter) {
      return false;
    }

    // Whether conditions are met to skip showing the highlighter (ex: duplicate calls).
    let skipShow = false;

    // Clear any autohide timer associated with this highlighter type.
    // This clears any existing timer for duplicate calls to show() if:
    // - called with different options.duration
    // - called once with options.duration, then without (see deepEqual() above)
    clearTimeout(activeTimer);

    switch (type) {
      // Hide the visible selector highlighter if called for the same node,
      // but with a different selector.
      case TYPES.SELECTOR:
        if (
          nodeFront === activeNodeFront &&
          options?.selector !== activeOptions?.selector
        ) {
          await this.hideHighlighterType(TYPES.SELECTOR);
        }
        break;

      // For others, hide the existing highlighter before showing it for a different node.
      // Else, if the node is the same and options are the same, skip a duplicate call.
      // Duplicate calls to show the highlighter for the same node are allowed
      // if the options are different (for example, when scheduling autohide).
      default:
        if (nodeFront !== activeNodeFront) {
          await this.hideHighlighterType(type);
        } else if (deepEqual(options, activeOptions)) {
          skipShow = true;
        }
    }

    return skipShow;
  }

  /**
   * Optionally run some operations before hiding a highlighter of a given type.
   * Runs only if a highlighter of that type exists.
   *
   * @param {String} type
   *         highlighter type
   * @return {Promise}
   */
  #beforeHideHighlighterType(type) {
    switch (type) {
      // Log telemetry for hiding the flexbox and grid highlighters.
      case TYPES.FLEXBOX:
      case TYPES.GRID:
        const toolID = GLEAN_TOOL_IDS[type];
        const conditions = {
          [TYPES.FLEXBOX]: () => {
            // always stop the timer when the flexbox highlighter is about to be hidden.
            return true;
          },
          [TYPES.GRID]: () => {
            // stop the timer only once the last grid highlighter is about to be hidden.
            return this.gridHighlighters.size === 1;
          },
        };

        if (toolID && conditions[type].call(this)) {
          this.telemetry.toolClosed(toolID, this);
        }

        break;
    }
  }

  /**
   * Get the maximum number of possible active highlighter instances of a given type.
   *
   * @param  {String} type
   *         Highlighter type
   * @return {Number}
   *         Default 1
   */
  #getMaxActiveHighlighters(type) {
    let max;

    switch (type) {
      // Grid highligthters are special (there is a parent-child relationship between
      // subgrid and parent grid) so we suppport multiple visible instances.
      // Grid highlighters are performance-intensive and this limit is somewhat arbitrary
      // to guard against performance degradation.
      case TYPES.GRID:
        max = this.maxGridHighlighters;
        break;
      // By default, for all other highlighter types, only one instance may visible.
      // Before showing a new highlighter, any other instance will be hidden.
      default:
        max = 1;
    }

    return max;
  }

  /**
   * Get a highlighter instance of the given type for the given node front.
   *
   * @param  {String} type
   *         Highlighter type.
   * @param  {NodeFront} nodeFront
   *         Node front of the element to be highlighted with the requested highlighter.
   * @return {Promise}
   *         Promise which resolves with a highlighter instance
   */
  async #getHighlighterTypeForNode(type, nodeFront) {
    const { inspectorFront } = nodeFront;
    const max = this.#getMaxActiveHighlighters(type);
    let highlighter;

    // If only one highlighter instance may be visible, get a highlighter front
    // and cache it to return it on future requests.
    // Otherwise, return a new highlighter front every time and clean-up manually.
    if (max === 1) {
      highlighter = await inspectorFront.getOrCreateHighlighterByType(type);
    } else {
      highlighter = await inspectorFront.getHighlighterByType(type);
    }

    return highlighter;
  }

  /**
   * Get the currently active highlighter of a given type.
   *
   * @param  {String} type
   *         Highlighter type.
   * @return {Highlighter|null}
   *         Highlighter instance
   *         or null if no highlighter of that type is active.
   */
  getActiveHighlighter(type) {
    if (!this.#activeHighlighters.has(type)) {
      return null;
    }

    const { highlighter } = this.#activeHighlighters.get(type);
    return highlighter;
  }

  /**
   * Get an object with data associated with the active highlighter of a given type.
   * This data object contains:
   *   - nodeFront: NodeFront of the highlighted node
   *   - highlighter: Highlighter instance
   *   - options: Configuration options passed to the highlighter
   *   - timer: (Optional) index of timer set with setTimout() to autohide the highlighter
   * Returns an empty object if a highlighter of the given type is not active.
   *
   * @param  {String} type
   *         Highlighter type.
   * @return {Object}
   */
  getDataForActiveHighlighter(type) {
    if (!this.#activeHighlighters.has(type)) {
      return {};
    }

    return this.#activeHighlighters.get(type);
  }

  /**
   * Get the configuration options of the active highlighter of a given type.
   *
   * @param  {String} type
   *         Highlighter type.
   * @return {Object}
   */
  getOptionsForActiveHighlighter(type) {
    const { options } = this.getDataForActiveHighlighter(type);
    return options;
  }

  /**
   * Get the node front highlighted by a given highlighter type.
   *
   * @param  {String} type
   *         Highlighter type.
   * @return {NodeFront|null}
   *         Node front of the element currently being highlighted
   *         or null if no highlighter of that type is active.
   */
  getNodeForActiveHighlighter(type) {
    if (!this.#activeHighlighters.has(type)) {
      return null;
    }

    const { nodeFront } = this.#activeHighlighters.get(type);
    return nodeFront;
  }

  /**
   * Highlight a given node front with a given type of highlighter.
   *
   * Highlighters are shown for one node at a time. Before showing the same highlighter
   * type on another node, it will first be hidden from the previously highlighted node.
   * In pages with frames running in different processes, this ensures highlighters from
   * other frames do not stay visible.
   *
   * @param  {String} type
   *          Highlighter type to show.
   * @param  {NodeFront} nodeFront
   *          Node front of the element to be highlighted.
   * @param  {Options} options
   *         Optional object with options to pass to the highlighter.
   * @return {Promise}
   */
  async showHighlighterTypeForNode(type, nodeFront, options) {
    const promise = this.#beforeShowHighlighterTypeForNode(
      type,
      nodeFront,
      options
    );

    // Set a pending highlighter in order to detect if, while we were awaiting, there was
    // a more recent request to highlight a node with the same type, or a request to hide
    // the highlighter. Then we will abort this one in favor of the newer one.
    // This needs to be done before the 'await' in order to be synchronous, but after
    // calling #beforeShowHighlighterTypeForNode, since it can call hideHighlighterType.
    const id = Symbol();
    this.#pendingHighlighters.set(type, id);
    const skipShow = await promise;

    if (this.#pendingHighlighters.get(type) !== id) {
      return;
    } else if (skipShow || nodeFront.isDestroyed()) {
      this.#pendingHighlighters.delete(type);
      return;
    }

    const highlighter = await this.#getHighlighterTypeForNode(type, nodeFront);

    if (this.#pendingHighlighters.get(type) !== id) {
      return;
    }
    this.#pendingHighlighters.delete(type);

    // Set a timer to automatically hide the highlighter if a duration is provided.
    const timer = this.scheduleAutoHideHighlighterType(type, options?.duration);
    // TODO: support case for multiple highlighter instances (ex: multiple grids)
    this.#activeHighlighters.set(type, {
      nodeFront,
      highlighter,
      options,
      timer,
    });
    await highlighter.show(nodeFront, options);
    this.#afterShowHighlighterTypeForNode(type, nodeFront, options);

    // Emit any type-specific highlighter shown event for tests
    // which have not yet been updated to listen for the generic event
    if (HIGHLIGHTER_EVENTS[type]?.shown) {
      this.emit(HIGHLIGHTER_EVENTS[type].shown, nodeFront, options);
    }
    this.emit("highlighter-shown", { type, highlighter, nodeFront, options });
  }

  /**
   * Set a timer to automatically hide all highlighters of a given type after a delay.
   *
   * @param  {String} type
   *         Highlighter type to hide.
   * @param  {Number|undefined} duration
   *         Delay in milliseconds after which to hide the highlighter.
   *         If a duration is not provided, return early without scheduling a task.
   * @return {Number|undefined}
   *         Index of the scheduled task returned by setTimeout().
   */
  scheduleAutoHideHighlighterType(type, duration) {
    if (!duration) {
      return undefined;
    }

    const timer = setTimeout(async () => {
      await this.hideHighlighterType(type);
      clearTimeout(timer);
    }, duration);

    return timer;
  }

  /**
   * Hide all instances of a given highlighter type.
   *
   * @param  {String} type
   *         Highlighter type to hide.
   * @return {Promise}
   */
  async hideHighlighterType(type) {
    if (this.#pendingHighlighters.has(type)) {
      // Abort pending highlighters for the given type.
      this.#pendingHighlighters.delete(type);
    }
    if (!this.#activeHighlighters.has(type)) {
      return;
    }

    const data = this.getDataForActiveHighlighter(type);
    const { highlighter, nodeFront, timer } = data;
    // Clear any autohide timer associated with this highlighter type.
    clearTimeout(timer);
    // Remove any metadata used to restore this highlighter type on page refresh.
    this.#restorableHighlighters.delete(type);
    this.#activeHighlighters.delete(type);
    this.#beforeHideHighlighterType(type);
    await highlighter.hide();

    // Emit any type-specific highlighter hidden event for tests
    // which have not yet been updated to listen for the generic event
    if (HIGHLIGHTER_EVENTS[type]?.hidden) {
      this.emit(HIGHLIGHTER_EVENTS[type].hidden, nodeFront);
    }
    this.emit("highlighter-hidden", { type, ...data });
  }

  /**
   * Returns true if the grid highlighter can be toggled on/off for the given node, and
   * false otherwise. A grid container can be toggled on if the max grid highlighters
   * is only 1 or less than the maximum grid highlighters that can be displayed or if
   * the grid highlighter already highlights the given node.
   *
   * @param  {NodeFront} node
   *         Grid container NodeFront.
   * @return {Boolean}
   */
  canGridHighlighterToggle(node) {
    return (
      this.maxGridHighlighters === 1 ||
      this.gridHighlighters.size < this.maxGridHighlighters ||
      this.gridHighlighters.has(node)
    );
  }

  /**
   * Returns true when the maximum number of grid highlighter instances is reached.
   * FIXME: Bug 1572652 should address this constraint.
   *
   * @return {Boolean}
   */
  isGridHighlighterLimitReached() {
    return this.gridHighlighters.size === this.maxGridHighlighters;
  }

  /**
   * Returns whether `node` is somewhere inside the DOM of the rule view.
   *
   * @param {DOMNode} node
   * @return {Boolean}
   */
  isRuleView(node) {
    return !!node.closest("#ruleview-panel");
  }

  /**
   * Add the highlighters overlay to the view. This will start tracking mouse events
   * and display highlighters when needed.
   *
   * @param  {CssRuleView|CssComputedView|LayoutView} view
   *         Either the rule-view or computed-view panel to add the highlighters overlay.
   */
  addToView(view) {
    const el = view.element;
    el.addEventListener("click", this.onClick, true);
    el.addEventListener("mousemove", this.onMouseMove);
    el.addEventListener("mouseout", this.onMouseOut);
    el.ownerDocument.defaultView.addEventListener("mouseout", this.onMouseOut);
  }

  /**
   * Remove the overlay from the given view. This will stop tracking mouse movement and
   * showing highlighters.
   *
   * @param  {CssRuleView|CssComputedView|LayoutView} view
   *         Either the rule-view or computed-view panel to remove the highlighters
   *         overlay.
   */
  removeFromView(view) {
    const el = view.element;
    el.removeEventListener("click", this.onClick, true);
    el.removeEventListener("mousemove", this.onMouseMove);
    el.removeEventListener("mouseout", this.onMouseOut);
  }

  /**
   * Toggle the shapes highlighter for the given node.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the element with a shape to highlight.
   * @param  {Object} options
   *         Object used for passing options to the shapes highlighter.
   * @param {TextProperty} textProperty
   *        TextProperty where to write changes.
   */
  async toggleShapesHighlighter(node, options, textProperty) {
    const shapesEditor = await this.getInContextEditor(node, "shapesEditor");
    if (!shapesEditor) {
      return;
    }
    shapesEditor.toggle(node, options, textProperty);
  }

  /**
   * Show the shapes highlighter for the given node.
   * This method delegates to the in-context shapes editor.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the element with a shape to highlight.
   * @param  {Object} options
   *         Object used for passing options to the shapes highlighter.
   */
  async showShapesHighlighter(node, options) {
    const shapesEditor = await this.getInContextEditor(node, "shapesEditor");
    if (!shapesEditor) {
      return;
    }
    shapesEditor.show(node, options);
  }

  /**
   * Called after the shape highlighter was shown.
   *
   * @param  {Object} data
   *         Data associated with the event.
   *         Contains:
   *         - {NodeFront} node: The NodeFront of the element that is highlighted.
   *         - {Object} options: Options that were passed to ShapesHighlighter.show()
   */
  onShapesHighlighterShown(data) {
    const { node, options } = data;
    this.shapesHighlighterShown = node;
    this.state.shapes.options = options;
    this.emit("shapes-highlighter-shown", node, options);
  }

  /**
   * Hide the shapes highlighter if visible.
   * This method delegates the to the in-context shapes editor which wraps
   * the shapes highlighter with additional functionality.
   *
   * @param  {NodeFront} node.
   */
  async hideShapesHighlighter(node) {
    const shapesEditor = await this.getInContextEditor(node, "shapesEditor");
    if (!shapesEditor) {
      return;
    }
    shapesEditor.hide();
  }

  /**
   * Called after the shapes highlighter was hidden.
   */
  onShapesHighlighterHidden() {
    this.emit(
      "shapes-highlighter-hidden",
      this.shapesHighlighterShown,
      this.state.shapes.options
    );
    this.shapesHighlighterShown = null;
    this.state.shapes = {};
  }

  /**
   * Show the shapes highlighter for the given element, with the given point highlighted.
   *
   * @param {NodeFront} node
   *        The NodeFront of the element to highlight.
   * @param {String} point
   *        The point to highlight in the shapes highlighter.
   */
  async hoverPointShapesHighlighter(node, point) {
    if (node == this.shapesHighlighterShown) {
      const options = Object.assign({}, this.state.shapes.options);
      options.hoverPoint = point;
      await this.showShapesHighlighter(node, options);
    }
  }

  /**
   * Returns the flexbox highlighter color for the given node.
   */
  async getFlexboxHighlighterColor() {
    // Load the Redux slice for flexbox if not yet available.
    const state = this.store.getState();
    if (!state.flexbox) {
      this.store.injectReducer("flexbox", flexboxReducer);
    }

    // Attempt to get the flexbox highlighter color from the Redux store.
    const { flexbox } = this.store.getState();
    const color = flexbox.color;

    if (color) {
      return color;
    }

    // If the flexbox inspector has not been initialized, attempt to get the flexbox
    // highlighter from the async storage.
    const customHostColors =
      (await asyncStorage.getItem("flexboxInspectorHostColors")) || {};

    // Get the hostname, if there is no hostname, fall back on protocol
    // ex: `data:` uri, and `about:` pages
    let hostname;
    try {
      hostname =
        parseURL(this.target.url).hostname ||
        parseURL(this.target.url).protocol;
    } catch (e) {
      this.#handleRejection(e);
    }

    return hostname && customHostColors[hostname]
      ? customHostColors[hostname]
      : DEFAULT_HIGHLIGHTER_COLOR;
  }

  /**
   * Toggle the flexbox highlighter for the given flexbox container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the flexbox container element to highlight.
   * @param. {String} trigger
   *         String name matching "layout", "markup" or "rule" to indicate where the
   *         flexbox highlighter was toggled on from. "layout" represents the layout view.
   *         "markup" represents the markup view. "rule" represents the rule view.
   */
  async toggleFlexboxHighlighter(node, trigger) {
    const highlightedNode = this.getNodeForActiveHighlighter(TYPES.FLEXBOX);
    if (node == highlightedNode) {
      await this.hideFlexboxHighlighter(node);
      return;
    }

    await this.showFlexboxHighlighter(node, {}, trigger);
  }

  /**
   * Show the flexbox highlighter for the given flexbox container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the flexbox container element to highlight.
   * @param  {Object} options
   *         Object used for passing options to the flexbox highlighter.
   * @param. {String} trigger
   *         String name matching "layout", "markup" or "rule" to indicate where the
   *         flexbox highlighter was toggled on from. "layout" represents the layout view.
   *         "markup" represents the markup view. "rule" represents the rule view.
   *         Will be passed as an option even though the highlighter doesn't use it
   *         in order to log telemetry in #afterShowHighlighterTypeForNode()
   */
  async showFlexboxHighlighter(node, options, trigger) {
    const color = await this.getFlexboxHighlighterColor(node);
    await this.showHighlighterTypeForNode(TYPES.FLEXBOX, node, {
      ...options,
      trigger,
      color,
    });
  }

  /**
   * Hide the flexbox highlighter if any instance is visible.
   */
  async hideFlexboxHighlighter() {
    await this.hideHighlighterType(TYPES.FLEXBOX);
  }

  /**
   * Create a grid highlighter settings object for the provided nodeFront.
   *
   * @param  {NodeFront} nodeFront
   *         The NodeFront for which we need highlighter settings.
   */
  getGridHighlighterSettings(nodeFront) {
    // Load the Redux slices for grids and grid highlighter settings if not yet available.
    const state = this.store.getState();
    if (!state.grids) {
      this.store.injectReducer("grids", gridsReducer);
    }

    if (!state.highlighterSettings) {
      this.store.injectReducer(
        "highlighterSettings",
        highlighterSettingsReducer
      );
    }

    // Get grids and grid highlighter settings from the latest Redux state
    // in case they were just added above.
    const { grids, highlighterSettings } = this.store.getState();
    const grid = grids.find(g => g.nodeFront === nodeFront);
    const color = grid ? grid.color : DEFAULT_HIGHLIGHTER_COLOR;
    const zIndex = grid ? grid.zIndex : 0;
    return Object.assign({}, highlighterSettings, { color, zIndex });
  }

  /**
   * Return a list of all node fronts that are highlighted with a Grid highlighter.
   *
   * @return {Array}
   */
  getHighlightedGridNodes() {
    return [...Array.from(this.gridHighlighters.keys())];
  }

  /**
   * Toggle the grid highlighter for the given grid container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to highlight.
   * @param. {String} trigger
   *         String name matching "grid", "markup" or "rule" to indicate where the
   *         grid highlighter was toggled on from. "grid" represents the grid view.
   *         "markup" represents the markup view. "rule" represents the rule view.
   */
  async toggleGridHighlighter(node, trigger) {
    if (this.gridHighlighters.has(node)) {
      await this.hideGridHighlighter(node);
      return;
    }

    await this.showGridHighlighter(node, {}, trigger);
  }

  /**
   * Show the grid highlighter for the given grid container element.
   * Allow as many active highlighter instances as permitted by the
   * maxGridHighlighters limit (default 3).
   *
   * Logic of showing grid highlighters:
   * - GRID:
   *  - Show a highlighter for a grid container when explicitly requested
   *    (ex. click badge in Markup view) and count it against the limit.
   *  - When the limit of active highlighters is reached, do no show any more
   *    until other instances are hidden. If configured to show only one instance,
   *    hide the existing highlighter before showing a new one.
   *
   * - SUBGRID:
   *  - When a highlighter for a subgrid is shown, also show a highlighter for its parent
   *    grid, but with faded-out colors (serves as a visual reference for the subgrid)
   *  - The "active" state of the highlighter for the parent grid is not reflected
   *    in the UI (checkboxes in the Layout panel, badges in the Markup view, etc.)
   *  - The highlighter for the parent grid DOES NOT count against the highlighter limit
   *  - If the highlighter for the parent grid is explicitly requested to be shown
   *    (ex: click badge in Markup view), show it in full color and reflect its "active"
   *    state in the UI (checkboxes in the Layout panel, badges in the Markup view)
   *  - When a highlighter for a subgrid is hidden, also hide the highlighter for its
   *    parent grid; if the parent grid was explicitly requested separately, keep the
   *    highlighter for the parent grid visible, but show it in full color.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to highlight.
   * @param  {Object} options
   *         Object used for passing options to the grid highlighter.
   * @param  {String} trigger
   *         String name matching "grid", "markup" or "rule" to indicate where the
   *         grid highlighter was toggled on from. "grid" represents the grid view.
   *         "markup" represents the markup view. "rule" represents the rule view.
   */
  async showGridHighlighter(node, options, trigger) {
    if (!this.gridHighlighters.has(node)) {
      // If only one grid highlighter can be shown at a time, hide the other instance.
      // Otherwise, if the max highlighter limit is reached, do not show another one.
      if (this.maxGridHighlighters === 1) {
        await this.hideGridHighlighter(
          this.gridHighlighters.keys().next().value
        );
      } else if (this.gridHighlighters.size === this.maxGridHighlighters) {
        return;
      }
    }

    // If the given node is already highlighted as the parent grid for a subgrid,
    // hide the parent grid highlighter because it will be explicitly shown below.
    const isHighlightedAsParentGrid = Array.from(this.gridHighlighters.values())
      .map(value => value.parentGridNode)
      .includes(node);
    if (isHighlightedAsParentGrid) {
      await this.hideParentGridHighlighter(node);
    }

    // Show a translucent highlight of the parent grid container if the given node is
    // a subgrid and the parent grid container is not already explicitly highlighted.
    let parentGridNode = null;
    let parentGridHighlighter = null;
    if (node.displayType === "subgrid") {
      parentGridNode = await node.walkerFront.getParentGridNode(node);
      parentGridHighlighter =
        await this.showParentGridHighlighter(parentGridNode);
    }

    // When changing highlighter colors, we call highlighter.show() again with new options
    // Reuse the active highlighter instance if present; avoid creating new highlighters
    let highlighter;
    if (this.gridHighlighters.has(node)) {
      highlighter = this.gridHighlighters.get(node).highlighter;
    }

    if (!highlighter) {
      highlighter = await this.#getHighlighterTypeForNode(TYPES.GRID, node);
    }

    this.gridHighlighters.set(node, {
      highlighter,
      parentGridNode,
      parentGridHighlighter,
    });

    options = { ...options, ...this.getGridHighlighterSettings(node) };
    await highlighter.show(node, options);

    this.#afterShowHighlighterTypeForNode(TYPES.GRID, node, {
      ...options,
      trigger,
    });

    try {
      // Save grid highlighter state.
      const { url } = this.target;

      const selectors =
        await this.inspector.commands.inspectorCommand.getNodeFrontSelectorsFromTopDocument(
          node
        );

      this.state.grids.set(node, { selectors, options, url });

      // Emit the NodeFront of the grid container element that the grid highlighter was
      // shown for, and its options for testing the highlighter setting options.
      this.emit("grid-highlighter-shown", node, options);

      // XXX: Shim to use generic highlighter events until addressing Bug 1572652
      // Ensures badges in the Markup view reflect the state of the grid highlighter.
      this.emit("highlighter-shown", {
        type: TYPES.GRID,
        nodeFront: node,
        highlighter,
        options,
      });
    } catch (e) {
      this.#handleRejection(e);
    }
  }

  /**
   * Show the grid highlighter for the given subgrid's parent grid container element.
   * The parent grid highlighter is shown with faded-out colors, as opposed
   * to the full-color grid highlighter shown when calling showGridHighlighter().
   * If the grid container is already explicitly highlighted (i.e. standalone grid),
   * skip showing the another grid highlighter for it.
   *
   * @param   {NodeFront} node
   *          The NodeFront of the parent grid container element to highlight.
   * @returns {Promise}
   *          Resolves with either the highlighter instance or null if it was skipped.
   */
  async showParentGridHighlighter(node) {
    const isHighlighted = Array.from(this.gridHighlighters.keys()).includes(
      node
    );

    if (!node || isHighlighted) {
      return null;
    }

    // Get the parent grid highlighter for the parent grid container if one already exists
    let highlighter = this.getParentGridHighlighter(node);
    if (!highlighter) {
      highlighter = await this.#getHighlighterTypeForNode(TYPES.GRID, node);
    }
    const options = {
      ...this.getGridHighlighterSettings(node),
      // Configure the highlighter with faded-out colors.
      globalAlpha: SUBGRID_PARENT_ALPHA,
      isParent: true,
    };
    await highlighter.show(node, options);

    this.emitForTests("highlighter-shown", {
      type: TYPES.GRID,
      nodeFront: node,
      highlighter,
      options,
    });

    return highlighter;
  }

  /**
   * Get the parent grid highlighter associated with the given node
   * if the node is a parent grid container for a highlighted subgrid.
   *
   * @param  {NodeFront} node
   *         NodeFront of the parent grid container for a subgrid.
   * @return {CustomHighlighterFront|null}
   */
  getParentGridHighlighter(node) {
    // Find the highlighter map value for the subgrid whose parent grid is the given node.
    const value = Array.from(this.gridHighlighters.values()).find(
      ({ parentGridNode }) => {
        return parentGridNode === node;
      }
    );

    if (!value) {
      return null;
    }

    const { parentGridHighlighter } = value;
    return parentGridHighlighter;
  }

  /**
   * Restore the parent grid highlighter for a subgrid.
   *
   * A grid node can be highlighted both explicitly (ex: by clicking a badge in the
   * Markup view) and implicitly, as a parent grid for a subgrid.
   *
   * An explicit grid highlighter overwrites a subgrid's parent grid highlighter.
   * After an explicit grid highlighter for a node is hidden, but that node is also the
   * parent grid container for a subgrid which is still highlighted, restore the implicit
   * parent grid highlighter.
   *
   * @param  {NodeFront} node
   *         NodeFront for a grid node which may also be a subgrid's parent grid
   *         container.
   * @return {Promise}
   */
  async restoreParentGridHighlighter(node) {
    // Find the highlighter map entry for the subgrid whose parent grid is the given node.
    const entry = Array.from(this.gridHighlighters.entries()).find(
      ([, value]) => {
        return value?.parentGridNode === node;
      }
    );

    if (!Array.isArray(entry)) {
      return;
    }

    const [highlightedSubgridNode, data] = entry;
    if (!data.parentGridHighlighter) {
      const parentGridHighlighter = await this.showParentGridHighlighter(node);
      this.gridHighlighters.set(highlightedSubgridNode, {
        ...data,
        parentGridHighlighter,
      });
    }
  }

  /**
   * Hide the grid highlighter for the given grid container element.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the grid container element to unhighlight.
   */
  async hideGridHighlighter(node) {
    const { highlighter, parentGridNode } =
      this.gridHighlighters.get(node) || {};

    if (!highlighter) {
      return;
    }

    // Hide the subgrid's parent grid highlighter, if any.
    if (parentGridNode) {
      await this.hideParentGridHighlighter(parentGridNode);
    }

    this.#beforeHideHighlighterType(TYPES.GRID);
    // Don't just hide the highlighter, destroy the front instance to release memory.
    // If another highlighter is shown later, a new front will be created.
    highlighter.destroy();
    this.gridHighlighters.delete(node);
    this.state.grids.delete(node);

    // It's possible we just destroyed the grid highlighter for a node which also serves
    // as a subgrid's parent grid. If so, restore the parent grid highlighter.
    await this.restoreParentGridHighlighter(node);

    // Emit the NodeFront of the grid container element that the grid highlighter was
    // hidden for.
    this.emit("grid-highlighter-hidden", node);

    // XXX: Shim to use generic highlighter events until addressing Bug 1572652
    // Ensures badges in the Markup view reflect the state of the grid highlighter.
    this.emit("highlighter-hidden", {
      type: TYPES.GRID,
      nodeFront: node,
    });
  }

  /**
   * Hide the parent grid highlighter for the given parent grid container element.
   * If there are multiple subgrids with the same parent grid, do not hide the parent
   * grid highlighter.
   *
   * @param  {NodeFront} node
   *         The NodeFront of the parent grid container element to unhiglight.
   */
  async hideParentGridHighlighter(node) {
    let count = 0;
    let parentGridHighlighter;
    let subgridNode;
    for (const [key, value] of this.gridHighlighters.entries()) {
      if (value.parentGridNode === node) {
        parentGridHighlighter = value.parentGridHighlighter;
        subgridNode = key;
        count++;
      }
    }

    if (!parentGridHighlighter || count > 1) {
      return;
    }

    // Destroy the highlighter front instance to release memory.
    parentGridHighlighter.destroy();

    // Update the grid highlighter entry to indicate the parent grid highlighter is gone.
    this.gridHighlighters.set(subgridNode, {
      ...this.gridHighlighters.get(subgridNode),
      parentGridHighlighter: null,
    });
  }

  /**
   * Toggle the geometry editor highlighter for the given element.
   *
   * @param {NodeFront} node
   *        The NodeFront of the element to highlight.
   */
  async toggleGeometryHighlighter(node) {
    if (node == this.geometryEditorHighlighterShown) {
      await this.hideGeometryEditor();
      return;
    }

    await this.showGeometryEditor(node);
  }

  /**
   * Show the geometry editor highlightor for the given element.
   *
   * @param {NodeFront} node
   *        THe NodeFront of the element to highlight.
   */
  async showGeometryEditor(node) {
    const highlighter = await this.#getHighlighterTypeForNode(
      TYPES.GEOMETRY,
      node
    );
    if (!highlighter) {
      return;
    }

    const isShown = await highlighter.show(node);
    if (!isShown) {
      return;
    }

    this.emit("geometry-editor-highlighter-shown");
    this.geometryEditorHighlighterShown = node;
  }

  /**
   * Hide the geometry editor highlighter.
   */
  async hideGeometryEditor() {
    if (!this.geometryEditorHighlighterShown) {
      return;
    }

    const highlighter =
      this.geometryEditorHighlighterShown.inspectorFront.getKnownHighlighter(
        TYPES.GEOMETRY
      );

    if (!highlighter) {
      return;
    }

    await highlighter.hide();

    this.emit("geometry-editor-highlighter-hidden");
    this.geometryEditorHighlighterShown = null;
  }

  /**
   * Restores the saved flexbox highlighter state.
   */
  async restoreFlexboxState() {
    const state = this.#restorableHighlighters.get(TYPES.FLEXBOX);
    if (!state) {
      return;
    }

    this.#restorableHighlighters.delete(TYPES.FLEXBOX);
    await this.restoreState(TYPES.FLEXBOX, state, this.showFlexboxHighlighter);
  }

  /**
   * Restores the saved grid highlighter state.
   */
  async restoreGridState() {
    // The NodeFronts that are used as the keys in the grid state Map are no longer in the
    // tree after a reload. To clean up the grid state, we create a copy of the values of
    // the grid state before restoring and clear it.
    const values = [...this.state.grids.values()];
    this.state.grids.clear();

    try {
      for (const gridState of values) {
        await this.restoreState(
          TYPES.GRID,
          gridState,
          this.showGridHighlighter
        );
      }
    } catch (e) {
      this.#handleRejection(e);
    }
  }

  /**
   * Helper function called by restoreFlexboxState, restoreGridState.
   * Restores the saved highlighter state for the given highlighter
   * and their state.
   *
   * @param  {String} type
   *         Highlighter type to be restored.
   * @param  {Object} state
   *         Object containing the metadata used to restore the highlighter.
   *         {Array} state.selectors
   *         Array of CSS selector which identifies the node to be highlighted.
   *         If the node is in the top-level document, the array contains just one item.
   *         Otherwise, if the node is nested within a stack of iframes, each iframe is
   *         identified by its unique selector; the last item in the array identifies
   *         the target node within its host iframe document.
   *         {Object} state.options
   *         Configuration options to use when showing the highlighter.
   *         {String} state.url
   *         URL of the top-level target when the metadata was stored. Used to identify
   *         if there was a page refresh or a navigation away to a different page.
   * @param  {Function} showFunction
   *         The function that shows the highlighter
   * @return {Promise} that resolves when the highlighter was restored and shown.
   */
  async restoreState(type, state, showFunction) {
    const { selectors = [], options, url } = state;

    if (!selectors.length || url !== this.target.url) {
      // Bail out if no selector was saved, or if we are on a different page.
      this.emit(`highlighter-discarded`, { type });
      return;
    }

    const nodeFront =
      await this.inspector.commands.inspectorCommand.findNodeFrontFromSelectors(
        selectors
      );

    if (nodeFront) {
      await showFunction(nodeFront, options);
      this.emit(`highlighter-restored`, { type });
    } else {
      this.emit(`highlighter-discarded`, { type });
    }
  }

  /**
   * Get an instance of an in-context editor for the given type.
   *
   * In-context editors behave like highlighters but with added editing capabilities which
   * need to write value changes back to something, like to properties in the Rule view.
   * They typically exist in the context of the page, like the ShapesInContextEditor.
   *
   * @param  {NodeFront} node.
   * @param  {String} type
   *         Type of in-context editor. Currently supported: "shapesEditor"
   * @return {Object|null}
   *         Reference to instance for given type of in-context editor or null.
   */
  async getInContextEditor(node, type) {
    if (this.editors[type]) {
      return this.editors[type];
    }

    let editor;

    switch (type) {
      case "shapesEditor":
        const highlighter = await this.#getHighlighterTypeForNode(
          TYPES.SHAPES,
          node
        );
        if (!highlighter) {
          return null;
        }
        const ShapesInContextEditor = require("resource://devtools/client/shared/widgets/ShapesInContextEditor.js");

        editor = new ShapesInContextEditor(
          highlighter,
          this.inspector,
          this.state
        );
        editor.on("show", this.onShapesHighlighterShown);
        editor.on("hide", this.onShapesHighlighterHidden);
        break;
      default:
        throw new Error(`Unsupported in-context editor '${name}'`);
    }

    this.editors[type] = editor;

    return editor;
  }

  /**
   * Get a highlighter front given a type. It will only be initialized once.
   *
   * @param  {String} type
   *         The highlighter type. One of this.highlighters.
   * @return {Promise} that resolves to the highlighter
   */
  async #getHighlighter(type) {
    if (this.highlighters[type]) {
      return this.highlighters[type];
    }

    let highlighter;

    try {
      highlighter = await this.inspectorFront.getHighlighterByType(type);
    } catch (e) {
      this.#handleRejection(e);
    }

    if (!highlighter) {
      return null;
    }

    this.highlighters[type] = highlighter;
    return highlighter;
  }

  /**
   * Ignore unexpected errors from async function calls
   * if HighlightersOverlay has been destroyed.
   *
   * @param {Error} error
   */
  #handleRejection = error => {
    if (!this.destroyed) {
      console.error(error);
    }
  };

  /**
   * Toggle the class "active" on the given shape point in the rule view if the current
   * inspector selection is highlighted by the shapes highlighter.
   *
   * @param {NodeFront} node
   *        The NodeFront of the shape point to toggle
   * @param {Boolean} active
   *        Whether the shape point should be active
   */
  _toggleShapePointActive(node, active) {
    if (this.inspector.selection.nodeFront != this.shapesHighlighterShown) {
      return;
    }

    node.classList.toggle("active", active);
  }

  /**
   * Hide the currently shown hovered highlighter.
   */
  #hideHoveredHighlighter() {
    if (
      !this.hoveredHighlighterShown ||
      !this.highlighters[this.hoveredHighlighterShown]
    ) {
      return;
    }

    // For some reason, the call to highlighter.hide doesn't always return a
    // promise. This causes some tests to fail when trying to install a
    // rejection handler on the result of the call. To avoid this, check
    // whether the result is truthy before installing the handler.
    const onHidden = this.highlighters[this.hoveredHighlighterShown].hide();
    if (onHidden) {
      onHidden.catch(console.error);
    }

    this.hoveredHighlighterShown = null;
    this.emit("css-transform-highlighter-hidden");
  }

  /**
   * Given a node front and a function that hides the given node's highlighter, hides
   * the highlighter if the node front is no longer in the DOM tree. This is called
   * from the "markupmutation" event handler.
   *
   * @param  {NodeFront} node
   *         The NodeFront of a highlighted DOM node.
   * @param  {Function} hideHighlighter
   *         The function that will hide the highlighter of the highlighted node.
   */
  async #hideHighlighterIfDeadNode(node, hideHighlighter) {
    if (!node) {
      return;
    }

    try {
      const isInTree =
        node.walkerFront && (await node.walkerFront.isInDOMTree(node));
      if (!isInTree) {
        await hideHighlighter(node);
      }
    } catch (e) {
      this.#handleRejection(e);
    }
  }

  /**
   * Is the current hovered node a css transform property value in the
   * computed-view.
   *
   * @param  {Object} nodeInfo
   * @return {Boolean}
   */
  #isComputedViewTransform(nodeInfo) {
    if (nodeInfo.view != "computed") {
      return false;
    }
    return (
      nodeInfo.type === VIEW_NODE_VALUE_TYPE &&
      nodeInfo.value.property === "transform"
    );
  }

  /**
   * Does the current clicked node have the shapes highlighter toggle in the
   * rule-view.
   *
   * @param  {DOMNode} node
   * @return {Boolean}
   */
  #isRuleViewShapeSwatch(node) {
    return (
      this.isRuleView(node) && node.classList.contains("inspector-shapeswatch")
    );
  }

  /**
   * Is the current hovered node a css transform property value in the rule-view.
   *
   * @param  {Object} nodeInfo
   * @return {Boolean}
   */
  #isRuleViewTransform(nodeInfo) {
    if (nodeInfo.view != "rule") {
      return false;
    }
    const isTransform =
      nodeInfo.type === VIEW_NODE_VALUE_TYPE &&
      nodeInfo.value.property === "transform";
    const isEnabled =
      nodeInfo.value.enabled &&
      !nodeInfo.value.overridden &&
      !nodeInfo.value.pseudoElement;
    return isTransform && isEnabled;
  }

  /**
   * Is the current hovered node a highlightable shape point in the rule-view.
   *
   * @param  {Object} nodeInfo
   * @return {Boolean}
   */
  isRuleViewShapePoint(nodeInfo) {
    if (nodeInfo.view != "rule") {
      return false;
    }
    const isShape =
      nodeInfo.type === VIEW_NODE_SHAPE_POINT_TYPE &&
      (nodeInfo.value.property === "clip-path" ||
        nodeInfo.value.property === "shape-outside");
    const isEnabled =
      nodeInfo.value.enabled &&
      !nodeInfo.value.overridden &&
      !nodeInfo.value.pseudoElement;
    return (
      isShape &&
      isEnabled &&
      nodeInfo.value.toggleActive &&
      !this.state.shapes.options.transformMode
    );
  }

  onClick(event) {
    if (this.#isRuleViewShapeSwatch(event.target)) {
      event.stopPropagation();

      const view = this.inspector.getPanel("ruleview").view;
      const nodeInfo = view.getNodeInfo(event.target);

      this.toggleShapesHighlighter(
        this.inspector.selection.nodeFront,
        {
          mode: event.target.dataset.mode,
          transformMode: event.metaKey || event.ctrlKey,
        },
        nodeInfo.value.textProperty
      );
    }
  }

  /**
   * Handler for "display-change" events from walker fronts. Hides the flexbox or
   * grid highlighter if their respective node is no longer a flex container or
   * grid container.
   *
   * @param  {Array} nodes
   *         An array of nodeFronts
   */
  async onDisplayChange(nodes) {
    const highlightedGridNodes = this.getHighlightedGridNodes();

    for (const node of nodes) {
      const display = node.displayType;

      // Hide the flexbox highlighter if the node is no longer a flexbox container.
      if (
        display !== "flex" &&
        display !== "inline-flex" &&
        node == this.getNodeForActiveHighlighter(TYPES.FLEXBOX)
      ) {
        await this.hideFlexboxHighlighter(node);
        return;
      }

      // Hide the grid highlighter if the node is no longer a grid container.
      if (
        display !== "grid" &&
        display !== "inline-grid" &&
        display !== "subgrid" &&
        highlightedGridNodes.includes(node)
      ) {
        await this.hideGridHighlighter(node);
        return;
      }
    }
  }

  onMouseMove(event) {
    // Bail out if the target is the same as for the last mousemove.
    if (event.target === this.#lastHovered) {
      return;
    }

    // Only one highlighter can be displayed at a time, hide the currently shown.
    this.#hideHoveredHighlighter();

    this.#lastHovered = event.target;

    const view = this.isRuleView(this.#lastHovered)
      ? this.inspector.getPanel("ruleview").view
      : this.inspector.getPanel("computedview").computedView;
    const nodeInfo = view.getNodeInfo(event.target);
    if (!nodeInfo) {
      return;
    }

    if (this.isRuleViewShapePoint(nodeInfo)) {
      const { point } = nodeInfo.value;
      this.hoverPointShapesHighlighter(
        this.inspector.selection.nodeFront,
        point
      );
      return;
    }

    // Choose the type of highlighter required for the hovered node.
    let type;
    if (
      this.#isRuleViewTransform(nodeInfo) ||
      this.#isComputedViewTransform(nodeInfo)
    ) {
      type = TYPES.TRANSFORM;
    }

    if (type) {
      this.hoveredHighlighterShown = type;
      const node = this.inspector.selection.nodeFront;
      this.#getHighlighter(type).then(highlighter =>
        highlighter.show(node).then(shown => {
          if (shown) {
            this.emit("css-transform-highlighter-shown", highlighter);
          }
        })
      );
    }
  }

  onMouseOut(event) {
    // Only hide the highlighter if the mouse leaves the currently hovered node.
    if (
      !this.#lastHovered ||
      (event && this.#lastHovered.contains(event.relatedTarget))
    ) {
      return;
    }

    // Otherwise, hide the highlighter.
    const view = this.isRuleView(this.#lastHovered)
      ? this.inspector.getPanel("ruleview").view
      : this.inspector.getPanel("computedview").computedView;
    const nodeInfo = view.getNodeInfo(this.#lastHovered);
    if (nodeInfo && this.isRuleViewShapePoint(nodeInfo)) {
      this.hoverPointShapesHighlighter(
        this.inspector.selection.nodeFront,
        null
      );
    }
    this.#lastHovered = null;
    this.#hideHoveredHighlighter();
  }

  /**
   * Handler function called when a new root-node has been added in the
   * inspector. Nodes may have been added / removed and highlighters should
   * be updated.
   */
  #onResourceAvailable = async resources => {
    for (const resource of resources) {
      if (
        resource.resourceType !== this.resourceCommand.TYPES.ROOT_NODE ||
        // It might happen that the ROOT_NODE resource (which is a Front) is already
        // destroyed, and in such case we want to ignore it.
        resource.isDestroyed()
      ) {
        // Only handle root-node resources.
        // Note that we could replace this with DOCUMENT_EVENT resources, since
        // the actual root-node resource is not used here.
        continue;
      }

      if (resource.targetFront.isTopLevel && resource.isTopLevelDocument) {
        // The topmost root node will lead to the destruction and recreation of
        // the MarkupView, and highlighters will be refreshed afterwards. This is
        // handled by the inspector.
        continue;
      }

      await this.#hideOrphanedHighlighters();
    }
  };

  /**
   * Handler function for "markupmutation" events. Hides the flexbox/grid/shapes
   * highlighter if the flexbox/grid/shapes container is no longer in the DOM tree.
   */
  async onMarkupMutation(mutations) {
    const hasInterestingMutation = mutations.some(
      mut => mut.type === "childList"
    );
    if (!hasInterestingMutation) {
      // Bail out if the mutations did not remove nodes, or if no grid highlighter is
      // displayed.
      return;
    }

    await this.#hideOrphanedHighlighters();
  }

  /**
   * Hide every active highlighter whose nodeFront is no longer present in the DOM.
   * Returns a promise that resolves when all orphaned highlighters are hidden.
   *
   * @return {Promise}
   */
  async #hideOrphanedHighlighters() {
    await this.#hideHighlighterIfDeadNode(
      this.shapesHighlighterShown,
      this.hideShapesHighlighter
    );

    // Hide all active highlighters whose nodeFront is no longer attached.
    const promises = [];
    for (const [type, data] of this.#activeHighlighters) {
      promises.push(
        this.#hideHighlighterIfDeadNode(data.nodeFront, () => {
          return this.hideHighlighterType(type);
        })
      );
    }

    const highlightedGridNodes = this.getHighlightedGridNodes();
    for (const node of highlightedGridNodes) {
      promises.push(
        this.#hideHighlighterIfDeadNode(node, this.hideGridHighlighter)
      );
    }

    return Promise.all(promises);
  }

  /**
   * Hides any visible highlighter and clear internal state. This should be called to
   * have a clean slate, for example when the page navigates or when a given frame is
   * selected in the iframe picker.
   */
  async hideAllHighlighters() {
    this.destroyEditors();

    // Hide any visible highlighters and clear any timers set to autohide highlighters.
    for (const { highlighter, timer } of this.#activeHighlighters.values()) {
      await highlighter.hide();
      clearTimeout(timer);
    }

    this.#activeHighlighters.clear();
    this.#pendingHighlighters.clear();
    this.gridHighlighters.clear();

    this.geometryEditorHighlighterShown = null;
    this.hoveredHighlighterShown = null;
    this.shapesHighlighterShown = null;
  }

  /**
   * Display a message about the simple highlighters which can be enabled for
   * users relying on prefers-reduced-motion. This message will be a toolbox
   * notification, which will contain a button to open the settings panel and
   * will no longer be displayed if the user decides to explicitly close the
   * message.
   */
  #showSimpleHighlightersMessage() {
    const pref = "devtools.inspector.simple-highlighters.message-dismissed";
    const messageDismissed = Services.prefs.getBoolPref(pref, false);
    if (messageDismissed) {
      return;
    }
    const notificationBox = this.inspector.toolbox.getNotificationBox();
    const message = HighlightersBundle.formatValueSync(
      "simple-highlighters-message"
    );

    notificationBox.appendNotification(
      message,
      "simple-highlighters-message",
      null,
      notificationBox.PRIORITY_INFO_MEDIUM,
      [
        {
          label: HighlightersBundle.formatValueSync(
            "simple-highlighters-settings-button"
          ),
          callback: async () => {
            const { panelDoc } = await this.toolbox.selectTool("options");
            const option = panelDoc.querySelector(
              "[data-pref='devtools.inspector.simple-highlighters-reduced-motion']"
            ).parentNode;
            option.scrollIntoView({ block: "center" });
            option.classList.add("options-panel-highlight");

            // Emit a test-only event to know when the settings panel is opened.
            this.toolbox.emitForTests("test-highlighters-settings-opened");
          },
        },
      ],
      evt => {
        if (evt === "removed") {
          // Flip the preference when the message is dismissed.
          Services.prefs.setBoolPref(pref, true);
        }
      }
    );
  }

  /**
   * Destroy and clean-up all instances of in-context editors.
   */
  destroyEditors() {
    for (const type in this.editors) {
      this.editors[type].off("show");
      this.editors[type].off("hide");
      this.editors[type].destroy();
    }

    this.editors = {};
  }

  /**
   * Destroy and clean-up all instances of highlighters.
   */
  destroyHighlighters() {
    // Destroy all highlighters and clear any timers set to autohide highlighters.
    const values = [
      ...this.#activeHighlighters.values(),
      ...this.gridHighlighters.values(),
    ];
    for (const { highlighter, parentGridHighlighter, timer } of values) {
      if (highlighter) {
        highlighter.destroy();
      }

      if (parentGridHighlighter) {
        parentGridHighlighter.destroy();
      }

      if (timer) {
        clearTimeout(timer);
      }
    }

    this.#activeHighlighters.clear();
    this.#pendingHighlighters.clear();
    this.gridHighlighters.clear();

    for (const type in this.highlighters) {
      if (this.highlighters[type]) {
        this.highlighters[type].finalize();
        this.highlighters[type] = null;
      }
    }
  }

  /**
   * Destroy this overlay instance, removing it from the view and destroying
   * all initialized highlighters.
   */
  destroy() {
    this.inspector.off("markupmutation", this.onMarkupMutation);
    this.resourceCommand.unwatchResources(
      [this.resourceCommand.TYPES.ROOT_NODE],
      { onAvailable: this.#onResourceAvailable }
    );

    this.walkerEventListener.destroy();
    this.walkerEventListener = null;

    this.destroyEditors();
    this.destroyHighlighters();

    this.#lastHovered = null;

    this.inspector = null;
    this.state = null;
    this.store = null;
    this.telemetry = null;

    this.geometryEditorHighlighterShown = null;
    this.hoveredHighlighterShown = null;
    this.shapesHighlighterShown = null;

    this.destroyed = true;
  }
}

HighlightersOverlay.TYPES = HighlightersOverlay.prototype.TYPES = TYPES;

module.exports = HighlightersOverlay;

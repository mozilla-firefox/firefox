/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const flags = require("resource://devtools/shared/flags.js");
const { executeSoon } = require("resource://devtools/shared/DevToolsUtils.js");
const { Toolbox } = require("resource://devtools/client/framework/toolbox.js");
const createStore = require("resource://devtools/client/inspector/store.js");
const InspectorStyleChangeTracker = require("resource://devtools/client/inspector/shared/style-change-tracker.js");
const { PrefObserver } = require("resource://devtools/client/shared/prefs.js");
const {
  START_IGNORE_ACTION,
} = require("resource://devtools/client/shared/redux/middleware/ignore.js");

// Use privileged promise in panel documents to prevent having them to freeze
// during toolbox destruction. See bug 1402779.
const Promise = require("Promise");
const osString = Services.appinfo.OS;

loader.lazyRequireGetter(
  this,
  "HTMLBreadcrumbs",
  "resource://devtools/client/inspector/breadcrumbs.js",
  true
);
loader.lazyRequireGetter(
  this,
  "KeyShortcuts",
  "resource://devtools/client/shared/key-shortcuts.js"
);
loader.lazyRequireGetter(
  this,
  "InspectorSearch",
  "resource://devtools/client/inspector/inspector-search.js",
  true
);
loader.lazyRequireGetter(
  this,
  "ToolSidebar",
  "resource://devtools/client/inspector/toolsidebar.js",
  true
);
loader.lazyRequireGetter(
  this,
  "MarkupView",
  "resource://devtools/client/inspector/markup/markup.js"
);
loader.lazyRequireGetter(
  this,
  "HighlightersOverlay",
  "resource://devtools/client/inspector/shared/highlighters-overlay.js"
);
loader.lazyRequireGetter(
  this,
  "PICKER_TYPES",
  "resource://devtools/shared/picker-constants.js"
);
loader.lazyRequireGetter(
  this,
  "captureAndSaveScreenshot",
  "resource://devtools/client/shared/screenshot.js",
  true
);
loader.lazyRequireGetter(
  this,
  "debounce",
  "resource://devtools/shared/debounce.js",
  true
);

const {
  LocalizationHelper,
  localizeMarkup,
} = require("resource://devtools/shared/l10n.js");
const INSPECTOR_L10N = new LocalizationHelper(
  "devtools/client/locales/inspector.properties"
);
const {
  FluentL10n,
} = require("resource://devtools/client/shared/fluent-l10n/fluent-l10n.js");

// Sidebar dimensions
const INITIAL_SIDEBAR_SIZE = 350;

// How long we wait to debounce resize events
const LAZY_RESIZE_INTERVAL_MS = 200;

// If the toolbox's width is smaller than the given amount of pixels, the sidebar
// automatically switches from 'landscape/horizontal' to 'portrait/vertical' mode.
const PORTRAIT_MODE_WIDTH_THRESHOLD = 700;
// If the toolbox's width docked to the side is smaller than the given amount of pixels,
// the sidebar automatically switches from 'landscape/horizontal' to 'portrait/vertical'
// mode.
const SIDE_PORTAIT_MODE_WIDTH_THRESHOLD = 1000;

const THREE_PANE_ENABLED_PREF = "devtools.inspector.three-pane-enabled";
const THREE_PANE_CHROME_ENABLED_PREF =
  "devtools.inspector.chrome.three-pane-enabled";
const DEFAULT_COLOR_UNIT_PREF = "devtools.defaultColorUnit";

/**
 * Represents an open instance of the Inspector for a tab.
 * The inspector controls the breadcrumbs, the markup view, and the sidebar
 * (computed view, rule view, font view and animation inspector).
 *
 * Events:
 * - ready
 *      Fired when the inspector panel is opened for the first time and ready to
 *      use
 * - new-root
 *      Fired after a new root (navigation to a new page) event was fired by
 *      the walker, and taken into account by the inspector (after the markup
 *      view has been reloaded)
 * - markuploaded
 *      Fired when the markup-view frame has loaded
 * - breadcrumbs-updated
 *      Fired when the breadcrumb widget updates to a new node
 * - boxmodel-view-updated
 *      Fired when the box model updates to a new node
 * - markupmutation
 *      Fired after markup mutations have been processed by the markup-view
 * - computed-view-refreshed
 *      Fired when the computed rules view updates to a new node
 * - computed-view-property-expanded
 *      Fired when a property is expanded in the computed rules view
 * - computed-view-property-collapsed
 *      Fired when a property is collapsed in the computed rules view
 * - computed-view-sourcelinks-updated
 *      Fired when the stylesheet source links have been updated (when switching
 *      to source-mapped files)
 * - rule-view-refreshed
 *      Fired when the rule view updates to a new node
 * - rule-view-sourcelinks-updated
 *      Fired when the stylesheet source links have been updated (when switching
 *      to source-mapped files)
 */
class Inspector extends EventEmitter {
  constructor(toolbox, commands, win) {
    super();

    this.#toolbox = toolbox;
    this.#commands = commands;
    this.panelDoc = win.document;
    this.panelWin = win;
    this.panelWin.inspector = this;
    this.telemetry = toolbox.telemetry;
    this.store = createStore(this);

    this.onResourceAvailable = this.onResourceAvailable.bind(this);
    this.onRootNodeAvailable = this.onRootNodeAvailable.bind(this);
    this.onPickerCanceled = this.onPickerCanceled.bind(this);
    this.onPickerHovered = this.onPickerHovered.bind(this);
    this.onPickerPicked = this.onPickerPicked.bind(this);
    this.onSidebarHidden = this.onSidebarHidden.bind(this);
    this.onSidebarResized = this.onSidebarResized.bind(this);
    this.onSidebarSelect = this.onSidebarSelect.bind(this);
    this.onSidebarShown = this.onSidebarShown.bind(this);
    this.onSidebarToggle = this.onSidebarToggle.bind(this);
    this.addNode = this.addNode.bind(this);
    this.onEyeDropperDone = this.onEyeDropperDone.bind(this);
    this.onEyeDropperButtonClicked = this.onEyeDropperButtonClicked.bind(this);

    this.prefObserver = new PrefObserver("devtools.");
    this.prefObserver.on(
      DEFAULT_COLOR_UNIT_PREF,
      this.#handleDefaultColorUnitPrefChange
    );
    this.defaultColorUnit = Services.prefs.getStringPref(
      DEFAULT_COLOR_UNIT_PREF
    );
  }

  #toolbox;
  #commands;
  // Map [panel id => panel instance]
  // Stores all the instances of sidebar panels like rule view, computed view, ...
  #panels = new Map();
  #fluentL10n;
  #defaultStartupNode;
  #defaultStartupNodeDomReference;
  #defaultStartupNodeSelectionReason;
  #defaultNode;
  #watchedResources;
  #highlighters;
  #newRootStart;
  #markupFrame;
  #markupBox;
  #isThreePaneModeEnabled;
  #search;
  #cssProperties;
  #destroyed;
  #pendingSelectionUnique;
  #InspectorTabPanel;
  #InspectorSplitBox;
  #TabBar;
  #updateProgress;

  /**
   * InspectorPanel.open() is effectively an asynchronous constructor.
   * Set any attributes or listeners that rely on the document being loaded or fronts
   * from the InspectorFront and Target here.
   *
   * @param {object} options
   * @param {NodeFront|undefined} options.defaultStartupNode: Optional node front that
   *        will be selected when the first root node is available.
   * @param {ElementIdentifier|undefined} options.defaultStartupNodeDomReference: Optional
   *        element identifier whose matching node front will be selected when the first
   *        root node is available.
   *        Will be ignored if defaultStartupNode is passed.
   * @param {string | undefined} options.defaultStartupNodeSelectionReason: Optional string
   *        that will be used as a reason for the node selection when either
   *        defaultStartupNode or defaultStartupNodeDomReference is passed
   * @returns {Inspector}
   */
  async init(options = {}) {
    // Localize all the nodes containing a data-localization attribute.
    localizeMarkup(this.panelDoc);

    this.#fluentL10n = new FluentL10n();
    await this.#fluentL10n.init(["devtools/client/compatibility.ftl"]);

    // Add the class that will display the main inspector panel with: search input,
    // markup view and breadcrumbs.
    this.panelDoc
      .getElementById("inspector-main-content")
      .classList.add("initialized");

    // Setup the splitter before watching targets & resources.
    // The markup view will be initialized after we get the first root-node
    // resource, and the splitter should be initialized before that.
    // The markup view is rendered in an iframe and the splitter will move the
    // parent of the iframe in the DOM tree which would reset the state of the
    // iframe if it had already been initialized.
    this.#setupSplitter();

    // Optional NodeFront/ElementIdentifier set on inspector startup, to be selected once the first root
    // node is available.
    this.#defaultStartupNode = options.defaultStartupNode;
    this.#defaultStartupNodeDomReference =
      options.defaultStartupNodeDomReference;
    this.#defaultStartupNodeSelectionReason =
      options.defaultStartupNodeSelectionReason;

    // NodeFront for the DOM Element selected when opening the inspector, or after each
    // navigation (i.e. each time a new Root Node is available)
    // This is used as a fallback if the currently selected node is removed.
    this.#defaultNode = null;

    await this.commands.targetCommand.watchTargets({
      types: [this.commands.targetCommand.TYPES.FRAME],
      onAvailable: this.#onTargetAvailable,
      onSelected: this.#onTargetSelected,
      onDestroyed: this.#onTargetDestroyed,
    });

    const { TYPES } = this.commands.resourceCommand;
    this.#watchedResources = [
      // To observe CSS change before opening changes view.
      TYPES.CSS_CHANGE,
      TYPES.DOCUMENT_EVENT,
      TYPES.REFLOW,
    ];
    // The root node is retrieved from onTargetSelected which is now called
    // on startup as well as on any navigation (= new top level target).
    //
    // We only listen to new root node in the browser toolbox, which is the last
    // configuration to use one target for multiple window global.
    const isBrowserToolbox =
      this.commands.descriptorFront.isBrowserProcessDescriptor;
    if (isBrowserToolbox) {
      this.#watchedResources.push(TYPES.ROOT_NODE);
    }

    await this.commands.resourceCommand.watchResources(this.#watchedResources, {
      onAvailable: this.onResourceAvailable,
    });

    // Store the URL of the target page prior to navigation in order to ensure
    // telemetry counts in the Grid Inspector are not double counted on reload.
    this.previousURL = this.currentTarget.url;

    // Note: setupSidebar() really has to be called after the first target has
    // been processed, so that the cssProperties getter works.
    // But the rest could be moved before the watch* calls.
    this.styleChangeTracker = new InspectorStyleChangeTracker(this);
    this.#setupSidebar();
    this.breadcrumbs = new HTMLBreadcrumbs(this);
    this.#setupExtensionSidebars();
    this.#setupSearchBox();
    this.#createInspectorShortcuts();

    this.#onNewSelection();

    this.toolbox.on("host-changed", this.#onHostChanged);
    this.toolbox.nodePicker.on("picker-node-hovered", this.onPickerHovered);
    this.toolbox.nodePicker.on("picker-node-canceled", this.onPickerCanceled);
    this.toolbox.nodePicker.on("picker-node-picked", this.onPickerPicked);
    this.selection.on("new-node-front", this.#onNewSelection);
    this.selection.on("detached-front", this.#onDetached);

    // Log the 3 pane inspector setting on inspector open. The question we want to answer
    // is:
    // "What proportion of users use the 3 pane vs 2 pane inspector on inspector open?"
    Glean.devtoolsInspector.threePaneEnabled[this.isThreePaneModeEnabled].add(
      1
    );

    return this;
  }

  // The onTargetAvailable argument is mandatory for TargetCommand.watchTargets.
  // The inspector ignore all targets but the currently selected one,
  // so all the target work is done from onTargetSelected.
  #onTargetAvailable = async ({ targetFront }) => {
    if (!targetFront.isTopLevel) {
      return;
    }

    // Fetch data and fronts which aren't WindowGlobal specific
    // and can be fetched once from the top level target.
    await Promise.all([
      this.#getCssProperties(targetFront),
      this.#getAccessibilityFront(targetFront),
    ]);
  };

  #onTargetSelected = async ({ targetFront }) => {
    // We don't use this.highlighters since it creates a HighlightersOverlay if it wasn't
    // the case yet.
    if (this.#highlighters) {
      this.#highlighters.hideAllHighlighters();
    }
    if (targetFront.isDestroyed()) {
      return;
    }

    await this.#initInspectorFront(targetFront);

    // the target might have been destroyed when reloading quickly,
    // while waiting for inspector front initialization
    if (targetFront.isDestroyed()) {
      return;
    }

    const { walker } = await targetFront.getFront("inspector");
    const rootNodeFront = await walker.getRootNode();

    // onRootNodeAvailable will take care of populating the markup view
    await this.onRootNodeAvailable(rootNodeFront);
  };

  #onTargetDestroyed = ({ targetFront }) => {
    // Ignore all targets but the top level one
    if (!targetFront.isTopLevel) {
      return;
    }

    this.#defaultNode = null;
    this.selection.setNodeFront(null);
  };

  onResourceAvailable(resources) {
    // Store all onRootNodeAvailable calls which are asynchronous.
    const rootNodeAvailablePromises = [];

    for (const resource of resources) {
      const isTopLevelTarget = !!resource.targetFront?.isTopLevel;
      const isTopLevelDocument = !!resource.isTopLevelDocument;

      if (
        resource.resourceType ===
          this.commands.resourceCommand.TYPES.ROOT_NODE &&
        // It might happen that the ROOT_NODE resource (which is a Front) is already
        // destroyed, and in such case we want to ignore it.
        !resource.isDestroyed() &&
        isTopLevelTarget &&
        isTopLevelDocument
      ) {
        rootNodeAvailablePromises.push(this.onRootNodeAvailable(resource));
      }

      // Only consider top level document, and ignore remote iframes top document
      if (
        resource.resourceType ===
          this.commands.resourceCommand.TYPES.DOCUMENT_EVENT &&
        resource.name === "will-navigate" &&
        isTopLevelTarget
      ) {
        this.#onWillNavigate();
      }

      if (
        resource.resourceType === this.commands.resourceCommand.TYPES.REFLOW
      ) {
        this.emit("reflow");
        if (resource.targetFront === this.selection?.nodeFront?.targetFront) {
          // This event will be fired whenever a reflow is detected in the target front of the
          // selected node front (so when a reflow is detected inside any of the windows that
          // belong to the BrowsingContext where the currently selected node lives).
          this.emit("reflow-in-selected-target");
        }
      }
    }

    return Promise.all(rootNodeAvailablePromises);
  }

  /**
   * Reset the inspector on new root mutation.
   */
  async onRootNodeAvailable(rootNodeFront) {
    // Record new-root timing for telemetry
    this.#newRootStart = this.panelWin.performance.now();

    this.selection.setNodeFront(null);
    this.#destroyMarkup();

    try {
      const defaultNode = await this.#getDefaultNodeForSelection(rootNodeFront);
      if (!defaultNode) {
        return;
      }

      this.selection.setNodeFront(defaultNode, {
        reason:
          this.#defaultStartupNodeSelectionReason ??
          "inspector-default-selection",
      });
      this.#defaultStartupNodeSelectionReason = null;

      await this.#initMarkupView();

      // Setup the toolbar again, since its content may depend on the current document.
      this.#setupToolbar();
    } catch (e) {
      this.#handleRejectionIfNotDestroyed(e);
    }
  }

  async #initMarkupView() {
    if (!this.#markupFrame) {
      this.#markupFrame = this.panelDoc.createElement("iframe");
      this.#markupFrame.setAttribute(
        "aria-label",
        INSPECTOR_L10N.getStr("inspector.panelLabel.markupView")
      );
      this.#markupFrame.setAttribute("flex", "1");
      // This is needed to enable tooltips inside the iframe document.
      this.#markupFrame.setAttribute("tooltip", "aHTMLTooltip");

      this.#markupBox = this.panelDoc.getElementById("markup-box");
      this.#markupBox.style.visibility = "hidden";
      this.#markupBox.appendChild(this.#markupFrame);

      const onMarkupFrameLoaded = new Promise(r =>
        this.#markupFrame.addEventListener("load", r, {
          capture: true,
          once: true,
        })
      );

      this.#markupFrame.setAttribute("src", "markup/markup.xhtml");

      await onMarkupFrameLoaded;
    }

    this.#markupFrame.contentWindow.focus();
    this.#markupBox.style.visibility = "visible";
    this.markup = new MarkupView(this, this.#markupFrame, this.#toolbox.win);
    // TODO: We might be able to merge markuploaded, new-root and reloaded.
    this.emitForTests("markuploaded");

    const onExpand = this.markup.expandNode(this.selection.nodeFront);

    // Restore the highlighter states prior to emitting "new-root".
    if (this.#highlighters) {
      await Promise.all([
        this.highlighters.restoreFlexboxState(),
        this.highlighters.restoreGridState(),
      ]);
    }
    this.emit("new-root");

    // Wait for full expand of the selected node in order to ensure
    // the markup view is fully emitted before firing 'reloaded'.
    // 'reloaded' is used to know when the panel is fully updated
    // after a page reload.
    await onExpand;

    this.emit("reloaded");

    // Record the time between new-root event and inspector fully loaded.
    if (this.#newRootStart) {
      // Only log the timing when inspector is not destroyed and is in foreground.
      if (this.toolbox && this.toolbox.currentToolId == "inspector") {
        const delay = this.panelWin.performance.now() - this.#newRootStart;
        Glean.devtoolsInspector.newRootToReloadDelay.accumulateSingleSample(
          delay
        );
      }
      this.#newRootStart = null;
    }
  }

  async #initInspectorFront(targetFront) {
    this.inspectorFront = await targetFront.getFront("inspector");
    this.walker = this.inspectorFront.walker;
  }

  get toolbox() {
    return this.#toolbox;
  }

  get commands() {
    return this.#commands;
  }

  /**
   * Get the list of InspectorFront instances that correspond to all of the inspectable
   * targets in remote frames nested within the document inspected here, as well as the
   * current InspectorFront instance.
   *
   * @return {Array} The list of InspectorFront instances.
   */
  async getAllInspectorFronts() {
    return this.commands.targetCommand.getAllFronts(
      [this.commands.targetCommand.TYPES.FRAME],
      "inspector"
    );
  }

  get highlighters() {
    if (!this.#highlighters) {
      this.#highlighters = new HighlightersOverlay(this);
    }

    return this.#highlighters;
  }

  get #threePanePrefName() {
    // All other contexts: webextension and browser toolbox
    // are considered as "chrome"
    return this.commands.descriptorFront.isTabDescriptor
      ? THREE_PANE_ENABLED_PREF
      : THREE_PANE_CHROME_ENABLED_PREF;
  }

  get isThreePaneModeEnabled() {
    if (!this.#isThreePaneModeEnabled) {
      this.#isThreePaneModeEnabled = Services.prefs.getBoolPref(
        this.#threePanePrefName
      );
    }
    return this.#isThreePaneModeEnabled;
  }

  set isThreePaneModeEnabled(value) {
    this.#isThreePaneModeEnabled = value;
    Services.prefs.setBoolPref(
      this.#threePanePrefName,
      this.#isThreePaneModeEnabled
    );
  }

  get search() {
    if (!this.#search) {
      this.#search = new InspectorSearch(
        this,
        this.searchBox,
        this.searchClearButton,
        this.searchPrevButton,
        this.searchNextButton
      );
    }

    return this.#search;
  }

  get selection() {
    return this.toolbox.selection;
  }

  get cssProperties() {
    return this.#cssProperties.cssProperties;
  }

  get fluentL10n() {
    return this.#fluentL10n;
  }

  // Duration in milliseconds after which to hide the highlighter for the picked node.
  // While testing, disable auto hiding to prevent intermittent test failures.
  // Some tests are very slow. If the highlighter is hidden after a delay, the test may
  // find itself midway through without a highlighter to test.
  // This value is exposed on Inspector so individual tests can restore it when needed.
  HIGHLIGHTER_AUTOHIDE_TIMER = flags.testing ? 0 : 1000;

  #handleDefaultColorUnitPrefChange = () => {
    this.defaultColorUnit = Services.prefs.getStringPref(
      DEFAULT_COLOR_UNIT_PREF
    );
  };

  /**
   * Handle promise rejections for various asynchronous actions, and only log errors if
   * the inspector panel still exists.
   * This is useful to silence useless errors that happen when the inspector is closed
   * while still initializing (and making protocol requests).
   */
  #handleRejectionIfNotDestroyed = e => {
    if (!this.#destroyed) {
      console.error(e);
    }
  };

  #onWillNavigate = () => {
    this.#defaultNode = null;
    this.selection.setNodeFront(null);
    if (this.#highlighters) {
      this.#highlighters.hideAllHighlighters();
    }
    this.#destroyMarkup();
    this.#pendingSelectionUnique = null;
  };

  async #getCssProperties(targetFront) {
    this.#cssProperties = await targetFront.getFront("cssProperties");
  }

  async #getAccessibilityFront(targetFront) {
    this.accessibilityFront = await targetFront.getFront("accessibility");
    return this.accessibilityFront;
  }

  /**
   * Return a promise that will resolve to the default node for selection.
   *
   * @param {NodeFront} rootNodeFront
   *        The current root node front for the top walker.
   */
  async #getDefaultNodeForSelection(rootNodeFront) {
    let node;
    if (this.#defaultStartupNode) {
      node = this.#defaultStartupNode;
      this.#defaultStartupNode = null;
      this.#defaultStartupNodeDomReference = null;
      return node;
    }

    // Save the _pendingSelectionUnique on the current inspector instance.
    const pendingSelectionUnique = Symbol("pending-selection");
    this.#pendingSelectionUnique = pendingSelectionUnique;

    if (this.#defaultStartupNodeDomReference) {
      const domReference = this.#defaultStartupNodeDomReference;
      // nullify before calling the async getNodeActorFromContentDomReference so calls
      // made to getDefaultNodeForSelection while the promise is pending will be properly
      // ignored with the check on pendingSelectionUnique
      this.#defaultStartupNode = null;
      this.#defaultStartupNodeDomReference = null;

      try {
        node =
          await this.inspectorFront.getNodeActorFromContentDomReference(
            domReference
          );
      } catch (e) {
        console.warn(
          "Couldn't retrieve node front from dom reference",
          domReference
        );
      }
    }

    if (this.#pendingSelectionUnique !== pendingSelectionUnique) {
      // If this method was called again while waiting, bail out.
      return null;
    }

    if (node) {
      return node;
    }

    const walker = rootNodeFront.walkerFront;
    const cssSelectors = this.selectionCssSelectors;
    // Try to find a default node using three strategies:
    const defaultNodeSelectors = [
      // - first try to match css selectors for the selection
      () =>
        cssSelectors.length
          ? this.commands.inspectorCommand.findNodeFrontFromSelectors(
              cssSelectors
            )
          : null,
      // - otherwise try to get the "body" element
      () => walker.querySelector(rootNodeFront, "body"),
      // - finally get the documentElement element if nothing else worked.
      () => walker.documentElement(),
    ];

    // Try all default node selectors until a valid node is found.
    for (const selector of defaultNodeSelectors) {
      node = await selector();
      if (this.#pendingSelectionUnique !== pendingSelectionUnique) {
        // If this method was called again while waiting, bail out.
        return null;
      }

      if (node) {
        this.#defaultNode = node;
        return node;
      }
    }

    return null;
  }

  /**
   * Top level target front getter.
   */
  get currentTarget() {
    return this.commands.targetCommand.selectedTargetFront;
  }

  /**
   * Hooks the searchbar to show result and auto completion suggestions.
   */
  #setupSearchBox() {
    this.searchBox = this.panelDoc.getElementById("inspector-searchbox");
    this.searchClearButton = this.panelDoc.getElementById(
      "inspector-searchinput-clear"
    );
    this.searchResultsContainer = this.panelDoc.getElementById(
      "inspector-searchlabel-container"
    );
    this.searchNavigationContainer = this.panelDoc.getElementById(
      "inspector-searchnavigation-container"
    );
    this.searchPrevButton = this.panelDoc.getElementById(
      "inspector-searchnavigation-button-prev"
    );
    this.searchNextButton = this.panelDoc.getElementById(
      "inspector-searchnavigation-button-next"
    );
    this.searchResultsLabel = this.panelDoc.getElementById(
      "inspector-searchlabel"
    );

    this.searchResultsLabel.addEventListener("click", this.#onSearchLabelClick);

    this.searchBox.addEventListener("focus", this.#listenForSearchEvents, {
      once: true,
    });
  }

  #onSearchLabelClick = () => {
    // Focus on the search box as the search label
    // appears to be "inside" input
    this.searchBox.focus();
  };

  #listenForSearchEvents = () => {
    this.search.on("search-cleared", this.#clearSearchResultsLabel);
    this.search.on("search-result", this.#updateSearchResultsLabel);
  };

  #isFromInspectorWindow = event => {
    const win = event.originalTarget.documentGlobal;
    return win === this.panelWin || win.parent === this.panelWin;
  };

  #createInspectorShortcuts = () => {
    this.inspectorShortcuts = new KeyShortcuts({
      window: this.panelDoc.defaultView,
      // The inspector search shortcuts need to be available from everywhere in the
      // inspector, and the inspector uses iframes (markupview, sidepanel webextensions).
      // Use the chromeEventHandler as the target to catch events from all frames.
      target: this.toolbox.getChromeEventHandler(),
    });

    const searchboxKey = INSPECTOR_L10N.getStr("inspector.searchHTML.key");
    this.inspectorShortcuts.on(searchboxKey, event => {
      // Prevent overriding same shortcut from the computed/rule views
      if (
        event.originalTarget.closest("#sidebar-panel-ruleview") ||
        event.originalTarget.closest("#sidebar-panel-computedview") ||
        !this.#isFromInspectorWindow(event)
      ) {
        return;
      }
      event.preventDefault();
      this.searchBox.focus();
    });
    const eyedropperKey = INSPECTOR_L10N.getStr("inspector.eyedropper.key");
    this.inspectorShortcuts.on(eyedropperKey, event => {
      if (!this.#isFromInspectorWindow(event)) {
        return;
      }
      event.preventDefault();
      this.onEyeDropperButtonClicked();
    });
  };

  get searchSuggestions() {
    return this.search.autocompleter;
  }

  #clearSearchResultsLabel = result => {
    // Pipe the search-cleared event as this.search is a getter that will create
    // the InspectorSearch instance, which we don't really need/want when a callsite
    // only want to react to the search being cleared.
    this.emit("search-cleared");
    return this.#updateSearchResultsLabel(result, true);
  };

  #updateSearchResultsLabel = (result, clear = false) => {
    let str = "";
    if (!clear) {
      if (result) {
        str = INSPECTOR_L10N.getFormatStr(
          "inspector.searchResultsCount2",
          result.resultsIndex + 1,
          result.resultsLength
        );
        this.searchNavigationContainer.hidden = false;
      } else {
        str = INSPECTOR_L10N.getStr("inspector.searchResultsNone");
        this.searchNavigationContainer.hidden = true;
      }

      this.searchResultsContainer.hidden = false;
    } else {
      this.searchResultsContainer.hidden = true;
    }

    this.searchResultsLabel.textContent = str;
  };

  get React() {
    return this.#toolbox.React;
  }

  get ReactDOM() {
    return this.#toolbox.ReactDOM;
  }

  get ReactRedux() {
    return this.#toolbox.ReactRedux;
  }

  get browserRequire() {
    return this.#toolbox.browserRequire;
  }

  get InspectorTabPanel() {
    if (!this.#InspectorTabPanel) {
      this.#InspectorTabPanel = this.React.createFactory(
        this.browserRequire(
          "devtools/client/inspector/components/InspectorTabPanel"
        )
      );
    }
    return this.#InspectorTabPanel;
  }

  get InspectorSplitBox() {
    if (!this.#InspectorSplitBox) {
      this.#InspectorSplitBox = this.React.createFactory(
        this.browserRequire(
          "devtools/client/shared/components/splitter/SplitBox"
        )
      );
    }
    return this.#InspectorSplitBox;
  }

  get TabBar() {
    if (!this.#TabBar) {
      this.#TabBar = this.React.createFactory(
        this.browserRequire("devtools/client/shared/components/tabs/TabBar")
      );
    }
    return this.#TabBar;
  }

  /**
   * Check if the inspector should use the landscape mode.
   *
   * @return {boolean} true if the inspector should be in landscape mode.
   */
  #useLandscapeMode() {
    if (!this.panelDoc) {
      return true;
    }

    const splitterBox = this.panelDoc.getElementById("inspector-splitter-box");
    const width = splitterBox.clientWidth;

    return this.isThreePaneModeEnabled &&
      (this.toolbox.hostType == Toolbox.HostType.LEFT ||
        this.toolbox.hostType == Toolbox.HostType.RIGHT)
      ? width > SIDE_PORTAIT_MODE_WIDTH_THRESHOLD
      : width > PORTRAIT_MODE_WIDTH_THRESHOLD;
  }

  /**
   * Build Splitter located between the main and side area of
   * the Inspector panel.
   */
  #setupSplitter() {
    const { width, height, splitSidebarWidth } = this.getSidebarSize();

    this.sidebarSplitBoxRef = this.React.createRef();

    const splitter = this.InspectorSplitBox({
      className: "inspector-sidebar-splitter",
      initialWidth: width,
      initialHeight: height,
      minSize: "10%",
      maxSize: "80%",
      splitterSize: 1,
      endPanelControl: true,
      startPanel: this.InspectorTabPanel({
        id: "inspector-main-content",
      }),
      endPanel: this.InspectorSplitBox({
        initialWidth: splitSidebarWidth,
        minSize: "225px",
        maxSize: "80%",
        splitterSize: this.isThreePaneModeEnabled ? 1 : 0,
        endPanelControl: this.isThreePaneModeEnabled,
        startPanel: this.InspectorTabPanel({
          id: "inspector-rules-container",
        }),
        endPanel: this.InspectorTabPanel({
          id: "inspector-sidebar-container",
        }),
        ref: this.sidebarSplitBoxRef,
      }),
      vert: this.#useLandscapeMode(),
      onControlledPanelResized: this.onSidebarResized,
    });

    this.splitBox = this.ReactDOM.render(
      splitter,
      this.panelDoc.getElementById("inspector-splitter-box")
    );

    this.panelWin.addEventListener("resize", this.#onLazyPanelResize, true);
  }

  #onLazyPanelResize = debounce(
    () => {
      // We can be called on a closed window or destroyed toolbox because of the deferred task.
      if (
        this.panelWin?.closed ||
        this.#destroyed ||
        this.#toolbox.currentToolId !== "inspector"
      ) {
        return;
      }

      this.splitBox.setState({ vert: this.#useLandscapeMode() });
      this.emit("inspector-resize");
    },
    LAZY_RESIZE_INTERVAL_MS,
    this
  );

  getSidebarSize() {
    let width;
    let height;
    let splitSidebarWidth;

    // Initialize splitter size from preferences.
    try {
      width = Services.prefs.getIntPref("devtools.toolsidebar-width.inspector");
      height = Services.prefs.getIntPref(
        "devtools.toolsidebar-height.inspector"
      );
      splitSidebarWidth = Services.prefs.getIntPref(
        "devtools.toolsidebar-width.inspector.splitsidebar"
      );
    } catch (e) {
      // Set width and height of the splitter. Only one
      // value is really useful at a time depending on the current
      // orientation (vertical/horizontal).
      // Having both is supported by the splitter component.
      width = this.isThreePaneModeEnabled
        ? INITIAL_SIDEBAR_SIZE * 2
        : INITIAL_SIDEBAR_SIZE;
      height = INITIAL_SIDEBAR_SIZE;
      splitSidebarWidth = INITIAL_SIDEBAR_SIZE;
    }

    return { width, height, splitSidebarWidth };
  }

  onSidebarHidden() {
    // Store the current splitter size to preferences.
    const state = this.splitBox.state;
    Services.prefs.setIntPref(
      "devtools.toolsidebar-width.inspector",
      state.width
    );
    Services.prefs.setIntPref(
      "devtools.toolsidebar-height.inspector",
      state.height
    );
    Services.prefs.setIntPref(
      "devtools.toolsidebar-width.inspector.splitsidebar",
      this.sidebarSplitBoxRef.current.state.width
    );
  }

  onSidebarResized(width, height) {
    this.toolbox.emit("inspector-sidebar-resized", { width, height });
  }

  /**
   * Returns inspector tab that is active.
   */
  getActiveSidebar() {
    return Services.prefs.getCharPref("devtools.inspector.activeSidebar");
  }

  setActiveSidebar(toolId) {
    Services.prefs.setCharPref("devtools.inspector.activeSidebar", toolId);
  }

  /**
   * Returns tab that is explicitly selected by user.
   */
  getSelectedSidebar() {
    return Services.prefs.getCharPref("devtools.inspector.selectedSidebar");
  }

  setSelectedSidebar(toolId) {
    Services.prefs.setCharPref("devtools.inspector.selectedSidebar", toolId);
  }

  onSidebarSelect(toolId) {
    // Save the currently selected sidebar panel
    this.setSelectedSidebar(toolId);
    this.setActiveSidebar(toolId);

    // Then forces the panel creation by calling getPanel
    // (This allows lazy loading the panels only once we select them)
    this.getPanel(toolId);

    this.toolbox.emit("inspector-sidebar-select", toolId);
  }

  onSidebarShown() {
    const { width, height, splitSidebarWidth } = this.getSidebarSize();
    this.splitBox.setState({ width, height });
    this.sidebarSplitBoxRef.current.setState({ width: splitSidebarWidth });
  }

  async onSidebarToggle() {
    this.isThreePaneModeEnabled = !this.isThreePaneModeEnabled;
    await this.#setupToolbar();
    this.#addRuleView({ skipQueue: true });
  }

  /**
   * Sets the inspector sidebar split box state. Shows the splitter inside the sidebar
   * split box, specifies the end panel control and resizes the split box width depending
   * on the width of the toolbox.
   */
  #setSidebarSplitBoxState() {
    const toolboxWidth = this.panelDoc.getElementById(
      "inspector-splitter-box"
    ).clientWidth;

    // Get the inspector sidebar's (right panel in horizontal mode or bottom panel in
    // vertical mode) width.
    const sidebarWidth = this.splitBox.state.width;
    // This variable represents the width of the right panel in horizontal mode or
    // bottom-right panel in vertical mode width in 3 pane mode.
    let sidebarSplitboxWidth;

    if (this.#useLandscapeMode()) {
      // Whether or not doubling the inspector sidebar's (right panel in horizontal mode
      // or bottom panel in vertical mode) width will be bigger than half of the
      // toolbox's width.
      const canDoubleSidebarWidth = sidebarWidth * 2 < toolboxWidth / 2;

      // Resize the main split box's end panel that contains the middle and right panel.
      // Attempts to resize the main split box's end panel to be double the size of the
      // existing sidebar's width when switching to 3 pane mode. However, if the middle
      // and right panel's width together is greater than half of the toolbox's width,
      // split all 3 panels to be equally sized by resizing the end panel to be 2/3 of
      // the current toolbox's width.
      this.splitBox.setState({
        width: canDoubleSidebarWidth
          ? sidebarWidth * 2
          : (toolboxWidth * 2) / 3,
      });

      // In landscape/horizontal mode, set the right panel back to its original
      // inspector sidebar width if we can double the sidebar width. Otherwise, set
      // the width of the right panel to be 1/3 of the toolbox's width since all 3
      // panels will be equally sized.
      sidebarSplitboxWidth = canDoubleSidebarWidth
        ? sidebarWidth
        : toolboxWidth / 3;
    } else {
      // In portrait/vertical mode, set the bottom-right panel to be 1/2 of the
      // toolbox's width.
      sidebarSplitboxWidth = toolboxWidth / 2;
    }

    // Show the splitter inside the sidebar split box. Sets the width of the inspector
    // sidebar and specify that the end (right in horizontal or bottom-right in
    // vertical) panel of the sidebar split box should be controlled when resizing.
    this.sidebarSplitBoxRef.current.setState({
      endPanelControl: true,
      splitterSize: 1,
      width: sidebarSplitboxWidth,
    });
  }

  /**
   * Adds the rule view to the middle (in landscape/horizontal mode) or bottom-left panel
   * (in portrait/vertical mode) or inspector sidebar depending on whether or not it is 3
   * pane mode. Rule view is selected when switching to 2 pane mode. Selected sidebar pref
   * is used otherwise.
   */
  #addRuleView({ skipQueue = false } = {}) {
    const selectedSidebar = this.getSelectedSidebar();
    const ruleViewSidebar = this.sidebarSplitBoxRef.current.startPanelContainer;

    if (this.isThreePaneModeEnabled) {
      // Convert to 3 pane mode by removing the rule view from the inspector sidebar
      // and adding the rule view to the middle (in landscape/horizontal mode) or
      // bottom-left (in portrait/vertical mode) panel.
      ruleViewSidebar.style.display = "block";

      this.#setSidebarSplitBoxState();

      // Force the rule view panel creation by calling getPanel
      this.getPanel("ruleview");

      this.sidebar.removeTab("ruleview");
      this.sidebar.select(selectedSidebar);

      this.ruleViewSideBar.addExistingTab(
        "ruleview",
        INSPECTOR_L10N.getStr("inspector.sidebar.ruleViewTitle"),
        true
      );

      this.ruleViewSideBar.show();
    } else {
      // When switching to 2 pane view, always set rule view as the active sidebar.
      this.setActiveSidebar("ruleview");
      // Removes the rule view from the 3 pane mode and adds the rule view to the main
      // inspector sidebar.
      ruleViewSidebar.style.display = "none";

      // Set the width of the split box (right panel in horziontal mode and bottom panel
      // in vertical mode) to be the width of the inspector sidebar.
      const splitterBox = this.panelDoc.getElementById(
        "inspector-splitter-box"
      );
      this.splitBox.setState({
        width: this.#useLandscapeMode()
          ? this.sidebarSplitBoxRef.current.state.width
          : splitterBox.clientWidth,
      });

      // Hide the splitter to prevent any drag events in the sidebar split box and
      // specify that the end (right panel in horziontal mode or bottom panel in vertical
      // mode) panel should be uncontrolled when resizing.
      this.sidebarSplitBoxRef.current.setState({
        endPanelControl: false,
        splitterSize: 0,
      });

      this.ruleViewSideBar.hide();
      this.ruleViewSideBar.removeTab("ruleview");

      if (skipQueue) {
        this.sidebar.addExistingTab(
          "ruleview",
          INSPECTOR_L10N.getStr("inspector.sidebar.ruleViewTitle"),
          true,
          0
        );
      } else {
        this.sidebar.queueExistingTab(
          "ruleview",
          INSPECTOR_L10N.getStr("inspector.sidebar.ruleViewTitle"),
          true,
          0
        );
      }
    }

    // Adding or removing a tab from sidebar sets selectedSidebar by the active tab,
    // which we should revert.
    this.setSelectedSidebar(selectedSidebar);

    this.emit("ruleview-added");
  }

  /**
   * Returns a boolean indicating whether a sidebar panel instance exists.
   */
  hasPanel(id) {
    return this.#panels.has(id);
  }

  /**
   * Lazily get and create panel instances displayed in the sidebar
   */
  getPanel(id) {
    if (this.#panels.has(id)) {
      return this.#panels.get(id);
    }

    let panel;
    switch (id) {
      case "animationinspector": {
        const AnimationInspector = this.browserRequire(
          "devtools/client/inspector/animation/animation"
        );
        panel = new AnimationInspector(this, this.panelWin);
        break;
      }
      case "boxmodel": {
        // box-model isn't a panel on its own, it used to, now it is being used by
        // the layout view which retrieves an instance via getPanel.
        const BoxModel = require("resource://devtools/client/inspector/boxmodel/box-model.js");
        panel = new BoxModel(this, this.panelWin);
        break;
      }
      case "changesview": {
        const ChangesView = this.browserRequire(
          "devtools/client/inspector/changes/ChangesView"
        );
        panel = new ChangesView(this, this.panelWin);
        break;
      }
      case "compatibilityview": {
        const CompatibilityView = this.browserRequire(
          "devtools/client/inspector/compatibility/CompatibilityView"
        );
        panel = new CompatibilityView(this, this.panelWin);
        break;
      }
      case "computedview": {
        const { ComputedViewTool } = this.browserRequire(
          "devtools/client/inspector/computed/computed"
        );
        panel = new ComputedViewTool(this, this.panelWin);
        break;
      }
      case "fontinspector": {
        const FontInspector = this.browserRequire(
          "devtools/client/inspector/fonts/fonts"
        );
        panel = new FontInspector(this, this.panelWin);
        break;
      }
      case "layoutview": {
        const LayoutView = this.browserRequire(
          "devtools/client/inspector/layout/layout"
        );
        panel = new LayoutView(this, this.panelWin);
        break;
      }
      case "ruleview": {
        const {
          RuleViewTool,
        } = require("resource://devtools/client/inspector/rules/rules.js");
        panel = new RuleViewTool(this, this.panelWin);
        break;
      }
      default:
        // This is a custom panel or a non lazy-loaded one.
        return null;
    }

    if (panel) {
      this.#panels.set(id, panel);
    }

    return panel;
  }

  /**
   * Build the sidebar.
   */
  #setupSidebar() {
    const sidebar = this.panelDoc.getElementById("inspector-sidebar");
    const options = {
      showAllTabsMenu: true,
      allTabsMenuButtonTooltip: INSPECTOR_L10N.getStr(
        "allTabsMenuButton.tooltip"
      ),
      sidebarToggleButton: {
        collapsed: !this.isThreePaneModeEnabled,
        collapsePaneTitle: INSPECTOR_L10N.getStr("inspector.hideThreePaneMode"),
        expandPaneTitle: INSPECTOR_L10N.getStr("inspector.showThreePaneMode"),
        onClick: this.onSidebarToggle,
      },
    };

    this.sidebar = new ToolSidebar(sidebar, this, options);
    this.sidebar.on("select", this.onSidebarSelect);

    const ruleSideBar = this.panelDoc.getElementById("inspector-rules-sidebar");
    this.ruleViewSideBar = new ToolSidebar(ruleSideBar, this, {
      hideTabstripe: true,
    });

    // Append all side panels
    this.#addRuleView();

    // Inspector sidebar panels in order of appearance.
    const sidebarPanels = [];
    sidebarPanels.push({
      id: "layoutview",
      title: INSPECTOR_L10N.getStr("inspector.sidebar.layoutViewTitle2"),
    });

    sidebarPanels.push({
      id: "computedview",
      title: INSPECTOR_L10N.getStr("inspector.sidebar.computedViewTitle"),
    });

    sidebarPanels.push({
      id: "changesview",
      title: INSPECTOR_L10N.getStr("inspector.sidebar.changesViewTitle"),
    });

    sidebarPanels.push({
      id: "compatibilityview",
      title: INSPECTOR_L10N.getStr("inspector.sidebar.compatibilityViewTitle"),
    });

    sidebarPanels.push({
      id: "fontinspector",
      title: INSPECTOR_L10N.getStr("inspector.sidebar.fontInspectorTitle"),
    });

    sidebarPanels.push({
      id: "animationinspector",
      title: INSPECTOR_L10N.getStr("inspector.sidebar.animationInspectorTitle"),
    });

    const defaultTab = this.getActiveSidebar();

    for (const { id, title } of sidebarPanels) {
      // The Computed panel is not a React-based panel. We pick its element container from
      // the DOM and wrap it in a React component (InspectorTabPanel) so it behaves like
      // other panels when using the Inspector's tool sidebar.
      if (id === "computedview") {
        this.sidebar.queueExistingTab(id, title, defaultTab === id);
      } else {
        // When `panel` is a function, it is called when the tab should render. It is
        // expected to return a React component to populate the tab's content area.
        // Calling this method on-demand allows us to lazy-load the requested panel.
        this.sidebar.queueTab(
          id,
          title,
          {
            props: {
              id,
              title,
            },
            panel: () => {
              return this.getPanel(id).provider;
            },
          },
          defaultTab === id
        );
      }
    }

    this.sidebar.addAllQueuedTabs();

    // Persist splitter state in preferences.
    this.sidebar.on("show", this.onSidebarShown);
    this.sidebar.on("hide", this.onSidebarHidden);
    this.sidebar.on("destroy", this.onSidebarHidden);

    this.sidebar.show();
  }

  /**
   * Setup any extension sidebar already registered to the toolbox when the inspector.
   * has been created for the first time.
   */
  #setupExtensionSidebars() {
    for (const [sidebarId, { title }] of this.toolbox
      .inspectorExtensionSidebars) {
      this.addExtensionSidebar(sidebarId, { title });
    }
  }

  /**
   * Create a side-panel tab controlled by an extension
   * using the devtools.panels.elements.createSidebarPane and sidebar object API
   *
   * @param {string} id
   *        An unique id for the sidebar tab.
   * @param {object} options
   * @param {string} options.title
   *        The tab title
   */
  addExtensionSidebar(id, { title }) {
    if (this.#panels.has(id)) {
      throw new Error(
        `Cannot create an extension sidebar for the existent id: ${id}`
      );
    }

    // Load the ExtensionSidebar component via the Browser Loader as it ultimately loads Reps and Object Inspector,
    // which are expected to be loaded in a document scope.
    const ExtensionSidebar = this.browserRequire(
      "resource://devtools/client/inspector/extensions/extension-sidebar.js"
    );
    const extensionSidebar = new ExtensionSidebar(this, { id, title });

    // TODO(rpl): pass some extension metadata (e.g. extension name and icon) to customize
    // the render of the extension title (e.g. use the icon in the sidebar and show the
    // extension name in a tooltip).
    this.addSidebarTab(id, title, extensionSidebar.provider, false);

    this.#panels.set(id, extensionSidebar);

    // Emit the created ExtensionSidebar instance to the listeners registered
    // on the toolbox by the "devtools.panels.elements" WebExtensions API.
    this.toolbox.emit(`extension-sidebar-created-${id}`, extensionSidebar);
  }

  /**
   * Remove and destroy a side-panel tab controlled by an extension (e.g. when the
   * extension has been disable/uninstalled while the toolbox and inspector were
   * still open).
   *
   * @param {string} id
   *        The id of the sidebar tab to destroy.
   */
  removeExtensionSidebar(id) {
    if (!this.#panels.has(id)) {
      throw new Error(`Unable to find a sidebar panel with id "${id}"`);
    }

    const panel = this.#panels.get(id);

    const ExtensionSidebar = this.browserRequire(
      "resource://devtools/client/inspector/extensions/extension-sidebar.js"
    );
    if (!(panel instanceof ExtensionSidebar)) {
      throw new Error(
        `The sidebar panel with id "${id}" is not an ExtensionSidebar`
      );
    }

    this.#panels.delete(id);
    this.sidebar.removeTab(id);
    panel.destroy();
  }

  /**
   * Register a side-panel tab. This API can be used outside of
   * DevTools (e.g. from an extension) as well as by DevTools
   * code base.
   *
   * @param {string} tab uniq id
   * @param {string} title tab title
   * @param {React.Component} panel component. See `InspectorPanelTab` as an example.
   * @param {boolean} selected true if the panel should be selected
   */
  addSidebarTab(id, title, panel, selected) {
    this.sidebar.addTab(id, title, panel, selected);
  }

  /**
   * Method to check whether the document is a HTML document and
   * pickColorFromPage method is available or not.
   *
   * @return {boolean} true if the eyedropper highlighter is supported by the current
   *         document.
   */
  async supportsEyeDropper() {
    try {
      return await this.inspectorFront.supportsHighlighters();
    } catch (e) {
      console.error(e);
      return false;
    }
  }

  async #setupToolbar() {
    this.#teardownToolbar();

    // Setup the add-node button.
    this.addNodeButton = this.panelDoc.getElementById(
      "inspector-element-add-button"
    );
    this.addNodeButton.addEventListener("click", this.addNode);

    // Setup the eye-dropper icon if we're in an HTML document and we have actor support.
    const canShowEyeDropper = await this.supportsEyeDropper();

    // Bail out if the inspector was destroyed in the meantime and panelDoc is no longer
    // available.
    if (!this.panelDoc) {
      return;
    }

    if (canShowEyeDropper) {
      this.eyeDropperButton = this.panelDoc.getElementById(
        "inspector-eyedropper-toggle"
      );
      this.eyeDropperButton.disabled = false;
      const shortcutKey = INSPECTOR_L10N.getStr(
        "inspector.eyedropper.key"
      ).replace("CmdOrCtrl", osString == "Darwin" ? "Cmd" : "Ctrl");

      this.eyeDropperButton.title = INSPECTOR_L10N.getFormatStr(
        "inspector.eyedropper.label2",
        shortcutKey
      );
      this.eyeDropperButton.addEventListener(
        "click",
        this.onEyeDropperButtonClicked
      );
    } else {
      const eyeDropperButton = this.panelDoc.getElementById(
        "inspector-eyedropper-toggle"
      );
      eyeDropperButton.disabled = true;
      eyeDropperButton.title = INSPECTOR_L10N.getStr(
        "eyedropper.disabled.title"
      );
    }

    this.emit("inspector-toolbar-updated");
  }

  #teardownToolbar() {
    if (this.addNodeButton) {
      this.addNodeButton.removeEventListener("click", this.addNode);
      this.addNodeButton = null;
    }

    if (this.eyeDropperButton) {
      this.eyeDropperButton.removeEventListener(
        "click",
        this.onEyeDropperButtonClicked
      );
      this.eyeDropperButton = null;
    }
  }

  #selectionCssSelectors = null;

  /**
   * Set the array of CSS selectors for the currently selected node.
   * We use an array of selectors in case the element is in iframes.
   * Will store the current target url along with it to allow pre-selection at
   * reload
   */
  set selectionCssSelectors(cssSelectors = []) {
    if (this.#destroyed) {
      return;
    }

    this.#selectionCssSelectors = {
      selectors: cssSelectors,
      url: this.currentTarget.url,
    };
  }

  /**
   * Get the CSS selectors for the current selection if any, that is, if a node
   * is actually selected and that node has been selected while on the same url
   */
  get selectionCssSelectors() {
    if (
      this.#selectionCssSelectors &&
      this.#selectionCssSelectors.url === this.currentTarget.url
    ) {
      return this.#selectionCssSelectors.selectors;
    }
    return [];
  }

  /**
   * On any new selection made by the user, store the array of css selectors
   * of the selected node so it can be restored after reload of the same page
   */
  #updateSelectionCssSelectors() {
    if (!this.selection.isElementNode()) {
      return;
    }

    this.commands.inspectorCommand
      .getNodeFrontSelectorsFromTopDocument(this.selection.nodeFront)
      .then(selectors => {
        this.selectionCssSelectors = selectors;
        // emit an event so tests relying on the property being set can properly wait
        // for it.
        this.emitForTests("selection-css-selectors-updated", selectors);
      }, this.#handleRejectionIfNotDestroyed);
  }

  /**
   * Can a new HTML element be inserted into the currently selected element?
   *
   * @return {boolean}
   */
  canAddHTMLChild() {
    const selection = this.selection;

    // Don't allow to insert an element into these elements. This should only
    // contain elements where walker.insertAdjacentHTML has no effect.
    const invalidTagNames = ["html", "iframe"];

    return (
      selection.isHTMLNode() &&
      selection.isElementNode() &&
      !selection.isPseudoElementNode() &&
      !selection.isNativeAnonymousNode() &&
      !invalidTagNames.includes(selection.nodeFront.nodeName.toLowerCase())
    );
  }

  /**
   * Update the state of the add button in the toolbar depending on the current selection.
   */
  #updateAddElementButton() {
    const btn = this.panelDoc.getElementById("inspector-element-add-button");
    if (this.canAddHTMLChild()) {
      btn.removeAttribute("disabled");
    } else {
      btn.setAttribute("disabled", "true");
    }
  }

  /**
   * Handler for the "host-changed" event from the toolbox. Resets the inspector
   * sidebar sizes when the toolbox host type changes.
   */
  #onHostChanged = async () => {
    // Eagerly call our resize handling code to process the fact that we
    // switched hosts. If we don't do this, we'll wait for resize events + 200ms
    // to have passed, which causes the old layout to noticeably show up in the
    // new host, followed by the updated one.
    await this.#onLazyPanelResize();
    // Note that we may have been destroyed by now, especially in tests, so we
    // need to check if that's happened before touching anything else.
    if (!this.currentTarget || !this.isThreePaneModeEnabled) {
      return;
    }

    // When changing hosts, the toolbox chromeEventHandler might change, for instance when
    // switching from docked to window hosts. Recreate the inspector shortcuts.
    this.inspectorShortcuts.destroy();
    this.#createInspectorShortcuts();
    this.#setSidebarSplitBoxState();
  };

  /**
   * When a new node is selected.
   */
  #onNewSelection = (value, reason) => {
    if (reason === "selection-destroy") {
      return;
    }

    this.#updateAddElementButton();
    this.#updateSelectionCssSelectors();

    const selfUpdate = this.updating("inspector-panel");
    executeSoon(() => {
      try {
        selfUpdate(this.selection.nodeFront);
        Glean.devtoolsInspector.nodeSelectionCount.add(1);
      } catch (ex) {
        console.error(ex);
      }
    });
  };

  /**
   * Delay the "inspector-updated" notification while a tool
   * is updating itself.  Returns a function that must be
   * invoked when the tool is done updating with the node
   * that the tool is viewing.
   */
  updating(name) {
    if (
      this.#updateProgress &&
      this.#updateProgress.node != this.selection.nodeFront
    ) {
      this.#cancelUpdate();
    }

    if (!this.#updateProgress) {
      // Start an update in progress.
      const self = this;
      this.#updateProgress = {
        node: this.selection.nodeFront,
        outstanding: new Set(),
        checkDone() {
          if (this !== self.#updateProgress) {
            return;
          }
          // Cancel update if there is no `selection` anymore.
          // It can happen if the inspector panel is already destroyed.
          if (!self.selection || this.node !== self.selection.nodeFront) {
            self.#cancelUpdate();
            return;
          }
          if (this.outstanding.size !== 0) {
            return;
          }

          self.#updateProgress = null;
          self.emit("inspector-updated", name);
        },
      };
    }

    const progress = this.#updateProgress;
    const done = function () {
      progress.outstanding.delete(done);
      progress.checkDone();
    };
    progress.outstanding.add(done);
    return done;
  }

  /**
   * Cancel notification of inspector updates.
   */
  #cancelUpdate() {
    this.#updateProgress = null;
  }

  /**
   * When a node is deleted, select its parent node or the defaultNode if no
   * parent is found (may happen when deleting an iframe inside which the
   * node was selected).
   */
  #onDetached = parentNode => {
    this.breadcrumbs.cutAfter(this.breadcrumbs.indexOf(parentNode));
    const nodeFront = parentNode ? parentNode : this.#defaultNode;
    this.selection.setNodeFront(nodeFront, { reason: "detached" });
  };

  /**
   * Destroy the inspector.
   */
  destroy() {
    if (this.#destroyed) {
      return;
    }
    this.#destroyed = true;

    // Prevents any further action from being dispatched
    this.store.dispatch(START_IGNORE_ACTION);

    this.#cancelUpdate();

    this.panelWin.removeEventListener("resize", this.onPanelWindowResize, true);
    this.selection.off("new-node-front", this.#onNewSelection);
    this.selection.off("detached-front", this.#onDetached);
    this.toolbox.nodePicker.off("picker-node-canceled", this.onPickerCanceled);
    this.toolbox.nodePicker.off("picker-node-hovered", this.onPickerHovered);
    this.toolbox.nodePicker.off("picker-node-picked", this.onPickerPicked);

    // Destroy the sidebar first as it may unregister stuff
    // and still use random attributes on inspector and layout panel
    this.sidebar.destroy();
    // Unregister sidebar listener *after* destroying it
    // in order to process its destroy event and save sidebar sizes
    this.sidebar.off("select", this.onSidebarSelect);
    this.sidebar.off("show", this.onSidebarShown);
    this.sidebar.off("hide", this.onSidebarHidden);
    this.sidebar.off("destroy", this.onSidebarHidden);

    for (const [, panel] of this.#panels) {
      panel.destroy({ fromInspectorDestroy: true });
    }
    this.#panels.clear();

    if (this.#highlighters) {
      this.#highlighters.destroy();
    }

    if (this.#search) {
      this.#search.destroy();
      this.#search = null;
    }

    this.ruleViewSideBar.destroy();
    this.ruleViewSideBar = null;

    this.#destroyMarkup();

    this.#teardownToolbar();

    this.prefObserver.destroy();

    this.breadcrumbs.destroy();
    this.styleChangeTracker.destroy();
    this.inspectorShortcuts.destroy();
    this.inspectorShortcuts = null;

    this.commands.targetCommand.unwatchTargets({
      types: [this.commands.targetCommand.TYPES.FRAME],
      onAvailable: this.#onTargetAvailable,
      onSelected: this.#onTargetSelected,
      onDestroyed: this.#onTargetDestroyed,
    });
    const { resourceCommand } = this.commands;
    resourceCommand.unwatchResources(this.#watchedResources, {
      onAvailable: this.onResourceAvailable,
    });

    this.#InspectorTabPanel = null;
    this.#TabBar = null;
    this.#InspectorSplitBox = null;
    this.sidebarSplitBoxRef = null;
    // Note that we do not unmount inspector-splitter-box
    // as it regresses inspector closing performance while not releasing
    // any object (bug 1729925)
    this.splitBox = null;

    this.#isThreePaneModeEnabled = null;
    this.#markupBox = null;
    this.#markupFrame = null;
    this.#toolbox = null;
    this.#commands = null;
    this.breadcrumbs = null;
    this.inspectorFront = null;
    this.#cssProperties = null;
    this.accessibilityFront = null;
    this.#highlighters = null;
    this.walker = null;
    this.#defaultNode = null;
    this.panelDoc = null;
    this.panelWin.inspector = null;
    this.panelWin = null;
    this.resultsLength = null;
    this.searchBox.removeEventListener("focus", this.#listenForSearchEvents);
    this.searchBox = null;
    this.show3PaneTooltip = null;
    this.sidebar = null;
    this.store = null;
    this.telemetry = null;
    this.searchResultsLabel.removeEventListener(
      "click",
      this.#onSearchLabelClick
    );
    this.searchResultsLabel = null;
  }

  #destroyMarkup() {
    if (this.markup) {
      this.markup.destroy();
      this.markup = null;
    }

    if (this.#markupBox) {
      this.#markupBox.style.visibility = "hidden";
    }
  }

  onEyeDropperButtonClicked() {
    this.eyeDropperButton.classList.contains("checked")
      ? this.hideEyeDropper()
      : this.showEyeDropper();
  }

  startEyeDropperListeners() {
    this.toolbox.tellRDMAboutPickerState(true, PICKER_TYPES.EYEDROPPER);
    this.inspectorFront.once("color-pick-canceled", this.onEyeDropperDone);
    this.inspectorFront.once("color-picked", this.onEyeDropperDone);
    this.once("new-root", this.onEyeDropperDone);
  }

  stopEyeDropperListeners() {
    this.toolbox
      .tellRDMAboutPickerState(false, PICKER_TYPES.EYEDROPPER)
      .catch(console.error);
    this.inspectorFront.off("color-pick-canceled", this.onEyeDropperDone);
    this.inspectorFront.off("color-picked", this.onEyeDropperDone);
    this.off("new-root", this.onEyeDropperDone);
  }

  onEyeDropperDone() {
    this.eyeDropperButton.classList.remove("checked");
    this.stopEyeDropperListeners();
    this.panelWin.focus();
  }

  /**
   * Show the eyedropper on the page.
   *
   * @return {Promise} resolves when the eyedropper is visible.
   */
  showEyeDropper() {
    // The eyedropper button doesn't exist, most probably because the actor doesn't
    // support the pickColorFromPage, or because the page isn't HTML.
    if (!this.eyeDropperButton) {
      return null;
    }
    // turn off node picker when color picker is starting
    this.toolbox.nodePicker.stop({ canceled: true }).catch(console.error);
    this.eyeDropperButton.classList.add("checked");
    this.startEyeDropperListeners();
    return this.inspectorFront
      .pickColorFromPage({ copyOnSelect: true })
      .catch(console.error);
  }

  /**
   * Hide the eyedropper.
   *
   * @return {Promise} resolves when the eyedropper is hidden.
   */
  hideEyeDropper() {
    // The eyedropper button doesn't exist, most probably  because the page isn't HTML.
    if (!this.eyeDropperButton) {
      return null;
    }

    this.eyeDropperButton.classList.remove("checked");
    this.stopEyeDropperListeners();
    return this.inspectorFront.cancelPickColorFromPage().catch(console.error);
  }

  /**
   * Create a new node as the last child of the current selection, expand the
   * parent and select the new node.
   */
  async addNode() {
    if (!this.canAddHTMLChild()) {
      return;
    }

    // turn off node picker when add node is triggered
    this.toolbox.nodePicker.stop({ canceled: true });

    // turn off color picker when add node is triggered
    this.hideEyeDropper();

    const nodeFront = this.selection.nodeFront;
    const html = "<div></div>";

    // Insert the html and expect a childList markup mutation.
    const onMutations = this.once("markupmutation");
    await nodeFront.walkerFront.insertAdjacentHTML(
      this.selection.nodeFront,
      "beforeEnd",
      html
    );
    await onMutations;

    // Expand the parent node.
    this.markup.expandNode(nodeFront);
  }

  /**
   * Toggle a pseudo class.
   */
  togglePseudoClass(pseudo) {
    if (this.selection.isElementNode()) {
      const node = this.selection.nodeFront;
      if (node.hasPseudoClassLock(pseudo)) {
        return node.walkerFront.removePseudoClassLock(node, pseudo, {
          parents: true,
        });
      }

      const hierarchical = pseudo == ":hover" || pseudo == ":active";
      return node.walkerFront.addPseudoClassLock(node, pseudo, {
        parents: hierarchical,
      });
    }
    return Promise.resolve();
  }

  /**
   * Returns true if the "Change pseudo class" (either via the ":hov" panel checkboxes,
   * or the markup view context menu entries) can be performed for the currently selected node.
   *
   * @returns {boolean}
   */
  canTogglePseudoClassForSelectedNode() {
    if (!this.selection) {
      return false;
    }

    return (
      this.selection.isElementNode() && !this.selection.isPseudoElementNode()
    );
  }

  /**
   * Initiate screenshot command on selected node.
   */
  async screenshotNode() {
    // Bug 1332936 - it's possible to call `screenshotNode` while the BoxModel highlighter
    // is still visible, therefore showing it in the picture.
    // Note that other highlighters will still be visible. See Bug 1663881
    await this.highlighters.hideHighlighterType(
      this.highlighters.TYPES.BOXMODEL
    );

    const clipboardEnabled = Services.prefs.getBoolPref(
      "devtools.screenshot.clipboard.enabled"
    );
    const args = {
      file: !clipboardEnabled,
      nodeActorID: this.selection.nodeFront.actorID,
      clipboard: clipboardEnabled,
    };

    const messages = await captureAndSaveScreenshot(
      this.selection.nodeFront.targetFront,
      this.panelWin,
      args
    );
    const notificationBox = this.toolbox.getNotificationBox();
    const priorityMap = {
      error: notificationBox.PRIORITY_CRITICAL_HIGH,
      warn: notificationBox.PRIORITY_WARNING_HIGH,
    };
    for (const { text, level } of messages) {
      // captureAndSaveScreenshot returns "saved" messages, that indicate where the
      // screenshot was saved. We don't want to display them as the download UI can be
      // used to open the file.
      if (level !== "warn" && level !== "error") {
        continue;
      }
      notificationBox.appendNotification(text, null, null, priorityMap[level]);
    }
  }

  /**
   * Returns an object containing the shared handler functions used in React components.
   */
  getCommonComponentProps() {
    return {
      setSelectedNode: this.selection.setNodeFront,
    };
  }

  onPickerCanceled() {
    this.highlighters.hideHighlighterType(this.highlighters.TYPES.BOXMODEL);
  }

  onPickerHovered(nodeFront) {
    this.highlighters.showHighlighterTypeForNode(
      this.highlighters.TYPES.BOXMODEL,
      nodeFront
    );
  }

  onPickerPicked(nodeFront) {
    if (this.toolbox.isDebugTargetFenix()) {
      // When debugging a phone, as we don't have the "hover overlay", we want to provide
      // feedback to the user so they know where they tapped
      this.highlighters.showHighlighterTypeForNode(
        this.highlighters.TYPES.BOXMODEL,
        nodeFront,
        { duration: this.HIGHLIGHTER_AUTOHIDE_TIMER }
      );
      return;
    }
    this.highlighters.hideHighlighterType(this.highlighters.TYPES.BOXMODEL);
  }

  async inspectNodeActor(nodeGrip, reason) {
    const nodeFront =
      await this.inspectorFront.getNodeFrontFromNodeGrip(nodeGrip);
    if (!nodeFront) {
      console.error(
        "The object cannot be linked to the inspector, the " +
          "corresponding nodeFront could not be found."
      );
      return false;
    }

    const isAttached = await this.walker.isInDOMTree(nodeFront);
    if (!isAttached) {
      console.error("Selected DOMNode is not attached to the document tree.");
      return false;
    }

    await this.selection.setNodeFront(nodeFront, { reason });
    return true;
  }

  /**
   * Called by toolbox.js on `Esc` keydown to check if the inspector panel
   * should prevent the split console from being toggled.
   *
   * @returns {boolean} true if the split console toggle should be prevented.
   */
  shouldPreventSplitConsoleToggle() {
    // If the event tooltip is displayed, hide it and prevent the split console
    // from being toggled.
    if (
      this.markup.hasEventDetailsTooltip() &&
      this.markup.eventDetailsTooltip.isVisible()
    ) {
      this.markup.eventDetailsTooltip.hide();
      return true;
    }
    return false;
  }

  /**
   * Check if this inspector instance already started being destroyed.
   *
   * @returns {boolean} true if the inspector destroy() was already called.
   */
  isDestroyed() {
    return !!this.#destroyed;
  }
}

exports.Inspector = Inspector;

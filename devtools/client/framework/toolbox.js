/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const MAX_ORDINAL = 99;
const SPLITCONSOLE_OPEN_PREF = "devtools.toolbox.splitconsole.open";
const SPLITCONSOLE_ENABLED_PREF = "devtools.toolbox.splitconsole.enabled";
const SPLITCONSOLE_HEIGHT_PREF = "devtools.toolbox.splitconsoleHeight";
const DEVTOOLS_ALWAYS_ON_TOP = "devtools.toolbox.alwaysOnTop";
const DISABLE_AUTOHIDE_PREF = "ui.popup.disable_autohide";
const PSEUDO_LOCALE_PREF = "intl.l10n.pseudo";
const HTML_NS = "http://www.w3.org/1999/xhtml";
const REGEX_4XX_5XX = /^[4,5]\d\d$/;

const BROWSERTOOLBOX_SCOPE_PREF = "devtools.browsertoolbox.scope";
const BROWSERTOOLBOX_SCOPE_EVERYTHING = "everything";
const BROWSERTOOLBOX_SCOPE_PARENTPROCESS = "parent-process";

const { debounce } = require("resource://devtools/shared/debounce.js");
const { throttle } = require("resource://devtools/shared/throttle.js");
const {
  safeAsyncMethod,
} = require("resource://devtools/shared/async-utils.js");
var { gDevTools } = require("resource://devtools/client/framework/devtools.js");
var EventEmitter = require("resource://devtools/shared/event-emitter.js");
const Selection = require("resource://devtools/client/framework/selection.js");
var Telemetry = require("resource://devtools/client/shared/telemetry.js");
const {
  getUnicodeUrl,
} = require("resource://devtools/client/shared/unicode-url.js");
var { DOMHelpers } = require("resource://devtools/shared/dom-helpers.js");
const { KeyCodes } = require("resource://devtools/client/shared/keycodes.js");
const {
  FluentL10n,
} = require("resource://devtools/client/shared/fluent-l10n/fluent-l10n.js");
const {
  START_IGNORE_ACTION,
} = require("resource://devtools/client/shared/redux/middleware/ignore.js");

var Startup = Cc["@mozilla.org/devtools/startup-clh;1"].getService(
  Ci.nsISupports
).wrappedJSObject;

const { BrowserLoader } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/browser-loader.sys.mjs"
);

const {
  MultiLocalizationHelper,
} = require("resource://devtools/shared/l10n.js");
const L10N = new MultiLocalizationHelper(
  "devtools/client/locales/toolbox.properties",
  "chrome://branding/locale/brand.properties",
  "devtools/client/locales/menus.properties"
);

loader.lazyRequireGetter(
  this,
  "registerStoreObserver",
  "resource://devtools/client/shared/redux/subscriber.js",
  true
);
loader.lazyRequireGetter(
  this,
  "createToolboxStore",
  "resource://devtools/client/framework/store.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["registerWalkerListeners", "removeTarget"],
  "resource://devtools/client/framework/actions/index.js",
  true
);
loader.lazyRequireGetter(
  this,
  ["selectTarget"],
  "resource://devtools/shared/commands/target/actions/targets.js",
  true
);
loader.lazyRequireGetter(
  this,
  "TRACER_LOG_METHODS",
  "resource://devtools/shared/specs/tracer.js",
  true
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  LocalModeMappings:
    "resource://devtools/client/framework/LocalModeMappings.sys.mjs",
});
loader.lazyRequireGetter(this, "flags", "resource://devtools/shared/flags.js");
loader.lazyRequireGetter(
  this,
  "KeyShortcuts",
  "resource://devtools/client/shared/key-shortcuts.js"
);
loader.lazyRequireGetter(
  this,
  "ZoomKeys",
  "resource://devtools/client/shared/zoom-keys.js"
);
loader.lazyRequireGetter(
  this,
  "ToolboxButtons",
  "resource://devtools/client/definitions.js",
  true
);
loader.lazyRequireGetter(
  this,
  "SourceMapURLService",
  "resource://devtools/client/framework/source-map-url-service.js",
  true
);
loader.lazyRequireGetter(
  this,
  "BrowserConsoleManager",
  "resource://devtools/client/webconsole/browser-console-manager.js",
  true
);
loader.lazyRequireGetter(
  this,
  "viewSource",
  "resource://devtools/client/shared/view-source.js"
);
loader.lazyRequireGetter(
  this,
  "buildHarLog",
  "resource://devtools/client/netmonitor/src/har/har-builder-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "NetMonitorAPI",
  "resource://devtools/client/netmonitor/src/api.js",
  true
);
loader.lazyRequireGetter(
  this,
  "sortPanelDefinitions",
  "resource://devtools/client/framework/toolbox-tabs-order-manager.js",
  true
);
loader.lazyRequireGetter(
  this,
  "createEditContextMenu",
  "resource://devtools/client/framework/toolbox-context-menu.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getSelectedTarget",
  "resource://devtools/shared/commands/target/selectors/targets.js",
  true
);
loader.lazyRequireGetter(
  this,
  "remoteClientManager",
  "resource://devtools/client/shared/remote-debugging/remote-client-manager.js",
  true
);
loader.lazyRequireGetter(
  this,
  "ResponsiveUIManager",
  "resource://devtools/client/responsive/manager.js"
);
loader.lazyRequireGetter(
  this,
  "DevToolsUtils",
  "resource://devtools/shared/DevToolsUtils.js"
);
loader.lazyRequireGetter(
  this,
  "NodePicker",
  "resource://devtools/client/inspector/node-picker.js"
);

loader.lazyGetter(this, "domNodeConstants", () => {
  return require("resource://devtools/shared/dom-node-constants.js");
});

loader.lazyRequireGetter(
  this,
  "NodeFront",
  "resource://devtools/client/fronts/node.js",
  true
);

loader.lazyRequireGetter(
  this,
  "PICKER_TYPES",
  "resource://devtools/shared/picker-constants.js"
);

loader.lazyRequireGetter(
  this,
  "HarAutomation",
  "resource://devtools/client/netmonitor/src/har/har-automation.js",
  true
);

loader.lazyRequireGetter(
  this,
  "getThreadOptions",
  "resource://devtools/client/shared/thread-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "SourceMapLoader",
  "resource://devtools/client/shared/source-map-loader/index.js",
  true
);
loader.lazyRequireGetter(
  this,
  "openProfilerTab",
  "resource://devtools/client/performance-new/shared/browser.js",
  true
);
loader.lazyGetter(this, "ProfilerBackground", () => {
  return ChromeUtils.importESModule(
    "resource://devtools/client/performance-new/shared/background.sys.mjs"
  );
});

const DEVTOOLS_STYLESHEETS_IN_DEBUGGER =
  "devtools.debugger.features.stylesheets-in-debugger";

const CONFIGURATION_PREFS = {
  "devtools.cache.disabled": {
    name: "cacheDisabled",
  },
  "devtools.custom-formatters.enabled": {
    name: "customFormatters",
  },
  "devtools.serviceWorkers.testing.enabled": {
    name: "serviceWorkersTestingEnabled",
  },
  "devtools.inspector.simple-highlighters-reduced-motion": {
    name: "useSimpleHighlightersForReducedMotion",
  },
  "devtools.debugger.features.overlay": {
    name: "pauseOverlay",
    thread: true,
  },
  "devtools.command-button-jstracer.enabled": {
    name: "isTracerFeatureEnabled",
  },
  "devtools.netmonitor.bodyLimit": {
    name: "networkBodyLimit",
  },
};
exports.CONFIGURATION_PREFS = CONFIGURATION_PREFS;

/**
 * A "Toolbox" is the component that holds all the tools for one specific
 * target. Visually, it's a document that includes the tools tabs and all
 * the iframes where the tool panels will be living in.
 */
class Toolbox extends EventEmitter {
  #additionalToolDefinitions;
  #appBoundary;
  #autohideHasBeenToggled;
  #chromeEventHandler;
  #componentMount;
  #currentToolId;
  #debounceUpdateFocusedState;
  #defaultToolId;
  #defaultToolOptions;
  #descriptorFront;
  #destroyer;
  #errorCount;
  #hostType;
  #inspectorExtensionSidebars;
  #lastFocusedElement;
  #netMonitorAPI;
  #nodePicker;
  #notificationBox;
  #panelDefinitions;
  #parserWorker;
  #pausedTargets;
  #preferenceFrontRequest;
  #pseudoLocaleChanged;
  #resolveIsOpen;
  #sourceMapLoader;
  #sourceMapURLService;
  #splitConsole;
  #splitConsoleEnabled;
  #store;
  #tabBar;
  #throttledSetToolboxButtons;
  #toolNames;
  #toolPanels;
  #visibleAdditionalTools;
  #visibleIframes;
  #webExtensions;
  #win;
  #windowHostShortcuts;

  /**
   * @param {object} options
   * @param {object} options.commands
   *        The context to inspect identified by this commands.
   * @param {string} options.selectedTool
   *        Tool to select initially
   * @param {object} options.selectedToolOptions
   *        Object that will be passed to the panel init function
   * @param {Toolbox.HostType} options.hostType
   *        Type of host that will host the toolbox (e.g. sidebar, window)
   * @param {DOMWindow} options.contentWindow
   *        The window object of the toolbox document
   * @param {string} options.frameId
   *        A unique identifier to differentiate toolbox documents from the
   *        chrome codebase when passing DOM messages
   */
  constructor({
    commands,
    selectedTool,
    selectedToolOptions,
    hostType,
    contentWindow,
    frameId,
  }) {
    super();

    this.#win = contentWindow;
    this.frameId = frameId;
    this.selection = new Selection();
    this.telemetry = new Telemetry({ useSessionId: true });
    // This attribute helps identify one particular toolbox instance.
    this.sessionId = this.telemetry.sessionId;

    // This attribute is meant to be a public attribute on the Toolbox object
    // It exposes commands modules listed in devtools/shared/commands/index.js
    // which are an abstraction on top of RDP methods.
    // See devtools/shared/commands/README.md
    this.commands = commands;
    this.#descriptorFront = commands.descriptorFront;

    // Map of the available DevTools WebExtensions:
    //   Map<extensionUUID, extensionName>
    this.#webExtensions = new Map();

    this.#toolPanels = new Map();
    this.#inspectorExtensionSidebars = new Map();

    this.#netMonitorAPI = null;

    // Map of frames (id => frame-info) and currently selected frame id.
    this.frameMap = new Map();
    this.selectedFrameId = null;

    // Number of targets currently paused
    this.#pausedTargets = new Set();

    /**
     * KeyShortcuts instance specific to WINDOW host type.
     * This is the key shortcuts that are only register when the toolbox
     * is loaded in its own window. Otherwise, these shortcuts are typically
     * registered by devtools-startup.js module.
     */
    this.#windowHostShortcuts = null;

    // List of currently displayed panel's iframes
    this.#visibleIframes = new Set();

    this.toggleNoAutohide = this.toggleNoAutohide.bind(this);
    this.toggleAlwaysOnTop = this.toggleAlwaysOnTop.bind(this);
    this.disablePseudoLocale = () => this.changePseudoLocale("none");
    this.enableAccentedPseudoLocale = () => this.changePseudoLocale("accented");
    this.enableBidiPseudoLocale = () => this.changePseudoLocale("bidi");
    this.closeToolbox = this.closeToolbox.bind(this);
    this.destroy = this.destroy.bind(this);
    this.updateToolboxButtonsVisibility =
      this.updateToolboxButtonsVisibility.bind(this);
    this.selectTool = this.selectTool.bind(this);
    this.toggleSplitConsole = this.toggleSplitConsole.bind(this);
    this.toggleOptions = this.toggleOptions.bind(this);

    // `component` might be null if the toolbox was destroying during the throttling
    this.#throttledSetToolboxButtons = throttle(
      () => this.component?.setToolboxButtons(this.toolbarButtons),
      500,
      this
    );

    this.#debounceUpdateFocusedState = debounce(
      () => {
        this.component?.setFocusedState(this.#isToolboxFocused);
      },
      500,
      this
    );

    if (!selectedTool) {
      selectedTool = Services.prefs.getCharPref(this.#prefs.LAST_TOOL);
    }
    this.#defaultToolId = selectedTool;
    this.#defaultToolOptions = selectedToolOptions;

    this.#hostType = hostType;

    this.isOpen = new Promise(
      function (resolve) {
        this.#resolveIsOpen = resolve;
      }.bind(this)
    );

    this.#descriptorFront.on("descriptor-destroyed", this.destroy);

    this.on("host-changed", this.#refreshHostTitle);
    this.on("select", this.#onToolSelected);

    this.selection.on("new-node-front", this.#onNewSelectedNodeFront);

    gDevTools.on("tool-registered", this.#toolRegistered);
    gDevTools.on("tool-unregistered", this.#toolUnregistered);

    /**
     * Get text direction for the current locale direction.
     *
     * `getComputedStyle` forces a synchronous reflow, so use a lazy getter in order to
     * call it only once.
     */
    loader.lazyGetter(this, "direction", () => {
      const { documentElement } = this.doc;
      const isRtl =
        this.win.getComputedStyle(documentElement).direction === "rtl";
      return isRtl ? "rtl" : "ltr";
    });
  }

  /**
   * The toolbox can be 'hosted' either embedded in a browser window
   * or in a separate window.
   */
  static HostType = {
    BOTTOM: "bottom",
    RIGHT: "right",
    LEFT: "left",
    WINDOW: "window",
    BROWSERTOOLBOX: "browsertoolbox",
    // This is typically used by `about:debugging`, when opening toolbox in a new tab,
    // via `about:devtools-toolbox` URLs.
    PAGE: "page",
  };

  #URL = "about:devtools-toolbox";

  #prefs = {
    LAST_TOOL: "devtools.toolbox.selectedTool",
  };

  get nodePicker() {
    if (!this.#nodePicker) {
      this.#nodePicker = new NodePicker(this.commands, this.selection);
      this.#nodePicker.on("picker-starting", this.#onPickerStarting);
      this.#nodePicker.on("picker-started", this.#onPickerStarted);
      this.#nodePicker.on("picker-stopped", this.#onPickerStopped);
      this.#nodePicker.on("picker-node-canceled", this.#onPickerCanceled);
      this.#nodePicker.on("picker-node-picked", this.#onPickerPicked);
      this.#nodePicker.on("picker-node-previewed", this.#onPickerPreviewed);
    }

    return this.#nodePicker;
  }

  get store() {
    if (!this.#store) {
      this.#store = createToolboxStore();
    }
    return this.#store;
  }

  get currentToolId() {
    return this.#currentToolId;
  }

  set currentToolId(id) {
    this.#currentToolId = id;
    this.component.setCurrentToolId(id);
  }

  get defaultToolId() {
    return this.#defaultToolId;
  }

  get panelDefinitions() {
    return this.#panelDefinitions;
  }

  set panelDefinitions(definitions) {
    this.#panelDefinitions = definitions;
    this.#combineAndSortPanelDefinitions();
  }

  get visibleAdditionalTools() {
    if (!this.#visibleAdditionalTools) {
      this.#visibleAdditionalTools = [];
    }

    return this.#visibleAdditionalTools;
  }

  set visibleAdditionalTools(tools) {
    this.#visibleAdditionalTools = tools;
    if (this.isReady) {
      this.#combineAndSortPanelDefinitions();
    }
  }

  /**
   * Combines the built-in panel definitions and the additional tool definitions that
   * can be set by add-ons.
   */
  #combineAndSortPanelDefinitions() {
    let definitions = [
      ...this.#panelDefinitions,
      ...this.getVisibleAdditionalTools(),
    ];
    definitions = sortPanelDefinitions(definitions);
    this.component.setPanelDefinitions(definitions);
  }

  lastUsedToolId = null;

  /**
   * Returns a *copy* of the #toolPanels collection.
   *
   * @return {Map} panels
   *         All the running panels in the toolbox
   */
  getToolPanels() {
    return new Map(this.#toolPanels);
  }

  /**
   * Access the panel for a given tool
   */
  getPanel(id) {
    return this.#toolPanels.get(id);
  }

  /**
   * Get the panel instance for a given tool once it is ready.
   * If the tool is already opened, the promise will resolve immediately,
   * otherwise it will wait until the tool has been opened before resolving.
   *
   * Note that this does not open the tool, use selectTool if you'd
   * like to select the tool right away.
   *
   * @param  {string} id
   *         The id of the panel, for example "jsdebugger".
   * @returns Promise
   *          A promise that resolves once the panel is ready.
   */
  getPanelWhenReady(id) {
    const panel = this.getPanel(id);
    return new Promise(resolve => {
      if (panel) {
        resolve(panel);
      } else {
        this.on(id + "-ready", initializedPanel => {
          resolve(initializedPanel);
        });
      }
    });
  }

  /**
   * This is a shortcut for getPanel(currentToolId) because it is much more
   * likely that we're going to want to get the panel that we've just made
   * visible
   */
  getCurrentPanel() {
    return this.#toolPanels.get(this.currentToolId);
  }

  /**
   * Get the current top level target the toolbox is debugging.
   *
   * This will only be defined *after* calling Toolbox.open(),
   * after it has called `targetCommands.startListening`.
   */
  get target() {
    return this.commands.targetCommand.targetFront;
  }

  get threadFront() {
    return this.commands.targetCommand.targetFront.threadFront;
  }

  /**
   * Get/alter the host of a Toolbox, i.e. is it in browser or in a separate
   * tab. See HostType for more details.
   */
  get hostType() {
    return this.#hostType;
  }

  /**
   * Shortcut to the window containing the toolbox UI
   */
  get win() {
    return this.#win;
  }

  /**
   * When the toolbox is loaded in a frame with type="content", win.parent will not return
   * the parent Chrome window. This getter should return the parent Chrome window
   * regardless of the frame type. See Bug 1539979.
   */
  get topWindow() {
    return DevToolsUtils.getTopWindow(this.win);
  }

  get topDoc() {
    return this.topWindow.document;
  }

  /**
   * Shortcut to the document containing the toolbox UI
   */
  get doc() {
    return this.win.document;
  }

  /**
   * Get the toggled state of the split console
   */
  get splitConsole() {
    return this.#splitConsole;
  }

  /**
   * Get the focused state of the split console
   */
  isSplitConsoleFocused() {
    if (!this.#splitConsole) {
      return false;
    }
    const focusedWin = Services.focus.focusedWindow;
    return (
      focusedWin &&
      focusedWin ===
        this.doc.querySelector("#toolbox-panel-iframe-webconsole").contentWindow
    );
  }

  /**
   * Get the enabled split console setting, and if it's not set, set it with updateIsSplitConsoleEnabled
   *
   * @returns {boolean} devtools.toolbox.splitconsole.enabled option
   */
  isSplitConsoleEnabled() {
    if (typeof this.#splitConsoleEnabled !== "boolean") {
      this.updateIsSplitConsoleEnabled();
    }

    return this.#splitConsoleEnabled;
  }

  get isBrowserToolbox() {
    return this.hostType === Toolbox.HostType.BROWSERTOOLBOX;
  }

  get isMultiProcessBrowserToolbox() {
    return this.isBrowserToolbox;
  }

  /**
   * Set a given target as selected (which may impact the console evaluation context selector).
   *
   * @param {string} targetActorID: The actorID of the target we want to select.
   */
  selectTarget(targetActorID) {
    if (this.getSelectedTargetFront()?.actorID !== targetActorID) {
      // The selected target is managed by the TargetCommand's store.
      // So dispatch this action against that other store.
      this.commands.targetCommand.store.dispatch(selectTarget(targetActorID));
    }
  }

  /**
   * @returns {ThreadFront|null} The selected thread front, or null if there is none.
   */
  getSelectedTargetFront() {
    // The selected target is managed by the TargetCommand's store.
    // So pull the state from that other store.
    const selectedTarget = getSelectedTarget(
      this.commands.targetCommand.store.getState()
    );
    if (!selectedTarget) {
      return null;
    }

    return this.commands.client.getFrontByID(selectedTarget.actorID);
  }

  /**
   * For now, the debugger isn't hooked to TargetCommand's store
   * to display its thread list. So manually forward target selection change
   * to the debugger via a dedicated action
   */
  #onTargetCommandStateChange = (state, oldState) => {
    if (getSelectedTarget(state) !== getSelectedTarget(oldState)) {
      const dbg = this.getPanel("jsdebugger");
      if (!dbg) {
        return;
      }

      const threadActorID = getSelectedTarget(state)?.threadFront?.actorID;
      if (!threadActorID) {
        return;
      }

      dbg.selectThread(threadActorID);
    }
  };

  /**
   * Called on each new THREAD_STATE resource
   *
   * @param {object} resource The THREAD_STATE resource
   */
  #onThreadStateChanged(resource) {
    if (resource.state == "paused") {
      this.#onTargetPaused(resource.targetFront, resource.why.type);
    } else if (resource.state == "resumed") {
      this.#onTargetResumed(resource.targetFront);
    }
  }

  /**
   * This listener is called by TracerCommand, sooner than the JSTRACER_STATE resource.
   * This is called when the frontend toggles the tracer, before the server started interpreting the request.
   * This allows to open the console before we start receiving traces.
   */
  async onTracerToggled() {
    const { tracerCommand } = this.commands;
    if (!tracerCommand.isTracingEnabled) {
      return;
    }
    const { logMethod } = this.commands.tracerCommand.getTracingOptions();
    if (
      logMethod == TRACER_LOG_METHODS.CONSOLE &&
      this.currentToolId !== "webconsole"
    ) {
      await this.openSplitConsole({ focusConsoleInput: false });
    } else if (logMethod == TRACER_LOG_METHODS.DEBUGGER_SIDEBAR) {
      const panel = await this.selectTool("jsdebugger");
      panel.showTracerSidebar();
    }
  }

  /**
   * Called on each new JSTRACER_STATE resource
   *
   * @param {object} resource The JSTRACER_STATE resource
   */
  async #onTracingStateChanged(resource) {
    const { profile } = resource;
    if (!profile) {
      return;
    }
    const browser = await openProfilerTab({ defaultPanel: "stack-chart" });

    const profileCaptureResult = {
      type: "SUCCESS",
      profile,
    };
    ProfilerBackground.registerProfileCaptureForBrowser(
      browser,
      profileCaptureResult,
      null
    );
  }

  /**
   * Called whenever a given target got its execution paused.
   *
   * Be careful, this method is synchronous, but highlightTool, raise, selectTool
   * are all async.
   *
   * @param {TargetFront} targetFront
   * @param {string} reason
   *        Reason why the execution paused
   */
  #onTargetPaused(targetFront, reason) {
    // Suppress interrupted events by default because the thread is
    // paused/resumed a lot for various actions.
    if (reason === "interrupted") {
      return;
    }

    this.highlightTool("jsdebugger");

    if (
      reason === "debuggerStatement" ||
      reason === "mutationBreakpoint" ||
      reason === "eventBreakpoint" ||
      reason === "breakpoint" ||
      reason === "exception" ||
      reason === "resumeLimit" ||
      reason === "XHR" ||
      reason === "breakpointConditionThrown"
    ) {
      this.raise();
      this.selectTool("jsdebugger", reason);
      // Each Target/Thread can be paused only once at a time,
      // so, for each pause, we should have a related resumed event.
      // But we may have multiple targets paused at the same time
      this.#pausedTargets.add(targetFront);
      this.emit("toolbox-paused");
    }
  }

  /**
   * Called whenever a given target got its execution resumed.
   *
   * @param {TargetFront} targetFront
   */
  #onTargetResumed(targetFront) {
    if (this.isHighlighted("jsdebugger")) {
      this.#pausedTargets.delete(targetFront);
      if (this.#pausedTargets.size == 0) {
        this.emit("toolbox-resumed");
        this.unhighlightTool("jsdebugger");
      }
    }
  }

  /**
   * This method will be called for the top-level target, as well as any potential
   * additional targets we may care about.
   */
  #onTargetAvailable = async ({ targetFront, isTargetSwitching }) => {
    if (targetFront.isTopLevel) {
      // Attach to a new top-level target.
      // For now, register these event listeners only on the top level target
      if (!targetFront.targetForm.ignoreSubFrames) {
        targetFront.on("frame-update", this.#updateFrames);
      }
      const consoleFront = await targetFront.getFront("console");
      consoleFront.on("inspectObject", this.#onInspectObject);
    }

    // Walker listeners allow to monitor DOM Mutation breakpoint updates.
    // All targets should be monitored.
    targetFront.watchFronts("inspector", async inspectorFront => {
      registerWalkerListeners(this.store, inspectorFront.walker);
    });

    if (targetFront.isTopLevel && isTargetSwitching) {
      // These methods expect the target to be attached, which is guaranteed by the time
      // #onTargetAvailable is called by the targetCommand.
      await this.#listFrames();
      // The target may have been destroyed while calling #listFrames if we navigate quickly
      if (targetFront.isDestroyed()) {
        return;
      }
    }

    if (targetFront.targetForm.ignoreSubFrames) {
      this.#updateFrames({
        frames: [
          {
            id: targetFront.actorID,
            targetFront,
            url: targetFront.url,
            title: targetFront.title,
            isTopLevel: targetFront.isTopLevel,
          },
        ],
      });
    }

    // If a new popup is debugged, automagically switch the toolbox to become
    // an independant window so that we can easily keep debugging the new tab.
    // Only do that if that's not the current top level, otherwise it means
    // we opened a toolbox dedicated to the popup.
    if (
      targetFront.targetForm.isPopup &&
      !targetFront.isTopLevel &&
      this.#descriptorFront.isLocalTab
    ) {
      await this.switchHostToTab(targetFront.targetForm.browsingContextID);
    }
  };

  #onTargetSelected = async ({ targetFront }) => {
    this.#updateFrames({ selected: targetFront.actorID });
    this.selectTarget(targetFront.actorID);
    this.#refreshHostTitle();
  };

  #onTargetDestroyed = ({ targetFront }) => {
    removeTarget(this.store, targetFront);

    if (targetFront.isTopLevel) {
      const consoleFront = targetFront.getCachedFront("console");
      // If the target has already been destroyed, its console front will
      // also already be destroyed and so we won't be able to retrieve it.
      // Nor is it important to clear its listener as fronts automatically clears
      // all their listeners on destroy.
      if (consoleFront) {
        consoleFront.off("inspectObject", this.#onInspectObject);
      }
      targetFront.off("frame-update", this.#updateFrames);
    } else if (this.selection) {
      this.selection.onTargetDestroyed(targetFront);
    }

    // When navigating the old (top level) target can get destroyed before the thread state changed
    // event for the target is received, so it gets lost. This currently happens with bf-cache
    // navigations when paused, so lets make sure we resumed if not.
    //
    // We should also resume if a paused non-top-level target is destroyed
    if (targetFront.isTopLevel || this.#pausedTargets.has(targetFront)) {
      this.#onTargetResumed(targetFront);
    }

    if (targetFront.targetForm.ignoreSubFrames) {
      this.#updateFrames({
        frames: [
          {
            // The Target Front may already be destroyed and `actorID` be null.
            id: targetFront.persistedActorID,
            destroy: true,
          },
        ],
      });
    }
  };

  #onTargetThreadFrontResumeWrongOrder = () => {
    const box = this.getNotificationBox();
    box.appendNotification(
      L10N.getStr("toolbox.resumeOrderWarning"),
      "wrong-resume-order",
      "",
      box.PRIORITY_WARNING_HIGH
    );
  };

  /**
   * Open the toolbox
   */
  async open() {
    try {
      const isToolboxURL = this.win.location.href.startsWith(this.#URL);
      if (isToolboxURL) {
        // Update the URL so that onceDOMReady watch for the right url.
        this.#URL = this.win.location.href;
      }

      // Mount toolbox React components and update all its state that can be updated synchronously.
      this.onReactLoaded = this.#initializeReactComponent();

      this.commands.targetCommand.on(
        "target-thread-wrong-order-on-resume",
        this.#onTargetThreadFrontResumeWrongOrder
      );
      this.commands.targetCommand.on(
        "target-location-updated",
        this.#onTargetLocationUpdated.bind(this)
      );
      registerStoreObserver(
        this.commands.targetCommand.store,
        this.#onTargetCommandStateChange
      );

      // Optimization: fire up a few other things before waiting on
      // the iframe being ready (makes startup faster)
      await this.commands.targetCommand.startListening();

      // Transfer settings early, before watching resources as it may impact them.
      // (this is the case for custom formatter pref and console messages)
      await this.#listenAndApplyConfigurationPref();

      // The targetCommand is created right before this code.
      // It means that this call to watchTargets is the first,
      // and we are registering the first target listener, which means
      // Toolbox.#onTargetAvailable will be called first, before any other
      // onTargetAvailable listener that might be registered on targetCommand.
      await this.commands.targetCommand.watchTargets({
        types: this.commands.targetCommand.ALL_TYPES,
        onAvailable: this.#onTargetAvailable,
        onSelected: this.#onTargetSelected,
        onDestroyed: this.#onTargetDestroyed,
      });

      const watchedResources = [
        // Watch for console API messages, errors and network events in order to populate
        // the error count icon in the toolbox.
        this.commands.resourceCommand.TYPES.CONSOLE_MESSAGE,
        this.commands.resourceCommand.TYPES.ERROR_MESSAGE,
        this.commands.resourceCommand.TYPES.DOCUMENT_EVENT,
        this.commands.resourceCommand.TYPES.THREAD_STATE,
      ];

      let tracerInitialization;
      if (
        Services.prefs.getBoolPref(
          "devtools.command-button-jstracer.enabled",
          false
        )
      ) {
        watchedResources.push(
          this.commands.resourceCommand.TYPES.JSTRACER_STATE
        );
        tracerInitialization = this.commands.tracerCommand.initialize();
        this.onTracerToggled = this.onTracerToggled.bind(this);
        this.commands.tracerCommand.on("toggle", this.onTracerToggled);
      }

      if (!this.isBrowserToolbox) {
        // Independently of watching network event resources for the error count icon,
        // we need to start tracking network activity on toolbox open for targets such
        // as tabs, in order to ensure there is always at least one listener existing
        // for network events across the lifetime of the various panels, so stopping
        // the resource command from clearing out its cache of network event resources.
        watchedResources.push(
          this.commands.resourceCommand.TYPES.NETWORK_EVENT
        );
      }

      const onResourcesWatched = this.commands.resourceCommand.watchResources(
        watchedResources,
        {
          onAvailable: this.#onResourceAvailable,
          onUpdated: this.#onResourceUpdated,
        }
      );

      await this.onReactLoaded;

      this.isReady = true;

      const framesPromise = this.#listFrames();

      Services.prefs.addObserver(
        BROWSERTOOLBOX_SCOPE_PREF,
        this.#refreshHostTitle
      );

      this.#buildDockOptions();
      this.#buildInitialPanelDefinitions();
      this.#setDebugTargetData();

      this.#addChromeEventHandlerEvents();

      // Get the tab bar of the ToolboxController to attach the "keypress" event listener to.
      this.#tabBar = this.doc.querySelector(".devtools-tabbar");
      this.#tabBar.addEventListener("keypress", this.#onToolbarArrowKeypress);

      this.#componentMount.setAttribute(
        "aria-label",
        L10N.getStr("toolbox.label")
      );

      this.webconsolePanel = this.doc.querySelector(
        "#toolbox-panel-webconsole"
      );
      this.doc
        .getElementById("toolbox-console-splitter")
        .addEventListener("command", this.#saveSplitConsoleHeight);

      this.#buildButtons();

      this.#pingTelemetry();

      // The isToolSupported check needs to happen after the target is
      // remoted, otherwise we could have done it in the toolbox constructor
      // (bug 1072764).
      const toolDef = gDevTools.getToolDefinition(this.#defaultToolId);
      if (!toolDef || !toolDef.isToolSupported(this)) {
        this.#defaultToolId = "webconsole";
      }

      // Update all ToolboxController state that can only be done asynchronously
      await this.#setInitialMeatballState();

      // Start rendering the toolbox toolbar before selecting the tool, as the tools
      // can take a few hundred milliseconds seconds to start up.
      //
      // Delay React rendering as Toolbox.open is synchronous.
      // Even if this involve promises, it is synchronous. Toolbox.open already loads
      // react modules and freeze the event loop for a significant time.
      // requestIdleCallback allows releasing it to allow user events to be processed.
      // Use 16ms maximum delay to allow one frame to be rendered at 60FPS
      // (1000ms/60FPS=16ms)
      this.win.requestIdleCallback(
        () => {
          this.component.setCanRender();
        },
        { timeout: 16 }
      );

      await this.selectTool(
        this.#defaultToolId,
        "initial_panel",
        this.#defaultToolOptions
      );

      // Wait until the original tool is selected so that the split
      // console input will receive focus.
      let splitConsolePromise = Promise.resolve();
      if (Services.prefs.getBoolPref(SPLITCONSOLE_OPEN_PREF)) {
        splitConsolePromise = this.openSplitConsole();
        this.telemetry.addEventProperty(
          this.topWindow,
          "open",
          "tools",
          null,
          "splitconsole",
          true
        );
      } else {
        this.telemetry.addEventProperty(
          this.topWindow,
          "open",
          "tools",
          null,
          "splitconsole",
          false
        );
      }

      await Promise.all([
        splitConsolePromise,
        framesPromise,
        onResourcesWatched,
        tracerInitialization,
      ]);

      // We do not expect the focus to be restored when using about:debugging toolboxes
      // Otherwise, when reloading the toolbox, the debugged tab will be focused.
      if (this.hostType !== Toolbox.HostType.PAGE) {
        // Request the actor to restore the focus to the content page once the
        // target is detached. This typically happens when the console closes.
        // We restore the focus as it may have been stolen by the console input.
        await this.commands.targetConfigurationCommand.updateConfiguration({
          restoreFocus: true,
        });
      }

      await this.initHarAutomation();
      // Local Mode mappings only work with local tab debugging.
      // It sounds irrelevant for workers/add-ons, and would require
      // some very specific work to be functional for remote tabs debugging.
      if (this.#descriptorFront.isLocalTab) {
        await lazy.LocalModeMappings.setup(this);
      }

      // The requestIdleCallback in this method may not have run yet, and
      // consumers expect a usable toolbar once the toolbox is open.
      this.component?.setCanRender();

      this.emit("ready");
      this.#resolveIsOpen();
    } catch (exception) {
      console.error(
        "Exception while opening the toolbox",
        String(exception),
        exception
      );
      // While the exception stack is correctly printed in the Browser console when
      // passing `e` to console.error, it is not on the stdout, so print it via dump.
      dump(exception.stack + "\n");
      if (exception.clientPacket) {
        dump(
          "Client packet:" +
            JSON.stringify(exception.clientPacket, null, 2) +
            "\n"
        );
      }
      if (exception.serverPacket) {
        dump(
          "Server packet:" +
            JSON.stringify(exception.serverPacket, null, 2) +
            "\n"
        );
      }

      try {
        // React may not be fully loaded yet and still waiting for Fluent or toolbox.xhtml document load.
        // Wait for it in order to have a functional AppErrorBoundary
        await this.onReactLoaded;

        // If React managed to load, try to display the exception to the user via AppErrorBoundary component.
        // But ignore the exception if the React component itself thrown while rendering (errorInfo is defined)
        if (this.#appBoundary && !this.#appBoundary.state.errorInfo) {
          this.#appBoundary.handleException(exception, this, true);
        }
      } catch (e) {
        // Ignore any further error related to AppErrorBoundary as it would prevent closing the toolbox.
        // The exception was already logged to stdout.
      }
    }
  }

  /**
   * Retrieve the ChromeEventHandler associated to the toolbox frame.
   * When DevTools are loaded in a content frame, this will return the containing chrome
   * frame. Events from nested frames will bubble up to this chrome frame, which allows to
   * listen to events from nested frames.
   */
  getChromeEventHandler() {
    if (!this.win || !this.win.docShell) {
      return null;
    }
    return this.win.docShell.chromeEventHandler;
  }

  /**
   * Attach events on the chromeEventHandler for the current window. When loaded in a
   * frame with type set to "content", events will not bubble across frames. The
   * chromeEventHandler does not have this limitation and will catch all events triggered
   * on any of the frames under the devtools document.
   *
   * Events relying on the chromeEventHandler need to be added and removed at specific
   * moments in the lifecycle of the toolbox, so all the events relying on it should be
   * grouped here.
   */
  #addChromeEventHandlerEvents() {
    // win.docShell.chromeEventHandler might not be accessible anymore when removing the
    // events, so we can't rely on a dynamic getter here.
    // Keep a reference on the chromeEventHandler used to addEventListener to be sure we
    // can remove the listeners afterwards.
    this.#chromeEventHandler = this.getChromeEventHandler();
    if (!this.#chromeEventHandler) {
      return;
    }

    // Add shortcuts and window-host-shortcuts that use the ChromeEventHandler as target.
    this.#addShortcuts();
    this.#addWindowHostShortcuts();

    // We want to have both keydown and keypress: the split console should be toggled
    // after an Escape keypress, but we might want to prevent the event to be fired
    // if the current panel's `shouldPreventSplitConsoleToggle` needs to handle
    // the Escape key before that. For example, if we have opened popover in a panel,
    // the keypress event happens too late and the popover is already dismissed,
    // so we can't check if we should toggle the split console or not.
    this.#chromeEventHandler.addEventListener("keydown", this.#onKeydown);
    this.#chromeEventHandler.addEventListener(
      "keypress",
      this.#splitConsoleOnKeypress
    );
    this.#chromeEventHandler.addEventListener("focus", this.#onFocus, true);
    this.#chromeEventHandler.addEventListener("blur", this.#onBlur, true);
    this.#chromeEventHandler.addEventListener(
      "contextmenu",
      this.#onContextMenu
    );
    this.#chromeEventHandler.addEventListener("mousedown", this.#onMouseDown);
  }

  #removeChromeEventHandlerEvents() {
    if (!this.#chromeEventHandler) {
      return;
    }

    // Remove shortcuts and window-host-shortcuts that use the ChromeEventHandler as
    // target.
    this.#removeShortcuts();
    this.#removeWindowHostShortcuts();

    this.#chromeEventHandler.removeEventListener(
      "keypress",
      this.#splitConsoleOnKeypress
    );
    this.#chromeEventHandler.removeEventListener("keydown", this.#onKeydown);
    this.#chromeEventHandler.removeEventListener("focus", this.#onFocus, true);
    this.#chromeEventHandler.removeEventListener("focus", this.#onBlur, true);
    this.#chromeEventHandler.removeEventListener(
      "contextmenu",
      this.#onContextMenu
    );
    this.#chromeEventHandler.removeEventListener(
      "mousedown",
      this.#onMouseDown
    );

    this.#chromeEventHandler = null;
  }

  #addShortcuts() {
    // Create shortcuts instance for the toolbox
    if (!this.shortcuts) {
      this.shortcuts = new KeyShortcuts(
        // The toolbox key shortcuts should be triggered from any frame in DevTools.
        // Use the chromeEventHandler as the target to catch events from all frames.
        this.getChromeEventHandler()
      );
    }

    // Listen for the shortcut key to show the frame list
    this.shortcuts.on(L10N.getStr("toolbox.showFrames.key"), event => {
      if (event.target.id === "command-button-frames") {
        event.target.click();
      }
    });

    // Listen for tool navigation shortcuts.
    this.shortcuts.on(L10N.getStr("toolbox.nextTool.key"), event => {
      this.selectNextTool();
      event.preventDefault();
    });
    this.shortcuts.on(L10N.getStr("toolbox.previousTool.key"), event => {
      this.selectPreviousTool();
      event.preventDefault();
    });
    this.shortcuts.on(L10N.getStr("toolbox.toggleHost.key"), event => {
      this.switchToPreviousHost();
      event.preventDefault();
    });

    // List for Help/Settings key.
    this.shortcuts.on(L10N.getStr("toolbox.help.key"), this.toggleOptions);

    if (!this.isBrowserToolbox) {
      // Listen for Reload shortcuts
      [
        ["reload", false],
        ["reload2", false],
        ["forceReload", true],
        ["forceReload2", true],
      ].forEach(([id, bypassCache]) => {
        const key = L10N.getStr("toolbox." + id + ".key");
        this.shortcuts.on(key, event => {
          this.reload(bypassCache);

          // Prevent Firefox shortcuts from reloading the page
          event.preventDefault();
        });
      });
    }

    // Add zoom-related shortcuts.
    if (this.hostType != Toolbox.HostType.PAGE) {
      // When the toolbox is rendered in a tab (ie host type is PAGE), the
      // zoom should be handled by the default browser shortcuts.
      ZoomKeys.register(this.win, this.shortcuts);
    }
  }

  /**
   * Reload the debugged context.
   *
   * @param {boolean} bypassCache
   *        If true, bypass any cache when reloading.
   */
  async reload(bypassCache) {
    const box = this.getNotificationBox();
    const notification = box.getNotificationWithValue("reload-error");
    if (notification) {
      notification.close();
    }

    // When reloading a Web Extension, the top level target isn't destroyed.
    // Which prevents some panels (like console and netmonitor) from being correctly cleared.
    const consolePanel = this.getPanel("webconsole");
    if (consolePanel) {
      // Navigation to a null URL will be translated into a reload message
      // when persist log is enabled.
      consolePanel.hud.ui.handleWillNavigate({
        timeStamp: new Date(),
        url: null,
      });
    }
    const netPanel = this.getPanel("netmonitor");
    if (netPanel) {
      // Fake a navigation, which will clear the netmonitor, if persists is disabled.
      netPanel.panelWin.connector.willNavigate();
    }

    try {
      await this.commands.targetCommand.reloadTopLevelTarget(bypassCache);
    } catch (e) {
      let { message } = e;

      // Remove Protocol.JS exception header to focus on the likely manifest error
      message = message.replace("Protocol error (SyntaxError):", "");

      box.appendNotification(
        L10N.getFormatStr("toolbox.errorOnReload", message),
        "reload-error",
        "",
        box.PRIORITY_CRITICAL_HIGH
      );
    }
  }

  #removeShortcuts() {
    if (this.shortcuts) {
      this.shortcuts.destroy();
      this.shortcuts = null;
    }
  }

  /**
   * Adds the keys and commands to the Toolbox Window in window mode.
   */
  #addWindowHostShortcuts() {
    if (this.hostType != Toolbox.HostType.WINDOW) {
      // Those shortcuts are only valid for host type WINDOW.
      return;
    }

    if (!this.#windowHostShortcuts) {
      this.#windowHostShortcuts = new KeyShortcuts(
        // The window host key shortcuts should be triggered from any frame in DevTools.
        // Use the chromeEventHandler as the target to catch events from all frames.
        this.getChromeEventHandler()
      );
    }

    const shortcuts = this.#windowHostShortcuts;

    for (const item of Startup.KeyShortcuts) {
      const { id, toolId, shortcut, modifiers } = item;
      const electronKey = KeyShortcuts.parseXulKey(modifiers, shortcut);

      if (id == "browserConsole") {
        // Add key for toggling the browser console from the detached window
        shortcuts.on(electronKey, () => {
          BrowserConsoleManager.toggleBrowserConsole();
        });
      } else if (toolId) {
        // KeyShortcuts contain tool-specific and global key shortcuts,
        // here we only need to copy shortcut specific to each tool.
        shortcuts.on(electronKey, () => {
          this.selectTool(toolId, "key_shortcut").then(() =>
            this.fireCustomKey(toolId)
          );
        });
      }
    }

    // CmdOrCtrl+W is registered only when the toolbox is running in
    // detached window. In the other case the entire browser tab
    // is closed when the user uses this shortcut.
    shortcuts.on(L10N.getStr("toolbox.closeToolbox.key"), this.closeToolbox);

    // The others are only registered in window host type as for other hosts,
    // these keys are already registered by devtools-startup.js
    shortcuts.on(
      L10N.getStr("toolbox.toggleToolboxF12.key"),
      this.closeToolbox
    );
    if (lazy.AppConstants.platform == "macosx") {
      shortcuts.on(
        L10N.getStr("toolbox.toggleToolboxOSX.key"),
        this.closeToolbox
      );
    } else {
      shortcuts.on(L10N.getStr("toolbox.toggleToolbox.key"), this.closeToolbox);
    }
  }

  #removeWindowHostShortcuts() {
    if (this.#windowHostShortcuts) {
      this.#windowHostShortcuts.destroy();
      this.#windowHostShortcuts = null;
    }
  }

  #onContextMenu = e => {
    // Handle context menu events in standard input elements: <input> and <textarea>.
    // Also support for custom input elements using .devtools-input class
    // (e.g. CodeMirror instances).
    const isInInput =
      e.composedTarget.matches(
        "input:is([type=text], [type=search], :not([type])), textarea"
      ) || e.composedTarget.closest(".devtools-input");

    const doc = e.originalTarget.ownerDocument;
    const isHTMLPanel = doc.documentElement.namespaceURI === HTML_NS;

    if (
      // Context-menu events on input elements will use a custom context menu.
      isInInput ||
      // Context-menu events from HTML panels should not trigger the default
      // browser context menu for HTML documents.
      isHTMLPanel
    ) {
      e.stopPropagation();
      e.preventDefault();
    }

    if (isInInput) {
      this.openTextBoxContextMenu(e.screenX, e.screenY);
    }
  };

  #onMouseDown = e => {
    const isMiddleClick = e.button === 1;
    if (isMiddleClick) {
      // Middle clicks will trigger the scroll lock feature to turn on.
      // When the DevTools toolbox was running in an <iframe>, this behavior was
      // disabled by default. When running in a <browser> element, we now need
      // to catch and preventDefault() on those events.
      e.preventDefault();
    }
  };

  #getDebugTargetData() {
    const url = URL.parse(this.win.location);
    const remoteId = url ? url.searchParams.get("remoteId") : null;
    const runtimeInfo = remoteClientManager.getRuntimeInfoByRemoteId(remoteId);
    const connectionType =
      remoteClientManager.getConnectionTypeByRemoteId(remoteId);

    return {
      connectionType,
      runtimeInfo,
      descriptorType: this.#descriptorFront.descriptorType,
      descriptorName: this.#descriptorFront.name,
    };
  }

  isDebugTargetFenix() {
    return this.#getDebugTargetData()?.runtimeInfo?.isFenix;
  }

  /**
   * loading React modules when needed (to avoid performance penalties
   * during Firefox start up time).
   */
  get React() {
    return this.browserRequire("devtools/client/shared/vendor/react");
  }

  get ReactDOM() {
    return this.browserRequire("devtools/client/shared/vendor/react-dom");
  }

  get ReactRedux() {
    return this.browserRequire("devtools/client/shared/vendor/react-redux");
  }

  get ToolboxController() {
    return this.browserRequire(
      "devtools/client/framework/components/ToolboxController"
    );
  }

  get AppErrorBoundary() {
    return this.browserRequire(
      "resource://devtools/client/shared/components/AppErrorBoundary.js"
    );
  }

  /**
   * A common access point for the client-side mapping service for source maps that
   * any panel can use.  This is a "low-level" API that connects to
   * the source map worker.
   */
  get sourceMapLoader() {
    if (this.#sourceMapLoader) {
      return this.#sourceMapLoader;
    }
    this.#sourceMapLoader = new SourceMapLoader(this.commands.targetCommand);
    return this.#sourceMapLoader;
  }

  /**
   * Expose the "Parser" debugger worker to both webconsole and debugger.
   *
   * Note that the Browser Console will also self-instantiate it as it doesn't involve a toolbox.
   */
  get parserWorker() {
    if (this.#parserWorker) {
      return this.#parserWorker;
    }

    const {
      ParserDispatcher,
    } = require("resource://devtools/client/debugger/src/workers/parser/index.js");

    this.#parserWorker = new ParserDispatcher();
    return this.#parserWorker;
  }

  /**
   * Clients wishing to use source maps but that want the toolbox to
   * track the source and style sheet actor mapping can use this
   * source map service.  This is a higher-level service than the one
   * returned by |sourceMapLoader|, in that it automatically tracks
   * source and style sheet actor IDs.
   */
  get sourceMapURLService() {
    if (this.#sourceMapURLService) {
      return this.#sourceMapURLService;
    }
    this.#sourceMapURLService = new SourceMapURLService(
      this.commands,
      this.sourceMapLoader
    );
    return this.#sourceMapURLService;
  }

  // Return HostType id for telemetry
  #getTelemetryHostId() {
    switch (this.hostType) {
      case Toolbox.HostType.BOTTOM:
        return 0;
      case Toolbox.HostType.RIGHT:
        return 1;
      case Toolbox.HostType.WINDOW:
        return 2;
      case Toolbox.HostType.BROWSERTOOLBOX:
        return 3;
      case Toolbox.HostType.LEFT:
        return 4;
      case Toolbox.HostType.PAGE:
        return 5;
      default:
        return 9;
    }
  }

  // Return HostType string for telemetry
  #getTelemetryHostString() {
    switch (this.hostType) {
      case Toolbox.HostType.BOTTOM:
        return "bottom";
      case Toolbox.HostType.LEFT:
        return "left";
      case Toolbox.HostType.RIGHT:
        return "right";
      case Toolbox.HostType.WINDOW:
        return "window";
      case Toolbox.HostType.PAGE:
        return "page";
      case Toolbox.HostType.BROWSERTOOLBOX:
        return "other";
      default:
        return "bottom";
    }
  }

  #pingTelemetry() {
    Services.prefs.setBoolPref("devtools.everOpened", true);
    this.telemetry.toolOpened("toolbox", this);

    Glean.devtools.toolboxHost.accumulateSingleSample(
      this.#getTelemetryHostId()
    );

    // Log current theme. The question we want to answer is:
    // "What proportion of users use which themes?"
    const currentTheme = Services.prefs.getCharPref("devtools.theme");
    Glean.devtools.currentTheme[currentTheme].add(1);

    const browserWin = this.topWindow;
    this.telemetry.preparePendingEvent(browserWin, "open", "tools", null, [
      "entrypoint",
      "first_panel",
      "host",
      "shortcut",
      "splitconsole",
      "width",
    ]);
    this.telemetry.addEventProperty(
      browserWin,
      "open",
      "tools",
      null,
      "host",
      this.#getTelemetryHostString()
    );
  }

  /**
   * Create a simple object to store the state of a toolbox button. The checked state of
   * a button can be updated arbitrarily outside of the scope of the toolbar and its
   * controllers. In order to simplify this interaction this object emits an
   * "updatechecked" event any time the isChecked value is updated, allowing any consuming
   * components to listen and respond to updates.
   *
   * @param {object} options:
   *
   * @property {string} id - The id of the button or command.
   * @property {string} className - An optional additional className for the button.
   * @property {string} description - The value that will display as a tooltip and in
   *                    the options panel for enabling/disabling.
   * @property {boolean} disabled - An optional disabled state for the button.
   * @property {Function} onClick - The function to run when the button is activated by
   *                      click or keyboard shortcut. First argument will be the 'click'
   *                      event, and second argument is the toolbox instance.
   * @property {boolean} isInStartContainer - Buttons can either be placed at the start
   *                     of the toolbar, or at the end.
   * @property {Function} setup - Function run immediately to listen for events changing
   *                      whenever the button is checked or unchecked. The toolbox object
   *                      is passed as first argument and a callback is passed as second
   *                       argument, to be called whenever the checked state changes.
   * @property {Function} teardown - Function run on toolbox close to let a chance to
   *                      unregister listeners set when `setup` was called and avoid
   *                      memory leaks. The same arguments than `setup` function are
   *                      passed to `teardown`.
   * @property {Function} isToolSupported - Function to automatically enable/disable
   *                      the button based on the toolbox. If the toolbox don't support
   *                      the button feature, this method should return false.
   * @property {Function} isCurrentlyVisible - Function to automatically
   *                      hide/show the button based on current state.
   * @property {Function} isChecked - Optional function called to known if the button
   *                      is toggled or not. The function should return true when
   *                      the button should be displayed as toggled on.
   */
  #createButtonState(options) {
    let isCheckedValue = false;
    const {
      id,
      className,
      description,
      disabled,
      onClick,
      isInStartContainer,
      setup,
      teardown,
      isToolSupported,
      isCurrentlyVisible,
      isChecked,
      isToggle,
      onKeyDown,
      experimentalURL,
      highlighterTypes,
    } = options;
    const toolbox = this;
    const button = {
      id,
      className,
      description,
      disabled,
      async onClick(event) {
        if (typeof onClick == "function") {
          await onClick(event, toolbox);
          button.emit("updatechecked");
        }
      },
      onKeyDown(event) {
        if (typeof onKeyDown == "function") {
          onKeyDown(event, toolbox);
        }
      },
      isToolSupported,
      isCurrentlyVisible,
      get isChecked() {
        if (typeof isChecked == "function") {
          return isChecked(toolbox);
        }
        return isCheckedValue;
      },
      set isChecked(value) {
        // Note that if options.isChecked is given, this is ignored
        isCheckedValue = value;
        this.emit("updatechecked");
      },
      isToggle,
      // The preference for having this button visible.
      visibilityswitch: `devtools.${id}.enabled`,
      // The toolbar has a container at the start and end of the toolbar for
      // holding buttons. By default the buttons are placed in the end container.
      isInStartContainer: !!isInStartContainer,
      experimentalURL,
      highlighterTypes,
      getContextMenu() {
        if (options.getContextMenu) {
          return options.getContextMenu(toolbox);
        }
        return null;
      },
    };
    if (typeof setup == "function") {
      // Use async function as tracer's definition requires an async function to be passed
      // for "toggle" event listener.
      const onChange = async () => {
        button.emit("updatechecked");
      };
      setup(this, onChange);
      // Save a reference to the cleanup method that will unregister the onChange
      // callback. Immediately bind the function argument so that we don't have to
      // also save a reference to them.
      button.teardown = teardown.bind(options, this, onChange);
    }
    button.isVisible = this.#commandIsVisible(button);

    EventEmitter.decorate(button);

    return button;
  }

  #onKeydown = e => {
    if (e.keyCode !== KeyCodes.DOM_VK_ESCAPE) {
      return;
    }

    const currentPanel = this.getCurrentPanel();
    // Allow the current panel to prevent the split console from being toggled.
    if (typeof currentPanel.shouldPreventSplitConsoleToggle === "function") {
      if (currentPanel.shouldPreventSplitConsoleToggle()) {
        // this prevents the `keypress` event to be dispatched.
        e.preventDefault();
      }
    }
  };

  #splitConsoleOnKeypress = e => {
    if (e.keyCode !== KeyCodes.DOM_VK_ESCAPE || !this.isSplitConsoleEnabled()) {
      return;
    }

    this.toggleSplitConsole();
    // If the debugger is paused, don't let the ESC key stop any pending navigation.
    // If the host is page, don't let the ESC stop the load of the webconsole frame.
    if (
      this.threadFront.state == "paused" ||
      this.hostType === Toolbox.HostType.PAGE
    ) {
      e.preventDefault();
    }
  };

  /**
   * Add a shortcut key that should work when a split console
   * has focus to the toolbox.
   *
   * @param {string} key
   *        The electron key shortcut.
   * @param {Function} handler
   *        The callback that should be called when the provided key shortcut is pressed.
   * @param {string} whichTool
   *        The tool the key belongs to. The corresponding handler will only be triggered
   *        if this tool is active.
   */
  useKeyWithSplitConsole(key, handler, whichTool) {
    this.shortcuts.on(key, event => {
      if (this.currentToolId === whichTool && this.isSplitConsoleFocused()) {
        handler();
        event.preventDefault();
      }
    });
  }

  #addWindowListeners() {
    this.win.addEventListener("unload", this.destroy);
    this.win.addEventListener("message", this.#onBrowserMessage, true);
  }

  #removeWindowListeners() {
    // The host iframe's contentDocument may already be gone.
    if (this.win) {
      this.win.removeEventListener("unload", this.destroy);
      this.win.removeEventListener("message", this.#onBrowserMessage, true);
    }
  }

  // Called whenever the chrome send a message
  #onBrowserMessage = event => {
    if (event.data?.name === "switched-host") {
      this.#onSwitchedHost(event.data);
    }
    if (event.data?.name === "switched-host-to-tab") {
      this.#onSwitchedHostToTab(event.data.browsingContextID);
    }
    if (event.data?.name === "host-raised") {
      this.emit("host-raised");
    }
  };

  #saveSplitConsoleHeight = () => {
    const height = parseInt(this.webconsolePanel.style.height, 10);
    if (!isNaN(height)) {
      Services.prefs.setIntPref(SPLITCONSOLE_HEIGHT_PREF, height);
    }
  };

  /**
   * Make sure that the console is showing up properly based on all the
   * possible conditions.
   *   1) If the console tab is selected, then regardless of split state
   *      it should take up the full height of the deck, and we should
   *      hide the deck and splitter.
   *   2) If the console tab is not selected and it is split, then we should
   *      show the splitter, deck, and console.
   *   3) If the console tab is not selected and it is *not* split,
   *      then we should hide the console and splitter, and show the deck
   *      at full height.
   */
  #refreshConsoleDisplay() {
    const deck = this.doc.getElementById("toolbox-deck");
    const webconsolePanel = this.webconsolePanel;
    const splitter = this.doc.getElementById("toolbox-console-splitter");
    const openedConsolePanel = this.currentToolId === "webconsole";

    if (openedConsolePanel) {
      deck.setAttribute("hidden", "");
      deck.removeAttribute("expanded");
      splitter.hidden = true;
      webconsolePanel.removeAttribute("hidden");
      webconsolePanel.setAttribute("expanded", "");
    } else {
      deck.removeAttribute("hidden");
      deck.toggleAttribute("expanded", !this.splitConsole);
      splitter.hidden = !this.splitConsole;
      webconsolePanel.collapsed = !this.splitConsole;
      webconsolePanel.removeAttribute("expanded");
    }

    // Either restore the last known split console height, if in split console mode,
    // or ensure there is no height set to prevent shrinking the regular console.
    this.webconsolePanel.style.height = openedConsolePanel
      ? ""
      : Services.prefs.getIntPref(SPLITCONSOLE_HEIGHT_PREF) + "px";
  }

  /**
   * Handle any custom key events.  Returns true if there was a custom key
   * binding run.
   *
   * @param {string} toolId Which tool to run the command on (skip if not
   * current)
   */
  fireCustomKey(toolId) {
    const toolDefinition = gDevTools.getToolDefinition(toolId);

    if (
      toolDefinition.onkey &&
      (this.currentToolId === toolId ||
        (toolId == "webconsole" && this.splitConsole))
    ) {
      toolDefinition.onkey(this.getCurrentPanel(), this);
    }
  }

  /**
   * Build the notification box as soon as needed.
   */
  get notificationBox() {
    if (!this.#notificationBox) {
      let { NotificationBox, PriorityLevels } = this.browserRequire(
        "devtools/client/shared/components/NotificationBox"
      );

      NotificationBox = this.React.createFactory(NotificationBox);

      // Render NotificationBox and assign priority levels to it.
      const box = this.doc.getElementById("toolbox-notificationbox");
      this.#notificationBox = Object.assign(
        this.ReactDOM.render(NotificationBox({ wrapping: true }), box),
        PriorityLevels
      );
    }
    return this.#notificationBox;
  }

  /**
   * Build the options for changing hosts. Called every time
   * the host changes.
   */
  #buildDockOptions() {
    if (!this.#descriptorFront.isLocalTab) {
      this.component.setDockOptionsEnabled(false);
      this.component.setCanCloseToolbox(false);
      return;
    }

    this.component.setDockOptionsEnabled(true);
    this.component.setCanCloseToolbox(
      this.hostType !== Toolbox.HostType.WINDOW
    );

    const hostTypes = [];
    for (const type in Toolbox.HostType) {
      const position = Toolbox.HostType[type];
      if (
        position == Toolbox.HostType.BROWSERTOOLBOX ||
        position == Toolbox.HostType.PAGE
      ) {
        continue;
      }

      hostTypes.push({
        position,
        switchHost: this.switchHost.bind(this, position),
      });
    }

    this.component.setCurrentHostType(this.hostType);
    this.component.setHostTypes(hostTypes);
  }

  postMessage(msg) {
    // We sometime try to send messages in middle of destroy(), where the
    // toolbox iframe may already be detached.
    if (this.#destroyer) {
      return;
    }

    // Toolbox document is still chrome and disallow identifying message
    // origin via event.source as it is null. So use a custom id.
    msg.frameId = this.frameId;
    this.topWindow.postMessage(msg, "*");
  }

  /**
   * This will fetch the panel definitions from the constants in definitions module
   * and populate the state within the ToolboxController component.
   */
  async #buildInitialPanelDefinitions() {
    // Get the initial list of tab definitions. This list can be amended at a later time
    // by tools registering themselves.
    const definitions = gDevTools.getToolDefinitionArray();
    definitions.forEach(definition => this.#buildPanelForTool(definition));

    // Get the definitions that will only affect the main tab area.
    this.panelDefinitions = definitions.filter(
      definition =>
        definition.isToolSupported(this) && definition.id !== "options"
    );
  }

  async #setInitialMeatballState() {
    let disableAutohide, pseudoLocale;
    // Popup auto-hide disabling is only available in browser toolbox and webextension toolboxes.
    if (
      this.isBrowserToolbox ||
      this.#descriptorFront.isWebExtensionDescriptor
    ) {
      disableAutohide = await this.#isDisableAutohideEnabled();
    }
    // Pseudo locale items are only displayed in the browser toolbox
    if (this.isBrowserToolbox) {
      pseudoLocale = await this.getPseudoLocale();
    }
    // Parallelize the asynchronous calls, so that the DOM is only updated once when
    // updating the React components.
    if (typeof disableAutohide == "boolean") {
      this.component.setDisableAutohide(disableAutohide);
    }
    if (typeof pseudoLocale == "string") {
      this.component.setPseudoLocale(pseudoLocale);
    }
    if (
      this.#descriptorFront.isWebExtensionDescriptor &&
      this.hostType === Toolbox.HostType.WINDOW
    ) {
      const alwaysOnTop = Services.prefs.getBoolPref(
        DEVTOOLS_ALWAYS_ON_TOP,
        false
      );
      this.component.setAlwaysOnTop(alwaysOnTop);
    }
  }

  /**
   * Initiate toolbox React components and all it's properties. Do the initial render.
   */
  async #initializeReactComponent() {
    // Kick off async loading the Fluent bundles.
    const fluentL10n = new FluentL10n();
    const fluentInitPromise = fluentL10n.init(["devtools/client/toolbox.ftl"]);

    // To avoid any possible artifact, wait for the document to be fully loaded
    // before creating the Browser Loader based on toolbox window object.
    await new Promise(resolve => {
      DOMHelpers.onceDOMReady(
        this.win,
        () => {
          resolve();
        },
        this.#URL
      );
    });

    // We have to wait for the document to be loaded, otherwise we may receive
    // unexpected "unload" events.
    this.#addWindowListeners();

    // Setup the Toolbox Browser Loader, used to load React component modules
    // which expect to be loaded with toolbox.xhtml document as global scope.
    this.browserRequire = BrowserLoader({
      window: this.win,
      useOnlyShared: true,
    }).require;

    // Wait for the bundles to be ready to use
    await fluentInitPromise;
    const fluentBundles = fluentL10n.getBundles();

    // ToolboxController is wrapped into AppErrorBoundary in order to nicely
    // show any exception that may happen in React updates/renders.
    const element = this.React.createElement(
      this.AppErrorBoundary,
      {
        componentName: "General",
        panel: L10N.getStr("webDeveloperToolsMenu.label"),
      },
      this.React.createElement(this.ToolboxController, {
        ref: r => {
          this.component = r;
        },
        L10N,
        fluentBundles,
        currentToolId: this.currentToolId,
        selectTool: this.selectTool,
        toggleOptions: this.toggleOptions,
        toggleSplitConsole: this.toggleSplitConsole,
        toggleNoAutohide: this.toggleNoAutohide,
        toggleAlwaysOnTop: this.toggleAlwaysOnTop,
        disablePseudoLocale: this.disablePseudoLocale,
        enableAccentedPseudoLocale: this.enableAccentedPseudoLocale,
        enableBidiPseudoLocale: this.enableBidiPseudoLocale,
        closeToolbox: this.closeToolbox,
        focusButton: this.#onToolbarFocus,
        toolbox: this,
        onTabsOrderUpdated: this.#onTabsOrderUpdated,
      })
    );

    // Get the DOM element to mount the React components to.
    this.#componentMount = this.doc.getElementById("toolbox-toolbar-mount");
    this.#appBoundary = this.ReactDOM.render(element, this.#componentMount);
  }

  /**
   * Reset tabindex attributes across all focusable elements inside the toolbar.
   * Only have one element with tabindex=0 at a time to make sure that tabbing
   * results in navigating away from the toolbar container.
   *
   * @param  {FocusEvent} event
   */
  #onToolbarFocus = id => {
    this.component.setFocusedButton(id);
  };

  /**
   * On left/right arrow press, attempt to move the focus inside the toolbar to
   * the previous/next focusable element. This is not in the React component
   * as it is difficult to coordinate between different component elements.
   * The components are responsible for setting the correct tabindex value
   * for if they are the focused element.
   *
   * @param  {KeyboardEvent} event
   */
  #onToolbarArrowKeypress = event => {
    const { key, target, ctrlKey, shiftKey, altKey, metaKey } = event;

    // If any of the modifier keys are pressed do not attempt navigation as it
    // might conflict with global shortcuts (Bug 1327972).
    if (ctrlKey || shiftKey || altKey || metaKey) {
      return;
    }

    const buttons = [...this.#tabBar.querySelectorAll("button")];
    const curIndex = buttons.indexOf(target);

    if (curIndex === -1) {
      console.warn(
        target +
          " is not found among Developer Tools tab bar " +
          "focusable elements."
      );
      return;
    }

    let newTarget;
    const firstTabIndex = 0;
    const lastTabIndex = buttons.length - 1;
    const nextOrLastTabIndex = Math.min(lastTabIndex, curIndex + 1);
    const previousOrFirstTabIndex = Math.max(firstTabIndex, curIndex - 1);
    const ltr = this.direction === "ltr";

    if (key === "ArrowLeft") {
      // Do nothing if already at the beginning.
      if (
        (ltr && curIndex === firstTabIndex) ||
        (!ltr && curIndex === lastTabIndex)
      ) {
        return;
      }
      newTarget = buttons[ltr ? previousOrFirstTabIndex : nextOrLastTabIndex];
    } else if (key === "ArrowRight") {
      // Do nothing if already at the end.
      if (
        (ltr && curIndex === lastTabIndex) ||
        (!ltr && curIndex === firstTabIndex)
      ) {
        return;
      }
      newTarget = buttons[ltr ? nextOrLastTabIndex : previousOrFirstTabIndex];
    } else {
      return;
    }

    newTarget.focus();

    event.preventDefault();
    event.stopPropagation();
  };

  /**
   * Add buttons to the UI as specified in devtools/client/definitions.js
   */
  #buildButtons() {
    // Beyond the normal preference filtering
    this.toolbarButtons = [
      this.#buildErrorCountButton(),
      this.#buildPickerButton(),
      this.#buildFrameButton(),
    ];

    ToolboxButtons.forEach(definition => {
      const button = this.#createButtonState(definition);
      this.toolbarButtons.push(button);
    });

    this.#renderToolboxButtons();
  }

  /**
   * Button to select a frame for the inspector to target.
   */
  #buildFrameButton() {
    this.frameButton = this.#createButtonState({
      id: "command-button-frames",
      description: L10N.getStr("toolbox.frames.tooltip"),
      isToolSupported: toolbox => {
        return toolbox.target.getTrait("frames");
      },
      isCurrentlyVisible: () => {
        const hasFrames = this.frameMap.size > 1;
        const isOnOptionsPanel = this.currentToolId === "options";
        return hasFrames || isOnOptionsPanel;
      },
    });

    return this.frameButton;
  }

  /**
   * Button to display the number of errors.
   */
  #buildErrorCountButton() {
    this.errorCountButton = this.#createButtonState({
      id: "command-button-errorcount",
      isInStartContainer: false,
      isToolSupported: () => true,
      description: L10N.getStr("toolbox.errorCountButton.description"),
    });
    // Use updateErrorCountButton to set some properties so we don't have to repeat
    // the logic here.
    this.updateErrorCountButton();

    return this.errorCountButton;
  }

  /**
   * Toggle the picker, but also decide whether or not the highlighter should
   * focus the window. This is only desirable when the toolbox is mounted to the
   * window. When devtools is free floating, then the target window should not
   * pop in front of the viewer when the picker is clicked.
   *
   * Note: Toggle picker can be overwritten by panel other than the inspector to
   * allow for custom picker behaviour.
   */
  #onPickerClick = async () => {
    const focus =
      this.hostType === Toolbox.HostType.BOTTOM ||
      this.hostType === Toolbox.HostType.LEFT ||
      this.hostType === Toolbox.HostType.RIGHT;
    const currentPanel = this.getCurrentPanel();
    if (currentPanel.togglePicker) {
      currentPanel.togglePicker(focus);
    } else {
      this.nodePicker.togglePicker(focus);
    }
  };

  /**
   * If the picker is activated, then allow the Escape key to deactivate the
   * functionality instead of the default behavior of toggling the console.
   */
  #onPickerKeypress = event => {
    if (event.keyCode === KeyCodes.DOM_VK_ESCAPE) {
      const currentPanel = this.getCurrentPanel();
      if (currentPanel.cancelPicker) {
        currentPanel.cancelPicker();
      } else {
        this.nodePicker.stop({ canceled: true });
      }
      // Stop the console from toggling.
      event.stopImmediatePropagation();
    }
  };

  #onPickerStarting = async () => {
    if (this.isDestroying()) {
      return;
    }
    this.tellRDMAboutPickerState(true, PICKER_TYPES.ELEMENT);
    this.pickerButton.isChecked = true;
    await this.selectTool("inspector", "inspect_dom");
    // turn off color picker when node picker is starting
    this.getPanel("inspector").hideEyeDropper();
    this.on("select", this.#onToolSelectedStopPicker);
  };

  #onPickerStarted = async () => {
    this.doc.addEventListener("keypress", this.#onPickerKeypress, true);
  };

  #onPickerStopped = () => {
    if (this.isDestroying()) {
      return;
    }
    this.tellRDMAboutPickerState(false, PICKER_TYPES.ELEMENT);
    this.off("select", this.#onToolSelectedStopPicker);
    this.doc.removeEventListener("keypress", this.#onPickerKeypress, true);
    this.pickerButton.isChecked = false;
  };

  #onToolSelectedStopPicker = () => {
    this.nodePicker.stop({ canceled: true });
  };

  /**
   * When the picker is canceled, make sure the toolbox
   * gets the focus.
   */
  #onPickerCanceled = () => {
    if (this.hostType !== Toolbox.HostType.WINDOW) {
      this.win.focus();
    }
  };

  #onPickerPicked = nodeFront => {
    this.selection.setNodeFront(nodeFront, { reason: "picker-node-picked" });
  };

  #onPickerPreviewed = nodeFront => {
    this.selection.setNodeFront(nodeFront, { reason: "picker-node-previewed" });
  };

  /**
   * RDM sometimes simulates touch events. For this to work correctly at all times, it
   * needs to know when the picker is active or not.
   * This method communicates with the RDM Manager if it exists.
   *
   * @param {boolean} state
   * @param {string} pickerType
   *        One of devtools/shared/picker-constants
   */
  async tellRDMAboutPickerState(state, pickerType) {
    const { localTab } = this.commands.descriptorFront;

    if (!ResponsiveUIManager.isActiveForTab(localTab)) {
      return;
    }

    const ui = ResponsiveUIManager.getResponsiveUIForTab(localTab);
    await ui.setElementPickerState(state, pickerType);
  }

  /**
   * Launches the responsive mode with a specific width or height.
   *
   * @param  {object} options
   *         Object with width or/and height properties.
   */
  async launchResponsiveMode(options = {}) {
    const tab = this.commands.descriptorFront.localTab;
    const win = tab.ownerDocument.defaultView;

    await ResponsiveUIManager.openIfNeeded(win, tab, {
      trigger: "debugger",
    });
    this.emit("responsive-mode-opened");

    ResponsiveUIManager.getResponsiveUIForTab(tab).setViewportSize(options);
  }

  /**
   * The element picker button enables the ability to select a DOM node by clicking
   * it on the page.
   */
  #buildPickerButton() {
    this.pickerButton = this.#createButtonState({
      id: "command-button-pick",
      className: this.#getPickerAdditionalClassName(),
      description: this.#getPickerTooltip(),
      onClick: this.#onPickerClick,
      isInStartContainer: true,
      isToolSupported: toolbox => {
        return toolbox.target.getTrait("frames");
      },
      isToggle: true,
    });

    return this.pickerButton;
  }

  #getPickerAdditionalClassName() {
    if (this.isDebugTargetFenix()) {
      return "remote-fenix";
    }
    return null;
  }

  /**
   * Get the tooltip for the element picker button.
   * It has multiple possible keyboard shortcuts for macOS.
   *
   * @return {string}
   */
  #getPickerTooltip() {
    let shortcut = L10N.getStr("toolbox.elementPicker.key");
    shortcut = KeyShortcuts.parseElectronKey(shortcut);
    shortcut = KeyShortcuts.stringifyShortcut(shortcut);
    const shortcutMac = L10N.getStr("toolbox.elementPicker.mac.key");
    const isMac = Services.appinfo.OS === "Darwin";

    let label;
    if (this.isDebugTargetFenix()) {
      label = isMac
        ? "toolbox.androidElementPicker.mac.tooltip"
        : "toolbox.androidElementPicker.tooltip";
    } else {
      label = isMac
        ? "toolbox.elementPicker.mac.tooltip"
        : "toolbox.elementPicker.tooltip";
    }

    return isMac
      ? L10N.getFormatStr(label, shortcut, shortcutMac)
      : L10N.getFormatStr(label, shortcut);
  }

  async #listenAndApplyConfigurationPref() {
    // We have two configurations:
    //  * target specific configurations, which are set on all target actors, themself easily accessible from any actor.
    //    Most configurations should be set this way.
    //  * thread specific configurations, which are set on directly on the thread actor.
    //    Only configuration used by the thread actor should be set this way.
    const targetConfiguration = {};

    // Get the current thread settings from the prefs as well as debugger internal storage for breakpoints.
    const threadConfiguration = await getThreadOptions();

    for (const prefName in CONFIGURATION_PREFS) {
      const { name, thread } = CONFIGURATION_PREFS[prefName];
      const value = this.#getPrefValue(prefName);

      // Based on the `thread` boolean, this will be stored in either target or thread specific configuration
      if (thread) {
        threadConfiguration[name] = value;
      } else {
        targetConfiguration[name] = value;
      }

      // Also listen for any future change
      Services.prefs.addObserver(prefName, this.#onConfigurationPrefChange);
    }

    // Now communicate the configurations to the server
    await this.commands.targetConfigurationCommand.updateConfiguration(
      targetConfiguration
    );
    await this.commands.threadConfigurationCommand.updateConfiguration(
      threadConfiguration
    );
  }

  /**
   * Helper to retrieve any preference value regardless of its type.
   *
   * @param {string} name
   *        Preference name.
   * @return {string|number|boolean}
   *        Preference value
   */
  #getPrefValue(name) {
    const type = Services.prefs.getPrefType(name);
    switch (type) {
      case Services.prefs.PREF_STRING:
        return Services.prefs.getStringPref(name, "");
      case Services.prefs.PREF_INT:
        return Services.prefs.getIntPref(name, 0);
      case Services.prefs.PREF_BOOL:
        return Services.prefs.getBoolPref(name, false);
      default:
        throw new Error("Unknown pref type for: " + name);
    }
  }

  /**
   * Called whenever a preference registered in CONFIGURATION_PREFS
   * changes.
   * This is used to communicate the new setting's value to the server.
   *
   * @param {string} subject
   * @param {string} topic
   * @param {string} prefName
   *        The preference name which changed
   */
  #onConfigurationPrefChange = async (subject, topic, prefName) => {
    const { name, thread } = CONFIGURATION_PREFS[prefName];
    const value = this.#getPrefValue(prefName);

    const configurationCommand = thread
      ? this.commands.threadConfigurationCommand
      : this.commands.targetConfigurationCommand;
    await configurationCommand.updateConfiguration({
      [name]: value,
    });

    // This event is only emitted for tests in order to know when the setting has been applied by the backend.
    this.emitForTests("new-configuration-applied", prefName);
  };

  /**
   * Update the visibility of the buttons.
   *
   * @param {object} options
   * @param {boolean} options.fromWillNavigate: true if this is called because the
   *        page is going to navigate
   */
  updateToolboxButtonsVisibility({ fromWillNavigate = false } = {}) {
    let toggledHighlighters = false;
    for (const button of this.toolbarButtons) {
      button.isVisible = this.#commandIsVisible(button);

      // We want to hide highlighters when the toolbox button is disabled from the options panel
      if (button.highlighterTypes && !button.isVisible && button.isChecked) {
        button.onClick({});
        toggledHighlighters = true;
      }
    }

    if (toggledHighlighters || !fromWillNavigate) {
      this.#renderToolboxButtons();
    }
  }

  /**
   * Visually update picker button.
   * This function is called on every "select" event. Newly selected panel can
   * update the visual state of the picker button such as disabled state,
   * additional CSS classes (className), and tooltip (description).
   */
  updatePickerButton() {
    const button = this.pickerButton;
    const currentPanel = this.getCurrentPanel();

    if (currentPanel?.updatePickerButton) {
      currentPanel.updatePickerButton();
    } else {
      // If the current panel doesn't define a custom updatePickerButton,
      // revert the button to its default state
      button.description = this.#getPickerTooltip();
      button.className = this.#getPickerAdditionalClassName();
      button.disabled = null;
    }
  }

  /**
   * Update the visual state of the Frame picker button.
   */
  updateFrameButton() {
    if (this.isDestroying()) {
      return;
    }

    if (this.currentToolId === "options" && this.frameMap.size <= 1) {
      // If the button is only visible because the user is on the Options panel, disable
      // the button and set an appropriate description.
      this.frameButton.disabled = true;
      this.frameButton.description = L10N.getStr(
        "toolbox.frames.disabled.tooltip"
      );
    } else {
      // Otherwise, enable the button and update the description.
      this.frameButton.disabled = false;
      this.frameButton.description = L10N.getStr("toolbox.frames.tooltip");
    }

    // Highlight the button when a child frame is selected and visible.
    const selectedFrame = this.frameMap.get(this.selectedFrameId) || {};

    // We need to do something a bit different to avoid some test failures. This function
    // can be called from onWillNavigate, and the current target might have this `traits`
    // property nullifed, which is unfortunate as that's what isToolSupported is checking,
    // so it will throw.
    // So here, we check first if the button isn't going to be visible anyway (it only checks
    // for this.frameMap size) so we don't call #commandIsVisible.
    const isVisible = !this.frameButton.isCurrentlyVisible()
      ? false
      : this.#commandIsVisible(this.frameButton);

    this.frameButton.isVisible = isVisible;

    if (isVisible) {
      this.frameButton.isChecked = !selectedFrame.isTopLevel;
    }
  }

  updateErrorCountButton() {
    this.errorCountButton.isVisible =
      this.#commandIsVisible(this.errorCountButton) && this.#errorCount > 0;
    this.errorCountButton.errorCount = this.#errorCount;
  }

  /**
   * Setup the #splitConsoleEnabled, reflecting the enabled/disabled state of the Enable Split
   * Console setting, and close the split console if it's open and the setting is turned off
   */
  updateIsSplitConsoleEnabled() {
    this.#splitConsoleEnabled = Services.prefs.getBoolPref(
      SPLITCONSOLE_ENABLED_PREF,
      true
    );

    if (!this.#splitConsoleEnabled && this.splitConsole) {
      this.closeSplitConsole();
    }
  }

  /**
   * Ensure the visibility of each toolbox button matches the preference value.
   */
  #commandIsVisible(button) {
    const { isToolSupported, isCurrentlyVisible, visibilityswitch } = button;

    if (!Services.prefs.getBoolPref(visibilityswitch, true)) {
      return false;
    }

    if (isToolSupported && !isToolSupported(this)) {
      return false;
    }

    if (isCurrentlyVisible && !isCurrentlyVisible()) {
      return false;
    }

    return true;
  }

  /**
   * Build a panel for a tool definition.
   *
   * @param {string} toolDefinition
   *        Tool definition of the tool to build a tab for.
   */
  #buildPanelForTool(toolDefinition) {
    if (!toolDefinition.isToolSupported(this)) {
      return;
    }

    const deck = this.doc.getElementById("toolbox-deck");
    const id = toolDefinition.id;

    if (toolDefinition.ordinal == undefined || toolDefinition.ordinal < 0) {
      toolDefinition.ordinal = MAX_ORDINAL;
    }

    if (!toolDefinition.bgTheme) {
      toolDefinition.bgTheme = "theme-toolbar";
    }
    const panel = this.doc.createXULElement("vbox");
    panel.className = "toolbox-panel " + toolDefinition.bgTheme;

    // There is already a container for the webconsole frame.
    if (!this.doc.getElementById("toolbox-panel-" + id)) {
      panel.id = "toolbox-panel-" + id;
    }

    deck.appendChild(panel);
  }

  /**
   * Lazily created map of the additional tools registered to this toolbox.
   *
   * @returns {Map<string, object>}
   *          a map of the tools definitions registered to this
   *          particular toolbox (the key is the toolId string, the value
   *          is the tool definition plain javascript object).
   */
  get additionalToolDefinitions() {
    if (!this.#additionalToolDefinitions) {
      this.#additionalToolDefinitions = new Map();
    }

    return this.#additionalToolDefinitions;
  }

  /**
   * Retrieve the array of the additional tools registered to this toolbox.
   *
   * @return {Array<object>}
   *         the array of additional tool definitions registered on this toolbox.
   */
  getAdditionalTools() {
    if (this.#additionalToolDefinitions) {
      return Array.from(this.additionalToolDefinitions.values());
    }
    return [];
  }

  /**
   * Get the additional tools that have been registered and are visible.
   *
   * @return {Array<object>}
   *         the array of additional tool definitions registered on this toolbox.
   */
  getVisibleAdditionalTools() {
    return this.visibleAdditionalTools.map(toolId =>
      this.additionalToolDefinitions.get(toolId)
    );
  }

  /**
   * Test the existence of a additional tools registered to this toolbox by tool id.
   *
   * @param {string} toolId
   *        the id of the tool to test for existence.
   *
   * @return {boolean}
   */
  hasAdditionalTool(toolId) {
    return this.additionalToolDefinitions.has(toolId);
  }

  /**
   * Register and load an additional tool on this particular toolbox.
   *
   * @param {object} definition
   *        the additional tool definition to register and add to this toolbox.
   */
  addAdditionalTool(definition) {
    if (!definition.id) {
      throw new Error("Tool definition id is missing");
    }

    if (this.isToolRegistered(definition.id)) {
      throw new Error("Tool definition already registered: " + definition.id);
    }

    this.additionalToolDefinitions.set(definition.id, definition);
    this.visibleAdditionalTools = [
      ...this.visibleAdditionalTools,
      definition.id,
    ];

    const buildPanel = () => this.#buildPanelForTool(definition);

    if (this.isReady) {
      buildPanel();
    } else {
      this.once("ready", buildPanel);
    }
  }

  /**
   * Retrieve the registered inspector extension sidebars
   * (used by the inspector panel during its deferred initialization).
   */
  get inspectorExtensionSidebars() {
    return this.#inspectorExtensionSidebars;
  }

  /**
   * Register an extension sidebar for the inspector panel.
   *
   * @param {string} id
   *        An unique sidebar id
   * @param {object} options
   * @param {string} options.title
   *        A title for the sidebar
   */
  async registerInspectorExtensionSidebar(id, options) {
    this.#inspectorExtensionSidebars.set(id, options);

    // Defer the extension sidebar creation if the inspector
    // has not been created yet (and do not create the inspector
    // only to register an extension sidebar).
    if (!this.target.getCachedFront("inspector")) {
      return;
    }

    const inspector = this.getPanel("inspector");
    if (!inspector) {
      return;
    }

    inspector.addExtensionSidebar(id, options);
  }

  /**
   * Unregister an extension sidebar for the inspector panel.
   *
   * @param {string} id
   *        An unique sidebar id
   */
  unregisterInspectorExtensionSidebar(id) {
    // Unregister the sidebar from the toolbox if the toolbox is not already
    // being destroyed (otherwise we would trigger a re-rendering of the
    // inspector sidebar tabs while the toolbox is going away).
    if (this.#destroyer) {
      return;
    }

    const sidebarDef = this.#inspectorExtensionSidebars.get(id);
    if (!sidebarDef) {
      return;
    }

    this.#inspectorExtensionSidebars.delete(id);

    // Remove the created sidebar instance if the inspector panel
    // has been already created.
    if (!this.target.getCachedFront("inspector")) {
      return;
    }

    const inspector = this.getPanel("inspector");
    inspector.removeExtensionSidebar(id);
  }

  /**
   * Unregister and unload an additional tool from this particular toolbox.
   *
   * @param {string} toolId
   *        the id of the additional tool to unregister and remove.
   */
  removeAdditionalTool(toolId) {
    // Early exit if the toolbox is already destroying itself.
    if (this.#destroyer) {
      return;
    }

    if (!this.hasAdditionalTool(toolId)) {
      throw new Error(
        "Tool definition not registered to this toolbox: " + toolId
      );
    }

    this.additionalToolDefinitions.delete(toolId);
    this.visibleAdditionalTools = this.visibleAdditionalTools.filter(
      id => id !== toolId
    );
    this.unloadTool(toolId);
  }

  /**
   * Ensure the tool with the given id is loaded.
   *
   * @param {string} id
   *        The id of the tool to load.
   * @param {object} options
   *        Object that will be passed to the panel `open` method.
   */
  loadTool(id, options) {
    let iframe = this.doc.getElementById("toolbox-panel-iframe-" + id);
    if (iframe) {
      const panel = this.#toolPanels.get(id);
      return new Promise(resolve => {
        if (panel) {
          resolve(panel);
        } else {
          this.once(id + "-ready", initializedPanel => {
            resolve(initializedPanel);
          });
        }
      });
    }

    return new Promise((resolve, reject) => {
      // Retrieve the tool definition (from the global or the per-toolbox tool maps)
      const definition = this.getToolDefinition(id);

      if (!definition) {
        reject(new Error("no such tool id " + id));
        return;
      }

      iframe = this.doc.createXULElement("iframe");
      iframe.className = "toolbox-panel-iframe";
      iframe.id = "toolbox-panel-iframe-" + id;
      iframe.setAttribute("flex", 1);
      iframe.setAttribute("forceOwnRefreshDriver", "");
      iframe.tooltip = "aHTMLTooltip";

      gDevTools.emit(id + "-init", this, iframe);
      this.emit(id + "-init", iframe);

      const onLoad = async () => {
        // Try to set the dir attribute as early as possible.
        this.setIframeDocumentDir(iframe);

        // The build method should return a panel instance, so events can
        // be fired with the panel as an argument. However, in order to keep
        // backward compatibility with existing extensions do a check
        // for a promise return value.
        let built = definition.build(iframe.contentWindow, this, this.commands);

        if (!(typeof built.then == "function")) {
          const panel = built;
          iframe.panel = panel;

          // The panel instance is expected to fire (and listen to) various
          // framework events, so make sure it's properly decorated with
          // appropriate API (on, off, once, emit).
          // In this case we decorate panel instances directly returned by
          // the tool definition 'build' method.
          if (typeof panel.emit == "undefined") {
            EventEmitter.decorate(panel);
          }

          gDevTools.emit(id + "-build", this, panel);
          this.emit(id + "-build", panel);

          // The panel can implement an 'open' method for asynchronous
          // initialization sequence.
          if (typeof panel.open == "function") {
            built = panel.open(options);
          } else {
            built = new Promise(resolve => {
              resolve(panel);
            });
          }
        }

        // Wait till the panel is fully ready and fire 'ready' events.
        Promise.resolve(built).then(panel => {
          this.#toolPanels.set(id, panel);

          // Make sure to decorate panel object with event API also in case
          // where the tool definition 'build' method returns only a promise
          // and the actual panel instance is available as soon as the
          // promise is resolved.
          if (typeof panel.emit == "undefined") {
            EventEmitter.decorate(panel);
          }

          gDevTools.emit(id + "-ready", this, panel);
          this.emit(id + "-ready", panel);

          resolve(panel);
        }, console.error);
      };

      iframe.setAttribute("src", definition.url);
      if (definition.panelLabel) {
        iframe.setAttribute("aria-label", definition.panelLabel);
      }

      // If no parent yet, append the frame into default location.
      if (!iframe.parentNode) {
        const vbox = this.doc.getElementById("toolbox-panel-" + id);
        vbox.appendChild(iframe);
      }

      // Depending on the host, iframe.contentWindow is not always
      // defined at this moment. If it is not defined, we use an
      // event listener on the iframe DOM node. If it's defined,
      // we use the chromeEventHandler. We can't use a listener
      // on the DOM node every time because this won't work
      // if the (xul chrome) iframe is loaded in a content docshell.
      if (iframe.contentWindow) {
        const loadingUrl = definition.url || "about:blank";
        DOMHelpers.onceDOMReady(iframe.contentWindow, onLoad, loadingUrl);
      } else {
        const callback = () => {
          iframe.removeEventListener("DOMContentLoaded", callback);
          onLoad();
        };

        iframe.addEventListener("DOMContentLoaded", callback);
      }
    });
  }

  /**
   * Set the dir attribute on the content document element of the provided iframe.
   *
   * @param {IFrameElement} iframe
   */
  setIframeDocumentDir(iframe) {
    const docEl = iframe.contentWindow?.document.documentElement;
    if (!docEl || docEl.namespaceURI !== HTML_NS) {
      // Bail out if the content window or document is not ready or if the document is not
      // HTML.
      return;
    }

    if (docEl.hasAttribute("dir")) {
      // Set the dir attribute value only if dir is already present on the document.
      docEl.setAttribute("dir", this.direction);
    }
  }

  /**
   * Mark all in collection as unselected; and id as selected
   *
   * @param {string} collection
   *        DOM collection of items
   * @param {string} id
   *        The Id of the item within the collection to select
   */
  selectSingleNode(collection, id) {
    [...collection].forEach(node => {
      if (node.id === id) {
        node.setAttribute("selected", "true");
        node.setAttribute("aria-selected", "true");
      } else {
        node.removeAttribute("selected");
        node.removeAttribute("aria-selected");
      }
      // The webconsole panel is in a special location due to split console
      if (!node.id) {
        node = this.webconsolePanel;
      }

      const iframe = node.querySelector(".toolbox-panel-iframe");
      if (iframe) {
        let visible = node.id == id;
        // Prevents hiding the split-console if it is currently enabled
        if (node == this.webconsolePanel && this.splitConsole) {
          visible = true;
        }
        this.setIframeVisible(iframe, visible);
      }
    });
  }

  /**
   * Make a privileged iframe visible/hidden.
   *
   * For now, XUL Iframes loading chrome documents (i.e. <iframe type!="content" />)
   * can't be hidden at platform level. And so don't support 'visibilitychange' event.
   *
   * This helper workarounds that by at least being able to send these kind of events.
   * It will help panel react differently depending on them being displayed or in
   * background.
   */
  setIframeVisible(iframe, visible) {
    // Ideally, we would use <xul:browser type="content"> element in order to have top level BrowsingContexts
    // for each panel. But:
    //   1) It looks like nested <xul:browser type="content"> aren't creating top level BCs
    //   2) Using type="content" against panels breaks a few things.
    // Also see bug 1405342 for outdated approach.
    //
    // So keep using the following workaround which only fakes `visibilitychange`
    // enough to make the `visiblityChangeHanderStore` to work.
    const win = iframe.contentWindow;
    const doc = win.document;
    if (visible && !this.#visibleIframes.has(iframe)) {
      this.#visibleIframes.add(iframe);

      // Overload document's `visibilityState` attribute
      // Use defineProperty, as by default `document.visbilityState` is read only.
      Object.defineProperty(doc, "visibilityState", {
        get: () => {
          // Also acknowledge the toolbox visibility, which take the lead over
          // any panel visiblity
          return this.win?.browsingContext.isActive ? "visible" : "hidden";
        },
        configurable: true,
      });
    } else if (!visible && this.#visibleIframes.has(iframe)) {
      this.#visibleIframes.delete(iframe);

      Object.defineProperty(doc, "visibilityState", {
        value: "hidden",
        configurable: true,
      });
    } else {
      return;
    }

    // Fake the 'visibilitychange' event
    doc.dispatchEvent(new win.Event("visibilitychange"));
  }

  /**
   * Switch to the tool with the given id
   *
   * @param {string} id
   *        The id of the tool to switch to
   * @param {string} reason
   *        Reason the tool was opened
   * @param {object} options
   *        Object that will be passed to the panel
   */
  selectTool(id, reason = "unknown", options) {
    this.emit("panel-changed");

    if (this.currentToolId == id) {
      const panel = this.#toolPanels.get(id);
      if (panel) {
        // We have a panel instance, so the tool is already fully loaded.

        // re-focus tool to get key events again
        this.focusTool(id);

        // Return the existing panel in order to have a consistent return value.
        return Promise.resolve(panel);
      }
      // Otherwise, if there is no panel instance, it is still loading,
      // so we are racing another call to selectTool with the same id.
      return this.once("select").then(() =>
        Promise.resolve(this.#toolPanels.get(id))
      );
    }

    if (!this.isReady) {
      throw new Error("Can't select tool, wait for toolbox 'ready' event");
    }

    // Check if the tool exists.
    if (
      this.panelDefinitions.find(definition => definition.id === id) ||
      id === "options" ||
      this.additionalToolDefinitions.get(id)
    ) {
      if (this.currentToolId) {
        this.telemetry.toolClosed(this.currentToolId, this);
      }

      this.#pingTelemetrySelectTool(id, reason);
    } else {
      throw new Error("No tool found");
    }

    this.lastUsedToolId = this.currentToolId;
    this.currentToolId = id;
    this.#refreshConsoleDisplay();
    if (id != "options") {
      Services.prefs.setCharPref(this.#prefs.LAST_TOOL, id);
    }

    return this.loadTool(id, options).then(panel => {
      // If some other tool started being selected,
      // cancel any further operation, but still return the panel to the callsite.
      if (this.currentToolId != id) {
        return panel;
      }
      // Only select the panel once it is loaded to prevent showing it
      // while it is bootstrapping and prevent blinks
      const toolboxPanels = this.doc.querySelectorAll(".toolbox-panel");
      this.selectSingleNode(toolboxPanels, "toolbox-panel-" + id);

      // focus the tool's frame to start receiving key events
      this.focusTool(id);

      this.emit("select", id);
      this.emit(id + "-selected", panel);
      return panel;
    });
  }

  /**
   * Render the toolbox buttons
   */
  #renderToolboxButtons() {
    this.component.setToolboxButtons(this.toolbarButtons);
  }

  #pingTelemetrySelectTool(id, reason) {
    const width = Math.ceil(this.win.outerWidth / 50) * 50;
    const panelName = this.getTelemetryPanelNameOrOther(id);
    const prevPanelName = this.getTelemetryPanelNameOrOther(this.currentToolId);
    const cold = !this.getPanel(id);
    const pending = ["host", "width", "start_state", "panel_name", "cold"];

    // On first load this.currentToolId === undefined so we need to skip sending
    // a devtools.main.exit telemetry event.
    if (this.currentToolId) {
      this.telemetry.recordEvent("exit", prevPanelName, null, {
        host: this.#hostType,
        width,
        panel_name: prevPanelName,
        next_panel: panelName,
        reason,
      });
    }

    this.telemetry.addEventProperties(this.topWindow, "open", "tools", null, {
      width,
    });

    if (id === "webconsole") {
      pending.push("message_count");
    }

    this.telemetry.preparePendingEvent(this, "enter", panelName, null, pending);

    this.telemetry.addEventProperties(this, "enter", panelName, null, {
      host: this.#hostType,
      start_state: reason,
      panel_name: panelName,
      cold,
    });

    if (reason !== "initial_panel") {
      const width = Math.ceil(this.win.outerWidth / 50) * 50;
      this.telemetry.addEventProperty(
        this,
        "enter",
        panelName,
        null,
        "width",
        width
      );
    }

    // Cold webconsole event message_count is handled in
    // devtools/client/webconsole/webconsole-wrapper.js
    if (!cold && id === "webconsole") {
      this.telemetry.addEventProperty(
        this,
        "enter",
        "webconsole",
        null,
        "message_count",
        0
      );
    }

    this.telemetry.toolOpened(id, this);
  }

  /**
   * Focus a tool's panel by id
   *
   * @param  {string} id
   *         The id of tool to focus
   */
  focusTool(id, state = true) {
    const iframe = this.doc.getElementById("toolbox-panel-iframe-" + id);

    if (state) {
      iframe.focus();
    } else {
      iframe.blur();
    }
  }

  /**
   * Focus split console's input line
   */
  focusConsoleInput() {
    const consolePanel = this.getPanel("webconsole");
    if (consolePanel) {
      consolePanel.focusInput();
    }
  }

  /**
   * Disable all network logs in the console
   */
  disableAllConsoleNetworkLogs() {
    const consolePanel = this.getPanel("webconsole");
    if (consolePanel) {
      consolePanel.hud.ui.disableAllNetworkMessages();
    }
  }

  /**
   * If the console is split and we are focusing an element outside
   * of the console, then store the newly focused element, so that
   * it can be restored once the split console closes.
   *
   * @param Element originalTarget
   *        The DOM Element that just got focused.
   */
  #updateLastFocusedElementForSplitConsole(originalTarget) {
    // Ignore any non element nodes, or any elements contained
    // within the webconsole frame.
    const webconsoleURL = gDevTools.getToolDefinition("webconsole").url;
    if (
      originalTarget.nodeType !== 1 ||
      originalTarget.baseURI === webconsoleURL
    ) {
      return;
    }

    this.#lastFocusedElement = originalTarget;
  }

  // Report if the toolbox is currently focused,
  // or the focus in elsewhere in the browser or another app.
  #isToolboxFocused = false;

  #onFocus = ({ originalTarget }) => {
    this.#isToolboxFocused = true;
    this.#debounceUpdateFocusedState();

    this.#updateLastFocusedElementForSplitConsole(originalTarget);
  };

  #onBlur = () => {
    this.#isToolboxFocused = false;
    this.#debounceUpdateFocusedState();
  };

  #onTabsOrderUpdated = () => {
    this.#combineAndSortPanelDefinitions();
  };

  /**
   * Opens the split console.
   *
   * @param {boolean} focusConsoleInput
   *        By default, the console input will be focused.
   *        Pass false in order to prevent this.
   *
   * @returns {Promise} a promise that resolves once the tool has been
   *          loaded and focused.
   */
  openSplitConsole({ focusConsoleInput = true } = {}) {
    if (!this.isSplitConsoleEnabled()) {
      return this.selectTool(
        "webconsole",
        "use_in_console_with_disabled_split_console"
      );
    }

    this.#splitConsole = true;
    Services.prefs.setBoolPref(SPLITCONSOLE_OPEN_PREF, true);
    this.#refreshConsoleDisplay();

    // Ensure split console is visible if console was already loaded in background
    const iframe = this.webconsolePanel.querySelector(".toolbox-panel-iframe");
    if (iframe) {
      this.setIframeVisible(iframe, true);
    }

    return this.loadTool("webconsole").then(() => {
      if (!this.component) {
        return;
      }
      this.component.setIsSplitConsoleActive(true);
      this.telemetry.recordEvent("activate", "split_console", null, {
        host: this.#getTelemetryHostString(),
        width: Math.ceil(this.win.outerWidth / 50) * 50,
      });
      this.emit("split-console");
      if (focusConsoleInput) {
        this.focusConsoleInput();
      }
    });
  }

  /**
   * Closes the split console.
   *
   * @returns {Promise} a promise that resolves once the tool has been
   *          closed.
   */
  closeSplitConsole() {
    this.#splitConsole = false;
    Services.prefs.setBoolPref(SPLITCONSOLE_OPEN_PREF, false);
    this.#saveSplitConsoleHeight();

    this.#refreshConsoleDisplay();
    this.component.setIsSplitConsoleActive(false);

    this.telemetry.recordEvent("deactivate", "split_console", null, {
      host: this.#getTelemetryHostString(),
      width: Math.ceil(this.win.outerWidth / 50) * 50,
    });

    this.emit("split-console");

    if (this.#lastFocusedElement) {
      this.#lastFocusedElement.focus();
    }
    return Promise.resolve();
  }

  /**
   * Toggles the split state of the webconsole.  If the webconsole panel
   * is already selected then this command is ignored.
   *
   * @returns {Promise} a promise that resolves once the tool has been
   *          opened or closed.
   */
  toggleSplitConsole() {
    if (this.currentToolId !== "webconsole") {
      return this.splitConsole
        ? this.closeSplitConsole()
        : this.openSplitConsole();
    }

    return Promise.resolve();
  }

  /**
   * Toggles the options panel.
   * If the option panel is already selected then select the last selected panel.
   */
  toggleOptions(event) {
    // Flip back to the last used panel if we are already
    // on the options panel.
    if (
      this.currentToolId === "options" &&
      gDevTools.getToolDefinition(this.lastUsedToolId)
    ) {
      this.selectTool(this.lastUsedToolId, "toggle_settings_off");
    } else {
      this.selectTool("options", "toggle_settings_on");
    }

    // preventDefault will avoid a Linux only bug when the focus is on a text input
    // See Bug 1519087.
    event.preventDefault();
  }

  /**
   * Loads the tool next to the currently selected tool.
   */
  selectNextTool() {
    const definitions = this.component.panelDefinitions;
    const index = definitions.findIndex(({ id }) => id === this.currentToolId);
    const definition =
      index === -1 || index >= definitions.length - 1
        ? definitions[0]
        : definitions[index + 1];
    return this.selectTool(definition.id, "select_next_key");
  }

  /**
   * Loads the tool just left to the currently selected tool.
   */
  selectPreviousTool() {
    const definitions = this.component.panelDefinitions;
    const index = definitions.findIndex(({ id }) => id === this.currentToolId);
    const definition =
      index === -1 || index < 1
        ? definitions[definitions.length - 1]
        : definitions[index - 1];
    return this.selectTool(definition.id, "select_prev_key");
  }

  /**
   * Tells if the given tool is currently highlighted.
   * (doesn't mean selected, its tab header will be green)
   *
   * @param {string} id
   *        The id of the tool to check.
   */
  isHighlighted(id) {
    return this.component.state.highlightedTools.has(id);
  }

  /**
   * Highlights the tool's tab if it is not the currently selected tool.
   *
   * @param {string} id
   *        The id of the tool to highlight
   */
  async highlightTool(id) {
    if (!this.component) {
      await this.isOpen;
    }
    this.component.highlightTool(id);
  }

  /**
   * De-highlights the tool's tab.
   *
   * @param {string} id
   *        The id of the tool to unhighlight
   */
  async unhighlightTool(id) {
    if (!this.component) {
      await this.isOpen;
    }
    this.component.unhighlightTool(id);
  }

  /**
   * Raise the toolbox host.
   */
  raise() {
    this.postMessage({ name: "raise-host" });

    return this.once("host-raised");
  }

  /**
   * Fired when user just started navigating away to another web page.
   */
  async #onWillNavigate({ isFrameSwitching } = {}) {
    // On navigate, the server will resume all paused threads, but due to an
    // issue which can cause loosing outgoing messages/RDP packets, the THREAD_STATE
    // resources for the resumed state might not get received. So let assume it happens
    // make use the UI is the appropriate state.
    if (this.#pausedTargets.size > 0) {
      this.emit("toolbox-resumed");
      this.#pausedTargets.clear();
      if (this.isHighlighted("jsdebugger")) {
        this.unhighlightTool("jsdebugger");
      }
    }

    // Clearing the error count and the iframe list as soon as we navigate
    this.setErrorCount(0);
    if (!isFrameSwitching) {
      this.#updateFrames({ destroyAll: true });
    }

    this.updateToolboxButtonsVisibility({ fromWillNavigate: true });

    const toolId = this.currentToolId;
    // For now, only inspector, webconsole, netmonitor and accessibility fire "reloaded" event
    if (
      toolId != "inspector" &&
      toolId != "webconsole" &&
      toolId != "netmonitor" &&
      toolId != "accessibility"
    ) {
      return;
    }

    const start = this.win.performance.now();
    const panel = this.getPanel(toolId);
    // Ignore the timing if the panel is still loading
    if (!panel) {
      return;
    }

    await panel.once("reloaded");
    // The toolbox may have been destroyed while the panel was reloading
    if (this.isDestroying()) {
      return;
    }
    const delay = this.win.performance.now() - start;
    Glean.devtools.toolboxPageReloadDelay[toolId].accumulateSingleSample(delay);
  }

  /**
   * Refresh the host's title.
   */
  #refreshHostTitle = () => {
    let title;

    const { selectedTargetFront } = this.commands.targetCommand;
    if (this.target.isXpcShellTarget) {
      // This will only be displayed for local development and can remain
      // hardcoded in english.
      title = "XPCShell Toolbox";
    } else if (this.isMultiProcessBrowserToolbox) {
      const scope = Services.prefs.getCharPref(BROWSERTOOLBOX_SCOPE_PREF);
      if (scope == BROWSERTOOLBOX_SCOPE_EVERYTHING) {
        title = L10N.getStr("toolbox.multiProcessBrowserToolboxTitle");
      } else if (scope == BROWSERTOOLBOX_SCOPE_PARENTPROCESS) {
        title = L10N.getStr("toolbox.parentProcessBrowserToolboxTitle");
      } else {
        throw new Error("Unsupported scope: " + scope);
      }
    } else if (
      selectedTargetFront.name &&
      selectedTargetFront.name != selectedTargetFront.url
    ) {
      // For Web Extensions, the target name may only be the pathname of the target URL.
      // In such case, only print the absolute target url.
      if (
        this.#descriptorFront.isWebExtensionDescriptor &&
        selectedTargetFront.url.includes(selectedTargetFront.name)
      ) {
        title = L10N.getFormatStr(
          "toolbox.titleTemplate1",
          getUnicodeUrl(selectedTargetFront.url)
        );
      } else {
        title = L10N.getFormatStr(
          "toolbox.titleTemplate2",
          selectedTargetFront.name,
          getUnicodeUrl(selectedTargetFront.url)
        );
      }
    } else {
      title = L10N.getFormatStr(
        "toolbox.titleTemplate1",
        getUnicodeUrl(selectedTargetFront.url)
      );
    }
    this.postMessage({
      name: "set-host-title",
      title,
    });
  };

  /**
   * For a given URL, return its pathname.
   * This is handy for Web Extension as it should be the addon ID.
   *
   * @param {string} url
   * @return {string} pathname
   */
  getExtensionPathName(url) {
    const parsedURL = URL.parse(url);
    if (!parsedURL) {
      // Return the url if unable to resolve the pathname.
      return url;
    }
    // Only moz-extension URL should be shortened into the URL pathname.
    if (!lazy.ExtensionUtils.isExtensionUrl(parsedURL)) {
      return url;
    }
    return parsedURL.pathname;
  }

  /**
   * Returns an instance of the preference actor. This is a lazily initialized root
   * actor that persists preferences to the debuggee, instead of just to the DevTools
   * client. See the definition of the preference actor for more information.
   */
  get preferenceFront() {
    if (!this.#preferenceFrontRequest) {
      // Set the #preferenceFrontRequest property to allow the resetPreference toolbox
      // method to cleanup the preference set when the toolbox is closed.
      this.#preferenceFrontRequest =
        this.commands.client.mainRoot.getFront("preference");
    }
    return this.#preferenceFrontRequest;
  }

  /**
   * See: https://firefox-source-docs.mozilla.org/l10n/fluent/tutorial.html#manually-testing-ui-with-pseudolocalization
   *
   * @param {"bidi" | "accented" | "none"} pseudoLocale
   */
  async changePseudoLocale(pseudoLocale) {
    await this.isOpen;
    const prefFront = await this.preferenceFront;
    if (pseudoLocale === "none") {
      await prefFront.clearUserPref(PSEUDO_LOCALE_PREF);
    } else {
      await prefFront.setCharPref(PSEUDO_LOCALE_PREF, pseudoLocale);
    }
    this.component.setPseudoLocale(pseudoLocale);
    this.#pseudoLocaleChanged = true;
  }

  /**
   * Returns the pseudo-locale when the target is browser chrome, otherwise undefined.
   *
   * @returns {"bidi" | "accented" | "none" | undefined}
   */
  async getPseudoLocale() {
    if (!this.isBrowserToolbox) {
      return undefined;
    }

    const prefFront = await this.preferenceFront;
    const locale = await prefFront.getCharPref(PSEUDO_LOCALE_PREF);

    switch (locale) {
      case "bidi":
      case "accented":
        return locale;
      default:
        return "none";
    }
  }

  async toggleNoAutohide() {
    const front = await this.preferenceFront;

    const toggledValue = !(await this.#isDisableAutohideEnabled());

    front.setBoolPref(DISABLE_AUTOHIDE_PREF, toggledValue);

    if (
      this.isBrowserToolbox ||
      this.#descriptorFront.isWebExtensionDescriptor
    ) {
      this.component.setDisableAutohide(toggledValue);
    }
    this.#autohideHasBeenToggled = true;
  }

  /**
   * Toggling "always on top" behavior is a bit special.
   *
   * We toggle the preference and then destroy and re-create the toolbox
   * as there is no way to change this behavior on an existing window
   * (see bug 1788946).
   */
  async toggleAlwaysOnTop() {
    const currentValue = Services.prefs.getBoolPref(
      DEVTOOLS_ALWAYS_ON_TOP,
      false
    );
    Services.prefs.setBoolPref(DEVTOOLS_ALWAYS_ON_TOP, !currentValue);

    const addonId = this.#descriptorFront.id;
    await this.destroy();
    gDevTools.showToolboxForWebExtension(addonId);
  }

  async #isDisableAutohideEnabled() {
    if (
      !this.isBrowserToolbox &&
      !this.#descriptorFront.isWebExtensionDescriptor
    ) {
      return false;
    }

    const prefFront = await this.preferenceFront;
    return prefFront.getBoolPref(DISABLE_AUTOHIDE_PREF);
  }

  async #listFrames() {
    if (
      !this.target.getTrait("frames") ||
      this.target.targetForm.ignoreSubFrames
    ) {
      // We are not targetting a regular WindowGlobalTargetActor (it can be either an
      // addon or browser toolbox actor), or EFT is enabled.
      return;
    }

    try {
      const { frames } = await this.target.listFrames();
      this.#updateFrames({ frames });
    } catch (e) {
      console.error("Error while listing frames", e);
    }
  }

  /**
   * Called by the iframe picker when the user selected a frame.
   *
   * @param {string} frameIdOrTargetActorId
   */
  onIframePickerFrameSelected(frameIdOrTargetActorId) {
    if (!this.frameMap.has(frameIdOrTargetActorId)) {
      console.error(
        `Can't focus on frame "${frameIdOrTargetActorId}", it is not a known frame`
      );
      return;
    }

    const frameInfo = this.frameMap.get(frameIdOrTargetActorId);
    // If there is no targetFront in the frameData, this means EFT is not enabled.
    // Send packet to the backend to select specified frame and  wait for 'frameUpdate'
    // event packet to update the UI.
    if (!frameInfo.targetFront) {
      this.target.switchToFrame({ windowId: frameIdOrTargetActorId });
      return;
    }

    // Here, EFT is enabled, so we want to focus the toolbox on the specific targetFront
    // that was selected by the user. This will trigger this.#onTargetSelected which will
    // take care of updating the iframe picker state.
    this.commands.targetCommand.selectTarget(frameInfo.targetFront);
  }

  /**
   * Highlight a frame in the page
   *
   * @param {string} frameIdOrTargetActorId
   */
  async onHighlightFrame(frameIdOrTargetActorId) {
    // Only enable frame highlighting when the top level document is targeted
    if (!this.rootFrameSelected) {
      return null;
    }

    const frameInfo = this.frameMap.get(frameIdOrTargetActorId);
    if (!frameInfo) {
      return null;
    }

    let nodeFront;
    if (frameInfo.targetFront) {
      const inspectorFront = await frameInfo.targetFront.getFront("inspector");
      nodeFront = await inspectorFront.walker.documentElement();
      if (!nodeFront) {
        return null;
      }
    } else {
      const inspectorFront = await this.target.getFront("inspector");
      nodeFront = await inspectorFront.walker.getNodeActorFromWindowID(
        frameIdOrTargetActorId
      );
    }
    const highlighter = this.getHighlighter();
    return highlighter.highlight(nodeFront);
  }

  /**
   * Handles changes in document frames.
   *
   * @param {object} data
   * @param {boolean} data.destroyAll: All frames have been destroyed.
   * @param {number} data.selected: A frame has been selected
   * @param {object} data.frameData: Some frame data were updated
   * @param {string} data.frameData.url: new frame URL (it might have been blank or about:blank)
   * @param {string} data.frameData.title: new frame title
   * @param {number | string} data.frameData.id: frame ID / targetFront actorID when EFT is enabled.
   * @param {Array<object>} data.frames: List of frames. Every frame can have:
   * @param {number | string} data.frames[].id: frame ID / targetFront actorID when EFT is enabled.
   * @param {string} data.frames[].url: frame URL
   * @param {string} data.frames[].title: frame title
   * @param {boolean} data.frames[].destroy: Set to true if destroyed
   * @param {boolean} data.frames[].isTopLevel: true for top level window
   */
  #updateFrames = data => {
    // At the moment, frames `id` can either be outerWindowID (a Number),
    // or a targetActorID (a String).
    // In order to have the same type of data as a key of `frameMap`, we transform any
    // outerWindowID into a string.
    // This can be removed once EFT is enabled by default
    if (data.selected) {
      data.selected = data.selected.toString();
    } else if (data.frameData) {
      data.frameData.id = data.frameData.id.toString();
    } else if (data.frames) {
      data.frames.forEach(frame => {
        if (frame.id) {
          frame.id = frame.id.toString();
        }
      });
    }

    // Store (synchronize) data about all existing frames on the backend
    if (data.destroyAll) {
      this.frameMap.clear();
      this.selectedFrameId = null;
    } else if (data.selected) {
      // If we select the top level target, default back to no particular selected document.
      if (data.selected == this.target.actorID) {
        this.selectedFrameId = null;
      } else {
        this.selectedFrameId = data.selected;
      }
    } else if (data.frameData && this.frameMap.has(data.frameData.id)) {
      const existingFrameData = this.frameMap.get(data.frameData.id);
      if (
        existingFrameData.title == data.frameData.title &&
        existingFrameData.url == data.frameData.url
      ) {
        return;
      }

      this.frameMap.set(data.frameData.id, {
        ...existingFrameData,
        url: data.frameData.url,
        title: data.frameData.title,
      });
    } else if (data.frames) {
      data.frames.forEach(frame => {
        if (frame.destroy) {
          this.frameMap.delete(frame.id);

          // Reset the currently selected frame if it's destroyed.
          if (this.selectedFrameId == frame.id) {
            this.selectedFrameId = null;
          }
        } else {
          this.frameMap.set(frame.id, frame);
        }
      });
    }

    // If there is no selected frame select the first top level
    // frame by default. Note that there might be more top level
    // frames in case of the BrowserToolbox.
    if (!this.selectedFrameId) {
      const frames = [...this.frameMap.values()];
      const topFrames = frames.filter(frame => frame.isTopLevel);
      this.selectedFrameId = topFrames.length ? topFrames[0].id : null;
    }

    // Debounce the update to avoid unnecessary flickering/rendering.
    if (!this.debouncedToolbarUpdate) {
      this.debouncedToolbarUpdate = debounce(
        () => {
          // Toolbox may have been destroyed in the meantime
          if (this.component) {
            this.#renderToolboxButtons();
          }
          this.debouncedToolbarUpdate = null;
        },
        200,
        this
      );
    }

    const updateUiElements = () => {
      // We may need to hide/show the frames button now.
      this.updateFrameButton();

      if (this.debouncedToolbarUpdate) {
        this.debouncedToolbarUpdate();
      }
    };

    // This may have been called before the toolbox is ready (= the dom elements for
    // the iframe picker don't exist yet).
    if (!this.isReady) {
      this.once("ready").then(() => updateUiElements);
    } else {
      updateUiElements();
    }
  };

  /**
   * Returns whether a root frame (with no parent frame) is selected.
   */
  get rootFrameSelected() {
    // If the frame switcher is disabled, we won't have a selected frame ID.
    // In this case, we're always showing the root frame.
    if (!this.selectedFrameId) {
      return true;
    }

    return this.frameMap.get(this.selectedFrameId).isTopLevel;
  }

  /**
   * Switch to the last used host for the toolbox UI.
   */
  switchToPreviousHost() {
    return this.switchHost("previous");
  }

  /**
   * Switch to a new host for the toolbox UI. E.g. bottom, sidebar, window,
   * and focus the window when done.
   *
   * @param {string} hostType
   *        The host type of the new host object
   */
  switchHost(hostType) {
    if (hostType == this.hostType || !this.#descriptorFront.isLocalTab) {
      return null;
    }

    // chromeEventHandler will change after swapping hosts, remove events relying on it.
    this.#removeChromeEventHandlerEvents();

    this.emit("host-will-change", hostType);

    // ToolboxHostManager is going to call swapFrameLoaders which mess up with
    // focus. We have to blur before calling it in order to be able to restore
    // the focus after, in #onSwitchedHost.
    this.focusTool(this.currentToolId, false);

    // Host code on the chrome side will send back a message once the host
    // switched
    this.postMessage({
      name: "switch-host",
      hostType,
    });

    return this.once("host-changed");
  }

  /**
   * Request to Firefox UI to move the toolbox to another tab.
   * This is used when we move a toolbox to a new popup opened by the tab we were currently debugging.
   * We also move the toolbox back to the original tab we were debugging if we select it via Firefox tabs.
   *
   * @param {string} tabBrowsingContextID
   *        The BrowsingContext ID of the tab we want to move to.
   * @returns {Promise<undefined>}
   *        This will resolve only once we moved to the new tab.
   */
  switchHostToTab(tabBrowsingContextID) {
    this.postMessage({
      name: "switch-host-to-tab",
      tabBrowsingContextID,
    });

    return this.once("switched-host-to-tab");
  }

  #onSwitchedHost({ hostType }) {
    this.#hostType = hostType;

    this.#buildDockOptions();

    // chromeEventHandler changed after swapping hosts, add again events relying on it.
    this.#addChromeEventHandlerEvents();

    // We blurred the tools at start of switchHost, but also when clicking on
    // host switching button. We now have to restore the focus.
    this.focusTool(this.currentToolId, true);

    this.emit("host-changed");
    Glean.devtools.toolboxHost.accumulateSingleSample(
      this.#getTelemetryHostId()
    );

    this.component.setCurrentHostType(hostType);
  }

  /**
   * Event handler fired when the toolbox was moved to another tab.
   * This fires when the toolbox itself requests to be moved to another tab,
   * but also when we select the original tab where the toolbox originally was.
   *
   * @param {string} browsingContextID
   *        The BrowsingContext ID of the tab the toolbox has been moved to.
   */
  #onSwitchedHostToTab(browsingContextID) {
    const targets = this.commands.targetCommand.getAllTargets([
      this.commands.targetCommand.TYPES.FRAME,
    ]);
    const target = targets.find(
      target => target.browsingContextID == browsingContextID
    );

    this.commands.targetCommand.selectTarget(target);

    this.emit("switched-host-to-tab");
  }

  /**
   * Test the availability of a tool (both globally registered tools and
   * additional tools registered to this toolbox) by tool id.
   *
   * @param  {string} toolId
   *         Id of the tool definition to search in the per-toolbox or globally
   *         registered tools.
   *
   * @returns {bool}
   *         Returns true if the tool is registered globally or on this toolbox.
   */
  isToolRegistered(toolId) {
    return !!this.getToolDefinition(toolId);
  }

  /**
   * Return the tool definition registered globally or additional tools registered
   * to this toolbox.
   *
   * @param  {string} toolId
   *         Id of the tool definition to retrieve for the per-toolbox and globally
   *         registered tools.
   *
   * @returns {object}
   *         The plain javascript object that represents the requested tool definition.
   */
  getToolDefinition(toolId) {
    return (
      gDevTools.getToolDefinition(toolId) ||
      this.additionalToolDefinitions.get(toolId)
    );
  }

  /**
   * Internal helper that removes a loaded tool from the toolbox,
   * it removes a loaded tool panel and tab from the toolbox without removing
   * its definition, so that it can still be listed in options and re-added later.
   *
   * @param  {string} toolId
   *         Id of the tool to be removed.
   */
  unloadTool(toolId) {
    if (typeof toolId != "string") {
      throw new Error("Unexpected non-string toolId received.");
    }

    if (this.#toolPanels.has(toolId)) {
      const instance = this.#toolPanels.get(toolId);
      instance.destroy();
      this.#toolPanels.delete(toolId);
    }

    const panel = this.doc.getElementById("toolbox-panel-" + toolId);

    // Select another tool.
    if (this.currentToolId == toolId) {
      const index = this.panelDefinitions.findIndex(({ id }) => id === toolId);
      const nextTool = this.panelDefinitions[index + 1];
      const previousTool = this.panelDefinitions[index - 1];
      let toolNameToSelect;

      if (nextTool) {
        toolNameToSelect = nextTool.id;
      }
      if (previousTool) {
        toolNameToSelect = previousTool.id;
      }
      if (toolNameToSelect) {
        this.selectTool(toolNameToSelect, "tool_unloaded");
      }
    }

    // Remove this tool from the current panel definitions.
    this.panelDefinitions = this.panelDefinitions.filter(
      ({ id }) => id !== toolId
    );
    this.visibleAdditionalTools = this.visibleAdditionalTools.filter(
      id => id !== toolId
    );
    this.#combineAndSortPanelDefinitions();

    if (panel) {
      panel.remove();
    }

    if (this.hostType == Toolbox.HostType.WINDOW) {
      const doc = this.win.parent.document;
      const key = doc.getElementById("key_" + toolId);
      if (key) {
        key.remove();
      }
    }
  }

  /**
   * Handler for the tool-registered event.
   *
   * @param  {string} toolId
   *         Id of the tool that was registered
   */
  #toolRegistered = toolId => {
    // Tools can either be in the global devtools, or added to this specific toolbox
    // as an additional tool.
    let definition = gDevTools.getToolDefinition(toolId);
    let isAdditionalTool = false;
    if (!definition) {
      definition = this.additionalToolDefinitions.get(toolId);
      isAdditionalTool = true;
    }

    if (definition.isToolSupported(this)) {
      if (isAdditionalTool) {
        this.visibleAdditionalTools = [...this.visibleAdditionalTools, toolId];
        this.#combineAndSortPanelDefinitions();
      } else {
        this.panelDefinitions = this.panelDefinitions.concat(definition);
      }
      this.#buildPanelForTool(definition);

      // Emit the event so tools can listen to it from the toolbox level
      // instead of gDevTools.
      this.emit("tool-registered", toolId);
    }
  };

  /**
   * Handler for the tool-unregistered event.
   *
   * @param  {string} toolId
   *         id of the tool that was unregistered
   */
  #toolUnregistered = toolId => {
    this.unloadTool(toolId);

    // Emit the event so tools can listen to it from the toolbox level
    // instead of gDevTools
    this.emit("tool-unregistered", toolId);
  };

  /**
   * A helper function that returns an object containing methods to show and hide the
   * Box Model Highlighter on a given NodeFront or node grip (object with metadata which
   * can be used to obtain a NodeFront for a node), as well as helpers to listen to the
   * higligher show and hide events. The event helpers are used in tests where it is
   * cumbersome to load the Inspector panel in order to listen to highlighter events.
   *
   * @returns {object} an object of the following shape:
   *   - {AsyncFunction} highlight: A function that will show a Box Model Highlighter
   *                     for the provided NodeFront or node grip.
   *   - {AsyncFunction} unhighlight: A function that will hide any Box Model Highlighter
   *                     that is visible. If the `highlight` promise isn't settled yet,
   *                     it will wait until it's done and then unhighlight to prevent
   *                     zombie highlighters.
   *   - {AsyncFunction} waitForHighlighterShown: Returns a promise which resolves with
   *                     the "highlighter-shown" event data once the highlighter is shown.
   *   - {AsyncFunction} waitForHighlighterHidden: Returns a promise which resolves with
   *                     the "highlighter-hidden" event data once the highlighter is
   *                     hidden.
   */
  getHighlighter() {
    let pendingHighlight;

    /**
     * Return a promise wich resolves with a reference to the Inspector panel.
     *
     * @param {object} options: Options that will be passed to the inspector initialization
     */
    const _getInspector = async options => {
      const inspector = this.getPanel("inspector");
      if (inspector) {
        return inspector;
      }

      return this.loadTool("inspector", options);
    };

    /**
     * Returns a promise which resolves when a Box Model Highlighter emits the given event
     *
     * @param  {string} eventName
     *         Name of the event to listen to.
     * @return {Promise}
     *         Promise which resolves when the highlighter event occurs.
     *         Resolves with the data payload attached to the event.
     */
    async function _waitForHighlighterEvent(eventName) {
      const inspector = await _getInspector();
      return new Promise(resolve => {
        function _handler(data) {
          if (data.type === inspector.highlighters.TYPES.BOXMODEL) {
            inspector.highlighters.off(eventName, _handler);
            resolve(data);
          }
        }

        inspector.highlighters.on(eventName, _handler);
      });
    }

    return {
      // highlight might be triggered right before a test finishes. Wrap it
      // with safeAsyncMethod to avoid intermittents.
      highlight: this.#safeAsyncAfterDestroy(async (object, options) => {
        pendingHighlight = (async () => {
          let nodeFront = object;

          if (!(nodeFront instanceof NodeFront)) {
            const inspectorFront = await this.target.getFront("inspector");
            nodeFront = await inspectorFront.getNodeFrontFromNodeGrip(object);
          }

          if (!nodeFront) {
            return null;
          }

          const inspector = await _getInspector({
            // if the inspector wasn't initialized yet, this will ensure that we select
            // the highlighted node; otherwise the default selected node might be in
            // another thread, which will ultimately select this other thread in the
            // debugger, and might confuse users (see Bug 1837480)
            defaultStartupNode: nodeFront,
          });
          return inspector.highlighters.showHighlighterTypeForNode(
            inspector.highlighters.TYPES.BOXMODEL,
            nodeFront,
            options
          );
        })();
        return pendingHighlight;
      }),
      unhighlight: this.#safeAsyncAfterDestroy(async () => {
        if (pendingHighlight) {
          await pendingHighlight;
          pendingHighlight = null;
        }

        const inspector = await _getInspector();
        return inspector.highlighters.hideHighlighterType(
          inspector.highlighters.TYPES.BOXMODEL
        );
      }),

      waitForHighlighterShown: this.#safeAsyncAfterDestroy(async () => {
        return _waitForHighlighterEvent("highlighter-shown");
      }),

      waitForHighlighterHidden: this.#safeAsyncAfterDestroy(async () => {
        return _waitForHighlighterEvent("highlighter-hidden");
      }),
    };
  }

  /**
   * Shortcut to avoid throwing errors when an async method fails after toolbox
   * destroy. Should be used with methods that might be triggered by a user
   * input, regardless of the toolbox lifecycle.
   */
  #safeAsyncAfterDestroy(fn) {
    return safeAsyncMethod(fn, () => !!this.#destroyer);
  }

  #onNewSelectedNodeFront = async () => {
    // Emit a "selection-changed" event when the toolbox.selection has been set
    // to a new node (or cleared). Currently used in the WebExtensions APIs (to
    // provide the `devtools.panels.elements.onSelectionChanged` event).
    this.emit("selection-changed");

    const targetFrontActorID = this.selection?.nodeFront?.targetFront?.actorID;
    if (targetFrontActorID) {
      this.selectTarget(targetFrontActorID);
    }
  };

  #onToolSelected = () => {
    this.#refreshHostTitle();

    this.updatePickerButton();
    this.updateFrameButton();
    this.updateErrorCountButton();

    // Calling #renderToolboxButtons in case the visibility of a button changed.
    this.#renderToolboxButtons();
  };

  /**
   * Listener for "inspectObject" event on console top level target actor.
   */
  #onInspectObject = packet => {
    this.inspectObjectActor(packet.objectActor, packet.inspectFromAnnotation);
  };

  async inspectObjectActor(objectActor, inspectFromAnnotation) {
    const objectGrip = objectActor?.getGrip
      ? objectActor.getGrip()
      : objectActor;

    if (
      objectGrip.preview &&
      objectGrip.preview.nodeType === domNodeConstants.ELEMENT_NODE
    ) {
      await this.viewElementInInspector(objectGrip, inspectFromAnnotation);
      return;
    }

    if (objectGrip.class == "Function") {
      if (!objectGrip.location) {
        console.error("Missing location in Function objectGrip", objectGrip);
        return;
      }

      const { url, line, column } = objectGrip.location;
      await this.viewSourceInDebugger(url, line, column);
      return;
    }

    if (objectGrip.type !== "null" && objectGrip.type !== "undefined") {
      // Open then split console and inspect the object in the variables view,
      // when the objectActor doesn't represent an undefined or null value.
      if (this.currentToolId != "webconsole") {
        await this.openSplitConsole();
      }

      const panel = this.getPanel("webconsole");
      panel.hud.ui.inspectObjectActor(objectActor);
    }
  }

  /**
   * Get the toolbox's notification component
   *
   * @return The notification box component.
   */
  getNotificationBox() {
    return this.notificationBox;
  }

  async closeToolbox() {
    await this.destroy();
  }

  /**
   * Public API to check is the current toolbox is currently being destroyed.
   */
  isDestroying() {
    return !!this.#destroyer;
  }

  /**
   * Remove all UI elements, detach from target and clear up
   */
  destroy() {
    // If several things call destroy then we give them all the same
    // destruction promise so we're sure to destroy only once
    if (this.#destroyer) {
      return !!this.#destroyer;
    }

    // This pattern allows to immediately return the destroyer promise.
    // See Bug 1602727 for more details.
    let destroyerResolve;
    this.#destroyer = new Promise(r => (destroyerResolve = r));
    this.#destroyToolbox().then(destroyerResolve);

    return this.#destroyer;
  }

  async #destroyToolbox() {
    this.emit("destroy");

    // This flag will be checked by Fronts in order to decide if they should
    // skip their destroy.
    this.commands.client.isToolboxDestroy = true;

    this.#descriptorFront.off("descriptor-destroyed", this.destroy);

    this.off("select", this.#onToolSelected);
    this.off("host-changed", this.#refreshHostTitle);

    gDevTools.off("tool-registered", this.#toolRegistered);
    gDevTools.off("tool-unregistered", this.#toolUnregistered);

    for (const prefName in CONFIGURATION_PREFS) {
      Services.prefs.removeObserver(prefName, this.#onConfigurationPrefChange);
    }
    Services.prefs.removeObserver(
      BROWSERTOOLBOX_SCOPE_PREF,
      this.#refreshHostTitle
    );
    lazy.LocalModeMappings.destroy(this);

    // We normally handle toolClosed from selectTool() but in the event of the
    // toolbox closing we need to handle it here instead.
    this.telemetry.toolClosed(this.currentToolId, this);

    this.#lastFocusedElement = null;
    this.#pausedTargets = null;

    if (this.#sourceMapLoader) {
      this.#sourceMapLoader.destroy();
      this.#sourceMapLoader = null;
    }

    if (this.#parserWorker) {
      this.#parserWorker.stop();
      this.#parserWorker = null;
    }

    if (this.webconsolePanel) {
      this.#saveSplitConsoleHeight();
      this.webconsolePanel.removeEventListener(
        "resize",
        this.#saveSplitConsoleHeight
      );
      this.webconsolePanel = null;
    }
    if (this.#tabBar) {
      this.#tabBar.removeEventListener(
        "keypress",
        this.#onToolbarArrowKeypress
      );
    }
    if (this.#componentMount) {
      this.ReactDOM.unmountComponentAtNode(this.#componentMount);
      this.component = null;
      this.#componentMount = null;
      this.#tabBar = null;
      this.#appBoundary = null;
    }
    this.destroyHarAutomation();

    for (const [id, panel] of this.#toolPanels) {
      try {
        gDevTools.emit(id + "-destroy", this, panel);
        this.emit(id + "-destroy", panel);

        const rv = panel.destroy();
        if (rv) {
          console.error(
            `Panel ${id}'s destroy method returned something whereas it shouldn't (and should be synchronous).`
          );
        }
      } catch (e) {
        // We don't want to stop here if any panel fail to close.
        console.error("Panel " + id + ":", e);
      }
    }

    this.browserRequire = null;
    this.#toolNames = null;

    // Reset preferences set by the toolbox, then remove the preference front.
    const onResetPreference = this.resetPreference().then(() => {
      this.#preferenceFrontRequest = null;
    });

    this.commands.targetCommand.unwatchTargets({
      types: this.commands.targetCommand.ALL_TYPES,
      onAvailable: this.#onTargetAvailable,
      onSelected: this.#onTargetSelected,
      onDestroyed: this.#onTargetDestroyed,
    });

    const watchedResources = [
      this.commands.resourceCommand.TYPES.CONSOLE_MESSAGE,
      this.commands.resourceCommand.TYPES.ERROR_MESSAGE,
      this.commands.resourceCommand.TYPES.DOCUMENT_EVENT,
      this.commands.resourceCommand.TYPES.THREAD_STATE,
    ];

    if (!this.isBrowserToolbox) {
      watchedResources.push(this.commands.resourceCommand.TYPES.NETWORK_EVENT);
    }

    if (
      Services.prefs.getBoolPref(
        "devtools.command-button-jstracer.enabled",
        false
      )
    ) {
      watchedResources.push(this.commands.resourceCommand.TYPES.JSTRACER_STATE);
      this.commands.tracerCommand.off("toggle", this.onTracerToggled);
    }

    this.commands.resourceCommand.unwatchResources(watchedResources, {
      onAvailable: this.#onResourceAvailable,
    });

    // Unregister buttons listeners
    if (this.toolbarButtons) {
      this.toolbarButtons.forEach(button => {
        if (typeof button.teardown == "function") {
          // teardown arguments have already been bound in #createButtonState
          button.teardown();
        }
      });
    }

    // Grab a reference to win before toolbox state is cleared.
    const win = this.win;
    const host = this.#getTelemetryHostString();
    const width = Math.ceil(win.outerWidth / 50) * 50;
    const prevPanelName = this.getTelemetryPanelNameOrOther(this.currentToolId);

    this.telemetry.toolClosed("toolbox", this);
    this.telemetry.recordEvent("exit", prevPanelName, null, {
      host,
      width,
      panel_name: this.getTelemetryPanelNameOrOther(this.currentToolId),
      next_panel: "none",
      reason: "toolbox_close",
    });
    this.telemetry.recordEvent("close", "tools", null, {
      host,
      width,
    });

    // Wait for the preferences to be reset before destroying the target descriptor (which will destroy the preference front)
    const onceDestroyed = new Promise(resolve => {
      resolve(
        onResetPreference
          .catch(console.error)
          .then(async () => {
            // Destroy the node picker *after* destroying the panel,
            // which may still try to access it. (And might spawn a new one)
            if (this.#nodePicker) {
              this.#nodePicker.destroy();
              this.#nodePicker = null;
            }
            this.selection.destroy();
            this.selection = null;

            if (this.#netMonitorAPI) {
              this.#netMonitorAPI.destroy();
              this.#netMonitorAPI = null;
            }

            if (this.#sourceMapURLService) {
              await this.#sourceMapURLService.waitForSourcesLoading();
              this.#sourceMapURLService.destroy();
              this.#sourceMapURLService = null;
            }

            this.#removeWindowListeners();
            this.#removeChromeEventHandlerEvents();

            if (this.#store) {
              // Prevents any further action from being dispatched.
              // Do that late as NetMonitorAPI may still trigger some actions.
              this.#store.dispatch(START_IGNORE_ACTION);
              this.#store = null;
            }

            // All Commands need to be destroyed.
            // This is done after other destruction tasks since it may tear down
            // fronts and the debugger transport which earlier destroy methods may
            // require to complete.
            // (i.e. avoid exceptions about closing connection with pending requests)
            //
            // For similar reasons, only destroy the TargetCommand after every
            // other outstanding cleanup is done. Destroying the target list
            // will lead to destroy frame targets which can temporarily make
            // some fronts unresponsive and block the cleanup.
            return this.commands.destroy();
          }, console.error)
          .then(() => {
            this.emit("destroyed");

            this.#win = null;
            this.#toolPanels.clear();
            this.#descriptorFront = null;
            this.commands = null;
            this.#visibleIframes.clear();

            // Force GC to prevent long GC pauses when running tests and to free up
            // memory in general when the toolbox is closed.
            if (flags.testing) {
              win.windowUtils.garbageCollect();
            }
          })
          .catch(console.error)
      );
    });

    const leakCheckObserver = ({ wrappedJSObject: barrier }) => {
      // Make the leak detector wait until this toolbox is properly destroyed.
      barrier.client.addBlocker(
        "DevTools: Wait until toolbox is destroyed",
        onceDestroyed
      );
    };

    const topic = "shutdown-leaks-before-check";
    Services.obs.addObserver(leakCheckObserver, topic);

    await onceDestroyed;

    Services.obs.removeObserver(leakCheckObserver, topic);
  }

  /**
   * Open the textbox context menu at given coordinates.
   * Panels in the toolbox can call this on contextmenu events with event.screenX/Y
   * instead of having to implement their own copy/paste/selectAll menu.
   *
   * @param {number} x
   * @param {number} y
   */
  openTextBoxContextMenu(x, y) {
    const menu = createEditContextMenu(this.topWindow, "toolbox-menu");

    // Fire event for tests
    menu.once("open", () => this.emit("menu-open"));
    menu.once("close", () => this.emit("menu-close"));

    menu.popup(x, y, this.doc);
  }

  /**
   *  Retrieve the current textbox context menu, if available.
   */
  getTextBoxContextMenu() {
    return this.topDoc.getElementById("toolbox-menu");
  }

  /**
   * Reset preferences set by the toolbox.
   */
  async resetPreference() {
    if (
      // No preferences have been changed, so there is nothing to reset.
      !this.#preferenceFrontRequest ||
      // Did any pertinent prefs actually change? For autohide and the pseudo-locale,
      // only reset prefs in the Browser Toolbox if it's been toggled in the UI
      // (don't reset the pref if it was already set before opening)
      (!this.#autohideHasBeenToggled && !this.#pseudoLocaleChanged)
    ) {
      return;
    }

    const preferenceFront = await this.preferenceFront;

    if (this.#autohideHasBeenToggled) {
      await preferenceFront.clearUserPref(DISABLE_AUTOHIDE_PREF);
    }
    if (this.#pseudoLocaleChanged) {
      await preferenceFront.clearUserPref(PSEUDO_LOCALE_PREF);
    }
  }

  // HAR Automation

  async initHarAutomation() {
    const autoExport = Services.prefs.getBoolPref(
      "devtools.netmonitor.har.enableAutoExportToFile"
    );
    if (autoExport) {
      this.harAutomation = new HarAutomation();
      await this.harAutomation.initialize(this);
    }
  }
  destroyHarAutomation() {
    if (this.harAutomation) {
      this.harAutomation.destroy();
    }
  }

  /**
   * Returns gViewSourceUtils for viewing source.
   */
  get gViewSourceUtils() {
    return this.win.gViewSourceUtils;
  }

  /**
   * Open a CSS file when there is no line or column information available.
   *
   * @param {string} url The URL of the CSS file to open.
   */
  async viewStyleGeneratedSource(url) {
    if (typeof url !== "string") {
      console.warn("Failed to open generated source, no url given");
      return false;
    }

    // The style editor hides the generated file if the file has original
    // sources, so we have no choice but to open whichever original file
    // corresponds to the first line of the generated file.
    // TODO: Update this when sourcemaps support for stylesheets is supported
    // in the debugger.
    return this.viewStyleSourceByURL(url, 1);
  }

  /**
   * Given a URL for a stylesheet (generated or original), open in the debugger
   * if the `devtools.debugger.features.stylesheets-in-debugger` pref is enabled
   *  or open in the style editor if possible. Falls back to plain "view-source:".
   * If the stylesheet has a sourcemap, we will attempt to open the original
   * version of the file instead of the generated version.
   */
  async viewStyleSourceByURL(url, line, column) {
    if (typeof url !== "string") {
      console.warn("Failed to open source, no url given");
      return false;
    }
    if (typeof line !== "number") {
      console.warn(
        "No line given when navigating to source. If you're seeing this, there is a bug."
      );

      // This is a fallback in case of programming errors, but in a perfect
      // world, viewStyleSourceByURL would always get a line/column.
      line = 1;
      column = null;
    }

    // Instead view the stylesheet in the debugger since the pref is enabled
    if (Services.prefs.getBoolPref(DEVTOOLS_STYLESHEETS_IN_DEBUGGER)) {
      Glean.devtoolsDebuggerStylesheets.linksOpenedInDebuggerCount.add(1);
      return viewSource.viewSourceInDebugger(this, url, line, column, null);
    }
    Glean.devtoolsStyleeditorStylesheets.linksOpenedInStyleEditorCount.add(1);
    return viewSource.viewSourceInStyleEditor(this, url, line, column);
  }

  /**
   * Opens source in style editor. Falls back to plain "view-source:".
   * If the stylesheet has a sourcemap, we will attempt to open the original
   * version of the file instead of the generated version.
   */
  async viewStyleSourceByResource(stylesheetResource, line, column) {
    if (!stylesheetResource || typeof stylesheetResource !== "object") {
      console.warn("Failed to open source, no stylesheet given");
      return false;
    }
    if (typeof line !== "number") {
      console.warn(
        "No line given when navigating to source. If you're seeing this, there is a bug."
      );

      // This is a fallback in case of programming errors, but in a perfect
      // world, viewStyleSourceByResource would always get a line/colum.
      line = 1;
      column = null;
    }

    // Instead view the stylesheet in the debugger since the pref is enabled
    if (Services.prefs.getBoolPref(DEVTOOLS_STYLESHEETS_IN_DEBUGGER)) {
      Glean.devtoolsDebuggerStylesheets.linksOpenedInDebuggerCount.add(1);
      return viewSource.viewSourceInDebugger(
        this,
        stylesheetResource.href,
        line,
        column,
        stylesheetResource.resourceId
      );
    }
    Glean.devtoolsStyleeditorStylesheets.linksOpenedInStyleEditorCount.add(1);
    return viewSource.viewSourceInStyleEditor(
      this,
      stylesheetResource,
      line,
      column
    );
  }

  async viewElementInInspector(objectGrip, reason) {
    // Open the inspector and select the DOM Element.
    await this.loadTool("inspector");
    const inspector = this.getPanel("inspector");
    const nodeFound = await inspector.inspectNodeActor(objectGrip, reason);
    if (nodeFound) {
      await this.selectTool("inspector", reason);
    }
  }

  /**
   * Open a JS file when there is no line or column information available.
   *
   * @param {string} url The URL of the JS file to open.
   */
  async viewGeneratedSourceInDebugger(url) {
    if (typeof url !== "string") {
      console.warn("Failed to open generated source, no url given");
      return false;
    }

    return viewSource.viewSourceInDebugger(this, url, null, null, null, null);
  }

  /**
   * Opens source in debugger, the sourcemapped location will be selected in
   * the debugger panel, if the given location resolves to a know sourcemapped one.
   *
   * Falls back to plain "view-source:".
   *
   * @see devtools/client/shared/source-utils.js
   */
  async viewSourceInDebugger(
    sourceURL,
    sourceLine,
    sourceColumn,
    sourceId,
    reason
  ) {
    if (typeof sourceURL !== "string" && typeof sourceId !== "string") {
      console.warn("Failed to open generated source, no url/id given");
      return false;
    }
    if (typeof sourceLine !== "number") {
      console.warn(
        "No line given when navigating to source. If you're seeing this, there is a bug."
      );

      // This is a fallback in case of programming errors, but in a perfect
      // world, viewSourceInDebugger would always get a line/colum.
      sourceLine = 1;
      sourceColumn = null;
    }

    return viewSource.viewSourceInDebugger(
      this,
      sourceURL,
      sourceLine,
      sourceColumn,
      sourceId,
      reason
    );
  }

  /**
   * Opens source in plain "view-source:".
   *
   * @see devtools/client/shared/source-utils.js
   */
  viewSource(sourceURL, sourceLine, sourceColumn) {
    return viewSource.viewSource(this, sourceURL, sourceLine, sourceColumn);
  }

  // Support for WebExtensions API (`devtools.network.*`)

  /**
   * Return Netmonitor API object. This object offers Network monitor
   * public API that can be consumed by other panels or WE API.
   */
  async getNetMonitorAPI() {
    const netPanel = this.getPanel("netmonitor");

    // Return Net panel if it exists.
    if (netPanel) {
      return netPanel.panelWin.Netmonitor.api;
    }

    if (this.#netMonitorAPI) {
      return this.#netMonitorAPI;
    }

    // Create and initialize Network monitor API object.
    // This object is only connected to the backend - not to the UI.
    this.#netMonitorAPI = new NetMonitorAPI();
    await this.#netMonitorAPI.connect(this);

    return this.#netMonitorAPI;
  }

  /**
   * Returns data (HAR) collected by the Network panel.
   */
  async getHARFromNetMonitor() {
    const netMonitor = await this.getNetMonitorAPI();
    let har = await netMonitor.getHar();

    // Return default empty HAR file if needed.
    har = har || buildHarLog(Services.appinfo);

    // Return the log directly to be compatible with
    // Chrome WebExtension API.
    return har.log;
  }

  /**
   * Add listener for `onRequestFinished` events.
   *
   * @param {object} listener
   *        The listener to be called it's expected to be
   *        a function that takes ({harEntry, requestId})
   *        as first argument.
   */
  async addRequestFinishedListener(listener) {
    const netMonitor = await this.getNetMonitorAPI();
    netMonitor.addRequestFinishedListener(listener);
  }

  async removeRequestFinishedListener(listener) {
    const netMonitor = await this.getNetMonitorAPI();
    netMonitor.removeRequestFinishedListener(listener);

    // Destroy Network monitor API object if the following is true:
    // 1) there is no listener
    // 2) the Net panel doesn't exist/use the API object (if the panel
    //    exists it's also responsible for destroying it,
    //    see `NetMonitorPanel.open` for more details)
    const netPanel = this.getPanel("netmonitor");
    const hasListeners = netMonitor.hasRequestFinishedListeners();
    if (this.#netMonitorAPI && !hasListeners && !netPanel) {
      this.#netMonitorAPI.destroy();
      this.#netMonitorAPI = null;
    }
  }

  /**
   * Used to lazily fetch HTTP response content within
   * `onRequestFinished` event listener.
   *
   * @param {string} requestId
   *        Id of the request for which the response content
   *        should be fetched.
   */
  async fetchResponseContent(requestId) {
    const netMonitor = await this.getNetMonitorAPI();
    return netMonitor.fetchResponseContent(requestId);
  }

  // Support management of installed WebExtensions that provide a devtools_page.

  /**
   * List the subset of the active WebExtensions which have a devtools_page (used by
   * toolbox-options.js to create the list of the tools provided by the enabled
   * WebExtensions).
   *
   * @see devtools/client/framework/toolbox-options.js
   */
  listWebExtensions() {
    // Return an iterator of the enabled webextensions (we can't use the prefs list here,
    // because some of them may be disabled by the Addon Manager and still have a devtools
    // preference).
    return this.#webExtensions.entries().map(([uuid, { name, pref }]) => {
      return { uuid, name, pref };
    });
  }

  /**
   * Add a WebExtension to the list of the active extensions (given the extension UUID,
   * a unique id assigned to an extension when it is installed, and its name),
   * and emit a "webextension-registered" event to allow toolbox-options.js
   * to refresh the listed tools accordingly.
   *
   * @see browser/components/extensions/parent/ext-devtools.js
   */
  registerWebExtension(extensionUUID, { name, pref }) {
    // Ensure that an installed extension (active in the AddonManager) which
    // provides a devtools page is going to be listed in the toolbox options
    // (and refresh its name if it was already listed).
    this.#webExtensions.set(extensionUUID, { name, pref });
    this.emit("webextension-registered", extensionUUID);
  }

  /**
   * Remove an active WebExtension from the list of the active extensions (given the
   * extension UUID, a unique id assigned to an extension when it is installed, and its
   * name), and emit a "webextension-unregistered" event to allow toolbox-options.js
   * to refresh the listed tools accordingly.
   *
   * @see browser/components/extensions/parent/ext-devtools.js
   */
  unregisterWebExtension(extensionUUID) {
    // Ensure that an extension that has been disabled/uninstalled from the AddonManager
    // is going to be removed from the toolbox options.
    this.#webExtensions.delete(extensionUUID);
    this.emit("webextension-unregistered", extensionUUID);
  }

  /**
   * A helper function which returns true if the extension with the given UUID is listed
   * as active for the toolbox and has its related devtools about:config preference set
   * to true.
   *
   * @see browser/components/extensions/parent/ext-devtools.js
   */
  isWebExtensionEnabled(extensionUUID) {
    const extInfo = this.#webExtensions.get(extensionUUID);
    return extInfo && Services.prefs.getBoolPref(extInfo.pref, false);
  }

  /**
   * Returns a panel id in the case of built in panels or "other" in the case of
   * third party panels. This is necessary due to limitations in addon id strings,
   * the permitted length of event telemetry property values and what we actually
   * want to see in our telemetry.
   *
   * @param {string} id
   *        The panel id we would like to process.
   */
  getTelemetryPanelNameOrOther(id) {
    if (!this.#toolNames) {
      const definitions = gDevTools.getToolDefinitionArray();
      const definitionIds = definitions.map(definition => definition.id);

      this.#toolNames = new Set(definitionIds);
    }

    if (!this.#toolNames.has(id)) {
      return "other";
    }

    return id;
  }

  /**
   * Sets basic information on the DebugTargetInfo component
   */
  #setDebugTargetData() {
    // Note that local WebExtension are debugged via WINDOW host,
    // but we still want to display target data.
    if (
      this.hostType === Toolbox.HostType.PAGE ||
      this.#descriptorFront.isWebExtensionDescriptor
    ) {
      // Displays DebugTargetInfo which shows the basic information of debug target,
      // if `about:devtools-toolbox` URL opens directly.
      // DebugTargetInfo requires this.#debugTargetData to be populated
      this.component.setDebugTargetData(this.#getDebugTargetData());
    }
  }

  #onResourceAvailable = resources => {
    let errors = this.#errorCount || 0;

    const { TYPES } = this.commands.resourceCommand;
    for (const resource of resources) {
      const { resourceType } = resource;
      if (
        resourceType === TYPES.ERROR_MESSAGE &&
        // ERROR_MESSAGE resources can be warnings/info, but here we only want to count errors
        resource.pageError.error
      ) {
        errors++;
        continue;
      }

      if (resourceType === TYPES.CONSOLE_MESSAGE) {
        const { level } = resource;
        if (level === "error" || level === "exception" || level === "assert") {
          errors++;
        }

        // Reset the count on console.clear
        if (level === "clear") {
          errors = 0;
        }
      }

      // Only consider top level document, and ignore remote iframes top document
      if (
        resourceType === TYPES.DOCUMENT_EVENT &&
        resource.name === "will-navigate" &&
        resource.targetFront.isTopLevel
      ) {
        this.#onWillNavigate({
          isFrameSwitching: resource.isFrameSwitching,
        });
        // While we will call `setErrorCount(0)` from onWillNavigate, we also need to reset
        // `errors` local variable in order to clear previous errors processed in the same
        // throttling bucket as this will-navigate resource.
        errors = 0;
      }

      if (resourceType == TYPES.THREAD_STATE) {
        this.#onThreadStateChanged(resource);
      }
      if (resourceType == TYPES.JSTRACER_STATE) {
        this.#onTracingStateChanged(resource);
      }
    }

    this.setErrorCount(errors);
  };

  #onResourceUpdated = resources => {
    let errors = this.#errorCount || 0;

    for (const { update } of resources) {
      // In order to match webconsole behaviour, we treat 4xx and 5xx network calls as errors.
      if (
        update.resourceType ===
          this.commands.resourceCommand.TYPES.NETWORK_EVENT &&
        update.resourceUpdates.status &&
        update.resourceUpdates.status.toString().match(REGEX_4XX_5XX)
      ) {
        errors++;
      }
    }

    this.setErrorCount(errors);
  };

  /**
   * Called by TargetCommand whenever the top level target navigated to a new document
   * and its `url` and `title` are guaranteed to be updated to the new location.
   */
  #onTargetLocationUpdated(targetFront) {
    this.#updateFrames({
      frameData: {
        id: targetFront.actorID,
        url: targetFront.url,
        title: targetFront.title,
      },
    });

    if (targetFront.isTopLevel) {
      this.#refreshHostTitle();
      this.#setDebugTargetData();
    }
  }

  /**
   * Set the number of errors in the toolbar icon.
   *
   * @param {number} count
   */
  setErrorCount(count) {
    // Don't re-render if the number of errors changed
    if (!this.component || this.#errorCount === count) {
      return;
    }

    this.#errorCount = count;

    // Update button properties and trigger a render of the toolbox
    this.updateErrorCountButton();
    this.#throttledSetToolboxButtons();
  }
}

exports.Toolbox = Toolbox;

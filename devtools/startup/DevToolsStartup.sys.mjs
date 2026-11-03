/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This XPCOM component is loaded very early.
 * Be careful to lazy load dependencies as much as possible.
 *
 * It manages all the possible entry points for DevTools:
 * - Handles command line arguments like -jsconsole,
 * - Register all key shortcuts,
 * - Listen for "Browser Tools" system menu opening, under "Tools",
 * - Inject the wrench icon in toolbar customization, which is used
 *   by the "Browser Tools" list displayed in the hamburger menu,
 * - Register the JSON Viewer protocol handler.
 * - Inject the profiler recording button in toolbar customization.
 *
 * Only once any of these entry point is fired, this module ensures starting
 * core modules like 'devtools-browser.js' that hooks the browser windows
 * and ensure setting up tools.
 */

const kDebuggerPrefs = [
  "devtools.debugger.remote-enabled",
  "devtools.chrome.enabled",
];

const DEVTOOLS_POLICY_DISABLED_PREF = "devtools.policy.disabled";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserToolboxLauncher:
    "resource://devtools/client/framework/browser-toolbox/Launcher.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  CustomizableWidgets:
    "moz-src:///browser/components/customizableui/CustomizableWidgets.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ProfilerMenuButton:
    "resource://devtools/client/performance-new/popup/menu-button.sys.mjs",
  WebChannel: "resource://gre/modules/WebChannel.sys.mjs",
});

// We don't want to spend time initializing the full loader here so we create
// our own lazy require.
ChromeUtils.defineLazyGetter(lazy, "Telemetry", function () {
  const { require } = ChromeUtils.importESModule(
    "resource://devtools/shared/loader/Loader.sys.mjs"
  );
  // eslint-disable-next-line no-shadow
  const Telemetry = require("devtools/client/shared/telemetry");

  return Telemetry;
});

ChromeUtils.defineLazyGetter(lazy, "KeyShortcutsBundle", function () {
  return new Localization(["devtools/startup/key-shortcuts.ftl"], true);
});

/**
 * Safely retrieve a localized DevTools key shortcut from KeyShortcutsBundle.
 * If the shortcut is not available, this will return null. Consumer code
 * should rely on this to skip unavailable shortcuts.
 *
 * Note that all shortcuts should always be available, but there is a notable
 * exception, which is why we have to do this. When a localization change is
 * uplifted to beta, language packs will not be updated immediately when the
 * updated beta is available.
 *
 * This means that language pack users might get a new Beta version but will not
 * have a language pack with the new strings yet.
 *
 * @param {string} id
 * @returns {string|null}
 */
function getLocalizedKeyShortcut(id) {
  try {
    return lazy.KeyShortcutsBundle.formatValueSync(id);
  } catch (e) {
    console.error("Failed to retrieve DevTools localized shortcut for id", id);
    return null;
  }
}

ChromeUtils.defineLazyGetter(lazy, "KeyShortcuts", function () {
  const isMac = AppConstants.platform == "macosx";

  // Common modifier shared by most key shortcuts
  const modifiers = isMac ? "accel,alt" : "accel,shift";

  // List of all key shortcuts triggering installation UI
  // `id` should match tool's id from client/definitions.js
  const shortcuts = [
    // The following keys are also registered in /client/menus.js
    // And should be synced.

    // Both are toggling the toolbox on the last selected panel
    // or the default one.
    {
      id: "toggleToolbox",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-toggle-toolbox"),
      modifiers,
    },
    // All locales are using F12
    {
      id: "toggleToolboxF12",
      shortcut: getLocalizedKeyShortcut(
        "devtools-commandkey-toggle-toolbox-f12"
      ),
      modifiers: "", // F12 is the only one without modifiers
    },
    // Open the Browser Toolbox
    {
      id: "browserToolbox",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-browser-toolbox"),
      modifiers: "accel,alt,shift",
    },
    // Open the Browser Console
    {
      id: "browserConsole",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-browser-console"),
      modifiers: "accel,shift",
    },
    // Toggle the Responsive Design Mode
    {
      id: "responsiveDesignMode",
      shortcut: getLocalizedKeyShortcut(
        "devtools-commandkey-responsive-design-mode"
      ),
      modifiers,
    },
    // The following keys are also registered in /client/definitions.js
    // and should be synced.

    // Key for opening the Inspector
    {
      toolId: "inspector",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-inspector"),
      modifiers,
    },
    // Key for opening the Web Console
    {
      toolId: "webconsole",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-webconsole"),
      modifiers,
    },
    // Key for opening the Debugger
    {
      toolId: "jsdebugger",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-jsdebugger"),
      modifiers,
    },
    // Key for opening the Network Monitor
    {
      toolId: "netmonitor",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-netmonitor"),
      modifiers,
    },
    // Key for opening the Style Editor
    {
      toolId: "styleeditor",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-styleeditor"),
      modifiers: "shift",
    },
    // Key for opening the Performance Panel
    {
      toolId: "performance",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-performance"),
      modifiers: "shift",
    },
    // Key for opening the Storage Panel
    {
      toolId: "storage",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-storage"),
      modifiers: "shift",
    },
    // Key for opening the DOM Panel
    {
      toolId: "dom",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-dom"),
      modifiers,
    },
    // Key for opening the Accessibility Panel
    {
      toolId: "accessibility",
      shortcut: getLocalizedKeyShortcut(
        "devtools-commandkey-accessibility-f12"
      ),
      modifiers: "shift",
    },
  ];

  if (isMac) {
    // Add the extra key command for macOS, so you can open the inspector with cmd+shift+C
    // like on Chrome DevTools.
    shortcuts.push({
      id: "inspectorMac",
      toolId: "inspector",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-inspector"),
      modifiers: "accel,shift",
    });
  }

  if (lazy.ProfilerMenuButton.isInNavbar()) {
    shortcuts.push(...getProfilerKeyShortcuts());
  }

  // Allow toggling the JavaScript tracing not only from DevTools UI,
  // but also from the web page when it is focused.
  if (
    Services.prefs.getBoolPref(
      "devtools.command-button-jstracer.enabled",
      false
    )
  ) {
    shortcuts.push({
      id: "javascriptTracingToggle",
      shortcut: getLocalizedKeyShortcut(
        "devtools-commandkey-javascript-tracing-toggle"
      ),
      modifiers: "control,shift",
    });
  }

  return shortcuts;
});

function getProfilerKeyShortcuts() {
  return [
    // Start/stop the profiler
    {
      id: "profilerStartStop",
      shortcut: getLocalizedKeyShortcut(
        "devtools-commandkey-profiler-start-stop"
      ),
      modifiers: "control,shift",
    },
    // Capture a profile
    {
      id: "profilerCapture",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-profiler-capture"),
      modifiers: "control,shift",
    },
    // Because it's not uncommon for content or extension to bind this
    // shortcut, allow using alt as well for starting and stopping the profiler
    {
      id: "profilerStartStopAlternate",
      shortcut: getLocalizedKeyShortcut(
        "devtools-commandkey-profiler-start-stop"
      ),
      modifiers: "control,shift,alt",
    },
    {
      id: "profilerCaptureAlternate",
      shortcut: getLocalizedKeyShortcut("devtools-commandkey-profiler-capture"),
      modifiers: "control,shift,alt",
    },
  ];
}

/**
 * Validate the URL that will be used for the WebChannel for the profiler.
 *
 * @param {string} targetUrl
 * @returns {string}
 */
export function validateProfilerWebChannelUrl(targetUrl) {
  const frontEndUrl = "https://profiler.firefox.com";

  if (targetUrl !== frontEndUrl) {
    // The user can specify either localhost or deploy previews as well as
    // the official frontend URL for testing.
    if (
      // Allow a test URL.
      /^https?:\/\/example\.com$/.test(targetUrl) ||
      // Allows the following:
      //   "http://localhost:4242"
      //   "http://localhost:4242/"
      //   "http://localhost:3"
      //   "http://localhost:334798455"
      /^http:\/\/localhost:\d+\/?$/.test(targetUrl) ||
      // Allows the following:
      //   "https://deploy-preview-1234--perf-html.netlify.com"
      //   "https://deploy-preview-1234--perf-html.netlify.com/"
      //   "https://deploy-preview-1234567--perf-html.netlify.app"
      //   "https://main--perf-html.netlify.app"
      /^https:\/\/(?:deploy-preview-\d+|main)--perf-html\.netlify\.(?:com|app)\/?$/.test(
        targetUrl
      )
    ) {
      // This URL is one of the allowed ones to be used for configuration.
      return targetUrl;
    }

    console.error(
      `The preference "devtools.performance.recording.ui-base-url" was set to a ` +
        "URL that is not allowed. No WebChannel messages will be sent between the " +
        `browser and that URL. Falling back to ${frontEndUrl}. Only localhost ` +
        "and deploy previews URLs are allowed.",
      targetUrl
    );
  }

  return frontEndUrl;
}

ChromeUtils.defineLazyGetter(lazy, "ProfilerPopupBackground", function () {
  return ChromeUtils.importESModule(
    "resource://devtools/client/performance-new/shared/background.sys.mjs"
  );
});

// eslint-disable-next-line jsdoc/require-jsdoc
export class DevToolsStartup {
  constructor() {
    this.onWindowReady = this.onWindowReady.bind(this);
    this.addDevToolsItemsToSubview = this.addDevToolsItemsToSubview.bind(this);
    this.onMoreToolsViewShowing = this.onMoreToolsViewShowing.bind(this);
    this.toggleProfilerKeyShortcuts =
      this.toggleProfilerKeyShortcuts.bind(this);
    this.onDisabledByPolicyChanged = this.onDisabledByPolicyChanged.bind(this);
    this.onBrowserToolsMenuPopupShowing =
      this.onBrowserToolsMenuPopupShowing.bind(this);
  }
  /**
   * Boolean flag to check if DevTools have been already initialized or not.
   * By initialized, we mean that its main modules are loaded.
   */
  initialized = false;

  /**
   * Boolean flag to check if the devtools initialization was already sent to telemetry.
   * We only want to record one devtools entry point per Firefox run, but we are not
   * interested in all the entry points.
   */
  recorded = false;

  get telemetry() {
    if (!this._telemetry) {
      this._telemetry = new lazy.Telemetry();
    }
    return this._telemetry;
  }

  /**
   * The developer toggle widget definition. Built once and reused across
   * disable/enable cycles.
   */
  developerToggle = null;

  /**
   * Whether the developer toggle is currently registered with CustomizableUI.
   */
  developerToggleRegistered = false;

  /**
   * Flag that indicates if the profiler recording popup was already added to
   * customizableUI.
   */
  profilerRecordingButtonCreated = false;

  /**
   * Set of currently hooked windows
   */
  hookedWindows = new WeakSet();

  isDisabledByPolicy() {
    return Services.prefs.getBoolPref(DEVTOOLS_POLICY_DISABLED_PREF, false);
  }

  handle(cmdLine) {
    const flags = this.readCommandLineFlags(cmdLine);

    // handle() can be called after browser startup (e.g. opening links from other apps).
    const isInitialLaunch =
      cmdLine.state == Ci.nsICommandLine.STATE_INITIAL_LAUNCH;
    if (isInitialLaunch) {
      // Store devtoolsFlag to check it later in onWindowReady.
      this.devtoolsFlag = flags.devtools;

      /* eslint-disable mozilla/balanced-observers */
      // We are not expecting to remove those listeners until Firefox closes.

      // Only top level Firefox Windows fire a browser-delayed-startup-finished event
      Services.obs.addObserver(
        this.onWindowReady,
        "browser-delayed-startup-finished"
      );

      // Add DevTools menu items to the "More Tools" view.
      Services.obs.addObserver(
        this.onMoreToolsViewShowing,
        "web-developer-tools-view-showing"
      );
      // Add DevTools menu items so they can be picked up by the customize
      // keyboard shortcuts UI.
      Services.obs.addObserver(() => {
        // Initialize DevTools to create all menuitems in the system menu.
        this.initDevTools("CustomKeysUI");
      }, "customkeys-ui-showing");

      // React to the DisableDeveloperTools policy being applied or removed at
      // runtime.
      Services.prefs.addObserver(
        DEVTOOLS_POLICY_DISABLED_PREF,
        this.onDisabledByPolicyChanged
      );
      /* eslint-enable mozilla/balanced-observers */

      if (!this.isDisabledByPolicy()) {
        if (AppConstants.MOZ_DEV_EDITION) {
          // On DevEdition, the developer toggle is displayed by default in the navbar
          // area and should be created before the first paint.
          this.hookDeveloperToggle();
        }

        this.hookProfilerRecordingButton();
      }
    }

    if (flags.console) {
      this.commandLine = true;
      this.handleConsoleFlag(cmdLine);
    }
    if (flags.debugger) {
      this.commandLine = true;
      const binaryPath =
        typeof flags.debugger == "string" ? flags.debugger : null;
      this.handleDebuggerFlag(cmdLine, binaryPath);
    }

    if (flags.devToolsServer) {
      this.handleDevToolsServerFlag(cmdLine, flags.devToolsServer);
    }

    // If Firefox is already opened, and DevTools are also already opened,
    // try to open links passed via command line arguments.
    if (!isInitialLaunch && this.initialized && cmdLine.length) {
      this.checkForDebuggerLink(cmdLine);
    }
  }

  /**
   * Lookup in all arguments passed to firefox binary to find
   * URLs including a precise location, like this:
   *   https://domain.com/file.js:1:10 (URL ending with `:${line}:${number}`)
   * When such argument exists, try to open this source and precise location
   * in the debugger.
   *
   * @param {nsICommandLine} cmdLine
   */
  checkForDebuggerLink(cmdLine) {
    const urlFlagIdx = cmdLine.findFlag("url", false);
    // Bail out when there is no -url argument, or if that's last and so there is no URL after it.
    if (urlFlagIdx == -1 && urlFlagIdx + 1 < cmdLine.length) {
      return;
    }

    // The following code would only work if we have a top level browser window opened
    const window = Services.wm.getMostRecentBrowserWindow();
    if (!window) {
      return;
    }

    const urlParam = cmdLine.getArgument(urlFlagIdx + 1);

    // Avoid processing valid url like:
    //   http://foo@user:123
    // Note that when loading `http://foo.com` the URL of the default html page will be `http://foo.com/`.
    // So that there will always be another `/` after `https://`
    if (
      (urlParam.startsWith("http://") || urlParam.startsWith("https://")) &&
      urlParam.lastIndexOf("/") <= 7
    ) {
      return;
    }

    let match = urlParam.match(/^(?<url>\w+:.+):(?<line>\d+):(?<column>\d+)$/);
    if (!match) {
      // fallback on only having the line when there is no column
      match = urlParam.match(/^(?<url>\w+:.+):(?<line>\d+)?$/);
      if (!match) {
        return;
      }
    }

    // line and column are supposed to be 1-based.
    const { url, line, column } = match.groups;

    // Debugger internal uses 0-based column number.
    // NOTE: Non-debugger view-source doesn't use column number.
    const columnOneBased = parseInt(column || 0, 10);
    const columnZeroBased = columnOneBased > 0 ? columnOneBased - 1 : 0;

    // If for any reason the final url is invalid, ignore it
    try {
      Services.io.newURI(url);
    } catch (e) {
      return;
    }

    const require = this.initDevTools("CommandLine");
    const { gDevTools } = require("devtools/client/framework/devtools");
    const toolbox = gDevTools.getToolboxForTab(window.gBrowser.selectedTab);
    // Ignore the url if there is no devtools currently opened for the current tab
    if (!toolbox) {
      return;
    }

    // Avoid regular Firefox code from processing this argument,
    // otherwise we would open the source in DevTools and in a new tab.
    //
    // /!\ This has to be called synchronously from the call to `DevToolsStartup.handle(cmdLine)`
    //     Otherwise the next command lines listener will interpret the argument redundantly.
    cmdLine.removeArguments(urlFlagIdx, urlFlagIdx + 1);

    // Avoid opening a new empty top level window if there is no more arguments
    if (!cmdLine.length) {
      cmdLine.preventDefault = true;
    }

    // Immediately focus the browser window in order, to focus devtools, or the view-source tab.
    // Otherwise, without this, the terminal would still be the topmost window.
    toolbox.win.focus();

    // Note that the following method is async and returns a promise.
    // But the current method has to be synchronous because of cmdLine.removeArguments.
    // Also note that it will fallback to view-source when the source url isn't found in the debugger
    toolbox.viewSourceInDebugger(
      url,
      parseInt(line, 10),
      columnZeroBased,
      null,
      "CommandLine"
    );
  }

  readCommandLineFlags(cmdLine) {
    // All command line flags are disabled if DevTools are disabled by policy.
    if (this.isDisabledByPolicy()) {
      return {
        console: false,
        debugger: false,
        devtools: false,
        devToolsServer: false,
      };
    }

    const jsConsole = cmdLine.handleFlag("jsconsole", false);
    const devtools = cmdLine.handleFlag("devtools", false);

    let devToolsServer;
    try {
      devToolsServer = cmdLine.handleFlagWithParam(
        "start-debugger-server",
        false
      );
    } catch (e) {
      // We get an error if the option is given but not followed by a value.
      // By catching and trying again, the value is effectively optional.
      devToolsServer = cmdLine.handleFlag("start-debugger-server", false);
    }

    let debuggerFlag;
    try {
      debuggerFlag = cmdLine.handleFlagWithParam("jsdebugger", false);
    } catch (e) {
      // We get an error if the option is given but not followed by a value.
      // By catching and trying again, the value is effectively optional.
      debuggerFlag = cmdLine.handleFlag("jsdebugger", false);
    }

    return {
      console: jsConsole,
      debugger: debuggerFlag,
      devtools,
      devToolsServer,
    };
  }

  /**
   * Called when receiving the "browser-delayed-startup-finished" event for a new
   * top-level window.
   *
   * @param {Window} window
   */
  onWindowReady(window) {
    if (
      this.isDisabledByPolicy() ||
      AppConstants.MOZ_APP_NAME == "thunderbird"
    ) {
      return;
    }

    this.hookWindow(window);

    // This listener is called for all Firefox windows, but we want to execute some code
    // only once.
    if (!this._firstWindowReadyReceived) {
      this.onFirstWindowReady(window);
      this._firstWindowReadyReceived = true;
    }

    JsonView.initialize();
  }

  /**
   * React to the DisableDeveloperTools policy being toggled live: tear down any
   * open DevTools UI (toolboxes and Browser Console) when disabled, then hook
   * or unhook every browser window accordingly.
   */
  onDisabledByPolicyChanged() {
    const disabled = this.isDisabledByPolicy();

    if (disabled) {
      const { require } = ChromeUtils.importESModule(
        "resource://devtools/shared/loader/Loader.sys.mjs"
      );

      if (this.initialized) {
        // Close any open toolbox. They can only exist once DevTools have
        // been initialized, so don't load the framework otherwise
        const { gDevTools } = require("devtools/client/framework/devtools");
        gDevTools.closeAllToolboxes();
        lazy.BrowserToolboxLauncher.closeAll();
      }

      if (Services.wm.getMostRecentWindow("devtools:webconsole")) {
        // Close any open browser console.
        const {
          BrowserConsoleManager,
        } = require("devtools/client/webconsole/browser-console-manager");
        BrowserConsoleManager.closeBrowserConsole();
      }
      this.unhookDeveloperToggle();
    } else {
      this.hookDeveloperToggle();
    }

    for (const window of Services.wm.getEnumerator("navigator:browser")) {
      if (disabled) {
        this.unhookWindow(window);
      } else {
        this.hookWindow(window);
      }
    }
  }

  /**
   * Called when receiving the "browser-delayed-startup-finished" event for a top-level
   * window for the first time.
   *
   * @param {Window} window
   */
  onFirstWindowReady(window) {
    this.hookDeveloperToggle();

    if (this.devtoolsFlag) {
      this.handleDevToolsFlag(window);

      // In the case of the --jsconsole and --jsdebugger command line parameters
      // there was no browser window when they were processed so we act on the
      // this.commandline flag instead.
      if (this.commandLine) {
        this.sendEntryPointTelemetry("CommandLine");
      }
    }
  }

  /**
   * Register listeners to all possible entry points for Developer Tools.
   * But instead of implementing the actual actions, defer to DevTools codebase.
   * In most cases, it only needs to call this.initDevTools which handles the rest.
   * We do that to prevent loading any DevTools module until the user intent to use them.
   *
   * @param {Window} window
   */
  hookWindow(window) {
    if (this.hookedWindows.has(window)) {
      return;
    }

    if (window.document.documentElement.hasAttribute("mini-window")) {
      return;
    }

    // Key Shortcuts need to be added on all the created windows.
    this.hookKeyShortcuts(window);

    this.hookProfilerRecordingButton();

    // The developer menu hook only needs to be added if devtools have not been
    // initialized yet.
    if (!this.initialized) {
      this.hookBrowserToolsMenu(window);
    }

    this.hookedWindows.add(window);
  }

  /**
   * Unregister listeners to all possible entry points for Developer Tools.
   *
   * @param {Window} window
   */
  unhookWindow(window) {
    if (!this.hookedWindows.has(window)) {
      return;
    }

    this.unhookKeyShortcuts(window);
    this.unhookBrowserToolsMenu(window);

    this.hookedWindows.delete(window);
  }

  /**
   * Dynamically register a wrench icon in the customization menu.
   * You can use this button by right clicking on Firefox toolbar
   * and dragging it from the customization panel to the toolbar.
   * (i.e. this isn't displayed by default to users!)
   *
   * _But_, the "Browser Tools" entry in the hamburger menu (the menu with
   * 3 horizontal lines), is using this "developer-button" view to populate
   * its menu. So we have to register this button for the menu to work.
   *
   * Also, this menu duplicates its own entries from the "Browser Tools"
   * menu in the system menu, under "Tools" main menu item. The system
   * menu is being hooked by "hookBrowserToolsMenu" which ends up calling
   * devtools/client/framework/browser-menus to create the items for real,
   * initDevTools, from onViewShowing is also calling browser-menu.
   */
  hookDeveloperToggle() {
    if (AppConstants.MOZ_APP_NAME == "thunderbird") {
      return;
    }
    if (!this.developerToggle) {
      // Build the widget only once; it is reused every time the
      // toggle is re-registered after a disable/enable cycle.
      const id = "developer-button";
      const widget = lazy.CustomizableUI.getWidget(id);
      if (widget && widget.provider == lazy.CustomizableUI.PROVIDER_API) {
        return;
      }

      const panelviewId = "PanelUI-developer-tools";
      const subviewId = "PanelUI-developer-tools-view";

      this.developerToggle = {
        id,
        type: "view",
        viewId: panelviewId,
        l10nId: "toolbar-button-developer",
        shortcutId: "key_toggleToolbox",
        onViewShowing: event => {
          const doc = event.target.ownerDocument;
          const developerItems = lazy.PanelMultiView.getViewNode(
            doc,
            subviewId
          );
          this.addDevToolsItemsToSubview(developerItems);
        },
        onInit(anchor) {
          // Since onBeforeCreated already bails out when initialized, we can call
          // it right away.
          this.onBeforeCreated(anchor.ownerDocument);
        },
        onBeforeCreated: doc => {
          if (doc.documentElement.hasAttribute("mini-window")) {
            return;
          }

          // The developer toggle needs the "key_toggleToolbox" <key> element.
          // In DEV EDITION, the toggle is added before 1st paint and hookKeyShortcuts() is
          // not called yet when CustomizableUI creates the widget.
          this.hookKeyShortcuts(doc.defaultView);
        },
      };
    }

    if (!this.developerToggleRegistered) {
      lazy.CustomizableUI.createWidget(
        this.developerToggle,
        lazy.CustomizableUI.SOURCE_BUILTIN
      );
      lazy.CustomizableWidgets.push(this.developerToggle);
      this.developerToggleRegistered = true;
    }
  }

  /**
   * Unregister the developer toggle widget from CustomizableUI, keeping its
   * definition so it can be re-registered later.
   */
  unhookDeveloperToggle() {
    if (AppConstants.MOZ_APP_NAME == "thunderbird") {
      return;
    }
    if (!this.developerToggleRegistered) {
      return;
    }
    lazy.CustomizableUI.destroyWidget(this.developerToggle.id);
    const index = lazy.CustomizableWidgets.indexOf(this.developerToggle);
    if (index != -1) {
      lazy.CustomizableWidgets.splice(index, 1);
    }
    this.developerToggleRegistered = false;
  }

  addDevToolsItemsToSubview(subview) {
    // Initialize DevTools to create all menuitems in the system menu before
    // trying to copy them.
    this.initDevTools("HamburgerMenu");

    // Populate the subview with whatever menuitems are in the developer
    // menu. We skip menu elements, because the menu panel has no way
    // of dealing with those right now.
    const doc = subview.ownerDocument;
    const menu = doc.getElementById("menuWebDeveloperPopup");
    const itemsToDisplay = [...menu.children];

    lazy.CustomizableUI.clearSubview(subview);
    lazy.CustomizableUI.fillSubviewFromMenuItems(itemsToDisplay, subview);
  }

  onMoreToolsViewShowing(moreToolsView) {
    this.addDevToolsItemsToSubview(moreToolsView);
  }

  /**
   * Register the profiler recording button. This button will be available
   * in the customization palette for the Firefox toolbar. In addition, it can be
   * enabled from profiler.firefox.com.
   */
  hookProfilerRecordingButton() {
    if (this.profilerRecordingButtonCreated) {
      return;
    }
    const featureFlagPref = "devtools.performance.popup.feature-flag";
    const isPopupFeatureFlagEnabled =
      Services.prefs.getBoolPref(featureFlagPref);
    this.profilerRecordingButtonCreated = true;

    // Listen for messages from the front-end. This needs to happen even if the
    // button isn't enabled yet. This will allow the front-end to turn on the
    // popup for our users, regardless of if the feature is enabled by default.
    this.initializeProfilerWebChannel();

    if (isPopupFeatureFlagEnabled) {
      // Initialize the CustomizableUI widget.
      lazy.ProfilerMenuButton.initialize(this.toggleProfilerKeyShortcuts);
    } else {
      // The feature flag is not enabled, but watch for it to be enabled. If it is,
      // initialize everything.
      const enable = () => {
        lazy.ProfilerMenuButton.initialize(this.toggleProfilerKeyShortcuts);
        Services.prefs.removeObserver(featureFlagPref, enable);
      };
      Services.prefs.addObserver(featureFlagPref, enable);
    }

    if (!Cu.isInAutomation && Services.env.exists("MOZ_PROFILER_STARTUP")) {
      // If the profiler is active due to startup profiling, show the profiler
      // button in the nav bar. But do not do it in automation to avoid
      // side-effects with existing tests.
      lazy.ProfilerMenuButton.ensureButtonInNavbar();
    }
  }

  /**
   * Initialize the WebChannel for profiler.firefox.com. This function happens at
   * startup, so care should be taken to minimize its performance impact. The WebChannel
   * is a mechanism that is used to communicate between the browser, and front-end code.
   */
  initializeProfilerWebChannel() {
    let channel;

    // Register a channel for the URL in preferences. Also update the WebChannel if
    // the URL changes.
    const urlPref = "devtools.performance.recording.ui-base-url";

    // This method is only run once per Firefox instance, so it should not be
    // strictly necessary to remove observers here.
    // eslint-disable-next-line mozilla/balanced-observers
    Services.prefs.addObserver(urlPref, registerWebChannel);

    registerWebChannel();

    function registerWebChannel() {
      if (channel) {
        channel.stopListening();
      }

      const urlForWebChannel = Services.io.newURI(
        validateProfilerWebChannelUrl(Services.prefs.getStringPref(urlPref))
      );

      channel = new lazy.WebChannel("profiler.firefox.com", urlForWebChannel);

      channel.listen((id, message, target) => {
        // Defer loading the ProfilerPopupBackground script until it's absolutely needed,
        // as this code path gets loaded at startup.
        lazy.ProfilerPopupBackground.handleWebChannelMessage(
          channel,
          id,
          message,
          target
        );
      });
    }
  }

  /*
   * We listen to the "Browser Tools" system menu, which is under "Tools" main item.
   * This menu item is hardcoded empty in Firefox UI. We listen for its opening to
   * populate it lazily. Loading main DevTools module is going to populate it.
   */
  hookBrowserToolsMenu(window) {
    const menu = window.document.getElementById("browserToolsMenu");
    menu.addEventListener("popupshowing", this.onBrowserToolsMenuPopupShowing, {
      once: true,
    });
  }

  onBrowserToolsMenuPopupShowing() {
    this.initDevTools("SystemMenu");
  }

  /**
   * Remove the "Browser Tools" menu popupshowing listener from the given window.
   *
   * @param {Window} window
   */
  unhookBrowserToolsMenu(window) {
    const menu = window.document.getElementById("browserToolsMenu");
    menu.removeEventListener(
      "popupshowing",
      this.onBrowserToolsMenuPopupShowing
    );
  }

  /**
   * Check if the user is a DevTools user by looking at our selfxss pref.
   * This preference is incremented everytime the console is used (up to 5).
   *
   * @returns {boolean} true if the user can be considered as a devtools user.
   */
  isDevToolsUser() {
    const selfXssCount = Services.prefs.getIntPref("devtools.selfxss.count", 0);
    return selfXssCount > 0;
  }

  hookKeyShortcuts(window) {
    const doc = window.document;

    // hookKeyShortcuts can be called both from hookWindow and from the developer toggle
    // onBeforeCreated. Make sure shortcuts are only added once per window.
    if (doc.getElementById("devtoolsKeyset")) {
      return;
    }

    const keyset = doc.createXULElement("keyset");
    keyset.setAttribute("id", "devtoolsKeyset");

    this.attachKeys(doc, lazy.KeyShortcuts, keyset);

    // Appending a <key> element is not always enough. The <keyset> needs
    // to be detached and reattached to make sure the <key> is taken into
    // account (see bug 832984).
    const mainKeyset = doc.getElementById("mainKeyset");
    mainKeyset.parentNode.insertBefore(keyset, mainKeyset);
  }

  /**
   * Remove the DevTools key shortcuts from the given window.
   *
   * @param {Window} window
   */
  unhookKeyShortcuts(window) {
    const doc = window.document;
    const keyset = doc.getElementById("devtoolsKeyset");

    if (!keyset) {
      return;
    }
    keyset.remove();
  }

  /**
   * This method attaches on the key elements to the devtools keyset.
   *
   * @param {Document} doc
   * @param {Array<object>} keyShortcuts
   * @param {XULElement} [keyset]
   */
  attachKeys(doc, keyShortcuts, keyset = doc.getElementById("devtoolsKeyset")) {
    const window = doc.defaultView;
    for (const key of keyShortcuts) {
      if (!key.shortcut) {
        // Shortcuts might be missing when a user relies on a language packs
        // which is missing a recently uplifted shortcut. Language packs are
        // typically updated a few days after a code uplift.
        continue;
      }
      const xulKey = this.createKey(doc, key, () => this.onKey(window, key));
      keyset.appendChild(xulKey);
    }
  }

  /**
   * This method removes keys from the devtools keyset.
   *
   * @param {Document} doc
   * @param {Array<object>} keyShortcuts
   */
  removeKeys(doc, keyShortcuts) {
    for (const key of keyShortcuts) {
      const keyElement = doc.getElementById(this.getKeyElementId(key));
      if (keyElement) {
        keyElement.remove();
      }
    }
  }

  /**
   * We only want to have the keyboard shortcuts active when the menu button is on.
   * This function either adds or removes the elements.
   *
   * @param {boolean} isEnabled
   */
  toggleProfilerKeyShortcuts(isEnabled) {
    const profilerKeyShortcuts = getProfilerKeyShortcuts();
    for (const { document } of Services.wm.getEnumerator(null)) {
      const devtoolsKeyset = document.getElementById("devtoolsKeyset");
      const mainKeyset = document.getElementById("mainKeyset");

      if (!devtoolsKeyset || !mainKeyset) {
        // There may not be devtools keyset on this window.
        continue;
      }

      const areProfilerKeysPresent = !!document.getElementById(
        "key_profilerStartStop"
      );
      if (isEnabled === areProfilerKeysPresent) {
        // Don't double add or double remove the shortcuts.
        continue;
      }
      if (isEnabled) {
        this.attachKeys(document, profilerKeyShortcuts);
      } else {
        this.removeKeys(document, profilerKeyShortcuts);
      }
      // Appending a <key> element is not always enough. The <keyset> needs
      // to be detached and reattached to make sure the <key> is taken into
      // account (see bug 832984).
      mainKeyset.parentNode.insertBefore(devtoolsKeyset, mainKeyset);
    }
  }

  async onKey(window, key) {
    try {
      // The profiler doesn't care if DevTools is loaded, so provide a quick check
      // first to bail out of checking if DevTools is available.
      switch (key.id) {
        case "profilerStartStop":
        case "profilerStartStopAlternate": {
          lazy.ProfilerPopupBackground.toggleProfiler("aboutprofiling");
          return;
        }
        case "profilerCapture":
        case "profilerCaptureAlternate": {
          lazy.ProfilerPopupBackground.captureProfile("aboutprofiling");
          return;
        }
      }

      // Ignore the following key shortcut if DevTools aren't yet opened.
      // The key shortcut is registered in this core component in order to
      // work even when the web page is focused.
      if (key.id == "javascriptTracingToggle" && !this.initialized) {
        return;
      }

      // Record the timing at which this event started in order to compute later in
      // gDevTools.showToolbox, the complete time it takes to open the toolbox.
      // i.e. especially take `initDevTools` into account.
      const startTime = ChromeUtils.now();
      const require = this.initDevTools("KeyShortcut", key);
      const {
        gDevToolsBrowser,
      } = require("devtools/client/framework/devtools-browser");
      await gDevToolsBrowser.onKeyShortcut(window, key, startTime);
    } catch (e) {
      console.error(`Exception while trigerring key ${key}: ${e}\n${e.stack}`);
    }
  }

  getKeyElementId({ id, toolId }) {
    return "key_" + (id || toolId);
  }

  // Create a <xul:key> DOM Element
  createKey(doc, key, oncommand) {
    const { shortcut, modifiers: mod } = key;
    const k = doc.createXULElement("key");
    k.id = this.getKeyElementId(key);

    if (shortcut.startsWith("VK_")) {
      k.setAttribute("keycode", shortcut);
      if (shortcut.match(/^VK_\d$/)) {
        // Add the event keydown attribute to ensure that shortcuts work for combinations
        // such as ctrl shift 1.
        k.setAttribute("event", "keydown");
      }
    } else {
      k.setAttribute("key", shortcut);
    }

    if (mod) {
      k.setAttribute("modifiers", mod);
    }

    k.addEventListener("command", oncommand);

    return k;
  }

  initDevTools(reason, key = "") {
    // In the case of the --jsconsole and --jsdebugger command line parameters
    // there is no browser window yet so we don't send any telemetry yet.
    if (reason !== "CommandLine") {
      this.sendEntryPointTelemetry(reason, key);
    }

    this.initialized = true;
    const { require } = ChromeUtils.importESModule(
      "resource://devtools/shared/loader/Loader.sys.mjs"
    );
    // Ensure loading main devtools module that hooks up into browser UI
    // and initialize all devtools machinery.
    // eslint-disable-next-line import-x/no-unassigned-import
    require("devtools/client/framework/devtools-browser");
    return require;
  }

  handleConsoleFlag(cmdLine) {
    const window = Services.wm.getMostRecentWindow("devtools:webconsole");
    if (!window) {
      const require = this.initDevTools("CommandLine");
      const {
        BrowserConsoleManager,
      } = require("devtools/client/webconsole/browser-console-manager");
      BrowserConsoleManager.toggleBrowserConsole().catch(console.error);
    } else {
      // the Browser Console was already open
      window.focus();
    }

    if (cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_AUTO) {
      cmdLine.preventDefault = true;
    }
  }

  // Open the toolbox on the selected tab once the browser starts up.
  async handleDevToolsFlag(window) {
    const require = this.initDevTools("CommandLine");
    const { gDevTools } = require("devtools/client/framework/devtools");
    await gDevTools.showToolboxForTab(window.gBrowser.selectedTab);
  }

  _isRemoteDebuggingEnabled() {
    let remoteDebuggingEnabled = false;
    try {
      remoteDebuggingEnabled = kDebuggerPrefs.every(pref => {
        return Services.prefs.getBoolPref(pref);
      });
    } catch (ex) {
      console.error(ex);
      return false;
    }
    if (!remoteDebuggingEnabled) {
      const errorMsg =
        "Could not run chrome debugger! You need the following " +
        "prefs to be set to true: " +
        kDebuggerPrefs.join(", ");
      console.error(new Error(errorMsg));
      // Dump as well, as we're doing this from a commandline, make sure people
      // don't miss it:
      dump(errorMsg + "\n");
    }
    return remoteDebuggingEnabled;
  }

  handleDebuggerFlag(cmdLine, binaryPath) {
    if (!this._isRemoteDebuggingEnabled()) {
      return;
    }

    let devtoolsThreadResumed = false;
    const pauseOnStartup = cmdLine.handleFlag("wait-for-jsdebugger", false);
    if (pauseOnStartup) {
      const observe = function () {
        devtoolsThreadResumed = true;
        Services.obs.removeObserver(observe, "devtools-thread-ready");
      };
      Services.obs.addObserver(observe, "devtools-thread-ready");
    }

    const { BrowserToolboxLauncher } = ChromeUtils.importESModule(
      "resource://devtools/client/framework/browser-toolbox/Launcher.sys.mjs"
    );
    // --jsdebugger $binaryPath is an helper alias to set MOZ_BROWSER_TOOLBOX_BINARY=$binaryPath
    // See comment within BrowserToolboxLauncher.
    // Setting it as an environment variable helps it being reused if we restart the browser via CmdOrCtrl+R
    if (binaryPath && AppConstants.MOZILLA_OFFICIAL) {
      throw new Error(
        "Custom binary path for the browser toolbox passed via --jsdebugger only works for local build without MOZILLA_OFFICIAL build flag\n"
      );
    }
    Services.env.set("MOZ_BROWSER_TOOLBOX_BINARY", binaryPath);

    const browserToolboxLauncherConfig = {};

    // If user passed the --jsdebugger in mochitests, we want to enable the
    // multiprocess Browser Toolbox (by default it's parent process only)
    if (Services.prefs.getBoolPref("devtools.testing", false)) {
      browserToolboxLauncherConfig.forceMultiprocess = true;
    }
    BrowserToolboxLauncher.init(browserToolboxLauncherConfig);

    if (pauseOnStartup) {
      // Spin the event loop until the debugger connects.
      const tm = Cc["@mozilla.org/thread-manager;1"].getService();
      tm.spinEventLoopUntil(
        "DevToolsStartup.sys.mjs:handleDebuggerFlag",
        () => {
          return devtoolsThreadResumed;
        }
      );
    }

    if (cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_AUTO) {
      cmdLine.preventDefault = true;
    }
  }

  /**
   * Handle the --start-debugger-server command line flag. The options are:
   * --start-debugger-server
   *   The portOrPath parameter is boolean true in this case. Reads and uses the defaults
   *   from devtools.debugger.remote-port and devtools.debugger.remote-websocket prefs.
   *   The default values of these prefs are port 6000, WebSocket disabled.
   *
   * --start-debugger-server 6789
   *   Start the non-WebSocket server on port 6789.
   *
   * --start-debugger-server /path/to/filename
   *   Start the server on a Unix domain socket.
   *
   * --start-debugger-server ws:6789
   *   Start the WebSocket server on port 6789.
   *
   * --start-debugger-server ws:
   *   Start the WebSocket server on the default port (taken from d.d.remote-port)
   *
   * @param {nsICommandLine} cmdLine
   * @param {boolean|string} portOrPath
   */
  handleDevToolsServerFlag(cmdLine, portOrPath) {
    if (!this._isRemoteDebuggingEnabled()) {
      return;
    }

    let webSocket = false;
    const defaultPort = Services.prefs.getIntPref(
      "devtools.debugger.remote-port"
    );
    if (portOrPath === true) {
      // Default to pref values if no values given on command line
      webSocket = Services.prefs.getBoolPref(
        "devtools.debugger.remote-websocket"
      );
      portOrPath = defaultPort;
    } else if (portOrPath.startsWith("ws:")) {
      webSocket = true;
      const port = portOrPath.slice(3);
      portOrPath = Number(port) ? port : defaultPort;
    }

    const {
      useDistinctSystemPrincipalLoader,
      releaseDistinctSystemPrincipalLoader,
    } = ChromeUtils.importESModule(
      "resource://devtools/shared/loader/DistinctSystemPrincipalLoader.sys.mjs",
      { global: "shared" }
    );

    try {
      // Create a separate loader instance, so that we can be sure to receive
      // a separate instance of the DebuggingServer from the rest of the
      // devtools.  This allows us to safely use the tools against even the
      // actors and DebuggingServer itself, especially since we can mark
      // serverLoader as invisible to the debugger (unlike the usual loader
      // settings).
      const serverLoader = useDistinctSystemPrincipalLoader(this);
      const { DevToolsServer: devToolsServer } = serverLoader.require(
        "resource://devtools/server/devtools-server.js"
      );
      const { SocketListener } = serverLoader.require(
        "resource://devtools/shared/security/socket.js"
      );
      devToolsServer.init();

      // Force the server to be kept running when the last connection closes.
      // So that another client can connect after the previous one is disconnected.
      devToolsServer.keepAlive = true;

      devToolsServer.registerAllActors();
      devToolsServer.allowChromeProcess = true;
      const socketOptions = { portOrPath, webSocket };

      const listener = new SocketListener(devToolsServer, socketOptions);
      listener.open();
      dump("Started devtools server on " + portOrPath + "\n");

      // Prevent leaks on shutdown.
      const close = () => {
        Services.obs.removeObserver(close, "quit-application");
        dump("Stopped devtools server on " + portOrPath + "\n");
        if (listener) {
          listener.close();
        }
        if (devToolsServer) {
          devToolsServer.destroy();
        }
        releaseDistinctSystemPrincipalLoader(this);
      };
      Services.obs.addObserver(close, "quit-application");
    } catch (e) {
      dump("Unable to start devtools server on " + portOrPath + ": " + e);
    }

    if (cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_AUTO) {
      cmdLine.preventDefault = true;
    }
  }

  /**
   * Send entry point telemetry explaining how the devtools were launched. This
   * functionality also lives inside `devtools/client/framework/browser-menus.js`
   * because this codepath is only used the first time a toolbox is opened for a
   * tab.
   *
   * @param {string} reason
   *        One of "KeyShortcut", "SystemMenu", "HamburgerMenu", "ContextMenu",
   *        "CommandLine".
   * @param {string} key
   *        The key used by a key shortcut.
   */
  sendEntryPointTelemetry(reason, key = "") {
    if (!reason) {
      return;
    }

    let keys = "";

    if (reason === "KeyShortcut") {
      let { modifiers, shortcut } = key;

      modifiers = modifiers.replace(",", "+");

      if (shortcut.startsWith("VK_")) {
        shortcut = shortcut.substr(3);
      }

      keys = `${modifiers}+${shortcut}`;
    }

    const window = Services.wm.getMostRecentBrowserWindow();

    this.telemetry.addEventProperty(
      window,
      "open",
      "tools",
      null,
      "shortcut",
      keys
    );
    this.telemetry.addEventProperty(
      window,
      "open",
      "tools",
      null,
      "entrypoint",
      reason
    );

    if (this.recorded) {
      return;
    }

    // Only save the first call for each firefox run as next call
    // won't necessarely start the tool. For example key shortcuts may
    // only change the currently selected tool.
    try {
      Glean.devtools.entryPoint[reason].add(1);
    } catch (e) {
      dump("DevTools telemetry entry point failed: " + e + "\n");
    }
    this.recorded = true;
  }

  // Used by tests and the toolbox to register the same key shortcuts in toolboxes loaded
  // in a window window.
  get KeyShortcuts() {
    return lazy.KeyShortcuts;
  }
  get wrappedJSObject() {
    return this;
  }

  get jsdebuggerHelpInfo() {
    return `  --jsdebugger [<path>] Open the Browser Toolbox. Defaults to the local build
                     but can be overridden by a firefox path.
  --wait-for-jsdebugger Spin event loop until JS debugger connects.
                     Enables debugging (some) application startup code paths.
                     Only has an effect when \`--jsdebugger\` is also supplied.
  --start-debugger-server [ws:][ <port> | <path> ] Start the devtools server on
                     a TCP port or Unix domain socket path. Defaults to TCP port
                     6000. Use WebSocket protocol if ws: prefix is specified.
`;
  }

  get helpInfo() {
    return `  --jsconsole        Open the Browser Console.
  --devtools         Open DevTools on initial load.
${this.jsdebuggerHelpInfo}`;
  }

  classID = Components.ID("{9e9a9283-0ce9-4e4a-8f1c-ba129a032c32}");
  QueryInterface = ChromeUtils.generateQI(["nsICommandLineHandler"]);
}

/**
 * Singleton object that represents the JSON View in-content tool.
 * It has the same lifetime as the browser.
 */
const JsonView = {
  initialized: false,

  initialize() {
    // Prevent loading the frame script multiple times if we call this more than once.
    if (this.initialized) {
      return;
    }
    this.initialized = true;

    // Register for messages coming from the child process.
    // This is never removed as there is no particular need to unregister
    // it during shutdown.
    Services.mm.addMessageListener("devtools:jsonview:save", this.onSave);
  },

  // Message handlers for events from child processes

  /**
   * Save JSON to a file needs to be implemented here
   * in the parent process.
   *
   * @param {object} message
   */
  onSave(message) {
    const browser = message.target;
    const chrome = browser.documentGlobal;
    if (message.data === null) {
      // Save original contents
      chrome.saveBrowser(browser);
    } else {
      if (
        !message.data.startsWith("blob:resource://devtools/") ||
        browser.contentPrincipal.origin != "resource://devtools"
      ) {
        console.error("Got invalid request to save JSON data");
        return;
      }
      // The following code emulates saveBrowser, but:
      // - Uses the given blob URL containing the custom contents to save.
      // - Obtains the file name from the URL of the document, not the blob.
      // - avoids passing the document and explicitly passes system principal.
      //   We have a blob created by a null principal to save, and the null
      //   principal is from the child. Null principals don't survive crossing
      //   over IPC, so there's no other principal that'll work.
      const persistable = browser.frameLoader;
      persistable.startPersistence(null, {
        onDocumentReady(doc) {
          const uri = chrome.makeURI(doc.documentURI, doc.characterSet);
          const filename = chrome.getDefaultFileName(undefined, uri, doc, null);
          chrome.internalSave(
            message.data,
            null /* originalURL */,
            null,
            filename,
            null,
            doc.contentType,
            false /* bypass cache */,
            null /* filepicker title key */,
            null /* file chosen */,
            null /* referrer */,
            doc.cookieJarSettings,
            null /* initiating document */,
            false /* don't skip prompt for a location */,
            null /* cache key */,
            lazy.PrivateBrowsingUtils.isBrowserPrivate(
              browser
            ) /* private browsing ? */,
            Services.scriptSecurityManager.getSystemPrincipal()
          );
          // Close the persist document to tear down the IPC actor
          // that otherwise prevents the content window from being
          // destroyed until GC runs.
          doc.close();
        },
        onError() {
          throw new Error("JSON Viewer's onSave failed in startPersistence");
        },
      });
    }
  },
};

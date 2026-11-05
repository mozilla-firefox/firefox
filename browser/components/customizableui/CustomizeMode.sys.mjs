/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const kPrefCustomizationDebug = "browser.uiCustomization.debug";
const kPaletteId = "customization-palette";
const kDragDataTypePrefix = "text/toolbarwrapper-id/";
const kSkipSourceNodePref = "browser.uiCustomization.skipSourceNodeCheck";
const kDrawInTitlebarPref = "browser.tabs.inTitlebar";
const kCompactModeShowPref = "browser.compactmode.show";
const kBookmarksToolbarPref = "browser.toolbars.bookmarks.visibility";
const kKeepBroadcastAttributes = "keepbroadcastattributeswhencustomizing";

const kPanelItemContextMenu = "customizationPanelItemContextMenu";
const kPaletteItemContextMenu = "customizationPaletteItemContextMenu";

const kDownloadAutohideCheckboxId = "downloads-button-autohide-checkbox";
const kDownloadAutohidePanelId = "downloads-button-autohide-panel";
const kDownloadAutoHidePref = "browser.download.autohideButton";

const kSpecialWidgetL10nIds = {
  toolbarspring: "customize-mode-toolbarspring",
  toolbarseparator: "customize-mode-toolbarseparator",
  toolbarspacer: "customize-mode-toolbarspacer",
};

import { CustomizableUI } from "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUsageTelemetry: "resource:///modules/BrowserUsageTelemetry.sys.mjs",
  DragPositionManager:
    "moz-src:///browser/components/customizableui/DragPositionManager.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gTouchBarUpdater",
  "@mozilla.org/widget/touchbarupdater;1",
  Ci.nsITouchBarUpdater
);

let gDebug;
ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  gDebug = Services.prefs.getBoolPref(kPrefCustomizationDebug, false);
  let consoleOptions = {
    maxLogLevel: gDebug ? "all" : "log",
    prefix: "CustomizeMode",
  };
  return new ConsoleAPI(consoleOptions);
});

var gDraggingInToolbars;

var gTab;

function closeGlobalTab() {
  let win = gTab.documentGlobal;
  if (win.gBrowser.browsers.length == 1) {
    win.BrowserCommands.openTab();
  }
  win.gBrowser.removeTab(gTab, { animate: true });
  gTab = null;
}

var gTabsProgressListener = {
  onLocationChange(aBrowser, aWebProgress, aRequest, aLocation) {
    // Tear down customize mode when the customize mode tab loads some other page.
    // Customize mode will be re-entered if "about:blank" is loaded again, so
    // don't tear down in this case.
    if (
      !gTab ||
      gTab.linkedBrowser != aBrowser ||
      aLocation.spec == "about:blank"
    ) {
      return;
    }

    unregisterGlobalTab();
  },
};

function unregisterGlobalTab() {
  gTab.removeEventListener("TabClose", unregisterGlobalTab);
  let win = gTab.documentGlobal;
  win.removeEventListener("unload", unregisterGlobalTab);
  win.gBrowser.removeTabsProgressListener(gTabsProgressListener);

  gTab.removeAttribute("customizemode");

  gTab = null;
}

/**
 * This class manages the lifetime of the CustomizeMode UI in a single browser
 * window. There is one instance of CustomizeMode per browser window.
 */
export class CustomizeMode {
  constructor(aWindow) {
    this.#window = aWindow;
    this.#document = aWindow.document;
    this.#browser = aWindow.gBrowser;
    this.areas = new Set();

    this.#translationObserver = new aWindow.MutationObserver(mutations =>
      this.#onTranslations(mutations)
    );
    this.#window.MozXULElement.insertFTLIfNeeded("browser/customizeMode.ftl");
    this.#ensureCustomizationPanels();

    this.#attachEventListeners();

    // There are two palettes - there's the palette that can be overlayed with
    // toolbar items in browser.xhtml. This is invisible, and never seen by the
    // user. Then there's the visible palette, which gets populated and displayed
    // to the user when in customizing mode.
    this.visiblePalette = this.$(kPaletteId);
    this.pongArena = this.$("customization-pong-arena");

    if (this.#canDrawInTitlebar()) {
      this.#updateTitlebarCheckbox();
      Services.prefs.addObserver(kDrawInTitlebarPref, this);
    } else {
      this.$("customization-titlebar-visibility-checkbox").hidden = true;
    }

    // Observe pref changes to the bookmarks toolbar visibility,
    // since we won't get a toolbarvisibilitychange event if the
    // toolbar is changing from 'newtab' to 'always' in Customize mode
    // since the toolbar is shown with the 'newtab' setting.
    Services.prefs.addObserver(kBookmarksToolbarPref, this);

    this.#window.addEventListener("unload", this);
  }

  /**
   * True if CustomizeMode is in the process of being transitioned into or out
   * of.
   *
   * @type {boolean}
   */
  #transitioning = false;

  /**
   * A reference to the top-level browser window that this instance of
   * CustomizeMode is configured to use.
   *
   * @type {DOMWindow}
   */
  #window = null;

  /**
   * A reference to the top-level browser window document that this instance of
   * CustomizeMode is configured to use.
   *
   * @type {Document}
   */
  #document = null;

  /**
   * A reference to the Tabbrowser instance that belongs to the top-level
   * browser window that CustomizeMode is configured to use.
   *
   * @type {Tabbrowser}
   */
  #browser = null;

  /**
   * An array of customize target DOM nodes that this instance of CustomizeMode
   * can be used to manipulate. It is assumed that when targets are in this
   * Set, that they have drag / drop listeners attached and that their
   * customizable children have been wrapped as toolbarpaletteitems.
   *
   * @type {null|Set<DOMNode>}
   */
  areas = null;

  /**
   * When in customizing mode, we swap out the reference to the invisible
   * palette in gNavToolbox.palette for our visiblePalette. This way, for the
   * customizing browser window, when widgets are removed from customizable
   * areas and added to the palette, they're added to the visible palette.
   * #stowedPalette is a reference to the old invisible palette so we can
   * restore gNavToolbox.palette to its original state after exiting
   * customization mode.
   *
   * @type {null|DOMNode}
   */
  #stowedPalette = null;

  /**
   * If a drag and drop operation is underway for a customizable toolbar item,
   * this member is set to the current item being dragged over.
   *
   * @type {null|DOMNode}
   */
  #dragOverItem = null;

  /**
   * True if we're in the state of customizing this browser window.
   *
   * @type {boolean}
   */
  #customizing = false;

  /**
   * True if we're synthesizing drag and drop in customize mode for automated
   * tests and want to skip the checks that ensure that the source of the
   * drag events was this top-level browser window. This is controllable via
   * `browser.uiCustomization.skipSourceNodeCheck`.
   *
   * @type {boolean}
   */
  #skipSourceNodeCheck = false;

  /**
   * These are the commands we continue to leave enabled while in customize
   * mode. All other commands are disabled, and we remove the disabled attribute
   * when leaving customize mode.
   *
   * @type {Set<string>}
   */
  #enabledCommands = new Set([
    "cmd_newNavigator",
    "cmd_newNavigatorTab",
    "cmd_newNavigatorTabNoEvent",
    "cmd_close",
    "cmd_closeWindow",
    "cmd_maximizeWindow",
    "cmd_minimizeWindow",
    "cmd_restoreWindow",
    "cmd_quitApplication",
    "View:FullScreen",
    "Browser:NextTab",
    "Browser:PrevTab",
    "Browser:NewUserContextTab",
    "Tools:PrivateBrowsing",
    "zoomWindow",
  ]);

  /**
   * A MutationObserver used to hear about Fluent localization occurring for
   * customizable items.
   *
   * @type {MutationObserver|null}
   */
  #translationObserver = null;

  /**
   * A description of the size of a customizable item were it to be placed in a
   * particular customizable area.
   *
   * @typedef {object} ItemSizeForArea
   * @property {number} width
   *   The width of the customizable item when placed in a certain area.
   * @property {number} height
   *   The height of the customizable item when placed in a certain area.
   */

  /**
   * A WeakMap mapping dragged customizable items to a WeakMap of areas that
   * the item could be dragged into. That mapping maps to ItemSizeForArea
   * objects that describe the width and height of the item were it to be
   * dropped and placed within that area (since certain areas will encourage
   * items to expand or contract).
   *
   * @type {WeakMap<DOMNode, WeakMap<DOMNode, DragSizeForArea>>|null}
   */
  #dragSizeMap = null;

  /**
   * If this is set to true, this means that the user enabled the downloads
   * button auto-hide feature while the button was in the palette. If so, then
   * on exiting mode, the button is moved to its default position in the
   * navigation toolbar.
   */
  #moveDownloadsButtonToNavBar = false;

  /**
   * Returns the CustomizationHandler browser window global object. See
   * browser-customization.js.
   *
   * @type {object}
   */
  get #handler() {
    return this.#window.CustomizationHandler;
  }

  /**
   * Does cleanup of any listeners or observers when the browser window
   * that this CustomizeMode instance is configured to use unloads.
   */
  #uninit() {
    if (this.#canDrawInTitlebar()) {
      Services.prefs.removeObserver(kDrawInTitlebarPref, this);
    }
    Services.prefs.removeObserver(kBookmarksToolbarPref, this);
  }

  /**
   * This is a shortcut for this.#document.getElementById.
   *
   * @param {string} id
   *   The DOM ID to return a result for.
   * @returns {DOMNode|null}
   */
  $(id) {
    return this.#document.getElementById(id);
  }

  /**
   * Sets the fake tab element that will be associated with being in
   * customize mode. Customize mode looks similar to a "special kind of tab",
   * and when that tab is closed, we exit customize mode. When that tab is
   * switched to, we enter customize mode. If that tab is restored in the
   * foreground, we enter customize mode.
   *
   * This method assigns the special <xul:tab> that will represent customize
   * mode for this window, and sets up the relevant listeners to it. The
   * tab will have a "customizemode" attribute set to "true" on it, as well as
   * a special favicon.
   *
   * @param {MozTabbrowserTab} aTab
   *   The tab to act as the tab representation of customize mode.
   * @param {string} [aEntryPoint]
   *   @see CustomizeMode.enter()
   */
  setTab(aTab, aEntryPoint) {
    if (gTab == aTab) {
      return;
    }

    if (gTab) {
      closeGlobalTab();
    }

    gTab = aTab;

    gTab.setAttribute("customizemode", "true");

    if (gTab.linkedPanel) {
      gTab.linkedBrowser.stop();
    }

    let win = gTab.documentGlobal;

    win.gBrowser.setTabTitle(gTab);
    win.gBrowser.setIcon(gTab, "chrome://browser/skin/customize.svg");

    gTab.addEventListener("TabClose", unregisterGlobalTab);

    win.gBrowser.addTabsProgressListener(gTabsProgressListener);

    win.addEventListener("unload", unregisterGlobalTab);

    if (gTab.selected) {
      win.gCustomizeMode.enter(aEntryPoint);
    }
  }

  /**
   * Kicks off the process of entering customize mode for the window that this
   * CustomizeMode instance was constructed with. If this window happens to be
   * a popup window or web app window, the opener window will enter customize mode.
   *
   * Entering customize mode is a multistep asynchronous operation, but this
   * method returns immediately while this operation is underway. A
   * `customizationready` custom event is dispatched on the gNavToolbox when
   * this asynchronous process has completed.
   *
   * This method will return early if customize mode is already active in this
   * window.
   *
   * @param {string} [aEntryPoint]
   *   The UI entry point that was used to enter customize mode, for telemetry
   *   purposes (see the customize.opened event). If omitted (e.g. when
   *   re-entering customize mode on session restore or tab switching), then
   *   "other" is set as the entrypoint.
   */
  enter(aEntryPoint) {
    if (
      !this.#window.toolbar.visible ||
      this.#window.document.documentElement.hasAttribute("taskbartab") ||
      this.#window.document.documentElement.hasAttribute("mini-window")
    ) {
      let w = lazy.URILoadingHelper.getTargetWindow(this.#window, {
        skipPopups: true,
        skipTaskbarTabs: true,
      });
      if (w) {
        w.gCustomizeMode.enter(aEntryPoint);
        return;
      }
      let obs = () => {
        Services.obs.removeObserver(obs, "browser-delayed-startup-finished");
        w = lazy.URILoadingHelper.getTargetWindow(this.#window, {
          skipPopups: true,
          skipTaskbarTabs: true,
        });
        w.gCustomizeMode.enter(aEntryPoint);
      };
      Services.obs.addObserver(obs, "browser-delayed-startup-finished");
      this.#window.openTrustedLinkIn("about:newtab", "window");
      return;
    }
    this._wantToBeInCustomizeMode = true;

    if (this.#customizing || this.#handler.isEnteringCustomizeMode) {
      return;
    }

    // Exiting; want to re-enter once we've done that.
    if (this.#handler.isExitingCustomizeMode) {
      lazy.log.debug(
        "Attempted to enter while we're in the middle of exiting. " +
          "We'll exit after we've entered"
      );
      return;
    }

    if (!gTab) {
      this.setTab(
        this.#browser.addTab("about:blank", {
          inBackground: false,
          forceNotRemote: true,
          skipAnimation: true,
          triggeringPrincipal:
            Services.scriptSecurityManager.getSystemPrincipal(),
        }),
        aEntryPoint
      );
      return;
    }

    Glean.customize.opened.record({
      entry_point: aEntryPoint || "other",
      tab_orientation: CustomizableUI.verticalTabsEnabled
        ? "vertical"
        : "horizontal",
    });

    if (!gTab.selected) {
      // This will force another .enter() to be called via the
      // onlocationchange handler of the tabbrowser, so we return early.
      gTab.documentGlobal.gBrowser.selectedTab = gTab;
      return;
    }
    gTab.documentGlobal.focus();
    if (gTab.ownerDocument != this.#document) {
      return;
    }

    let window = this.#window;
    let document = this.#document;

    this.#handler.isEnteringCustomizeMode = true;

    // Always disable the reset button at the start of customize mode, it'll be re-enabled
    // if necessary when we finish entering:
    let resetButton = this.$("customization-reset-button");
    resetButton.setAttribute("disabled", "true");

    (async () => {
      // We shouldn't start customize mode until after browser-delayed-startup has finished:
      if (!this.#window.gBrowserInit.delayedStartupFinished) {
        await new Promise(resolve => {
          let delayedStartupObserver = aSubject => {
            if (aSubject == this.#window) {
              Services.obs.removeObserver(
                delayedStartupObserver,
                "browser-delayed-startup-finished"
              );
              resolve();
            }
          };

          Services.obs.addObserver(
            delayedStartupObserver,
            "browser-delayed-startup-finished"
          );
        });
      }

      CustomizableUI.dispatchToolboxEvent("beforecustomization", {}, window);
      CustomizableUI.notifyStartCustomizing(this.#window);

      // Add a keypress listener to the document so that we can quickly exit
      // customization mode when pressing ESC.
      document.addEventListener("keypress", this);

      // Same goes for the menu button - if we're customizing, a click on the
      // menu button means a quick exit from customization mode.
      window.PanelUI.hide();

      let panelHolder = document.getElementById("customization-panelHolder");
      let panelContextMenu = document.getElementById(kPanelItemContextMenu);
      this._previousPanelContextMenuParent = panelContextMenu.parentNode;
      document.getElementById("mainPopupSet").appendChild(panelContextMenu);
      panelHolder.appendChild(window.PanelUI.overflowFixedList);

      window.PanelUI.overflowFixedList.toggleAttribute("customizing", true);
      window.PanelUI.menuButton.disabled = true;
      document.getElementById("nav-bar-overflow-button").disabled = true;

      this.#transitioning = true;

      let customizer = document.getElementById("customization-container");
      let browser = document.getElementById("browser");
      browser.hidden = true;
      customizer.hidden = false;

      this.#wrapAreaItemsSync(CustomizableUI.AREA_TABSTRIP);

      this.#document.documentElement.toggleAttribute("customizing", true);

      let customizableToolbars = document.querySelectorAll(
        "toolbar[customizable=true]:not([autohide], [collapsed])"
      );
      for (let toolbar of customizableToolbars) {
        toolbar.toggleAttribute("customizing", true);
      }

      this.#updateOverflowPanelArrowOffset();

      // Let everybody in this window know that we're about to customize.
      CustomizableUI.dispatchToolboxEvent("customizationstarting", {}, window);

      await this.#wrapAllAreaItems();
      this.#populatePalette();

      this.#setupPaletteDragging();

      window.gNavToolbox.addEventListener("toolbarvisibilitychange", this);

      this.#updateResetButton();
      this.#updateUndoResetButton();
      this.#updateTouchBarButton();
      this.#updateDensityMenu();

      this.#skipSourceNodeCheck =
        Services.prefs.getPrefType(kSkipSourceNodePref) ==
          Ci.nsIPrefBranch.PREF_BOOL &&
        Services.prefs.getBoolPref(kSkipSourceNodePref);

      CustomizableUI.addListener(this);
      this.#customizing = true;
      this.#transitioning = false;

      // Show the palette now that the transition has finished.
      this.visiblePalette.hidden = false;
      window.setTimeout(() => {
        // Force layout reflow to ensure the animation runs,
        // and make it async so it doesn't affect the timing.
        this.visiblePalette.clientTop;
        this.visiblePalette.setAttribute("showing", "true");
      }, 0);
      this.#updateEmptyPaletteNotice();

      this.#setupDownloadAutoHideToggle();

      this.#handler.isEnteringCustomizeMode = false;

      CustomizableUI.dispatchToolboxEvent("customizationready", {}, window);

      if (!this._wantToBeInCustomizeMode) {
        this.exit();
      }
    })().catch(e => {
      lazy.log.error("Error entering customize mode", e);
      this.#handler.isEnteringCustomizeMode = false;
      // Exit customize mode to ensure proper clean-up when entering failed.
      this.exit();
    });
  }

  /**
   * Exits customize mode, if we happen to be in it. This is a no-op if we
   * are not in customize mode.
   *
   * This is a multi-step asynchronous operation, but this method returns
   * synchronously after the operation begins. An `aftercustomization`
   * custom event is dispatched on the gNavToolbox once the operation has
   * completed.
   */
  exit() {
    this._wantToBeInCustomizeMode = false;

    if (!this.#customizing || this.#handler.isExitingCustomizeMode) {
      return;
    }

    // Entering; want to exit once we've done that.
    if (this.#handler.isEnteringCustomizeMode) {
      lazy.log.debug(
        "Attempted to exit while we're in the middle of entering. " +
          "We'll exit after we've entered"
      );
      return;
    }

    if (this.resetting) {
      lazy.log.debug(
        "Attempted to exit while we're resetting. " +
          "We'll exit after resetting has finished."
      );
      return;
    }

    this.#handler.isExitingCustomizeMode = true;

    this.#translationObserver.disconnect();

    this.#teardownDownloadAutoHideToggle();

    CustomizableUI.removeListener(this);

    let window = this.#window;
    let document = this.#document;

    document.removeEventListener("keypress", this);

    this.#togglePong(false);

    // Disable the reset and undo reset buttons while transitioning:
    let resetButton = this.$("customization-reset-button");
    let undoResetButton = this.$("customization-undo-reset-button");
    undoResetButton.hidden = resetButton.disabled = true;

    this.#transitioning = true;

    this.#depopulatePalette();

    // We need to set this.#customizing to false and remove the `customizing`
    // attribute before removing the tab or else
    // XULBrowserWindow.onLocationChange might think that we're still in
    // customization mode and need to exit it for a second time.
    this.#customizing = false;
    document.documentElement.removeAttribute("customizing");

    if (this.#browser.selectedTab == gTab) {
      closeGlobalTab();
    }

    let customizer = document.getElementById("customization-container");
    let browser = document.getElementById("browser");
    customizer.hidden = true;
    browser.hidden = false;

    window.gNavToolbox.removeEventListener("toolbarvisibilitychange", this);

    this.#teardownPaletteDragging();

    (async () => {
      await this.#unwrapAllAreaItems();

      // And drop all area references.
      this.areas.clear();

      // Let everybody in this window know that we're starting to
      // exit customization mode.
      CustomizableUI.dispatchToolboxEvent("customizationending", {}, window);

      window.PanelUI.menuButton.disabled = false;
      let overflowContainer = document.getElementById(
        "widget-overflow-mainView"
      ).firstElementChild;
      overflowContainer.appendChild(window.PanelUI.overflowFixedList);
      document.getElementById("nav-bar-overflow-button").disabled = false;
      let panelContextMenu = document.getElementById(kPanelItemContextMenu);
      this._previousPanelContextMenuParent.appendChild(panelContextMenu);

      let customizableToolbars = document.querySelectorAll(
        "toolbar[customizable=true]:not([autohide])"
      );
      for (let toolbar of customizableToolbars) {
        toolbar.removeAttribute("customizing");
      }

      this.#maybeMoveDownloadsButtonToNavBar();

      delete this._lastLightweightTheme;
      this.#transitioning = false;
      this.#handler.isExitingCustomizeMode = false;
      CustomizableUI.dispatchToolboxEvent("aftercustomization", {}, window);
      CustomizableUI.notifyEndCustomizing(window);

      if (this._wantToBeInCustomizeMode) {
        this.enter();
      }
    })().catch(e => {
      lazy.log.error("Error exiting customize mode", e);
      this.#handler.isExitingCustomizeMode = false;
    });
  }

  /**
   * The overflow panel in customize mode should have its arrow pointing
   * at the overflow button. In order to do this correctly, we pass the
   * distance between the inside of window and the middle of the button
   * to the customize mode markup in which the arrow and panel are placed.
   *
   * The returned Promise resolves once the offset has been set on the panel
   * wrapper.
   *
   * @returns {Promise<undefined>}
   */
  async #updateOverflowPanelArrowOffset() {
    let currentDensity =
      this.#document.documentElement.getAttribute("uidensity");
    let offset = await this.#window.promiseDocumentFlushed(() => {
      let overflowButton = this.$("nav-bar-overflow-button");
      let buttonRect = overflowButton.getBoundingClientRect();
      let endDistance;
      if (this.#window.RTL_UI) {
        endDistance = buttonRect.left;
      } else {
        endDistance = this.#window.innerWidth - buttonRect.right;
      }
      return endDistance + buttonRect.width / 2;
    });
    if (
      !this.#document ||
      currentDensity != this.#document.documentElement.getAttribute("uidensity")
    ) {
      return;
    }
    this.$("customization-panelWrapper").style.setProperty(
      "--panel-arrow-offset",
      offset + "px"
    );
  }

  /**
   * Given some DOM node, attempts to resolve to the relevant child or overflow
   * target node that can have customizable items placed within it. It may
   * resolve to  aNode itself, if aNode can have customizable items placed
   * directly within it. If the node does not appear to have such a child, this
   * method will return `null`.
   *
   * @param {DOMNode} aNode
   * @returns {null|DOMNode}
   */
  #getCustomizableChildForNode(aNode) {
    // NB: adjusted from #getCustomizableParent to keep that method fast
    // (it's used during drags), and avoid multiple DOM loops
    let areas = CustomizableUI.areas;
    // Caching this length is important because otherwise we'll also iterate
    // over items we add to the end from within the loop.
    let numberOfAreas = areas.length;
    for (let i = 0; i < numberOfAreas; i++) {
      let area = areas[i];
      let areaNode = aNode.ownerDocument.getElementById(area);
      let customizationTarget = CustomizableUI.getCustomizationTarget(areaNode);
      if (customizationTarget && customizationTarget != areaNode) {
        areas.push(customizationTarget.id);
      }
      let overflowTarget =
        areaNode && areaNode.getAttribute("default-overflowtarget");
      if (overflowTarget) {
        areas.push(overflowTarget);
      }
    }
    areas.push(kPaletteId);

    while (aNode && aNode.parentNode) {
      let parent = aNode.parentNode;
      if (areas.includes(parent.id)) {
        return aNode;
      }
      aNode = parent;
    }
    return null;
  }

  /**
   * Kicks off an animation for aNode that causes it to scale down and become
   * transparent before being removed from a toolbar. This can be seen when
   * using the context menu to remove an item from a toolbar.
   *
   * For nodes that are within the overflow panel, aren't
   * toolbaritem / toolbarbuttons, or is the hidden downloads button, this
   * returns `null` immediately and no animation is performed. If reduced
   * motion is enabled, this returns `null` immediately and no animation is
   * performed.
   *
   * @param {DOMNode} aNode
   *   The node to be removed from the toolbar.
   * @returns {null|Promise<DOMNode>}
   *   Returns `null` if no animation is going to occur, or the DOMNode that
   *   the animation was performed on after the animation has completed.
   */
  #promiseWidgetAnimationOut(aNode) {
    if (
      this.#window.gReduceMotion ||
      aNode.getAttribute("cui-anchorid") == "nav-bar-overflow-button" ||
      (aNode.tagName != "toolbaritem" && aNode.tagName != "toolbarbutton") ||
      (aNode.id == "downloads-button" && aNode.hidden)
    ) {
      return null;
    }

    let animationNode;
    if (aNode.parentNode && aNode.parentNode.id.startsWith("wrapper-")) {
      animationNode = aNode.parentNode;
    } else {
      animationNode = aNode;
    }
    return new Promise(resolve => {
      function cleanupCustomizationExit() {
        resolveAnimationPromise();
      }

      function cleanupWidgetAnimationEnd(e) {
        if (
          e.animationName == "widget-animate-out" &&
          e.target.id == animationNode.id
        ) {
          resolveAnimationPromise();
        }
      }

      function resolveAnimationPromise() {
        animationNode.removeEventListener(
          "animationend",
          cleanupWidgetAnimationEnd
        );
        animationNode.removeEventListener(
          "customizationending",
          cleanupCustomizationExit
        );
        resolve(animationNode);
      }

      // Wait until the next frame before setting the class to ensure
      // we do start the animation.
      this.#window.requestAnimationFrame(() => {
        this.#window.requestAnimationFrame(() => {
          animationNode.classList.add("animate-out");
          animationNode.documentGlobal.gNavToolbox.addEventListener(
            "customizationending",
            cleanupCustomizationExit
          );
          animationNode.addEventListener(
            "animationend",
            cleanupWidgetAnimationEnd
          );
        });
      });
    });
  }

  /**
   * Moves a customizable item to the end of the navbar. This is used primarily
   * by the toolbar item context menus regardless of whether or not we're in
   * customize mode. If this item is within a toolbar, this may kick off an
   * animation that shrinks the item icon and causes it to become transparent
   * before the move occurs. The returned Promise resolves once the animation
   * and the move has completed.
   *
   * @param {DOMNode} aNode
   *   The node to be moved to the end of the navbar area.
   * @returns {Promise<undefined>}
   *   Resolves once the move has completed.
   */
  async addToToolbar(aNode) {
    aNode = this.#getCustomizableChildForNode(aNode);
    if (aNode.localName == "toolbarpaletteitem" && aNode.firstElementChild) {
      aNode = aNode.firstElementChild;
    }
    let widgetAnimationPromise = this.#promiseWidgetAnimationOut(aNode);
    let animationNode;
    if (widgetAnimationPromise) {
      animationNode = await widgetAnimationPromise;
    }

    let widgetToAdd = aNode.id;
    if (
      CustomizableUI.isSpecialWidget(widgetToAdd) &&
      aNode.closest("#customization-palette")
    ) {
      widgetToAdd = widgetToAdd.match(
        /^customizableui-special-(spring|spacer|separator)/
      )[1];
    }

    CustomizableUI.addWidgetToArea(widgetToAdd, CustomizableUI.AREA_NAVBAR);
    lazy.BrowserUsageTelemetry.recordWidgetChange(
      widgetToAdd,
      CustomizableUI.AREA_NAVBAR
    );
    if (!this.#customizing) {
      CustomizableUI.dispatchToolboxEvent("customizationchange");
    }

    // If the user explicitly moves this item, turn off autohide.
    if (aNode.id == "downloads-button") {
      Services.prefs.setBoolPref(kDownloadAutoHidePref, false);
      if (this.#customizing) {
        this.#showDownloadsAutoHidePanel();
      }
    }

    if (animationNode) {
      animationNode.classList.remove("animate-out");
    }
  }

  /**
   * Pins a customizable widget to the overflow panel. This is used by the
   * toolbar item context menus regardless of whether or not we're in customize
   * mode. It is also on the widget context menu within the overflow panel when
   * the widget is placed there temporarily during a toolbar overflow. If this
   * item is within a toolbar, this may kick off an* animation that shrinks the
   * item icon and causes it to become transparent before the move occurs. The
   * returned Promise resolves once the animation and the move has completed.
   *
   * @param {DOMNode} aNode
   *   The node to be moved to the overflow panel.
   * @param {string} aReason
   *   A string to describe why the item is being moved to the overflow panel.
   *   This is passed along to BrowserUsageTelemetry.recordWidgetChange, and
   *   is dash-delimited.
   *
   *   Examples:
   *
   *   "toolbar-context-menu": the reason is that the user chose to do this via
   *     the toolbar context menu.
   *
   *   "panelitem-context": the reason is that the user chose to do this via
   *     the overflow panel item context menu when the item was moved there
   *     temporarily during a toolbar overflow.
   * @returns {Promise<undefined>}
   *   Resolves once the move has completed.
   */
  async addToPanel(aNode, aReason) {
    aNode = this.#getCustomizableChildForNode(aNode);
    if (aNode.localName == "toolbarpaletteitem" && aNode.firstElementChild) {
      aNode = aNode.firstElementChild;
    }
    let widgetAnimationPromise = this.#promiseWidgetAnimationOut(aNode);
    let animationNode;
    if (widgetAnimationPromise) {
      animationNode = await widgetAnimationPromise;
    }

    let panel = CustomizableUI.AREA_FIXED_OVERFLOW_PANEL;
    CustomizableUI.addWidgetToArea(aNode.id, panel);
    lazy.BrowserUsageTelemetry.recordWidgetChange(aNode.id, panel, aReason);
    if (!this.#customizing) {
      CustomizableUI.dispatchToolboxEvent("customizationchange");
    }

    // If the user explicitly moves this item, turn off autohide.
    if (aNode.id == "downloads-button") {
      Services.prefs.setBoolPref(kDownloadAutoHidePref, false);
      if (this.#customizing) {
        this.#showDownloadsAutoHidePanel();
      }
    }

    if (animationNode) {
      animationNode.classList.remove("animate-out");
    }
    if (!this.#window.gReduceMotion) {
      let overflowButton = this.$("nav-bar-overflow-button");
      overflowButton.setAttribute("animate", "true");
      overflowButton.addEventListener(
        "animationend",
        function onAnimationEnd(event) {
          if (event.animationName.startsWith("overflow-animation")) {
            this.removeEventListener("animationend", onAnimationEnd);
            this.removeAttribute("animate");
          }
        }
      );
    }
  }

  /**
   * Removes a customizable item from its area and puts it in the palette. This
   * is used by the toolbar item context menus regardless of whether or not
   * we're in customize mode. It is also on the widget context menu within the
   * overflow panel when the widget is placed there temporarily during a toolbar
   * overflow. If this item is within a toolbar, this may kick off an animation
   * that shrinks the item icon and causes it to become transparent before the
   * removal occurs. The returned Promise resolves once the animation and the
   * removal has completed.
   *
   * @param {DOMNode} aNode
   *   The node to be removed and placed into the palette.
   * @param {string} aReason
   *   A string to describe why the item is being removed.
   *   This is passed along to BrowserUsageTelemetry.recordWidgetChange, and
   *   is dash-delimited.
   *
   *   Examples:
   *
   *   "toolbar-context-menu": the reason is that the user chose to do this via
   *     the toolbar context menu.
   *
   *   "panelitem-context": the reason is that the user chose to do this via
   *     the overflow panel item context menu when the item was moved there
   *     temporarily during a toolbar overflow.
   * @returns {Promise<undefined>}
   *   Resolves once the removal has completed.
   */
  async removeFromArea(aNode, aReason) {
    aNode = this.#getCustomizableChildForNode(aNode);
    if (aNode.localName == "toolbarpaletteitem" && aNode.firstElementChild) {
      aNode = aNode.firstElementChild;
    }
    let widgetAnimationPromise = this.#promiseWidgetAnimationOut(aNode);
    let animationNode;
    if (widgetAnimationPromise) {
      animationNode = await widgetAnimationPromise;
    }

    CustomizableUI.removeWidgetFromArea(aNode.id);
    lazy.BrowserUsageTelemetry.recordWidgetChange(aNode.id, null, aReason);
    if (!this.#customizing) {
      CustomizableUI.dispatchToolboxEvent("customizationchange");
    }

    // If the user explicitly removes this item, turn off autohide.
    if (aNode.id == "downloads-button") {
      Services.prefs.setBoolPref(kDownloadAutoHidePref, false);
      if (this.#customizing) {
        this.#showDownloadsAutoHidePanel();
      }
    }
    if (animationNode) {
      animationNode.classList.remove("animate-out");
    }
  }

  /**
   * Populates the visible palette seen in the content area when entering
   * customize mode. This moves items from the normal "hidden" palette that
   * belongs to the toolbox, and then temporarily overrides the toolbox
   * with the visible palette until we exit customize mode.
   */
  #populatePalette() {
    let fragment = this.#document.createDocumentFragment();
    let toolboxPalette = this.#window.gNavToolbox.palette;

    try {
      let unusedWidgets = CustomizableUI.getUnusedWidgets(toolboxPalette);
      for (let widget of unusedWidgets) {
        let paletteItem = this.#makePaletteItem(widget);
        if (!paletteItem) {
          continue;
        }
        fragment.appendChild(paletteItem);
      }

      let flexSpace = CustomizableUI.createSpecialWidget(
        "spring",
        this.#document
      );
      fragment.appendChild(this.wrapToolbarItem(flexSpace, "palette"));

      this.visiblePalette.appendChild(fragment);
      this.#stowedPalette = this.#window.gNavToolbox.palette;
      this.#window.gNavToolbox.palette = this.visiblePalette;

      // Now that the palette items are all here, disable all commands.
      // We do this here rather than directly in `enter` because we
      // need to do/undo this when we're called from reset(), too.
      this.#updateCommandsDisabledState(true);
    } catch (ex) {
      lazy.log.error(ex);
    }
  }

  /**
   * For a given widget, finds the associated node within this window and then
   * creates / updates a toolbarpaletteitem that will wrap that node to make
   * drag and drop operations on that node work while in customize mode.
   *
   * @param {WidgetGroupWrapper|XULWidgetGroupWrapper} aWidget
   * @returns {DOMNode}
   */
  #makePaletteItem(aWidget) {
    let widgetNode = aWidget.forWindow(this.#window).node;
    if (!widgetNode) {
      lazy.log.error(
        "Widget with id " + aWidget.id + " does not return a valid node"
      );
      return null;
    }
    // Do not build a palette item for hidden widgets; there's not much to show.
    if (widgetNode.hidden) {
      return null;
    }

    let wrapper = this.createOrUpdateWrapper(widgetNode, "palette");
    wrapper.appendChild(widgetNode);
    return wrapper;
  }

  /**
   * Unwraps and moves items from the visible palette back to the hidden palette
   * when exiting customize mode. This also reassigns the toolbox's palette
   * property to point back at the default hidden palette, as this was
   * overridden to be the visible palette in #populatePalette.
   */
  #depopulatePalette() {
    // Quick, undo the command disabling before we depopulate completely:
    this.#updateCommandsDisabledState(false);

    this.visiblePalette.hidden = true;
    let paletteChild = this.visiblePalette.firstElementChild;
    let nextChild;
    while (paletteChild) {
      nextChild = paletteChild.nextElementSibling;
      let itemId = paletteChild.firstElementChild.id;
      if (CustomizableUI.isSpecialWidget(itemId)) {
        this.visiblePalette.removeChild(paletteChild);
      } else {
        // XXXunf Currently this doesn't destroy the (now unused) node in the
        //       API provider case. It would be good to do so, but we need to
        //       keep strong refs to it in CustomizableUI (can't iterate of
        //       WeakMaps), and there's the question of what behavior
        //       wrappers should have if consumers keep hold of them.
        let unwrappedPaletteItem = this.unwrapToolbarItem(paletteChild);
        this.#stowedPalette.appendChild(unwrappedPaletteItem);
      }

      paletteChild = nextChild;
    }
    this.visiblePalette.hidden = false;
    this.#window.gNavToolbox.palette = this.#stowedPalette;
  }

  /**
   * For all <command> elements in the window document that have no ID, or
   * are not in the #enabledCommands set, puts them in the "disabled" state if
   * `shouldBeDisabled` is true. For any command that was already disabled, adds
   * a "wasdisabled" attribute to the command.
   *
   * If `shouldBeDisabled` is false, removes the "wasdisabled" attribute from
   * any command nodes that have them, and for those that don't, removes the
   * "disabled" attribute.
   *
   * @param {boolean} shouldBeDisabled
   *   True if all <command> elements not in #enabledCommands should be
   *   disabled. False otherwise.
   */
  #updateCommandsDisabledState(shouldBeDisabled) {
    for (let command of this.#document.querySelectorAll("command")) {
      if (!command.id || !this.#enabledCommands.has(command.id)) {
        if (shouldBeDisabled) {
          if (!command.hasAttribute("disabled")) {
            command.setAttribute("disabled", true);
          } else {
            command.setAttribute("wasdisabled", true);
          }
        } else if (command.getAttribute("wasdisabled") != "true") {
          command.removeAttribute("disabled");
        } else {
          command.removeAttribute("wasdisabled");
        }
      }
    }
  }

  /**
   * Checks if the passed in DOM node is one that can represent a
   * customizable widget.
   *
   * @param {DOMNode} aNode
   *   The node to check to see if it's a customizable widget node.
   * @returns {boolean}
   *   `true` if the passed in DOM node is a type that can be used for
   *   customizable widgets.
   */
  #isCustomizableItem(aNode) {
    return (
      aNode.localName == "toolbarbutton" ||
      aNode.localName == "toolbaritem" ||
      aNode.localName == "toolbarseparator" ||
      aNode.localName == "toolbarspring" ||
      aNode.localName == "toolbarspacer"
    );
  }

  /**
   * Checks if the passed in DOM node is a toolbarpaletteitem wrapper.
   *
   * @param {DOMNode} aNode
   *   The node to check for being wrapped.
   * @returns {boolean}
   *   `true` if the passed in DOM node is a toolbarpaletteitem, meaning
   *   that it was wrapped via createOrUpdateWrapper.
   */
  isWrappedToolbarItem(aNode) {
    return aNode.localName == "toolbarpaletteitem";
  }

  /**
   * Queues a function for the main thread to execute soon that will wrap
   * aNode in a toolbarpaletteitem (or update the wrapper if it already exists).
   *
   * @param {DOMNode} aNode
   *   The node to wrap in a toolbarpaletteitem.
   * @param {string} aPlace
   *   The string to set as the "place" attribute on the node when it is
   *   wrapped. This is expected to be one of the strings returned by
   *   CustomizableUI.getPlaceForItem.
   * @returns {Promise<DOMNode>}
   *   Resolves with the wrapper node, or the node itself if the node is not
   *   a customizable item.
   */
  #deferredWrapToolbarItem(aNode, aPlace) {
    return new Promise(resolve => {
      Services.tm.dispatchToMainThread(() => {
        let wrapper = this.wrapToolbarItem(aNode, aPlace);
        resolve(wrapper);
      });
    });
  }

  /**
   * Creates or updates a wrapping toolbarpaletteitem around aNode, presuming
   * the node is a customizable item.
   *
   * @param {DOMNode} aNode
   *   The node to wrap, or update the wrapper for.
   * @param {string} aPlace
   *   The string to set as the "place" attribute on the node when it is
   *   wrapped. This is expected to be one of the strings returned by
   *   CustomizableUI.getPlaceForItem.
   * @returns {DOMNode}
   *   The toolbarbpaletteitem wrapper, in the event that aNode is a
   *   customizable item. Otherwise, returns aNode.
   */
  wrapToolbarItem(aNode, aPlace) {
    if (!this.#isCustomizableItem(aNode)) {
      return aNode;
    }
    let wrapper = this.createOrUpdateWrapper(aNode, aPlace);

    // It's possible that this toolbar node is "mid-flight" and doesn't have
    // a parent, in which case we skip replacing it. This can happen if a
    // toolbar item has been dragged into the palette. In that case, we tell
    // CustomizableUI to remove the widget from its area before putting the
    // widget in the palette - so the node will have no parent.
    if (aNode.parentNode) {
      aNode = aNode.parentNode.replaceChild(wrapper, aNode);
    }
    wrapper.appendChild(aNode);
    return wrapper;
  }

  /**
   * Helper to set the title and tooltiptext on a toolbarpaletteitem wrapper
   * based on the wrapped node - either by reading the label/title attributes
   * of aNode, or (in the event of delayed Fluent localization) setting up a
   * mutation observer on aNode such that when the label and/or title do
   * get set, we re-enter #updateWrapperLabel to update the toolbarpaletteitem.
   *
   * @param {DOMNode} aNode
   *   The wrapped customizable item to update the wrapper for.
   * @param {boolean} aIsUpdate
   *   True if the node already has a pre-existing wrapper that is being
   *   updated rather than created.
   * @param {DOMNode} [aWrapper=aNode.parentElement]
   *   The toolbarpaletteitem wrapper, in the event that it's not the
   *   immediate ancestor of aNode for some reason.
   */
  #updateWrapperLabel(aNode, aIsUpdate, aWrapper = aNode.parentElement) {
    if (aNode.hasAttribute("label")) {
      aWrapper.setAttribute("title", aNode.getAttribute("label"));
      aWrapper.setAttribute("tooltiptext", aNode.getAttribute("label"));
    } else if (aNode.hasAttribute("title")) {
      aWrapper.setAttribute("title", aNode.getAttribute("title"));
      aWrapper.setAttribute("tooltiptext", aNode.getAttribute("title"));
    } else if (aNode.hasAttribute("data-l10n-id") && !aIsUpdate) {
      this.#translationObserver.observe(aNode, {
        attributes: true,
        attributeFilter: ["label", "title"],
      });
    }
  }

  /**
   * Called when a node without a label or title has those attributes updated.
   *
   * @param {MutationRecord[]} aMutations
   *   The list of mutations for the label/title attributes of the nodes that
   *   had neither of those attributes set when wrapping them.
   */
  #onTranslations(aMutations) {
    for (let mut of aMutations) {
      let { target } = mut;
      if (
        target.parentElement?.localName == "toolbarpaletteitem" &&
        (target.hasAttribute("label") || mut.target.hasAttribute("title"))
      ) {
        this.#updateWrapperLabel(target, true);
      }
    }
  }

  /**
   * Creates or updates a toolbarpaletteitem to wrap a customizable item. This
   * wrapper makes it possible to click and drag these customizable items around
   * in the DOM without the underlying item having its event handlers invoked.
   *
   * @param {DOMNode} aNode
   *   The node to create or update the toolbarpaletteitem wrapper for.
   * @param {string} aPlace
   *   The string to set as the "place" attribute on the node when it is
   *   wrapped. This is expected to be one of the strings returned by
   *   CustomizableUI.getPlaceForItem.
   * @param {boolean} aIsUpdate
   *   True if it is expected that aNode is already wrapped and that we're
   *   updating the wrapper rather than creating it.
   * @returns {DOMNode}
   *   Returns the created or updated toolbarpaletteitem wrapper.
   */
  createOrUpdateWrapper(aNode, aPlace, aIsUpdate) {
    let wrapper;
    if (
      aIsUpdate &&
      aNode.parentNode &&
      aNode.parentNode.localName == "toolbarpaletteitem"
    ) {
      wrapper = aNode.parentNode;
      aPlace = wrapper.getAttribute("place");
    } else {
      wrapper = this.#document.createXULElement("toolbarpaletteitem");
      // "place" is used to show the label when it's sitting in the palette.
      wrapper.setAttribute("place", aPlace);
    }

    // Ensure the wrapped item doesn't look like it's in any special state, and
    // can't be interactved with when in the customization palette.
    // Note that some buttons opt out of this with the
    // keepbroadcastattributeswhencustomizing attribute.
    if (
      aNode.hasAttribute("command") &&
      aNode.getAttribute(kKeepBroadcastAttributes) != "true"
    ) {
      wrapper.setAttribute("itemcommand", aNode.getAttribute("command"));
      aNode.removeAttribute("command");
    }

    if (
      aNode.hasAttribute("observes") &&
      aNode.getAttribute(kKeepBroadcastAttributes) != "true"
    ) {
      wrapper.setAttribute("itemobserves", aNode.getAttribute("observes"));
      aNode.removeAttribute("observes");
    }

    if (aNode.hasAttribute("checked")) {
      wrapper.setAttribute("itemchecked", "true");
      aNode.removeAttribute("checked");
    }

    if (aNode.hasAttribute("id")) {
      wrapper.setAttribute("id", "wrapper-" + aNode.getAttribute("id"));
    }

    this.#updateWrapperLabel(aNode, aIsUpdate, wrapper);

    if (aNode.hasAttribute("flex")) {
      wrapper.setAttribute("flex", aNode.getAttribute("flex"));
    }

    let removable =
      aPlace == "palette" || CustomizableUI.isWidgetRemovable(aNode);
    wrapper.setAttribute("removable", removable);

    // Allow touch events to initiate dragging in customize mode.
    // This is only supported on Windows for now.
    wrapper.setAttribute("touchdownstartsdrag", "true");

    let contextMenuAttrName = "";
    if (aNode.getAttribute("context")) {
      contextMenuAttrName = "context";
    } else if (aNode.getAttribute("contextmenu")) {
      contextMenuAttrName = "contextmenu";
    }
    let currentContextMenu = aNode.getAttribute(contextMenuAttrName);
    let contextMenuForPlace =
      aPlace == "panel" ? kPanelItemContextMenu : kPaletteItemContextMenu;
    if (aPlace != "toolbar") {
      wrapper.setAttribute("context", contextMenuForPlace);
    }
    // Only keep track of the menu if it is non-default.
    if (currentContextMenu && currentContextMenu != contextMenuForPlace) {
      aNode.setAttribute("wrapped-context", currentContextMenu);
      aNode.setAttribute("wrapped-contextAttrName", contextMenuAttrName);
      aNode.removeAttribute(contextMenuAttrName);
    } else if (currentContextMenu == contextMenuForPlace) {
      aNode.removeAttribute(contextMenuAttrName);
    }

    // Only add listeners for newly created wrappers:
    if (!aIsUpdate) {
      wrapper.addEventListener("mousedown", this);
      wrapper.addEventListener("mouseup", this);
    }

    if (CustomizableUI.isSpecialWidget(aNode.id)) {
      this.#document.l10n.setAttributes(
        wrapper,
        kSpecialWidgetL10nIds[aNode.nodeName]
      );
    }

    return wrapper;
  }

  /**
   * Queues a function for the main thread to execute soon that will unwrap
   * the node wrapped with aWrapper, which should be a toolbarpaletteitem.
   *
   * @param {DOMNode} aWrapper
   *   The toolbarpaletteitem wrapper around a node to unwrap.
   * @returns {Promise<DOMNode|null>}
   *   Resolves with the unwrapped node, or null in the event that some
   *   problem occurred while unwrapping (which will be logged).
   */
  #deferredUnwrapToolbarItem(aWrapper) {
    return new Promise(resolve => {
      Services.tm.dispatchToMainThread(() => {
        let item = null;
        try {
          item = this.unwrapToolbarItem(aWrapper);
        } catch (ex) {
          console.error(ex);
        }
        resolve(item);
      });
    });
  }

  /**
   * Unwraps a customizable item wrapped with a toolbarpaletteitem. If the
   * passed in aWrapper is not a toolbarpaletteitem, this just returns
   * aWrapper.
   *
   * @param {DOMNode} aWrapper
   *   The toolbarpaletteitem wrapper around a node to be unwrapped.
   * @returns {DOMNode|null}
   *   Returns the unwrapped customizable item, or null if something went wrong
   *   while unwrapping.
   */
  unwrapToolbarItem(aWrapper) {
    if (aWrapper.nodeName != "toolbarpaletteitem") {
      return aWrapper;
    }
    aWrapper.removeEventListener("mousedown", this);
    aWrapper.removeEventListener("mouseup", this);

    let place = aWrapper.getAttribute("place");

    let toolbarItem = aWrapper.firstElementChild;
    if (!toolbarItem) {
      lazy.log.error(
        "no toolbarItem child for " + aWrapper.tagName + "#" + aWrapper.id
      );
      aWrapper.remove();
      return null;
    }

    if (aWrapper.hasAttribute("itemobserves")) {
      toolbarItem.setAttribute(
        "observes",
        aWrapper.getAttribute("itemobserves")
      );
    }

    if (aWrapper.hasAttribute("itemchecked")) {
      toolbarItem.checked = true;
    }

    if (aWrapper.hasAttribute("itemcommand")) {
      let commandID = aWrapper.getAttribute("itemcommand");
      toolbarItem.setAttribute("command", commandID);

      let command = this.$(commandID);
      toolbarItem.toggleAttribute(
        "disabled",
        !!command?.hasAttribute("disabled")
      );
    }

    let wrappedContext = toolbarItem.getAttribute("wrapped-context");
    if (wrappedContext) {
      let contextAttrName = toolbarItem.getAttribute("wrapped-contextAttrName");
      toolbarItem.setAttribute(contextAttrName, wrappedContext);
      toolbarItem.removeAttribute("wrapped-contextAttrName");
      toolbarItem.removeAttribute("wrapped-context");
    } else if (place == "panel") {
      toolbarItem.setAttribute("context", kPanelItemContextMenu);
    }

    if (aWrapper.parentNode) {
      aWrapper.parentNode.replaceChild(toolbarItem, aWrapper);
    }
    return toolbarItem;
  }

  /**
   * For a given area, iterates the children within its customize target,
   * and asynchronously wraps each customizable child in a
   * toolbarpaletteitem. This also adds drag and drop handlers to the
   * customize target for the area.
   *
   * @param {string} aArea
   *   The ID of the area to wrap the children for in the window that this
   *   CustomizeMode was instantiated for.
   * @returns {Promise<DOMNode|null>}
   *   Resolves with the customize target DOMNode for aArea for the window that
   *   this CustomizeMode was instantiated for - or null if the customize target
   *   cannot be found or is unknown to CustomizeMode.
   */
  async #wrapAreaItems(aArea) {
    let target = CustomizableUI.getCustomizeTargetForArea(aArea, this.#window);
    if (!target || this.areas.has(target)) {
      return null;
    }

    this.#addCustomizeTargetDragAndDropHandlers(target);
    for (let child of target.children) {
      if (
        this.#isCustomizableItem(child) &&
        !this.isWrappedToolbarItem(child)
      ) {
        await this.#deferredWrapToolbarItem(
          child,
          CustomizableUI.getPlaceForItem(child)
        ).catch(lazy.log.error);
      }
    }
    this.areas.add(target);
    return target;
  }

  /**
   * A synchronous version of #wrapAreaItems that will wrap all of the
   * customizable children of aArea's customize target in toolbarpaletteitems.
   *
   * @param {string} aArea
   *   The ID of the area to wrap the children for in the window that this
   *   CustomizeMode was instantiated for.
   * @returns {DOMNode|null}
   *   Returns the customize target DOMNode for aArea for the window that
   *   this CustomizeMode was instantiated for - or null if the customize target
   *   cannot be found or is unknown to CustomizeMode.
   */
  #wrapAreaItemsSync(aArea) {
    let target = CustomizableUI.getCustomizeTargetForArea(aArea, this.#window);
    if (!target || this.areas.has(target)) {
      return null;
    }

    this.#addCustomizeTargetDragAndDropHandlers(target);
    try {
      for (let child of target.children) {
        if (
          this.#isCustomizableItem(child) &&
          !this.isWrappedToolbarItem(child)
        ) {
          this.wrapToolbarItem(child, CustomizableUI.getPlaceForItem(child));
        }
      }
    } catch (ex) {
      lazy.log.error(ex, ex.stack);
    }

    this.areas.add(target);
    return target;
  }

  /**
   * Iterates all areas and asynchronously wraps their customize target children
   * with toolbarpaletteitems in the window that this CustomizeMode was
   * constructed for.
   *
   * @returns {Promise<undefined>}
   *   Resolves when wrapping has completed.
   */
  async #wrapAllAreaItems() {
    for (let area of CustomizableUI.areas) {
      await this.#wrapAreaItems(area);
    }
  }

  /**
   * Adds capturing drag and drop handlers for some customize target for some
   * customizable area. These handlers delegate event handling to the
   * handleEvent method.
   *
   * @param {DOMNode} aTarget
   *   The customize target node to add drag and drop handlers for.
   */
  #addCustomizeTargetDragAndDropHandlers(aTarget) {
    // Allow dropping on the padding of the arrow panel.
    if (aTarget.id == CustomizableUI.AREA_FIXED_OVERFLOW_PANEL) {
      aTarget = this.$("customization-panelHolder");
    }
    aTarget.addEventListener("dragstart", this, true);
    aTarget.addEventListener("dragover", this, true);
    aTarget.addEventListener("dragleave", this, true);
    aTarget.addEventListener("drop", this, true);
    aTarget.addEventListener("dragend", this, true);
  }

  /**
   * Iterates all of the customizable item children of a customize target within
   * a particular area, and attempts to wrap them in toolbarpaletteitems.
   *
   * @param {DOMNode} target
   *   The customize target for some customizable area.
   */
  #wrapItemsInArea(target) {
    for (let child of target.children) {
      if (this.#isCustomizableItem(child)) {
        this.wrapToolbarItem(child, CustomizableUI.getPlaceForItem(child));
      }
    }
  }

  /**
   * Removes capturing drag and drop handlers for some customize target for some
   * customizable area added via #addCustomizeTargetDragAndDropHandlers.
   *
   * @param {DOMNode} aTarget
   *   The customize target node to remove drag and drop handlers for.
   */
  #removeCustomizeTargetDragAndDropHandlers(aTarget) {
    // Remove handler from different target if it was added to
    // allow dropping on the padding of the arrow panel.
    if (aTarget.id == CustomizableUI.AREA_FIXED_OVERFLOW_PANEL) {
      aTarget = this.$("customization-panelHolder");
    }
    aTarget.removeEventListener("dragstart", this, true);
    aTarget.removeEventListener("dragover", this, true);
    aTarget.removeEventListener("dragleave", this, true);
    aTarget.removeEventListener("drop", this, true);
    aTarget.removeEventListener("dragend", this, true);
  }

  /**
   * Iterates all of the customizable item children of a customize target within
   * a particular area that have been wrapped in toolbarpaletteitems, and
   * attemps to unwrap them.
   *
   * @param {DOMNode} target
   *   The customize target for some customizable area.
   */
  #unwrapItemsInArea(target) {
    for (let toolbarItem of target.children) {
      if (this.isWrappedToolbarItem(toolbarItem)) {
        this.unwrapToolbarItem(toolbarItem);
      }
    }
  }

  /**
   * Iterates all areas and asynchronously unwraps their customize target
   * children that had been previously wrapped in toolbarpaletteitems in the
   * window that this CustomizeMode was constructed for. This also removes
   * the drag and drop handlers for each area.
   *
   * @returns {Promise<undefined>}
   *   Resolves when unwrapping has completed.
   */
  #unwrapAllAreaItems() {
    return (async () => {
      for (let target of this.areas) {
        for (let toolbarItem of target.children) {
          if (this.isWrappedToolbarItem(toolbarItem)) {
            await this.#deferredUnwrapToolbarItem(toolbarItem);
          }
        }
        this.#removeCustomizeTargetDragAndDropHandlers(target);
      }
      this.areas.clear();
    })().catch(lazy.log.error);
  }

  /**
   * Resets the customization state of the browser across all windows to the
   * default settings.
   *
   * @returns {Promise<undefined>}
   *   Resolves once resetting the customization state has completed.
   */
  reset() {
    this.resetting = true;
    // Disable the reset button temporarily while resetting:
    let btn = this.$("customization-reset-button");
    btn.disabled = true;
    return (async () => {
      this.#depopulatePalette();
      await this.#unwrapAllAreaItems();

      CustomizableUI.reset();

      await this.#wrapAllAreaItems();
      this.#populatePalette();

      this.#updateResetButton();
      this.#updateUndoResetButton();
      this.#updateEmptyPaletteNotice();
      this.#moveDownloadsButtonToNavBar = false;
      this.resetting = false;
      if (!this._wantToBeInCustomizeMode) {
        this.exit();
      }
    })().catch(lazy.log.error);
  }

  /**
   * Reverts a reset operation back to the prior customization state.
   *
   * @see CustomizeMode.reset()
   * @returns {Promise<undefined>}
   */
  undoReset() {
    this.resetting = true;

    return (async () => {
      this.#depopulatePalette();
      await this.#unwrapAllAreaItems();

      CustomizableUI.undoReset();

      await this.#wrapAllAreaItems();
      this.#populatePalette();

      this.#updateResetButton();
      this.#updateUndoResetButton();
      this.#updateEmptyPaletteNotice();
      this.#moveDownloadsButtonToNavBar = false;
      this.resetting = false;
    })().catch(lazy.log.error);
  }

  /**
   * Handler for toolbarvisibilitychange events that fire within the window
   * that this CustomizeMode was constructed for.
   *
   * @param {CustomEvent} aEvent
   *   The toolbarvisibilitychange event that was fired.
   */
  #onToolbarVisibilityChange(aEvent) {
    let toolbar = aEvent.target;
    toolbar.toggleAttribute(
      "customizing",
      aEvent.detail.visible && toolbar.getAttribute("customizable") == "true"
    );
    this.#onUIChange();
  }

  /**
   * The callback called by CustomizableUI when a widget moves.
   */
  onWidgetMoved() {
    this.#onUIChange();
  }

  /**
   * The callback called by CustomizableUI when a widget is added to an area.
   */
  onWidgetAdded() {
    this.#onUIChange();
  }

  /**
   * The callback called by CustomizableUI when a widget is removed from an
   * area.
   */
  onWidgetRemoved() {
    this.#onUIChange();
  }

  /**
   * The callback called by CustomizableUI *before* a widget's DOM node is acted
   * upon by CustomizableUI (to add, move or remove it).
   *
   * @param {Element} aNodeToChange
   *   The DOM node being acted upon.
   * @param {Element|null} aSecondaryNode
   *   The DOM node (if any) before which a widget will be inserted.
   * @param {Element} aContainer
   *   The *actual* DOM container for the widget (could be an overflow panel in
   *   case of an overflowable toolbar).
   */
  onWidgetBeforeDOMChange(aNodeToChange, aSecondaryNode, aContainer) {
    if (aContainer.documentGlobal != this.#window || this.resetting) {
      return;
    }
    // If we get called for widgets that aren't in the window yet, they might not have
    // a parentNode at all.
    if (aNodeToChange.parentNode) {
      this.unwrapToolbarItem(aNodeToChange.parentNode);
    }
    if (aSecondaryNode) {
      this.unwrapToolbarItem(aSecondaryNode.parentNode);
    }
  }

  /**
   * The callback called by CustomizableUI *after* a widget's DOM node is acted
   * upon by CustomizableUI (to add, move or remove it).
   *
   * @param {Element} aNodeToChange
   *   The DOM node that was acted upon.
   * @param {Element|null} aSecondaryNode
   *   The DOM node (if any) that the widget was inserted before.
   * @param {Element} aContainer
   *   The *actual* DOM container for the widget (could be an overflow panel in
   *   case of an overflowable toolbar).
   */
  onWidgetAfterDOMChange(aNodeToChange, aSecondaryNode, aContainer) {
    if (aContainer.documentGlobal != this.#window || this.resetting) {
      return;
    }
    // If the node is still attached to the container, wrap it again:
    if (aNodeToChange.parentNode) {
      let place = CustomizableUI.getPlaceForItem(aNodeToChange);
      this.wrapToolbarItem(aNodeToChange, place);
      if (aSecondaryNode) {
        this.wrapToolbarItem(aSecondaryNode, place);
      }
    } else {
      // If not, it got removed.

      // If an API-based widget is removed while customizing, append it to the palette.
      // The #applyDrop code itself will take care of positioning it correctly, if
      // applicable. We need the code to be here so removing widgets using CustomizableUI's
      // API also does the right thing (and adds it to the palette)
      let widgetId = aNodeToChange.id;
      let widget = CustomizableUI.getWidget(widgetId);
      if (widget.provider == CustomizableUI.PROVIDER_API) {
        let paletteItem = this.#makePaletteItem(widget);
        this.visiblePalette.appendChild(paletteItem);
      }
    }
  }

  /**
   * The callback called by CustomizableUI when a widget is destroyed. Only
   * fired for API-based widgets.
   *
   * @param {string} aWidgetId
   *   The ID of the widget that was destroyed.
   */
  onWidgetDestroyed(aWidgetId) {
    let wrapper = this.$("wrapper-" + aWidgetId);
    if (wrapper) {
      wrapper.remove();
    }
  }

  /**
   * The callback called by CustomizableUI after a widget with id aWidgetId has
   * been created, and has been added to either its default area or the area in
   * which it was placed previously. If the widget has no default area and/or it
   * has never been placed anywhere, aArea may be null. Only fired for API-based
   * widgets.
   *
   * @param {string} aWidgetId
   *   The ID of the widget that was just created.
   * @param {string|null} aArea
   *   The ID of the area that the widget was placed in, or null if it is
   *   now in the customization palette.
   */
  onWidgetAfterCreation(aWidgetId, aArea) {
    // If the node was added to an area, we would have gotten an onWidgetAdded notification,
    // plus associated DOM change notifications, so only do stuff for the palette:
    if (!aArea) {
      let widgetNode = this.$(aWidgetId);
      if (widgetNode) {
        this.wrapToolbarItem(widgetNode, "palette");
      } else {
        let widget = CustomizableUI.getWidget(aWidgetId);
        this.visiblePalette.appendChild(this.#makePaletteItem(widget));
      }
    }
  }

  /**
   * Called by CustomizableUI after an area node is first built when it is
   * registered.
   *
   * @param {string} aArea
   *   The ID for the area that was just registered.
   * @param {DOMNode} aContainer
   *   The DOM node for the customizable area.
   */
  onAreaNodeRegistered(aArea, aContainer) {
    if (aContainer.ownerDocument == this.#document) {
      this.#wrapItemsInArea(aContainer);
      this.#addCustomizeTargetDragAndDropHandlers(aContainer);
      this.areas.add(aContainer);
    }
  }

  /**
   * Called by CustomizableUI after an area node is unregistered and no longer
   * available in this window.
   *
   * @param {string} aArea
   *   The ID for the area that was just registered.
   * @param {DOMNode} aContainer
   *   The DOM node for the customizable area.
   * @param {string} aReason
   *   One of the CustomizableUI.REASON_* constants to describe the reason
   *   that the area was unregistered for.
   */
  onAreaNodeUnregistered(aArea, aContainer, aReason) {
    if (
      aContainer.ownerDocument == this.#document &&
      aReason == CustomizableUI.REASON_AREA_UNREGISTERED
    ) {
      this.#unwrapItemsInArea(aContainer);
      this.#removeCustomizeTargetDragAndDropHandlers(aContainer);
      this.areas.delete(aContainer);
    }
  }

  /**
   * Opens about:preferences#appearance to the Window Density section in a new tab.
   */
  #openUIDensityPreferences() {
    this.#window.openPreferences("appearance-windowDensity");
  }

  /**
   * Updates the visible state of the UI density controls. When Nova is enabled,
   * a link to the Window Density section of about:preferences is shown instead
   * of the density dropdown, regardless of the compact mode pref. Otherwise the
   * dropdown is shown following the legacy compact mode visibility rules.
   */
  #updateDensityMenu() {
    let button = this.#document.getElementById(
      "customization-uidensity-button"
    );
    let link = this.#document.getElementById("customization-uidensity-link");

    if (this.#window.gUIDensity.novaEnabled) {
      button.hidden = true;
      link.hidden = false;
      return;
    }

    link.hidden = true;

    // If we're entering Customize Mode, and we're using compact mode,
    // then show the button after that.
    let gUIDensity = this.#window.gUIDensity;
    if (gUIDensity.getCurrentDensity().mode == gUIDensity.MODE_COMPACT) {
      Services.prefs.setBoolPref(kCompactModeShowPref, true);
    }

    button.hidden =
      !Services.prefs.getBoolPref(kCompactModeShowPref) &&
      !button.querySelector("#customization-uidensity-menuitem-touch");
  }

  /**
   * Opens about:addons in a new tab, showing the themes list.
   */
  #openAddonsManagerThemes() {
    this.#window.BrowserAddonUI.openAddonsMgr("addons://list/theme");
  }

  /**
   * Temporarily updates the density of the browser UI to suit the passed in
   * mode. This is used to preview the density of the browser while the user
   * hovers the various density options, and is reset when the user stops
   * hovering the options.
   *
   * @param {number|null} mode
   *   One of the density mode constants from gUIDensity - for example,
   *   gUIDensity.MODE_TOUCH.
   */
  #previewUIDensity(mode) {
    this.#window.gUIDensity.update(mode);
    this.#updateOverflowPanelArrowOffset();
  }

  /**
   * Resets the current UI density to the currently configured density. This
   * is used after temporarily previewing a density.
   */
  #resetUIDensity() {
    this.#window.gUIDensity.update();
    this.#updateOverflowPanelArrowOffset();
  }

  /**
   * Sets a UI density mode as the configured density.
   *
   * @param {number|null} mode
   *   One of the density mode constants from gUIDensity - for example,
   *   gUIDensity.MODE_TOUCH.
   */
  setUIDensity(mode) {
    let win = this.#window;
    let gUIDensity = win.gUIDensity;
    let panel = win.document.getElementById("customization-uidensity-menu");

    Services.prefs.setIntPref(gUIDensity.uiDensityPref, mode);

    this.#onUIChange();
    panel.hidePopup();
    this.#updateOverflowPanelArrowOffset();
  }

  /**
   * Updates the state of the UI density menupopup to correctly reflect the
   * current configured density and to list the available alternative densities.
   */
  #onUIDensityMenuShowing() {
    let win = this.#window;
    let doc = win.document;
    let gUIDensity = win.gUIDensity;
    let currentDensity = gUIDensity.getCurrentDensity();

    let normalItem = doc.getElementById(
      "customization-uidensity-menuitem-normal"
    );
    normalItem.mode = gUIDensity.MODE_NORMAL;

    let items = [normalItem];

    let compactItem = doc.getElementById(
      "customization-uidensity-menuitem-compact"
    );
    compactItem.mode = gUIDensity.MODE_COMPACT;

    if (Services.prefs.getBoolPref(kCompactModeShowPref)) {
      compactItem.hidden = false;
      items.push(compactItem);
    } else {
      compactItem.hidden = true;
    }

    let touchItem = doc.getElementById(
      "customization-uidensity-menuitem-touch"
    );
    // Touch mode can not be enabled in OSX right now.
    if (touchItem) {
      touchItem.mode = gUIDensity.MODE_TOUCH;
      items.push(touchItem);
    }

    // Mark the active mode menuitem.
    for (let item of items) {
      if (item.mode == currentDensity.mode) {
        item.setAttribute("aria-checked", "true");
        item.setAttribute("active", "true");
      } else {
        item.removeAttribute("aria-checked");
        item.removeAttribute("active");
      }
    }

    // Add menu items for automatically switching to Touch mode in Windows Tablet Mode.
    if (AppConstants.platform == "win") {
      let spacer = doc.getElementById("customization-uidensity-touch-spacer");
      let checkbox = doc.getElementById(
        "customization-uidensity-autotouchmode-checkbox"
      );
      spacer.removeAttribute("hidden");
      checkbox.removeAttribute("hidden");

      // Show a hint that the UI density was overridden automatically.
      if (currentDensity.overridden) {
        let sb = Services.strings.createBundle(
          "chrome://browser/locale/uiDensity.properties"
        );
        touchItem.setAttribute(
          "acceltext",
          sb.GetStringFromName("uiDensity.menuitem-touch.acceltext")
        );
      } else {
        touchItem.removeAttribute("acceltext");
      }

      let autoTouchMode = Services.prefs.getBoolPref(
        win.gUIDensity.autoTouchModePref
      );
      if (autoTouchMode) {
        checkbox.setAttribute("checked", "true");
      } else {
        checkbox.removeAttribute("checked");
      }
    }
  }

  /**
   * Sets "automatic" touch mode to enabled or disabled. Automatic touch mode
   * means that touch density is used automatically if the device has switched
   * into a tablet mode.
   *
   * @param {boolean} checked
   *   True if automatic touch mode should be enabled.
   */
  #updateAutoTouchMode(checked) {
    Services.prefs.setBoolPref("browser.touchmode.auto", checked);
    // Re-render the menu items since the active mode might have
    // change because of this.
    this.#onUIDensityMenuShowing();
    this.#onUIChange();
  }

  /**
   * Called anytime the UI configuration has changed in such a way that we need
   * to update the state and appearance of customize mode.
   */
  #onUIChange() {
    if (!this.resetting) {
      this.#updateResetButton();
      this.#updateUndoResetButton();
      this.#updateEmptyPaletteNotice();
    }
    CustomizableUI.dispatchToolboxEvent("customizationchange");
  }

  /**
   * Handles updating the state of customize mode if the palette has been
   * emptied such that only the special "spring" element remains (as this
   * cannot be removed from the palette).
   */
  #updateEmptyPaletteNotice() {
    let paletteItems =
      this.visiblePalette.getElementsByTagName("toolbarpaletteitem");
    let whimsyButton = this.$("whimsy-button");

    if (
      paletteItems.length == 1 &&
      paletteItems[0].id.includes("wrapper-customizableui-special-spring")
    ) {
      whimsyButton.hidden = false;
    } else {
      this.#togglePong(false);
      whimsyButton.hidden = true;
    }
  }

  /**
   * Updates the enabled / disabled state of the Restore Defaults button based
   * on whether or not we're already in the default state.
   */
  #updateResetButton() {
    let btn = this.$("customization-reset-button");
    btn.disabled = CustomizableUI.inDefaultState;
  }

  /**
   * Updates the hidden / visible state of the "undo reset" button based on
   * whether or not we've just performed a reset that can be undone.
   */
  #updateUndoResetButton() {
    let undoResetButton = this.$("customization-undo-reset-button");
    undoResetButton.hidden = !CustomizableUI.canUndoReset;
  }

  /**
   * On macOS, if a touch bar is available on the device, updates the
   * hidden / visible state of the Customize Touch Bar button and spacer.
   */
  #updateTouchBarButton() {
    if (AppConstants.platform != "macosx") {
      return;
    }
    let touchBarButton = this.$("customization-touchbar-button");
    let touchBarSpacer = this.$("customization-touchbar-spacer");

    let isTouchBarInitialized = lazy.gTouchBarUpdater.isTouchBarInitialized();
    touchBarButton.hidden = !isTouchBarInitialized;
    touchBarSpacer.hidden = !isTouchBarInitialized;
  }

  /**
   * Generic event handler used throughout most of the Customize Mode UI. This
   * is mainly used to dispatch events to more specific handlers based on the
   * event type.
   *
   * @param {Event} aEvent
   *   The event being handled.
   */
  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "toolbarvisibilitychange":
        this.#onToolbarVisibilityChange(aEvent);
        break;
      case "dragstart":
        this.#onDragStart(aEvent);
        break;
      case "dragover":
        this.#onDragOver(aEvent);
        break;
      case "drop":
        this.#onDragDrop(aEvent);
        break;
      case "dragleave":
        this.#onDragLeave(aEvent);
        break;
      case "dragend":
        this.#onDragEnd(aEvent);
        break;
      case "mousedown":
        this.#onMouseDown(aEvent);
        break;
      case "mouseup":
        this.#onMouseUp(aEvent);
        break;
      case "keypress":
        if (aEvent.keyCode == aEvent.DOM_VK_ESCAPE) {
          this.exit();
        }
        break;
      case "unload":
        this.#uninit();
        break;
    }
  }

  /**
   * Sets up the dragover/drop handlers on the visible palette. We handle
   * dragover/drop on the outer palette separately to avoid overlap with other
   * drag/drop handlers.
   */
  #setupPaletteDragging() {
    this.#addCustomizeTargetDragAndDropHandlers(this.visiblePalette);

    this.paletteDragHandler = aEvent => {
      let originalTarget = aEvent.originalTarget;
      if (
        this.#isUnwantedDragDrop(aEvent) ||
        this.visiblePalette.contains(originalTarget) ||
        this.$("customization-panelHolder").contains(originalTarget)
      ) {
        return;
      }
      // We have a dragover/drop on the palette.
      if (aEvent.type == "dragover") {
        this.#onDragOver(aEvent, this.visiblePalette);
      } else {
        this.#onDragDrop(aEvent, this.visiblePalette);
      }
    };
    let contentContainer = this.$("customization-content-container");
    contentContainer.addEventListener(
      "dragover",
      this.paletteDragHandler,
      true
    );
    contentContainer.addEventListener("drop", this.paletteDragHandler, true);
  }

  /**
   * Tears down the dragover/drop handlers on the visible palette added by
   * #setupPaletteDragging.
   */
  #teardownPaletteDragging() {
    lazy.DragPositionManager.stop();
    this.#removeCustomizeTargetDragAndDropHandlers(this.visiblePalette);

    let contentContainer = this.$("customization-content-container");
    contentContainer.removeEventListener(
      "dragover",
      this.paletteDragHandler,
      true
    );
    contentContainer.removeEventListener("drop", this.paletteDragHandler, true);
    delete this.paletteDragHandler;
  }

  /**
   * Implements nsIObserver. This is mainly to observe for preference changes.
   *
   * @param {nsISupports} aSubject
   *   The nsISupports subject for the notification topic that is being
   *   observed.
   * @param {string} aTopic
   *   The notification topic that is being observed.
   */
  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "nsPref:changed":
        this.#updateResetButton();
        this.#updateUndoResetButton();
        if (this.#canDrawInTitlebar()) {
          this.#updateTitlebarCheckbox();
        }
        break;
    }
  }

  /**
   * Returns true if the current platform and configuration allows us to draw in
   * the window titlebar.
   *
   * @returns {boolean}
   */
  #canDrawInTitlebar() {
    return this.#window.CustomTitlebar.systemSupported;
  }

  /**
   * De-lazifies the customization panel and the menupopup / panel template
   * holding various DOM nodes for customize mode. These things are lazily
   * added to the DOM to avoid polluting the browser window DOM with things
   * that only Customize Mode cares about.
   */
  #ensureCustomizationPanels() {
    let template = this.$("customizationPanel");
    template.replaceWith(template.content);

    let wrapper = this.$("customModeWrapper");
    wrapper.replaceWith(wrapper.content);
  }

  /**
   * Adds event listeners for all of the interactive elements in the window that
   * this Customize Mode instance was constructed with.
   */
  #attachEventListeners() {
    let container = this.$("customization-container");

    container.addEventListener("command", event => {
      switch (event.target.id) {
        case "customization-titlebar-visibility-checkbox":
          // NB: because command fires after click, by the time we've fired, the checkbox binding
          //     will already have switched the button's state, so this is correct:
          this.#toggleTitlebar(event.target.checked);
          break;
        case "customization-uidensity-menuitem-compact":
        case "customization-uidensity-menuitem-normal":
        case "customization-uidensity-menuitem-touch":
          this.setUIDensity(event.target.mode);
          break;
        case "customization-uidensity-autotouchmode-checkbox":
          this.#updateAutoTouchMode(event.target.checked);
          break;
        case "whimsy-button":
          this.#togglePong(event.target.checked);
          break;
        case "customization-touchbar-button":
          this.#customizeTouchBar();
          break;
        case "customization-undo-reset-button":
          Glean.customize.undoRestore.record();
          this.undoReset();
          break;
        case "customization-reset-button":
          Glean.customize.restoreDefaults.record();
          this.reset();
          break;
        case "customization-done-button":
          this.exit();
          break;
      }
    });

    container.addEventListener("popupshowing", event => {
      switch (event.target.id) {
        case "customization-toolbar-menu":
          this.#window.ToolbarContextMenu.onViewToolbarsPopupShowing(event);
          break;
        case "customization-uidensity-menu":
          this.#onUIDensityMenuShowing();
          break;
      }
    });

    let updateDensity = event => {
      switch (event.target.id) {
        case "customization-uidensity-menuitem-compact":
        case "customization-uidensity-menuitem-normal":
        case "customization-uidensity-menuitem-touch":
          this.#previewUIDensity(event.target.mode);
      }
    };
    let densityMenu = this.#document.getElementById(
      "customization-uidensity-menu"
    );
    densityMenu.addEventListener("focus", updateDensity);
    densityMenu.addEventListener("mouseover", updateDensity);

    let resetDensity = event => {
      switch (event.target.id) {
        case "customization-uidensity-menuitem-compact":
        case "customization-uidensity-menuitem-normal":
        case "customization-uidensity-menuitem-touch":
          this.#resetUIDensity();
      }
    };
    densityMenu.addEventListener("blur", resetDensity);
    densityMenu.addEventListener("mouseout", resetDensity);

    this.$("customization-uidensity-link").addEventListener("click", () => {
      this.#openUIDensityPreferences();
    });

    this.$("customization-lwtheme-link").addEventListener("click", () => {
      this.#openAddonsManagerThemes();
    });

    this.$(kPaletteItemContextMenu).addEventListener("popupshowing", event => {
      this.#onPaletteContextMenuShowing(event);
    });

    this.$(kPaletteItemContextMenu).addEventListener("command", event => {
      switch (event.target.id) {
        case "customizationPaletteItemContextMenuAddToToolbar":
          this.addToToolbar(
            event.target.parentNode.triggerNode,
            "palette-context"
          );
          break;
        case "customizationPaletteItemContextMenuAddToPanel":
          this.addToPanel(
            event.target.parentNode.triggerNode,
            "palette-context"
          );
          break;
      }
    });

    let autohidePanel = this.$(kDownloadAutohidePanelId);
    autohidePanel.addEventListener("popupshown", event => {
      this._downloadPanelAutoHideTimeout = this.#window.setTimeout(
        () => event.target.hidePopup(),
        4000
      );
    });
    autohidePanel.addEventListener("mouseover", () => {
      this.#window.clearTimeout(this._downloadPanelAutoHideTimeout);
    });
    autohidePanel.addEventListener("mouseout", event => {
      this._downloadPanelAutoHideTimeout = this.#window.setTimeout(
        () => event.target.hidePopup(),
        2000
      );
    });
    autohidePanel.addEventListener("popuphidden", () => {
      this.#window.clearTimeout(this._downloadPanelAutoHideTimeout);
    });

    this.$(kDownloadAutohideCheckboxId).addEventListener("command", event => {
      this.#onDownloadsAutoHideChange(event);
    });
  }

  /**
   * Updates the checked / unchecked state of the Titlebar checkbox, to
   * reflect whether or not we're currently configured to show the native
   * titlebar or not.
   */
  #updateTitlebarCheckbox() {
    let drawInTitlebar = Services.appinfo.drawInTitlebar;
    let checkbox = this.$("customization-titlebar-visibility-checkbox");
    // Drawing in the titlebar means 'hiding' the titlebar.
    // We use the attribute rather than a property because if we're not in
    // customize mode the button is hidden and properties don't work.
    if (drawInTitlebar) {
      checkbox.removeAttribute("checked");
    } else {
      checkbox.setAttribute("checked", "true");
    }
  }

  /**
   * Configures whether or not we should show the native titlebar.
   *
   * @param {boolean} aShouldShowTitlebar
   *   True if we should show the native titlebar. False to draw the browser
   *   UI into the titlebar instead.
   */
  #toggleTitlebar(aShouldShowTitlebar) {
    // Drawing in the titlebar means not showing the titlebar, hence the negation:
    Services.prefs.setIntPref(kDrawInTitlebarPref, !aShouldShowTitlebar);
  }

  /**
   * A convenient shortcut to calling getBoundsWithoutFlushing on this windows'
   * nsIDOMWindowUtils.
   *
   * @param {DOMNode} element
   *   An element for which to try to get the bounding client rect, but without
   *   flushing styles or layout.
   * @returns {DOMRect}
   */
  #getBoundsWithoutFlushing(element) {
    return this.#window.windowUtils.getBoundsWithoutFlushing(element);
  }

  /**
   * Handles the dragstart event on any customizable item in one of the
   * customizable areas.
   *
   * @param {DragEvent} aEvent
   *   The dragstart event being handled.
   */
  #onDragStart(aEvent) {
    __dumpDragData(aEvent);
    let item = aEvent.target;
    while (item && item.localName != "toolbarpaletteitem") {
      if (
        item.localName == "toolbar" ||
        item.id == kPaletteId ||
        item.id == "customization-panelHolder"
      ) {
        return;
      }
      item = item.parentNode;
    }

    let draggedItem = item.firstElementChild;
    let placeForItem = CustomizableUI.getPlaceForItem(item);

    let dt = aEvent.dataTransfer;
    let documentId = aEvent.target.ownerDocument.documentElement.id;

    dt.mozSetDataAt(kDragDataTypePrefix + documentId, draggedItem.id, 0);
    dt.effectAllowed = "move";

    let itemRect = this.#getBoundsWithoutFlushing(draggedItem);
    let itemCenter = {
      x: itemRect.left + itemRect.width / 2,
      y: itemRect.top + itemRect.height / 2,
    };
    this._dragOffset = {
      x: aEvent.clientX - itemCenter.x,
      y: aEvent.clientY - itemCenter.y,
    };

    let toolbarParent = draggedItem.closest("toolbar");
    if (toolbarParent) {
      let toolbarRect = this.#getBoundsWithoutFlushing(toolbarParent);
      toolbarParent.style.minHeight = toolbarRect.height + "px";
    }

    gDraggingInToolbars = new Set();

    // Hack needed so that the dragimage will still show the
    // item as it appeared before it was hidden.
    this._initializeDragAfterMove = () => {
      // For automated tests, we sometimes start exiting customization mode
      // before this fires, which leaves us with placeholders inserted after
      // we've exited. So we need to check that we are indeed customizing.
      if (this.#customizing && !this.#transitioning) {
        item.hidden = true;
        lazy.DragPositionManager.start(this.#window);
        let canUsePrevSibling =
          placeForItem == "toolbar" || placeForItem == "panel";
        if (item.nextElementSibling) {
          this.#setDragActive(
            item.nextElementSibling,
            "before",
            draggedItem.id,
            placeForItem
          );
          this.#dragOverItem = item.nextElementSibling;
        } else if (canUsePrevSibling && item.previousElementSibling) {
          this.#setDragActive(
            item.previousElementSibling,
            "after",
            draggedItem.id,
            placeForItem
          );
          this.#dragOverItem = item.previousElementSibling;
        }
        let currentArea = this.#getCustomizableParent(item);
        currentArea.setAttribute("draggingover", "true");
      }
      this._initializeDragAfterMove = null;
      this.#window.clearTimeout(this._dragInitializeTimeout);
    };
    this._dragInitializeTimeout = this.#window.setTimeout(
      this._initializeDragAfterMove,
      0
    );
  }

  /**
   * Handles the dragover event for any customizable area.
   *
   * @param {DragEvent} aEvent
   *   The dragover event being handled.
   * @param {DOMNode} [aOverrideTarget=undefined]
   *   Optional argument that allows callers to override the dragover target to
   *   be something other than the dragover event current target.
   */
  #onDragOver(aEvent, aOverrideTarget) {
    if (this.#isUnwantedDragDrop(aEvent)) {
      return;
    }
    if (this._initializeDragAfterMove) {
      this._initializeDragAfterMove();
    }

    __dumpDragData(aEvent);

    let document = aEvent.target.ownerDocument;
    let documentId = document.documentElement.id;
    if (!aEvent.dataTransfer.mozTypesAt(0).length) {
      return;
    }

    let draggedItemId = aEvent.dataTransfer.mozGetDataAt(
      kDragDataTypePrefix + documentId,
      0
    );
    let draggedWrapper = document.getElementById("wrapper-" + draggedItemId);
    let targetArea = this.#getCustomizableParent(
      aOverrideTarget || aEvent.currentTarget
    );
    let originArea = this.#getCustomizableParent(draggedWrapper);

    // Do nothing if the target or origin are not customizable.
    if (!targetArea || !originArea) {
      return;
    }

    // Do nothing if the widget is not allowed to be removed.
    if (
      targetArea.id == kPaletteId &&
      !CustomizableUI.isWidgetRemovable(draggedItemId)
    ) {
      return;
    }

    // Do nothing if the widget is not allowed to move to the target area.
    if (!CustomizableUI.canWidgetMoveToArea(draggedItemId, targetArea.id)) {
      return;
    }

    let targetAreaType = CustomizableUI.getPlaceForItem(targetArea);
    let targetNode = this.#getDragOverNode(
      aEvent,
      targetArea,
      targetAreaType,
      draggedItemId
    );

    // We need to determine the place that the widget is being dropped in
    // the target.
    let dragOverItem, dragValue;
    if (targetNode == CustomizableUI.getCustomizationTarget(targetArea)) {
      // We'll assume if the user is dragging directly over the target, that
      // they're attempting to append a child to that target.
      dragOverItem =
        (targetAreaType == "toolbar"
          ? this.#findVisiblePreviousSiblingNode(targetNode.lastElementChild)
          : targetNode.lastElementChild) || targetNode;
      dragValue = "after";
    } else {
      let targetParent = targetNode.parentNode;
      let position = Array.prototype.indexOf.call(
        targetParent.children,
        targetNode
      );
      if (position == -1) {
        dragOverItem =
          targetAreaType == "toolbar"
            ? this.#findVisiblePreviousSiblingNode(targetNode.lastElementChild)
            : targetNode.lastElementChild;
        dragValue = "after";
      } else {
        dragOverItem = targetParent.children[position];
        if (targetAreaType == "toolbar") {
          // Check if the aDraggedItem is hovered past the first half of dragOverItem
          let itemRect = this.#getBoundsWithoutFlushing(dragOverItem);
          let dropTargetCenter = itemRect.left + itemRect.width / 2;
          let existingDir = dragOverItem.getAttribute("dragover");
          let dirFactor = this.#window.RTL_UI ? -1 : 1;
          if (existingDir == "before") {
            dropTargetCenter +=
              ((parseInt(dragOverItem.style.borderInlineStartWidth) || 0) / 2) *
              dirFactor;
          } else {
            dropTargetCenter -=
              ((parseInt(dragOverItem.style.borderInlineEndWidth) || 0) / 2) *
              dirFactor;
          }
          let before = this.#window.RTL_UI
            ? aEvent.clientX > dropTargetCenter
            : aEvent.clientX < dropTargetCenter;
          dragValue = before ? "before" : "after";
        } else if (targetAreaType == "panel") {
          let itemRect = this.#getBoundsWithoutFlushing(dragOverItem);
          let dropTargetCenter = itemRect.top + itemRect.height / 2;
          let existingDir = dragOverItem.getAttribute("dragover");
          if (existingDir == "before") {
            dropTargetCenter +=
              (parseInt(dragOverItem.style.borderBlockStartWidth) || 0) / 2;
          } else {
            dropTargetCenter -=
              (parseInt(dragOverItem.style.borderBlockEndWidth) || 0) / 2;
          }
          dragValue = aEvent.clientY < dropTargetCenter ? "before" : "after";
        } else {
          dragValue = "before";
        }
      }
    }

    if (this.#dragOverItem && dragOverItem != this.#dragOverItem) {
      this.#cancelDragActive(this.#dragOverItem, dragOverItem);
    }

    if (
      dragOverItem != this.#dragOverItem ||
      dragValue != dragOverItem.getAttribute("dragover")
    ) {
      if (dragOverItem != CustomizableUI.getCustomizationTarget(targetArea)) {
        this.#setDragActive(
          dragOverItem,
          dragValue,
          draggedItemId,
          targetAreaType
        );
      }
      this.#dragOverItem = dragOverItem;
      targetArea.setAttribute("draggingover", "true");
    }

    aEvent.preventDefault();
    aEvent.stopPropagation();
  }

  /**
   * Handles the drop event on any customizable area.
   *
   * @param {DragEvent} aEvent
   *   The drop event being handled.
   * @param {DOMNode} [aOverrideTarget=undefined]
   *   Optional argument that allows callers to override the drop target to
   *   be something other than the drop event current target.
   */
  #onDragDrop(aEvent, aOverrideTarget) {
    if (this.#isUnwantedDragDrop(aEvent)) {
      return;
    }

    __dumpDragData(aEvent);
    this._initializeDragAfterMove = null;
    this.#window.clearTimeout(this._dragInitializeTimeout);

    let targetArea = this.#getCustomizableParent(
      aOverrideTarget || aEvent.currentTarget
    );
    let document = aEvent.target.ownerDocument;
    let documentId = document.documentElement.id;
    let draggedItemId = aEvent.dataTransfer.mozGetDataAt(
      kDragDataTypePrefix + documentId,
      0
    );
    let draggedWrapper = document.getElementById("wrapper-" + draggedItemId);
    let originArea = this.#getCustomizableParent(draggedWrapper);
    if (this.#dragSizeMap) {
      this.#dragSizeMap = new WeakMap();
    }
    // Do nothing if the target area or origin area are not customizable.
    if (!targetArea || !originArea) {
      return;
    }
    let targetNode = this.#dragOverItem;
    let dropDir = targetNode.getAttribute("dragover");
    // Need to insert *after* this node if we promised the user that:
    if (targetNode != targetArea && dropDir == "after") {
      if (targetNode.nextElementSibling) {
        targetNode = targetNode.nextElementSibling;
      } else {
        targetNode = targetArea;
      }
    }
    if (targetNode.tagName == "toolbarpaletteitem") {
      targetNode = targetNode.firstElementChild;
    }

    this.#cancelDragActive(this.#dragOverItem, null, true);

    try {
      this.#applyDrop(
        aEvent,
        targetArea,
        originArea,
        draggedItemId,
        targetNode
      );
    } catch (ex) {
      lazy.log.error(ex, ex.stack);
    }

    // If the user explicitly moves this item, turn off autohide.
    if (draggedItemId == "downloads-button") {
      Services.prefs.setBoolPref(kDownloadAutoHidePref, false);
      this.#showDownloadsAutoHidePanel();
    }
  }

  /**
   * A helper method for #onDragDrop that applies the changes to the browser
   * UI from the drop event on either a customizable item, or an area.
   *
   * @param {DragEvent} aEvent
   *   The drop event being handled.
   * @param {DOMNode} aTargetArea
   *   The target area node being dropped on.
   * @param {DOMNode} aOriginArea
   *   The origin area node that the dropped item originally came from.
   * @param {string} aDroppedItemId
   *   The ID value of the customizable item being dropped.
   * @param {DOMNode} aTargetNode
   *   The customizable item (or area) node being dropped on.
   */
  #applyDrop(aEvent, aTargetArea, aOriginArea, aDroppedItemId, aTargetNode) {
    let document = aEvent.target.ownerDocument;
    let draggedItem = document.getElementById(aDroppedItemId);
    draggedItem.hidden = false;
    draggedItem.removeAttribute("mousedown");

    let toolbarParent = draggedItem.closest("toolbar");
    if (toolbarParent) {
      toolbarParent.style.removeProperty("min-height");
    }

    // Do nothing if the target was dropped onto itself (ie, no change in area
    // or position).
    if (draggedItem == aTargetNode) {
      return;
    }

    if (!CustomizableUI.canWidgetMoveToArea(aDroppedItemId, aTargetArea.id)) {
      return;
    }

    // Is the target area the customization palette?
    if (aTargetArea.id == kPaletteId) {
      // Did we drag from outside the palette?
      if (aOriginArea.id !== kPaletteId) {
        if (!CustomizableUI.isWidgetRemovable(aDroppedItemId)) {
          return;
        }

        CustomizableUI.removeWidgetFromArea(aDroppedItemId, "drag");
        lazy.BrowserUsageTelemetry.recordWidgetChange(
          aDroppedItemId,
          null,
          "drag"
        );
        // Special widgets are removed outright, we can return here:
        if (CustomizableUI.isSpecialWidget(aDroppedItemId)) {
          return;
        }
      }
      draggedItem = draggedItem.parentNode;

      // If the target node is the palette itself, just append
      if (aTargetNode == this.visiblePalette) {
        this.visiblePalette.appendChild(draggedItem);
      } else {
        // The items in the palette are wrapped, so we need the target node's parent here:
        this.visiblePalette.insertBefore(draggedItem, aTargetNode.parentNode);
      }
      this.#onDragEnd(aEvent);
      return;
    }

    // Skipintoolbarset items won't really be moved:
    let areaCustomizationTarget =
      CustomizableUI.getCustomizationTarget(aTargetArea);
    if (draggedItem.getAttribute("skipintoolbarset") == "true") {
      // These items should never leave their area:
      if (aTargetArea != aOriginArea) {
        return;
      }
      let place = draggedItem.parentNode.getAttribute("place");
      this.unwrapToolbarItem(draggedItem.parentNode);
      if (aTargetNode == areaCustomizationTarget) {
        areaCustomizationTarget.appendChild(draggedItem);
      } else {
        this.unwrapToolbarItem(aTargetNode.parentNode);
        areaCustomizationTarget.insertBefore(draggedItem, aTargetNode);
        this.wrapToolbarItem(aTargetNode, place);
      }
      this.wrapToolbarItem(draggedItem, place);
      return;
    }

    // Force creating a new spacer/spring/separator if dragging from the palette
    if (
      CustomizableUI.isSpecialWidget(aDroppedItemId) &&
      aOriginArea.id == kPaletteId
    ) {
      aDroppedItemId = aDroppedItemId.match(
        /^customizableui-special-(spring|spacer|separator)/
      )[1];
    }

    // Is the target the customization area itself? If so, we just add the
    // widget to the end of the area.
    if (aTargetNode == areaCustomizationTarget) {
      CustomizableUI.addWidgetToArea(aDroppedItemId, aTargetArea.id);
      lazy.BrowserUsageTelemetry.recordWidgetChange(
        aDroppedItemId,
        aTargetArea.id,
        "drag"
      );
      this.#onDragEnd(aEvent);
      return;
    }

    // We need to determine the place that the widget is being dropped in
    // the target.
    let placement;
    let itemForPlacement = aTargetNode;
    // Skip the skipintoolbarset items when determining the place of the item:
    while (
      itemForPlacement &&
      itemForPlacement.getAttribute("skipintoolbarset") == "true" &&
      itemForPlacement.parentNode &&
      itemForPlacement.parentNode.nodeName == "toolbarpaletteitem"
    ) {
      itemForPlacement = itemForPlacement.parentNode.nextElementSibling;
      if (
        itemForPlacement &&
        itemForPlacement.nodeName == "toolbarpaletteitem"
      ) {
        itemForPlacement = itemForPlacement.firstElementChild;
      }
    }
    if (itemForPlacement) {
      let targetNodeId =
        itemForPlacement.nodeName == "toolbarpaletteitem"
          ? itemForPlacement.firstElementChild &&
            itemForPlacement.firstElementChild.id
          : itemForPlacement.id;
      placement = CustomizableUI.getPlacementOfWidget(targetNodeId);
    }
    if (!placement) {
      lazy.log.debug(
        "Could not get a position for " +
          aTargetNode.nodeName +
          "#" +
          aTargetNode.id +
          "." +
          aTargetNode.className
      );
    }
    let position = placement ? placement.position : null;

    // Is the target area the same as the origin? Since we've already handled
    // the possibility that the target is the customization palette, we know
    // that the widget is moving within a customizable area.
    if (aTargetArea == aOriginArea) {
      CustomizableUI.moveWidgetWithinArea(aDroppedItemId, position);
      lazy.BrowserUsageTelemetry.recordWidgetChange(
        aDroppedItemId,
        aTargetArea.id,
        "drag"
      );
    } else {
      CustomizableUI.addWidgetToArea(aDroppedItemId, aTargetArea.id, position);
      lazy.BrowserUsageTelemetry.recordWidgetChange(
        aDroppedItemId,
        aTargetArea.id,
        "drag"
      );
    }

    this.#onDragEnd(aEvent);

    // If we dropped onto a skipintoolbarset item, manually correct the drop location:
    if (aTargetNode != itemForPlacement) {
      let draggedWrapper = draggedItem.parentNode;
      let container = draggedWrapper.parentNode;
      container.insertBefore(draggedWrapper, aTargetNode.parentNode);
    }
  }

  /**
   * Handles the dragleave event on any customizable item or area.
   *
   * @param {DragEvent} aEvent
   *   The dragleave event being handled.
   */
  #onDragLeave(aEvent) {
    if (this.#isUnwantedDragDrop(aEvent)) {
      return;
    }

    __dumpDragData(aEvent);

    // When leaving customization areas, cancel the drag on the last dragover item
    // We've attached the listener to areas, so aEvent.currentTarget will be the area.
    // We don't care about dragleave events fired on descendants of the area,
    // so we check that the event's target is the same as the area to which the listener
    // was attached.
    if (this.#dragOverItem && aEvent.target == aEvent.currentTarget) {
      this.#cancelDragActive(this.#dragOverItem);
      this.#dragOverItem = null;
    }
  }

  /**
   * Handles the dragleave event on any customizable item being dragged.
   *
   * @param {DragEvent} aEvent
   *   The dragleave event being handled.
   */
  #onDragEnd(aEvent) {
    // To workaround bug 460801 we manually forward the drop event here when
    // dragend wouldn't be fired.
    //
    // Note that that means that this function may be called multiple times by a
    // single drag operation.
    if (this.#isUnwantedDragDrop(aEvent)) {
      return;
    }
    this._initializeDragAfterMove = null;
    this.#window.clearTimeout(this._dragInitializeTimeout);
    __dumpDragData(aEvent, "#onDragEnd");

    let document = aEvent.target.ownerDocument;
    let documentId = document.documentElement.id;
    if (!aEvent.dataTransfer.mozTypesAt(0)) {
      return;
    }

    let draggedItemId = aEvent.dataTransfer.mozGetDataAt(
      kDragDataTypePrefix + documentId,
      0
    );

    let draggedWrapper = document.getElementById("wrapper-" + draggedItemId);

    // DraggedWrapper might no longer available if a widget node is
    // destroyed after starting (but before stopping) a drag.
    if (draggedWrapper) {
      draggedWrapper.hidden = false;
      draggedWrapper.removeAttribute("mousedown");

      let toolbarParent = draggedWrapper.closest("toolbar");
      if (toolbarParent) {
        toolbarParent.style.removeProperty("min-height");
      }
    }

    if (this.#dragOverItem) {
      this.#cancelDragActive(this.#dragOverItem);
      this.#dragOverItem = null;
    }
    lazy.DragPositionManager.stop();
  }

  /**
   * True if the drag/drop event comes from a source other than one of our
   * browser windows. This check can be overridden for testing by setting
   * `browser.uiCustomization.skipSourceNodeCheck` to `true`.
   *
   * @param {DragEvent} aEvent
   *   A drag/drop event.
   * @returns {boolean}
   *   True if the event should be ignored.
   */
  #isUnwantedDragDrop(aEvent) {
    // The synthesized events for tests generated by synthesizePlainDragAndDrop
    // and synthesizeDrop in mochitests are used only for testing whether the
    // right data is being put into the dataTransfer. Neither cause a real drop
    // to occur, so they don't set the source node. There isn't a means of
    // testing real drag and drops, so this pref skips the check but it should
    // only be set by test code.
    if (this.#skipSourceNodeCheck) {
      return false;
    }

    /* Discard drag events that originated from a separate window to
       prevent content->chrome privilege escalations. */
    let mozSourceNode = aEvent.dataTransfer.mozSourceNode;
    // mozSourceNode is null in the dragStart event handler or if
    // the drag event originated in an external application.
    return !mozSourceNode || mozSourceNode.documentGlobal != this.#window;
  }

  /**
   * Handles applying drag preview effects to a customizable item or area while
   * a drag and drop operation is underway.
   *
   * @param {DOMNode} aDraggedOverItem
   *   A customizable item being dragged over within a customizable area, or
   *   a customizable area node.
   * @param {string} aValue
   *   A string to set as the "dragover" attribute on the dragged item to
   *   indicate which direction (before or after) the placeholder for the
   *   drag operation should go relative to aItem. This is either the
   *   string "before" or the string "after".
   * @param {string} aDraggedItemId
   *   The ID of the customizable item being dragged.
   * @param {string} aPlace
   *   The place string associated with the customizable area being dragged
   *   over. This is expected to be one of the strings returned by
   *   CustomizableUI.getPlaceForItem.
   */
  #setDragActive(aDraggedOverItem, aValue, aDraggedItemId, aPlace) {
    if (!aDraggedOverItem) {
      return;
    }

    if (aDraggedOverItem.getAttribute("dragover") != aValue) {
      aDraggedOverItem.setAttribute("dragover", aValue);

      let window = aDraggedOverItem.documentGlobal;
      let draggedItem = window.document.getElementById(aDraggedItemId);
      if (aPlace == "palette") {
        // We mostly delegate the complexity of grid placeholder effects to
        // DragPositionManager by way of #setGridDragActive.
        this.#setGridDragActive(aDraggedOverItem, draggedItem, aValue);
      } else {
        let targetArea = this.#getCustomizableParent(aDraggedOverItem);
        let makeSpaceImmediately = false;
        if (!gDraggingInToolbars.has(targetArea.id)) {
          gDraggingInToolbars.add(targetArea.id);
          let draggedWrapper = this.$("wrapper-" + aDraggedItemId);
          let originArea = this.#getCustomizableParent(draggedWrapper);
          makeSpaceImmediately = originArea == targetArea;
        }
        let propertyToMeasure = aPlace == "toolbar" ? "width" : "height";
        // Calculate width/height of the item when it'd be dropped in this position.
        let borderWidth = this.#getDragItemSize(aDraggedOverItem, draggedItem)[
          propertyToMeasure
        ];
        let layoutSide = aPlace == "toolbar" ? "Inline" : "Block";
        let prop, otherProp;
        if (aValue == "before") {
          prop = "border" + layoutSide + "StartWidth";
          otherProp = "border-" + layoutSide.toLowerCase() + "-end-width";
        } else {
          prop = "border" + layoutSide + "EndWidth";
          otherProp = "border-" + layoutSide.toLowerCase() + "-start-width";
        }
        if (makeSpaceImmediately) {
          aDraggedOverItem.setAttribute("notransition", "true");
        }
        aDraggedOverItem.style[prop] = borderWidth + "px";
        aDraggedOverItem.style.removeProperty(otherProp);
        if (makeSpaceImmediately) {
          // Force a layout flush:
          aDraggedOverItem.getBoundingClientRect();
          aDraggedOverItem.removeAttribute("notransition");
        }
      }
    }
  }

  /**
   * Reverts drag preview effects applied via #setDragActive from a customizable
   * item or area when a drag and drop operation ends.
   *
   * @param {DOMNode} aDraggedOverItem
   *   The customizable item or area that was being dragged over.
   * @param {DOMNode} aNextDraggedOverItem
   *   If non-null, this is the customizable item or area that is being
   *   dragged over now instead of aDraggedOverItem.
   * @param {boolean} aNoTransition
   *   True if the reversion of the drag preview effect should occur without
   *   a transition (for example, on a drop).
   */
  #cancelDragActive(aDraggedOverItem, aNextDraggedOverItem, aNoTransition) {
    let currentArea = this.#getCustomizableParent(aDraggedOverItem);
    if (!currentArea) {
      return;
    }
    let nextArea = aNextDraggedOverItem
      ? this.#getCustomizableParent(aNextDraggedOverItem)
      : null;
    if (currentArea != nextArea) {
      currentArea.removeAttribute("draggingover");
    }
    let areaType = CustomizableUI.getAreaType(currentArea.id);
    if (areaType) {
      if (aNoTransition) {
        aDraggedOverItem.setAttribute("notransition", "true");
      }
      aDraggedOverItem.removeAttribute("dragover");
      // Remove all property values in the case that the end padding
      // had been set.
      aDraggedOverItem.style.removeProperty("border-inline-start-width");
      aDraggedOverItem.style.removeProperty("border-inline-end-width");
      aDraggedOverItem.style.removeProperty("border-block-start-width");
      aDraggedOverItem.style.removeProperty("border-block-end-width");
      if (aNoTransition) {
        // Force a layout flush:
        aDraggedOverItem.getBoundingClientRect();
        aDraggedOverItem.removeAttribute("notransition");
      }
    } else {
      aDraggedOverItem.removeAttribute("dragover");
      if (aNextDraggedOverItem) {
        if (nextArea == currentArea) {
          // No need to do anything if we're still dragging in this area:
          return;
        }
      }
      // Otherwise, clear everything out:
      let positionManager =
        lazy.DragPositionManager.getManagerForArea(currentArea);
      positionManager.clearPlaceholders(currentArea, aNoTransition);
    }
  }

  /**
   * Handles applying drag preview effects to the customization palette grid.
   *
   * @param {DOMNode} aDragOverNode
   *   A customizable item being dragged over within the palette, or
   *   the palette node itself.
   * @param {DOMNode} aDraggedItem
   *   The customizable item being dragged.
   */
  #setGridDragActive(aDragOverNode, aDraggedItem) {
    let targetArea = this.#getCustomizableParent(aDragOverNode);
    let draggedWrapper = this.$("wrapper-" + aDraggedItem.id);
    let originArea = this.#getCustomizableParent(draggedWrapper);
    let positionManager =
      lazy.DragPositionManager.getManagerForArea(targetArea);
    let draggedSize = this.#getDragItemSize(aDragOverNode, aDraggedItem);
    positionManager.insertPlaceholder(
      targetArea,
      aDragOverNode,
      draggedSize,
      originArea == targetArea
    );
  }

  /**
   * Given a customizable item being dragged, and a DOMNode being dragged over,
   * returns the size of the dragged item were it to be placed within the area
   * associated with aDragOverNode.
   *
   * @param {DOMNode} aDragOverNode
   *   The node currently being dragged over.
   * @param {DOMNode} aDraggedItem
   *   The customizable item node currently being dragged.
   * @returns {ItemSizeForArea}
   */
  #getDragItemSize(aDragOverNode, aDraggedItem) {
    // Cache it good, cache it real good.
    if (!this.#dragSizeMap) {
      this.#dragSizeMap = new WeakMap();
    }
    if (!this.#dragSizeMap.has(aDraggedItem)) {
      this.#dragSizeMap.set(aDraggedItem, new WeakMap());
    }
    let itemMap = this.#dragSizeMap.get(aDraggedItem);
    let targetArea = this.#getCustomizableParent(aDragOverNode);
    let currentArea = this.#getCustomizableParent(aDraggedItem);
    // Return the size for this target from cache, if it exists.
    let size = itemMap.get(targetArea);
    if (size) {
      return size;
    }

    // Calculate size of the item when it'd be dropped in this position.
    let currentParent = aDraggedItem.parentNode;
    let currentSibling = aDraggedItem.nextElementSibling;
    const kAreaType = "cui-areatype";
    let areaType, currentType;

    if (targetArea != currentArea) {
      // Move the widget temporarily next to the placeholder.
      aDragOverNode.parentNode.insertBefore(aDraggedItem, aDragOverNode);
      // Update the node's areaType.
      areaType = CustomizableUI.getAreaType(targetArea.id);
      currentType =
        aDraggedItem.hasAttribute(kAreaType) &&
        aDraggedItem.getAttribute(kAreaType);
      if (areaType) {
        aDraggedItem.setAttribute(kAreaType, areaType);
      }
      this.wrapToolbarItem(aDraggedItem, areaType || "palette");
      CustomizableUI.onWidgetDrag(aDraggedItem.id, targetArea.id);
    } else {
      aDraggedItem.parentNode.hidden = false;
    }

    // Fetch the new size.
    let rect = aDraggedItem.parentNode.getBoundingClientRect();
    size = { width: rect.width, height: rect.height };
    // Cache the found value of size for this target.
    itemMap.set(targetArea, size);

    if (targetArea != currentArea) {
      this.unwrapToolbarItem(aDraggedItem.parentNode);
      // Put the item back into its previous position.
      currentParent.insertBefore(aDraggedItem, currentSibling);
      // restore the areaType
      if (areaType) {
        if (currentType === false) {
          aDraggedItem.removeAttribute(kAreaType);
        } else {
          aDraggedItem.setAttribute(kAreaType, currentType);
        }
      }
      this.createOrUpdateWrapper(aDraggedItem, null, true);
      CustomizableUI.onWidgetDrag(aDraggedItem.id);
    } else {
      aDraggedItem.parentNode.hidden = true;
    }
    return size;
  }

  /**
   * Walks the ancestry of a DOMNode element and finds the first customizable
   * area node in that ancestry, or null if no such customizable area node
   * can be found.
   *
   * @param {DOMNode} aElement
   *   The DOMNode for which to find the customizable area parent.
   * @returns {DOMNode}
   *   The customizable area parent of aElement.
   */
  #getCustomizableParent(aElement) {
    if (aElement) {
      // Deal with drag/drop on the padding of the panel.
      let containingPanelHolder = aElement.closest(
        "#customization-panelHolder"
      );
      if (containingPanelHolder) {
        return containingPanelHolder.querySelector(
          "#widget-overflow-fixed-list"
        );
      }
    }

    let areas = CustomizableUI.areas;
    areas.push(kPaletteId);
    return aElement.closest(areas.map(a => "#" + CSS.escape(a)).join(","));
  }

  /**
   * During a drag operation of a customizable item over a customizable area,
   * returns the node within that customizable area that the item is being
   * dragged over.
   *
   * @param {DragEvent} aEvent
   *   The dragover event being handled.
   * @param {DOMNode} aAreaElement
   *   The customizable area element that we should consider the dragover
   *   operation to be occurring on. This might actually be different from the
   *   target of the dragover event if we've retargeted the drag (see bug
   *   1396423 for an example of where we retarget a dragover area to the
   *   palette rather than the overflow panel).
   * @param {string} aPlace
   *   The place string associated with the customizable area being dragged
   *   over. This is expected to be one of the strings returned by
   *   CustomizableUI.getPlaceForItem.
   * @returns {DOMNode}
   *   The node within aAreaElement that we should assume the dragged item is
   *   being dragged over. If we cannot resolve this to a target, this falls
   *   back to just being the aEvent.target.
   */
  #getDragOverNode(aEvent, aAreaElement, aPlace) {
    let expectedParent =
      CustomizableUI.getCustomizationTarget(aAreaElement) || aAreaElement;
    if (!expectedParent.contains(aEvent.target)) {
      return expectedParent;
    }
    // Offset the drag event's position with the offset to the center of
    // the thing we're dragging
    let dragX = aEvent.clientX - this._dragOffset.x;
    let dragY = aEvent.clientY - this._dragOffset.y;

    // Ensure this is within the container
    let boundsContainer = expectedParent;
    let bounds = this.#getBoundsWithoutFlushing(boundsContainer);
    dragX = Math.min(bounds.right, Math.max(dragX, bounds.left));
    dragY = Math.min(bounds.bottom, Math.max(dragY, bounds.top));

    let targetNode;
    if (aPlace == "toolbar" || aPlace == "panel") {
      targetNode = aAreaElement.ownerDocument.elementFromPoint(dragX, dragY);
      while (targetNode && targetNode.parentNode != expectedParent) {
        targetNode = targetNode.parentNode;
      }
    } else {
      let positionManager =
        lazy.DragPositionManager.getManagerForArea(aAreaElement);
      // Make it relative to the container:
      dragX -= bounds.left;
      dragY -= bounds.top;
      // Find the closest node:
      targetNode = positionManager.find(aAreaElement, dragX, dragY);
    }
    return targetNode || aEvent.target;
  }

  /**
   * Handler for mousedown events in the customize mode UI for the primary
   * button. If the mousedown event is being fired on a customizable item, it
   * will have a "mousedown" attribute set to "true" on it.
   *
   * @param {MouseEvent} aEvent
   *   The mousedown event being handled.
   */
  #onMouseDown(aEvent) {
    lazy.log.debug("#onMouseDown");
    if (aEvent.button != 0) {
      return;
    }
    let item = this.#getWrapper(aEvent.target);
    if (item) {
      item.toggleAttribute("mousedown", true);
    }
  }

  /**
   * Handler for mouseup events in the customize mode UI for the primary
   * button. If the mouseup event is being fired on a customizable item, it
   * will have the "mousedown" attribute added in #onMouseDown removed.
   *
   * @param {MouseEvent} aEvent
   *   The mouseup event being handled.
   */
  #onMouseUp(aEvent) {
    lazy.log.debug("#onMouseUp");
    if (aEvent.button != 0) {
      return;
    }
    let item = this.#getWrapper(aEvent.target);
    if (item) {
      item.removeAttribute("mousedown");
    }
  }

  /**
   * Given a customizable item (or one of its descendants), returns the
   * toolbarpaletteitem wrapper node ancestor. If no such wrapper can be found,
   * this returns null.
   *
   * @param {DOMNode} aElement
   *   The customizable item node (or one of its descendants) to get the
   *   toolbarpaletteitem wrapper node ancestor for.
   * @returns {DOMNode|null}
   *   The toolbarpaletteitem wrapper node, or null if one cannot be found.
   */
  #getWrapper(aElement) {
    while (aElement && aElement.localName != "toolbarpaletteitem") {
      if (aElement.localName == "toolbar") {
        return null;
      }
      aElement = aElement.parentNode;
    }
    return aElement;
  }

  /**
   * Given some toolbarpaletteitem wrapper, walks the prior sibling elements
   * until it finds one that either isn't a toolbarpaletteitem, or doesn't have
   * it's first element hidden. Returns null if no such prior sibling element
   * can be found.
   *
   * @param {DOMNode} aReferenceNode
   *   The toolbarpaletteitem node to check the prior siblings for visibilty.
   *   If aReferenceNode is not a toolbarpaletteitem, this just returns the
   *   aReferenceNode immediately.
   * @returns {DOMNode|null}
   *   The first prior sibling with a visible first element child, or the
   *   first non-toolbarpaletteitem prior sibling, or null if no such item can
   *   be found.
   */
  #findVisiblePreviousSiblingNode(aReferenceNode) {
    while (
      aReferenceNode &&
      aReferenceNode.localName == "toolbarpaletteitem" &&
      aReferenceNode.firstElementChild.hidden
    ) {
      aReferenceNode = aReferenceNode.previousElementSibling;
    }
    return aReferenceNode;
  }

  /**
   * The popupshowing event handler for the context menu on the customization
   * palette.
   *
   * @param {WidgetMouseEvent} event
   *   The popupshowing event being fired for the context menu.
   */
  #onPaletteContextMenuShowing(event) {
    let isFlexibleSpace = event.target.triggerNode.id.includes(
      "wrapper-customizableui-special-spring"
    );
    event.target.querySelector(".customize-context-addToPanel").disabled =
      isFlexibleSpace;
  }

  /**
   * The popupshowing event handler for the context menu for items in the
   * overflow panel while in customize mode. This is currently public due to
   * bug 1378427 (also see bug 1747945).
   *
   * @param {WidgetMouseEvent} event
   *   The popupshowing event being fired for the context menu.
   */
  onPanelContextMenuShowing(event) {
    let inPermanentArea = !!event.target.triggerNode.closest(
      "#widget-overflow-fixed-list"
    );
    let doc = event.target.ownerDocument;
    doc.getElementById("customizationPanelItemContextMenuUnpin").hidden =
      !inPermanentArea;
    doc.getElementById("customizationPanelItemContextMenuPin").hidden =
      inPermanentArea;

    doc.documentGlobal.MozXULElement.insertFTLIfNeeded(
      "browser/toolbarContextMenu.ftl"
    );
    event.target.querySelectorAll("[data-lazy-l10n-id]").forEach(el => {
      el.setAttribute("data-l10n-id", el.getAttribute("data-lazy-l10n-id"));
      el.removeAttribute("data-lazy-l10n-id");
    });
  }

  /**
   * A window click event handler that checks to see if the item being clicked
   * in the window is the downloads button wrapper, with the primary button.
   * This is used to show the downloads button autohide panel.
   *
   * @param {MouseEvent} event
   *   The click event on the window.
   */
  #checkForDownloadsClick(event) {
    if (
      event.target.closest("#wrapper-downloads-button") &&
      event.button == 0
    ) {
      event.view.gCustomizeMode.#showDownloadsAutoHidePanel();
    }
  }

  /**
   * Adds a click event listener to the top-level window to check to see if the
   * downloads button is ever clicked while in customize mode. Callers should
   * ensure that #teardownDownloadAutoHideToggle is called when exiting
   * customize mode.
   */
  #setupDownloadAutoHideToggle() {
    this.#window.addEventListener("click", this.#checkForDownloadsClick, true);
  }

  /**
   * Removes the click event listener on the top-level window that was added by
   * #setupDownloadAutoHideToggle.
   */
  #teardownDownloadAutoHideToggle() {
    this.#window.removeEventListener(
      "click",
      this.#checkForDownloadsClick,
      true
    );
    this.$(kDownloadAutohidePanelId).hidePopup();
  }

  /**
   * Attempts to move the downloads button to the navigation toolbar in the
   * event that they've turned on the autohide feature for the button while
   * the button is in the palette.
   */
  #maybeMoveDownloadsButtonToNavBar() {
    // If the user toggled the autohide checkbox while the item was in the
    // palette, and hasn't moved it since, move the item to the default
    // location in the navbar for them.
    if (
      !CustomizableUI.getPlacementOfWidget("downloads-button") &&
      this.#moveDownloadsButtonToNavBar &&
      this.#window.DownloadsButton.autoHideDownloadsButton
    ) {
      let navbarPlacements = CustomizableUI.getWidgetIdsInArea("nav-bar");
      let insertionPoint = navbarPlacements.indexOf("urlbar-container");
      while (++insertionPoint < navbarPlacements.length) {
        let widget = navbarPlacements[insertionPoint];
        // If we find a non-searchbar, non-spacer node, break out of the loop:
        if (
          widget != "search-container" &&
          !(CustomizableUI.isSpecialWidget(widget) && widget.includes("spring"))
        ) {
          break;
        }
      }
      CustomizableUI.addWidgetToArea(
        "downloads-button",
        "nav-bar",
        insertionPoint
      );
      lazy.BrowserUsageTelemetry.recordWidgetChange(
        "downloads-button",
        "nav-bar",
        "move-downloads"
      );
    }
  }

  /**
   * Opens the panel that shows the toggle for auto-hiding the downloads button
   * when there are no downloads underway. If that panel is already open, it
   * is first closed. This panel is not shown if the downloads button is in
   * the overflow panel (since when the button is there, it does not autohide).
   *
   * @returns {Promise<undefined>}
   */
  async #showDownloadsAutoHidePanel() {
    let doc = this.#document;
    let panel = doc.getElementById(kDownloadAutohidePanelId);
    panel.hidePopup();
    let button = doc.getElementById("downloads-button");
    // We don't show the tooltip if the button is in the panel.
    if (button.closest("#widget-overflow-fixed-list")) {
      return;
    }

    let offsetX = 0,
      offsetY = 0;
    let panelOnTheLeft = false;
    let toolbarContainer = button.closest("toolbar");
    if (toolbarContainer && toolbarContainer.id == "nav-bar") {
      let navbarWidgets = CustomizableUI.getWidgetIdsInArea("nav-bar");
      if (
        navbarWidgets.indexOf("urlbar-container") <=
        navbarWidgets.indexOf("downloads-button")
      ) {
        panelOnTheLeft = true;
      }
    } else {
      await this.#window.promiseDocumentFlushed(() => {});

      if (!this.#customizing || !this._wantToBeInCustomizeMode) {
        return;
      }
      let buttonBounds = this.#getBoundsWithoutFlushing(button);
      let windowBounds = this.#getBoundsWithoutFlushing(doc.documentElement);
      panelOnTheLeft =
        buttonBounds.left + buttonBounds.width / 2 > windowBounds.width / 2;
    }
    let position;
    if (panelOnTheLeft) {
      // Tested in RTL, these get inverted automatically, so this does the
      // right thing without taking RTL into account explicitly.
      position = "topleft topright";
      if (toolbarContainer) {
        offsetX = 8;
      }
    } else {
      position = "topright topleft";
      if (toolbarContainer) {
        offsetX = -8;
      }
    }

    let checkbox = doc.getElementById(kDownloadAutohideCheckboxId);
    if (this.#window.DownloadsButton.autoHideDownloadsButton) {
      checkbox.setAttribute("checked", "true");
    } else {
      checkbox.removeAttribute("checked");
    }

    // We don't use the icon to anchor because it might be resizing because of
    // the animations for drag/drop. Hence the use of offsets.
    panel.openPopup(button, position, offsetX, offsetY);
  }

  /**
   * Called when the downloads button auto-hide toggle changes value.
   *
   * @param {CommandEvent} event
   *   The event that caused the toggle change.
   */
  #onDownloadsAutoHideChange(event) {
    let checkbox = event.target.ownerDocument.getElementById(
      kDownloadAutohideCheckboxId
    );
    Services.prefs.setBoolPref(kDownloadAutoHidePref, checkbox.checked);
    // Ensure we move the button (back) after the user leaves customize mode.
    event.view.gCustomizeMode.#moveDownloadsButtonToNavBar = checkbox.checked;
  }

  /**
   * Called when the button to customize the macOS touchbar is clicked.
   */
  #customizeTouchBar() {
    let updater = Cc["@mozilla.org/widget/touchbarupdater;1"].getService(
      Ci.nsITouchBarUpdater
    );
    updater.enterCustomizeMode();
  }

  /**
   * This is a method to toggle pong on or off in customize mode. You heard me.
   *
   * @param {boolean} enabled
   *   True if pong should be launched, or false if it should be torn down.
   */
  #togglePong(enabled) {
    // It's possible we're toggling for a reason other than hitting
    // the button (we might be exiting, for example), so make sure that
    // the state and checkbox are in sync.
    let whimsyButton = this.$("whimsy-button");
    whimsyButton.checked = enabled;

    if (enabled) {
      this.visiblePalette.setAttribute("whimsypong", "true");
      this.pongArena.hidden = false;
      if (!this.uninitWhimsy) {
        this.uninitWhimsy = this.#whimsypong();
      }
    } else {
      this.visiblePalette.removeAttribute("whimsypong");
      if (this.uninitWhimsy) {
        this.uninitWhimsy();
        this.uninitWhimsy = null;
      }
      this.pongArena.hidden = true;
    }
  }

  /**
   * This method contains a very simple implementation of a pong-like game.
   * Calling this method presumes that the pongArea element is visible.
   *
   * @returns {Function}
   *   Returns a clean-up function which tears down the launched pong game.
   */
  #whimsypong() {
    function update() {
      updateBall();
      updatePlayers();
    }

    function updateBall() {
      if (ball[1] <= 0 || ball[1] >= gameSide) {
        if (
          (ball[1] <= 0 && (ball[0] < p1 || ball[0] > p1 + paddleWidth)) ||
          (ball[1] >= gameSide && (ball[0] < p2 || ball[0] > p2 + paddleWidth))
        ) {
          updateScore(ball[1] <= 0 ? 0 : 1);
        } else {
          if (
            (ball[1] <= 0 &&
              (ball[0] - p1 < paddleEdge ||
                p1 + paddleWidth - ball[0] < paddleEdge)) ||
            (ball[1] >= gameSide &&
              (ball[0] - p2 < paddleEdge ||
                p2 + paddleWidth - ball[0] < paddleEdge))
          ) {
            ballDxDy[0] *= Math.random() + 1.3;
            ballDxDy[0] = Math.max(Math.min(ballDxDy[0], 6), -6);
            if (Math.abs(ballDxDy[0]) == 6) {
              ballDxDy[0] += Math.sign(ballDxDy[0]) * Math.random();
            }
          } else {
            ballDxDy[0] /= 1.1;
          }
          ballDxDy[1] *= -1;
          ball[1] = ball[1] <= 0 ? 0 : gameSide;
        }
      }
      ball = [
        Math.max(Math.min(ball[0] + ballDxDy[0], gameSide), 0),
        Math.max(Math.min(ball[1] + ballDxDy[1], gameSide), 0),
      ];
      if (ball[0] <= 0 || ball[0] >= gameSide) {
        ballDxDy[0] *= -1;
      }
    }

    function updatePlayers() {
      if (keydown) {
        let p1Adj = 1;
        if (
          (keydown == 37 && !window.RTL_UI) ||
          (keydown == 39 && window.RTL_UI)
        ) {
          p1Adj = -1;
        }
        p1 += p1Adj * 10 * keydownAdj;
      }

      let sign = Math.sign(ballDxDy[0]);
      if (
        (sign > 0 && ball[0] > p2 + paddleWidth / 2) ||
        (sign < 0 && ball[0] < p2 + paddleWidth / 2)
      ) {
        p2 += sign * 3;
      } else if (
        (sign > 0 && ball[0] > p2 + paddleWidth / 1.1) ||
        (sign < 0 && ball[0] < p2 + paddleWidth / 1.1)
      ) {
        p2 += sign * 9;
      }

      if (score >= winScore) {
        p1 = ball[0];
        p2 = ball[0];
      }
      p1 = Math.max(Math.min(p1, gameSide - paddleWidth), 0);
      p2 = Math.max(Math.min(p2, gameSide - paddleWidth), 0);
    }

    function updateScore(adj) {
      if (adj) {
        score += adj;
      } else if (--lives == 0) {
        quit = true;
      }
      ball = ballDef.slice();
      ballDxDy = ballDxDyDef.slice();
      ballDxDy[1] *= score / winScore + 1;
    }

    function draw() {
      let xAdj = window.RTL_UI ? -1 : 1;
      elements["wp-player1"].style.transform =
        "translate(" + xAdj * p1 + "px, -37px)";
      elements["wp-player2"].style.transform =
        "translate(" + xAdj * p2 + "px, " + gameSide + "px)";
      elements["wp-ball"].style.transform =
        "translate(" + xAdj * ball[0] + "px, " + ball[1] + "px)";
      elements["wp-score"].textContent = score;
      elements["wp-lives"].setAttribute("lives", lives);
      if (score >= winScore) {
        let arena = elements.arena;
        let image = "url(chrome://browser/skin/customizableui/whimsy.png)";
        let position = `${
          (window.RTL_UI ? gameSide : 0) + xAdj * ball[0] - 10
        }px ${ball[1] - 10}px`;
        let repeat = "no-repeat";
        let size = "20px";
        if (arena.style.backgroundImage) {
          if (arena.style.backgroundImage.split(",").length >= 160) {
            quit = true;
          }

          image += ", " + arena.style.backgroundImage;
          position += ", " + arena.style.backgroundPosition;
          repeat += ", " + arena.style.backgroundRepeat;
          size += ", " + arena.style.backgroundSize;
        }
        arena.style.backgroundImage = image;
        arena.style.backgroundPosition = position;
        arena.style.backgroundRepeat = repeat;
        arena.style.backgroundSize = size;
      }
    }

    function onkeydown(event) {
      keys.push(event.which);
      if (keys.length > 10) {
        keys.shift();
        let codeEntered = true;
        for (let i = 0; i < keys.length; i++) {
          if (keys[i] != keysCode[i]) {
            codeEntered = false;
            break;
          }
        }
        if (codeEntered) {
          elements.arena.setAttribute("kcode", "true");
          let spacer = document.querySelector(
            "#customization-palette > toolbarpaletteitem"
          );
          spacer.setAttribute("kcode", "true");
        }
      }
      if (event.which == 37 /* left */ || event.which == 39 /* right */) {
        keydown = event.which;
        keydownAdj *= 1.05;
      }
    }

    function onkeyup(event) {
      if (event.which == 37 || event.which == 39) {
        keydownAdj = 1;
        keydown = 0;
      }
    }

    function uninit() {
      document.removeEventListener("keydown", onkeydown);
      document.removeEventListener("keyup", onkeyup);
      if (rAFHandle) {
        window.cancelAnimationFrame(rAFHandle);
      }
      let arena = elements.arena;
      while (arena.firstChild) {
        arena.firstChild.remove();
      }
      arena.removeAttribute("score");
      arena.removeAttribute("lives");
      arena.removeAttribute("kcode");
      arena.style.removeProperty("background-image");
      arena.style.removeProperty("background-position");
      arena.style.removeProperty("background-repeat");
      arena.style.removeProperty("background-size");
      let spacer = document.querySelector(
        "#customization-palette > toolbarpaletteitem"
      );
      spacer.removeAttribute("kcode");
      elements = null;
      document = null;
      quit = true;
    }

    if (this.uninitWhimsy) {
      return this.uninitWhimsy;
    }

    let ballDef = [10, 10];
    let ball = [10, 10];
    let ballDxDyDef = [2, 2];
    let ballDxDy = [2, 2];
    let score = 0;
    let p1 = 0;
    let p2 = 10;
    let gameSide = 300;
    let paddleEdge = 30;
    let paddleWidth = 84;
    let keydownAdj = 1;
    let keydown = 0;
    let keys = [];
    let keysCode = [38, 38, 40, 40, 37, 39, 37, 39, 66, 65];
    let lives = 5;
    let winScore = 11;
    let quit = false;
    let document = this.#document;
    let rAFHandle = 0;
    let elements = {
      arena: document.getElementById("customization-pong-arena"),
    };

    document.addEventListener("keydown", onkeydown);
    document.addEventListener("keyup", onkeyup);

    for (let id of ["player1", "player2", "ball", "score", "lives"]) {
      let el = document.createXULElement("box");
      el.id = "wp-" + id;
      elements[el.id] = elements.arena.appendChild(el);
    }

    let spacer = this.visiblePalette.querySelector("toolbarpaletteitem");
    for (let player of ["#wp-player1", "#wp-player2"]) {
      let val = "-moz-element(#" + spacer.id + ") no-repeat";
      elements.arena.querySelector(player).style.background = val;
    }

    let window = this.#window;
    rAFHandle = window.requestAnimationFrame(function animate() {
      update();
      draw();
      if (quit) {
        elements["wp-score"].textContent = score;
        elements["wp-lives"] &&
          elements["wp-lives"].setAttribute("lives", lives);
        elements.arena.setAttribute("score", score);
        elements.arena.setAttribute("lives", lives);
      } else {
        rAFHandle = window.requestAnimationFrame(animate);
      }
    });

    return uninit;
  }
}

/**
 * A utility function that, when in debug mode, can emit drag data through the
 * debug logging mechanism for various drag and drop events.
 *
 * @param {DragEvent} aEvent
 *   The DragEvent to dump debug information to the log for.
 * @param {string|null} caller
 *   An optional string to indicate the caller of the log message.
 */
function __dumpDragData(aEvent, caller) {
  if (!gDebug) {
    return;
  }
  let str =
    "Dumping drag data (" +
    (caller ? caller + " in " : "") +
    "CustomizeMode.sys.mjs) {\n";
  str += "  type: " + aEvent.type + "\n";
  for (let el of ["target", "currentTarget", "relatedTarget"]) {
    if (aEvent[el]) {
      str +=
        "  " +
        el +
        ": " +
        aEvent[el] +
        "(localName=" +
        aEvent[el].localName +
        "; id=" +
        aEvent[el].id +
        ")\n";
    }
  }
  for (let prop in aEvent.dataTransfer) {
    if (typeof aEvent.dataTransfer[prop] != "function") {
      str +=
        "  dataTransfer[" + prop + "]: " + aEvent.dataTransfer[prop] + "\n";
    }
  }
  str += "}";
  lazy.log.debug(str);
}

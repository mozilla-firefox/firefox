/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Globals that browser.xhtml's scripts define, as members of BrowserWindow.
// Type a window value with it; a module that runs in the window includes
// browser-window.d.ts to see them as globals. System modules are left out:
// code holding a window value imports those itself. An object that a window
// script defines declares only the members that type-checked code reads.

type BrowserWindow = ChromeWindow & typeof BrowserWindow;

declare namespace BrowserWindow {
  type MozTabbrowserTab =
    import("moz-src:///browser/components/tabbrowser/content/tab.mjs").MozTabbrowserTab;

  type Tabbrowser =
    import("moz-src:///browser/components/tabbrowser/Tabbrowser.sys.mjs").Tabbrowser;

  var gBrowser: Tabbrowser;
  var gCustomizeMode: import("moz-src:///browser/components/customizableui/CustomizeMode.sys.mjs").CustomizeMode;
  var CustomizationHandler: import("moz-src:///browser/components/customizableui/BrowserCustomization.sys.mjs").CustomizationHandler;
  var gInitialPages: string[];
  var gMultiProcessBrowser: boolean;
  var gURLBar: import("chrome://browser/content/urlbar/UrlbarInput.mjs").UrlbarInput;
  var FullZoom: import("moz-src:///browser/components/tabbrowser/FullZoom.sys.mjs").FullZoom;
  var delayedStartupPromise: Promise<void>;
  var RTL_UI: boolean;

  function isBlankPageURL(aURL: string): boolean;
  function isElementVisible(aElement: Element | null): boolean;
  function isInitialPage(url: string | nsIURI): boolean;
  function CreateContainerTabMenu(event: Event): void;
  function createUserContextMenu(
    event: Event,
    options?: {
      target?: Element | null;
      isContextMenu?: boolean;
      isPanelList?: boolean;
      excludeUserContextId?: number | string | null;
      showDefaultTab?: boolean;
      showAddContainer?: boolean;
      showManageContainers?: boolean;
      containerSource?: string;
    }
  ): void;
  function showFullScreenViewContextMenuItems(popup: Element): void;
  function UpdatePopupNotificationsVisibility(): void;
  function openPreferences(
    paneID?: string,
    extraArgs?: { urlParams?: Record<string, string> }
  ): Promise<void>;
  function openTrustedLinkIn(
    url: string,
    where: string,
    params?: Parameters<
      typeof import("resource:///modules/URILoadingHelper.sys.mjs").URILoadingHelper.openLinkIn
    >[3]
  ): void;

  // browser-init.js
  var gBrowserInit: {
    readonly delayedStartupFinished: boolean;
    readonly firstContentWindowPaintPromise: Promise<void>;
  };

  // browser-commands.js
  var BrowserCommands: {
    home(aEvent?: Event): void;
    openTab(options?: { event?: Event; url?: string }): void;
  };

  // browser-customtitlebar.js
  var CustomTitlebar: {
    readonly enabled: boolean;
    allowedBy(condition: string, allow: boolean): void;
  };

  // browser-sidebar.js
  type SidebarStateProps =
    import("moz-src:///browser/components/sidebar/SidebarState.sys.mjs").SidebarStateProps;

  var SidebarController: {
    readonly _positionStart: boolean;
    getUIState(): SidebarStateProps;
    markSessionRestoreStateReceived(): void;
    updateUIState(state?: SidebarStateProps): Promise<void>;
  };

  // browser-sync.js
  var gSync: {
    updateTabContextMenu(
      aPopupMenu: XULPopupElement,
      aTargetTab: MozTabbrowserTab
    ): void;
  };

  // browser-webrtc.js
  var gSharedTabWarning: {
    willShowSharedTabWarning(tab: MozTabbrowserTab): boolean;
  };

  // browser.js
  var gNavToolbox: XULElement;

  const DynamicShortcutTooltip: {
    nodeToTooltipMap: Record<string, string>;
    cache: Map<string, string>;
    getText(nodeId: string): string;
  };

  var gKeywordURIFixup: {
    check(browser: MozBrowser, fixupInfo: nsIURIFixupInfo): void;
  };

  const gClickAndHoldListenersOnElement: {
    add(aElm: Element): void;
    remove(aButton: Element): void;
  };

  var FirefoxViewHandler: {
    tab: MozTabbrowserTab | null;
    openTab(section?: string): void;
  };

  // customizableui/content/panelUI.js
  var PanelUI: {
    showSubView(
      aViewId: string,
      aAnchor: Element,
      aEvent?: Event
    ): Promise<void>;
    _ensureShortcutsShown(view?: Element): void;
  };

  var XULBrowserWindow: {
    status: string;
    defaultStatus: string;
    overLink: string;
    busyUI: boolean;
  };

  type MousePosListener = {
    getMouseTargetRect(): {
      top: number;
      bottom: number;
      left: number;
      right: number;
    };
    onMouseEnter?(): void;
    onMouseLeave?(): void;
  };

  var MousePosTracker: {
    addListener(listener: MousePosListener): void;
    removeListener(listener: MousePosListener): void;
  };

  // tabbrowser/content/all-tabs-menu.mjs
  var gTabsPanel: typeof import("chrome://browser/content/tabbrowser/all-tabs-menu.mjs").gTabsPanel;

  // tabbrowser/content/browser-ctrlTab.mjs
  var ctrlTab: typeof import("chrome://browser/content/tabbrowser/browser-ctrlTab.mjs").ctrlTab;

  // tabbrowser/content/status-panel.mjs
  var StatusPanel: typeof import("chrome://browser/content/tabbrowser/status-panel.mjs").StatusPanel;

  // tabbrowser/content/tab-bar-visibility.mjs
  var TabBarVisibility: typeof import("chrome://browser/content/tabbrowser/tab-bar-visibility.mjs").TabBarVisibility;

  // tabbrowser/content/tab-context-menu.mjs
  var TabContextMenu: typeof import("chrome://browser/content/tabbrowser/tab-context-menu.mjs").TabContextMenu;

  // toolkit/content/viewZoomOverlay.js
  var ZoomManager: {
    readonly MIN: number;
    readonly MAX: number;
    useFullZoom: boolean;
    zoom: number;
    getZoomForBrowser(aBrowser: MozBrowser): number;
    setZoomForBrowser(aBrowser: MozBrowser, aVal: number): void;
    enlargeForBrowser(aBrowser: MozBrowser): void;
    reduceForBrowser(aBrowser: MozBrowser): void;
  };
}

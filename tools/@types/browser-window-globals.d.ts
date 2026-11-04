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
  var gInitialPages: string[];
  var gMultiProcessBrowser: boolean;
  var gURLBar: import("chrome://browser/content/urlbar/UrlbarInput.mjs").UrlbarInput;
  var delayedStartupPromise: Promise<void>;
  var RTL_UI: boolean;

  function isBlankPageURL(aURL: string): boolean;
  function isInitialPage(url: string | nsIURI): boolean;
  function CreateContainerTabMenu(event: Event): void;
  function UpdatePopupNotificationsVisibility(): void;
  function openPreferences(
    paneID?: string,
    extraArgs?: { urlParams?: Record<string, string> }
  ): Promise<void>;

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

  // browser-webrtc.js
  var gSharedTabWarning: {
    willShowSharedTabWarning(tab: MozTabbrowserTab): boolean;
  };

  // browser.js
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
  };
}

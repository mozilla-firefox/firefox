/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Globals that browser.xhtml's scripts define on the browser window. A project
// with modules that run in that window names this file in its tsconfig
// `include`. browser-window-globals.d.ts declares their types. The system
// modules the window also holds are left out: a module imports those itself.

/// <reference path="./browser-window-globals.d.ts" />

type Tabbrowser = BrowserWindow.Tabbrowser;

declare var gBrowser: BrowserWindow["gBrowser"];
declare var gCustomizeMode: BrowserWindow["gCustomizeMode"];
declare var gInitialPages: BrowserWindow["gInitialPages"];
declare var gMultiProcessBrowser: BrowserWindow["gMultiProcessBrowser"];
declare var gURLBar: BrowserWindow["gURLBar"];
declare var delayedStartupPromise: BrowserWindow["delayedStartupPromise"];
declare var RTL_UI: BrowserWindow["RTL_UI"];

declare var isBlankPageURL: BrowserWindow["isBlankPageURL"];
declare var isInitialPage: BrowserWindow["isInitialPage"];
declare var CreateContainerTabMenu: BrowserWindow["CreateContainerTabMenu"];
declare var UpdatePopupNotificationsVisibility: BrowserWindow["UpdatePopupNotificationsVisibility"];
declare var openPreferences: BrowserWindow["openPreferences"];

declare var gBrowserInit: BrowserWindow["gBrowserInit"];
declare var BrowserCommands: BrowserWindow["BrowserCommands"];
declare var CustomTitlebar: BrowserWindow["CustomTitlebar"];
declare var SidebarController: BrowserWindow["SidebarController"];
declare var gSharedTabWarning: BrowserWindow["gSharedTabWarning"];
declare var gKeywordURIFixup: BrowserWindow["gKeywordURIFixup"];
declare const DynamicShortcutTooltip: BrowserWindow["DynamicShortcutTooltip"];
declare const gClickAndHoldListenersOnElement: BrowserWindow["gClickAndHoldListenersOnElement"];
declare var FirefoxViewHandler: BrowserWindow["FirefoxViewHandler"];

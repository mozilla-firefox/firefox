/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ActionsProviderQuickActions:
    "moz-src:///browser/components/urlbar/ActionsProviderQuickActions.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  DevToolsShim: "chrome://devtools-startup/content/DevToolsShim.sys.mjs",
  ResetProfile: "resource://gre/modules/ResetProfile.sys.mjs",
  ScreenshotsUtils:
    "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
});

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

if (AppConstants.MOZ_UPDATER) {
  XPCOMUtils.defineLazyServiceGetter(
    lazy,
    "AUS",
    "@mozilla.org/updates/update-service;1",
    Ci.nsIApplicationUpdateService
  );
}

let openUrlFun = url => (_queryContext, controller) =>
  openUrl(url, controller.browserWindow);
let openUrl = (url, window) => {
  if (url.startsWith("about:")) {
    window.switchToTabHavingURI(Services.io.newURI(url), true, {
      ignoreFragment: "whenComparing",
    });
  } else {
    window.gBrowser.addTab(url, {
      inBackground: false,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  }
  return { focusContent: true };
};

let openAddonsUrl = url => {
  return (_queryContext, controller) => {
    controller.browserWindow.BrowserAddonUI.openAddonsMgr(url, {
      selectTabByViewId: true,
    });
  };
};

let currentWindow = () => lazy.BrowserWindowTracker.getTopWindow();
let currentBrowser = () => currentWindow().gBrowser.selectedBrowser;

let unmutedAudioTabs = () =>
  lazy.BrowserWindowTracker.orderedWindows.flatMap(win =>
    Array.from(win.gBrowser.tabs).filter(
      tab =>
        (tab.soundPlaying ||
          tab.hasAttribute("soundplaying-scheduledremoval")) &&
        !tab.muted
    )
  );

ChromeUtils.defineLazyGetter(lazy, "gFluentStrings", function () {
  return new Localization(
    [
      "branding/brand.ftl",
      "browser/browser.ftl",
      "toolkit/branding/brandings.ftl",
    ],
    true
  );
});

const DEFAULT_ACTIONS = {
  addons: {
    l10nCommands: ["quickactions-cmd-addons3"],
    icon: "chrome://mozapps/skin/extensions/category-extensions.svg",
    label: "quickactions-addons",
    onPick: openAddonsUrl("addons://discover/"),
  },
  bookmarks: {
    l10nCommands: ["quickactions-cmd-bookmarks", "quickactions-bookmarks2"],
    icon: "chrome://browser/skin/bookmark.svg",
    label: "quickactions-bookmarks2",
    onPick: (_queryContext, controller) => {
      controller.browserWindow.top.PlacesCommandHook.showPlacesOrganizer(
        "BookmarksToolbar"
      );
    },
  },
  clear: {
    l10nCommands: [
      "quickactions-cmd-clearrecenthistory2",
      "quickactions-clearrecenthistory",
    ],
    icon: "chrome://browser/skin/forget.svg",
    label: "quickactions-clearrecenthistory",
    onPick: (_queryContext, controller) => {
      controller.browserWindow.document
        .getElementById("Tools:Sanitize")
        .doCommand();
    },
  },
  manageai: {
    l10nCommands: ["quickactions-cmd-manageai"],
    icon: "chrome://global/skin/icons/highlights.svg",
    label: "quickactions-manageai",
    onPick: openUrlFun("about:preferences#ai"),
  },
  downloads: {
    l10nCommands: ["quickactions-cmd-downloads"],
    icon: "chrome://browser/skin/downloads/downloads.svg",
    label: "quickactions-downloads2",
    onPick: openUrlFun("about:downloads"),
  },
  editpdf: {
    l10nCommands: ["quickactions-cmd-editpdf"],
    icon: "chrome://global/skin/icons/pdf.svg",
    label: "quickactions-editpdf",
    onPick: openUrlFun("about:pdf"),
  },
  extensions: {
    l10nCommands: ["quickactions-cmd-extensions2"],
    icon: "chrome://mozapps/skin/extensions/category-extensions.svg",
    label: "quickactions-extensions",
    onPick: openAddonsUrl("addons://list/extension"),
  },
  help: {
    l10nCommands: ["quickactions-cmd-help"],
    icon: "chrome://global/skin/icons/help.svg",
    label: "quickactions-help",
    onPick: openUrlFun(
      "https://support.mozilla.org/products/firefox?as=u&utm_source=inproduct"
    ),
  },
  firefoxview: {
    l10nCommands: ["quickactions-cmd-firefoxview"],
    icon: "chrome://browser/skin/firefox-view.svg",
    label: "quickactions-firefoxview",
    onPick: (_queryContext, controller) => {
      controller.browserWindow.FirefoxViewHandler.openTab();
    },
  },
  inspect: {
    l10nCommands: ["quickactions-cmd-inspector2"],
    icon: "chrome://devtools/skin/images/open-inspector.svg",
    label: "quickactions-inspector2",
    // The inspect action is unsupported unless:
    // 1. DevTools is enabled.
    // 2. The user can be considered as a DevTools user.
    isUnsupported: () =>
      !lazy.DevToolsShim.isEnabled() || !lazy.DevToolsShim.isDevToolsUser(),
    // And, for supported users, it is inactive when:
    // 3. The url is about:devtools-toolbox.
    // 4. The inspector is already opened on the page.
    isInactive: () => {
      let win = currentWindow();
      return (
        win.gBrowser.currentURI.spec.startsWith("about:devtools-toolbox") ||
        lazy.DevToolsShim.hasToolboxForTab(win.gBrowser.selectedTab)
      );
    },
    onPick: (_queryContext, controller) => {
      openInspector(controller.browserWindow);
    },
  },
  colorpicker: {
    l10nCommands: ["quickactions-cmd-colorpicker"],
    icon: "chrome://devtools/skin/images/command-eyedropper.svg",
    label: "quickactions-colorpicker",
    isUnsupported: () => !lazy.DevToolsShim.isEnabled(),
    isInactive: () =>
      currentBrowser().currentURI.spec.startsWith("about:devtools-toolbox"),
    onPick: (_queryContext, controller) => {
      openColorPicker(controller.browserWindow);
    },
  },
  library: {
    l10nCommands: ["quickactions-cmd-library"],
    icon: "chrome://browser/skin/library.svg",
    label: "quickactions-library",
    onPick: (_queryContext, controller) => {
      controller.browserWindow.top.PlacesCommandHook.showPlacesOrganizer();
    },
  },
  logins: {
    l10nCommands: ["quickactions-cmd-logins"],
    icon: "chrome://browser/skin/login.svg",
    label: "quickactions-logins2",
    onPick: openUrlFun("about:logins"),
  },
  mute: {
    l10nCommands: ["quickactions-cmd-mute"],
    label: "quickactions-mute",
    icon: "chrome://global/skin/media/audio-muted.svg",
    isInactive: () => !unmutedAudioTabs().length,
    onPick: () => {
      for (let tab of unmutedAudioTabs()) {
        tab.toggleMuteAudio();
      }
    },
  },
  print: {
    l10nCommands: ["quickactions-cmd-print"],
    label: "quickactions-print2",
    icon: "chrome://global/skin/icons/print.svg",
    isUnsupported: () => {
      return !Services.prefs.getBoolPref("print.enabled");
    },
    onPick: (_queryContext, controller) => {
      controller.browserWindow.document.getElementById("cmd_print").doCommand();
    },
  },
  private: {
    l10nCommands: ["quickactions-cmd-private"],
    label: "quickactions-private2",
    icon: "chrome://global/skin/icons/indicator-private-browsing.svg",
    onPick: (_queryContext, controller) => {
      controller.browserWindow.OpenBrowserWindow({ private: true });
    },
  },
  refresh: {
    l10nCommands: ["quickactions-cmd-refresh"],
    icon: "chrome://branding/content/icon32.png",
    label: "quickactions-refresh",
    isUnsupported: () => !lazy.ResetProfile.resetSupported(),
    onPick: (_queryContext, controller) => {
      lazy.ResetProfile.openConfirmationDialog(controller.browserWindow);
    },
  },
  restart: {
    l10nCommands: ["quickactions-cmd-restart"],
    icon: "chrome://global/skin/icons/reload.svg",
    label: "quickactions-restart",
    onPick: restartBrowser,
  },
  savepdf: {
    l10nCommands: ["quickactions-cmd-savepdf2"],
    label: "quickactions-savepdf",
    icon: "chrome://global/skin/icons/pdf.svg",
    isUnsupported: () => {
      return !Services.prefs.getBoolPref("print.enabled");
    },
    onPick: (_queryContext, controller) => {
      // This writes over the users last used printer which we
      // should not do. Refactor to launch the print preview with
      // custom settings.
      Cc["@mozilla.org/gfx/printsettings-service;1"]
        .getService(Ci.nsIPrintSettingsService)
        .maybeSaveLastUsedPrinterNameToPrefs(
          controller.browserWindow.PrintUtils.SAVE_TO_PDF_PRINTER
        );
      controller.browserWindow.PrintUtils.startPrintWindow(
        controller.browserWindow.gBrowser.selectedBrowser.browsingContext,
        {}
      );
    },
  },
  screenshot: {
    l10nCommands: ["quickactions-cmd-screenshot2"],
    label: "quickactions-screenshot3",
    icon: "chrome://browser/skin/screenshot.svg",
    isUnsupported: () => {
      return !lazy.ScreenshotsUtils.screenshotsEnabled;
    },
    onPick: (_queryContext, controller) => {
      Services.obs.notifyObservers(
        controller.browserWindow,
        "menuitem-screenshot",
        "QuickActions"
      );
      return { focusContent: true };
    },
  },
  settings: {
    l10nCommands: ["quickactions-cmd-settings2"],
    icon: "chrome://global/skin/icons/settings.svg",
    label: "quickactions-settings2",
    onPick: openUrlFun("about:preferences"),
  },
  themes: {
    l10nCommands: ["quickactions-cmd-themes2"],
    icon: "chrome://mozapps/skin/extensions/category-themes.svg",
    label: "quickactions-themes",
    onPick: openAddonsUrl("addons://list/theme"),
  },
  translate: {
    l10nCommands: ["quickactions-cmd-translate"],
    icon: "chrome://browser/skin/translations-companion.svg",
    label: "quickactions-translate",
    isUnsupported: () => {
      return !(
        lazy.TranslationsParent.AIFeature.isEnabled &&
        Services.prefs.getBoolPref(
          "browser.translations.quickAction.enabled",
          false
        )
      );
    },
    onPick: async (_queryContext, controller) => {
      await lazy.TranslationsParent.openAboutTranslationsPage({
        browserWindow: controller.browserWindow,
        targetLanguage: "derive",
      });

      return { focusContent: true };
    },
  },
  update: {
    l10nCommands: ["quickactions-cmd-update"],
    icon: "chrome://global/skin/icons/update-icon.svg",
    label: "quickactions-update",
    isUnsupported: () =>
      !AppConstants.MOZ_UPDATER || !lazy.AUS.canUsuallyCheckForUpdates,
    isInactive: () =>
      lazy.AUS.currentState != Ci.nsIApplicationUpdateService.STATE_PENDING,
    onPick: restartBrowser,
  },
  viewsource: {
    l10nCommands: ["quickactions-cmd-viewsource2"],
    icon: "chrome://browser/skin/reader-mode.svg",
    label: "quickactions-viewsource2",
    isInactive: () => currentBrowser().currentURI.scheme == "view-source",
    onPick: (_queryContext, controller) =>
      openUrl(
        "view-source:" + controller.browserWindow.gBrowser.currentURI.spec,
        controller.browserWindow
      ),
  },
  labs: {
    l10nCommands: ["quickactions-cmd-labs"],
    icon: "chrome://global/skin/icons/experiments.svg",
    label: "quickactions-labs",
    onPick: openUrlFun("about:preferences#experimental"),
  },
};

function openInspector(window) {
  lazy.DevToolsShim.showToolboxForTab(window.gBrowser.selectedTab, {
    toolId: "inspector",
  });
}

function openColorPicker(window) {
  // The eyedropper menu item runs the standalone color picker without
  // opening the toolbox (see devtools/client/menus.js). Initializing
  // DevTools registers the menu item on every browser window.
  lazy.DevToolsShim.initDevTools();
  window.document.getElementById("menu_eyedropper")?.doCommand();
}

// TODO: We likely want a prompt to confirm with the user that they want to restart
// the browser.
function restartBrowser() {
  // Notify all windows that an application quit has been requested.
  let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
    Ci.nsISupportsPRBool
  );
  Services.obs.notifyObservers(
    cancelQuit,
    "quit-application-requested",
    "restart"
  );
  // Something aborted the quit process.
  if (cancelQuit.data) {
    return;
  }
  // If already in safe mode restart in safe mode.
  if (Services.appinfo.inSafeMode) {
    Services.startup.restartInSafeMode(Ci.nsIAppStartup.eAttemptQuit);
  } else {
    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
    );
  }
}

/**
 * Loads the default QuickActions.
 */
export class QuickActionsLoaderDefault {
  // Track the loading of the QuickActions to ensure they aren't loaded multiple times.
  static #loadedPromise = null;

  static async load() {
    let keys = Object.keys(DEFAULT_ACTIONS);
    for (const key of keys) {
      let actionData = DEFAULT_ACTIONS[key];
      let messages = await lazy.gFluentStrings.formatMessages(
        actionData.l10nCommands.map(id => ({ id }))
      );
      actionData.commands = messages
        .map(({ value }) => value.split(",").map(x => x.trim().toLowerCase()))
        .flat();
      lazy.ActionsProviderQuickActions.addAction(key, actionData);
    }
  }
  static async ensureLoaded() {
    if (!this.#loadedPromise) {
      this.#loadedPromise = this.load();
    }
    await this.#loadedPromise;
  }
}

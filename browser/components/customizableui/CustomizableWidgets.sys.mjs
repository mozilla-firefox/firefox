/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  RecentlyClosedTabsAndWindowsMenuUtils:
    "moz-src:///browser/components/sessionstore/RecentlyClosedTabsAndWindowsMenuUtils.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  ScreenshotsUtils:
    "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
  SELECTION_MODES:
    "moz-src:///browser/components/screenshots/ScreenshotsSelectionModes.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SharingUtils: "moz-src:///browser/components/sharing/SharingUtils.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
});

const kPrefCustomizationDebug = "browser.uiCustomization.debug";

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  let debug = Services.prefs.getBoolPref(kPrefCustomizationDebug, false);
  let consoleOptions = {
    maxLogLevel: debug ? "all" : "log",
    prefix: "CustomizableWidgets",
  };
  return new ConsoleAPI(consoleOptions);
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarRevampEnabled",
  "sidebar.revamp",
  false
);

/**
 * Synchronizes aNode's DOM attributes with aAttrs: each key is set as an
 * attribute with its value, or removed from aNode if the value is falsy.
 *
 * The following keys are not copied to aNode, but control how it is
 * localized:
 * - l10nId: Fluent identifier used to localize aNode.
 * - tooltipL10nId: Fluent identifier of a message whose value is set,
 *   asynchronously, as the "tooltiptext" attribute only. Use it instead of
 *   l10nId when other localizable attributes, such as "label", are set by
 *   other code, since Fluent would remove them.
 * - shortcutId: ID of the XUL <key> element for the shortcut, passed to the
 *   Fluent messages as the "$shortcut" variable.
 *
 * @param {Element} aNode
 *   The element to change the attributes of.
 * @param {object} aAttrs
 *   Attribute names mapped to their values, plus the special keys above.
 */
function setAttributes(aNode, aAttrs) {
  let doc = aNode.ownerDocument;
  for (let [name, value] of Object.entries(aAttrs)) {
    if (["l10nId", "tooltipL10nId", "shortcutId"].includes(name)) {
      continue;
    }
    if (!value) {
      if (aNode.hasAttribute(name)) {
        aNode.removeAttribute(name);
      }
    } else {
      aNode.setAttribute(name, value);
    }
  }

  let l10nArgs;
  if (aAttrs.shortcutId) {
    let shortcut = doc.getElementById(aAttrs.shortcutId);
    if (shortcut) {
      l10nArgs = { shortcut: lazy.ShortcutUtils.prettifyShortcut(shortcut) };
    }
  }
  if (aAttrs.l10nId) {
    doc.l10n.setAttributes(aNode, aAttrs.l10nId, l10nArgs);
  }
  if (aAttrs.tooltipL10nId) {
    doc.l10n
      .formatValue(aAttrs.tooltipL10nId, l10nArgs)
      .then(tooltip => aNode.setAttribute("tooltiptext", tooltip));
  }
}

/**
 * The array of built-in CustomizableUICreateWidgetProperties that are
 * registered as widgets upon browser start.
 *
 * @type {CustomizableUICreateWidgetProperties[]}
 */
export const CustomizableWidgets = [
  {
    id: "history-panelmenu",
    type: "view",
    viewId: "PanelUI-history",
    l10nId: "toolbar-button-history",
    shortcutId: "key_gotoHistory",
    recentlyClosedTabsPanel: "appMenu-library-recentlyClosedTabs",
    recentlyClosedWindowsPanel: "appMenu-library-recentlyClosedWindows",
    handleEvent(event) {
      switch (event.type) {
        case "PanelMultiViewHidden":
          this.onPanelMultiViewHidden(event);
          break;
        case "ViewShowing":
          this.onSubViewShowing(event);
          break;
        case "unload":
          this.onWindowUnload(event);
          break;
        case "command": {
          let { target } = event;
          let { PanelUI, PlacesCommandHook } = target.documentGlobal;
          if (target.id == "appMenuRecentlyClosedTabs") {
            PanelUI.showSubView(this.recentlyClosedTabsPanel, target);
          } else if (target.id == "appMenuRecentlyClosedWindows") {
            PanelUI.showSubView(this.recentlyClosedWindowsPanel, target);
          } else if (target.id == "appMenuSearchHistory") {
            PlacesCommandHook.searchHistory();
          }
          break;
        }
        default:
          throw new Error(`Unsupported event for '${this.id}'`);
      }
    },
    onViewShowing(event) {
      if (this._panelMenuView) {
        return;
      }

      let panelview = event.target;
      let document = panelview.ownerDocument;
      let window = document.defaultView;
      const closedTabCount = lazy.SessionStore.getClosedTabCount();

      lazy.PanelMultiView.getViewNode(
        document,
        "appMenuRecentlyClosedTabs"
      ).disabled = closedTabCount == 0;
      lazy.PanelMultiView.getViewNode(
        document,
        "appMenuRecentlyClosedWindows"
      ).disabled = lazy.SessionStore.getClosedWindowCount(window) == 0;

      lazy.PanelMultiView.getViewNode(
        document,
        "appMenu-restoreSession"
      ).hidden = !lazy.SessionStore.canRestoreLastSession;

      // We restrict the amount of results to 42. Not 50, but 42. Why? Because 42.
      let query =
        "place:queryType=" +
        Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY +
        "&sort=" +
        Ci.nsINavHistoryQueryOptions.SORT_BY_DATE_DESCENDING +
        "&maxResults=42&excludeQueries=1";

      this._panelMenuView = new window.PlacesPanelview(
        query,
        document.getElementById("appMenu_historyMenu"),
        panelview
      );
      // When either of these sub-subviews show, populate them with recently closed
      // objects data.
      lazy.PanelMultiView.getViewNode(
        document,
        this.recentlyClosedTabsPanel
      ).addEventListener("ViewShowing", this);
      lazy.PanelMultiView.getViewNode(
        document,
        this.recentlyClosedWindowsPanel
      ).addEventListener("ViewShowing", this);
      // When the popup is hidden (thus the panelmultiview node as well), make
      // sure to stop listening to PlacesDatabase updates.
      panelview.panelMultiView.addEventListener("PanelMultiViewHidden", this);
      panelview.addEventListener("command", this);
      window.addEventListener("unload", this);
    },
    onViewHiding() {
      lazy.log.debug("History view is being hidden!");
    },
    onPanelMultiViewHidden(event) {
      let panelMultiView = event.target;
      let document = panelMultiView.ownerDocument;
      if (this._panelMenuView) {
        this._panelMenuView.uninit();
        delete this._panelMenuView;
        lazy.PanelMultiView.getViewNode(
          document,
          this.recentlyClosedTabsPanel
        ).removeEventListener("ViewShowing", this);
        lazy.PanelMultiView.getViewNode(
          document,
          this.recentlyClosedWindowsPanel
        ).removeEventListener("ViewShowing", this);
        lazy.PanelMultiView.getViewNode(
          document,
          this.viewId
        ).removeEventListener("command", this);
      }
      panelMultiView.removeEventListener("PanelMultiViewHidden", this);
    },
    onWindowUnload() {
      if (this._panelMenuView) {
        delete this._panelMenuView;
      }
    },
    onSubViewShowing(event) {
      let panelview = event.target;
      let document = event.target.ownerDocument;
      let window = document.defaultView;

      this._panelMenuView.clearAllContents(panelview);

      const utils = lazy.RecentlyClosedTabsAndWindowsMenuUtils;
      const fragment =
        panelview.id == this.recentlyClosedTabsPanel
          ? utils.getTabsFragment(window, "toolbarbutton")
          : utils.getWindowsFragment(window, "toolbarbutton");
      let elementCount = fragment.childElementCount;
      this._panelMenuView._setEmptyPopupStatus(panelview, !elementCount);
      if (!elementCount) {
        return;
      }

      let body = document.createXULElement("vbox");
      body.className = "panel-subview-body";
      body.appendChild(fragment);
      let separator = document.createXULElement("toolbarseparator");
      let footer;
      while (--elementCount >= 0) {
        let element = body.children[elementCount];
        if (element.tagName != "toolbarbutton") {
          continue;
        }
        lazy.CustomizableUI.addShortcut(element);
        if (element.classList.contains("restoreallitem")) {
          footer = element;
        }
      }
      panelview.appendChild(body);
      panelview.appendChild(separator);
      panelview.appendChild(footer);
    },
  },
  {
    id: "save-page-button",
    l10nId: "toolbar-button-save-page",
    shortcutId: "key_savePage",
    onCreated(aNode) {
      aNode.setAttribute("command", "Browser:SavePage");
    },
  },
  {
    id: "print-button",
    l10nId: "navbar-print",
    shortcutId: "printKb",
    keepBroadcastAttributesWhenCustomizing: true,
    onCreated(aNode) {
      aNode.setAttribute("command", "cmd_printPreviewToggle");
    },
  },
  {
    id: "find-button",
    l10nId: "toolbar-button-find",
    shortcutId: "key_find",
    onCommand(aEvent) {
      let win = aEvent.target.documentGlobal;
      if (win.gLazyFindCommand) {
        win.gLazyFindCommand("onFindCommand");
      }
    },
  },
  {
    id: "open-file-button",
    l10nId: "toolbar-button-open-file",
    shortcutId: "openFileKb",
    onCreated(aNode) {
      aNode.setAttribute("command", "Browser:OpenFile");
    },
  },
  {
    id: "sidebar-button",
    l10nId: "show-sidebars",
    defaultArea: "nav-bar",
    _introducedByPref: "sidebar.revamp",
    onCommand(aEvent) {
      const { SidebarController } = aEvent.target.documentGlobal;
      if (lazy.sidebarRevampEnabled) {
        SidebarController.handleToolbarButtonClick();
      } else {
        SidebarController.toggle();
      }
    },
    onCreated(aNode) {
      if (lazy.sidebarRevampEnabled) {
        const { SidebarController } = aNode.documentGlobal;
        SidebarController.updateToolbarButton(aNode);
        aNode.setAttribute("overflows", "false");
        // Show the toolbar button badge by setting the badged attribute.
        // This activates badge styling by adding feature-callout class to the toolbarbutton-badge element.
        aNode.setAttribute("badged", true);
      } else {
        // Add an observer so the button is checked while the sidebar is open
        let doc = aNode.ownerDocument;
        let obChecked = doc.createXULElement("observes");
        obChecked.setAttribute("element", "sidebar-box");
        obChecked.setAttribute("attribute", "checked");
        let obPosition = doc.createXULElement("observes");
        obPosition.setAttribute("element", "sidebar-box");
        obPosition.setAttribute("attribute", "positionend");
        aNode.appendChild(obChecked);
        aNode.appendChild(obPosition);
      }
    },
  },
  {
    id: "zoom-controls",
    type: "custom",
    onBuild(aDocument) {
      let buttons = [
        {
          id: "zoom-out-button",
          command: "cmd_fullZoomReduce",
          closemenu: "none",
          l10nId: "toolbar-button-zoom-out",
          shortcutId: "key_fullZoomReduce",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "zoom-reset-button",
          command: "cmd_fullZoomReset",
          closemenu: "none",
          tooltipL10nId: "toolbar-button-zoom-reset-tooltip",
          shortcutId: "key_fullZoomReset",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "zoom-in-button",
          command: "cmd_fullZoomEnlarge",
          closemenu: "none",
          l10nId: "toolbar-button-zoom-in",
          shortcutId: "key_fullZoomEnlarge",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
      ];

      let node = aDocument.createXULElement("toolbaritem");
      node.setAttribute("id", "zoom-controls");
      aDocument.l10n.setAttributes(node, "toolbar-zoom-controls");
      // Set this as an attribute in addition to the property to make sure we can style correctly.
      node.setAttribute("removable", "true");
      node.classList.add("chromeclass-toolbar-additional");
      node.classList.add("toolbaritem-combined-buttons");

      buttons.forEach(function (aButton, aIndex) {
        if (aIndex != 0) {
          node.appendChild(aDocument.createXULElement("separator"));
        }
        let btnNode = aDocument.createXULElement("toolbarbutton");
        setAttributes(btnNode, aButton);
        node.appendChild(btnNode);
      });
      return node;
    },
  },
  {
    id: "edit-controls",
    type: "custom",
    onBuild(aDocument) {
      let buttons = [
        {
          id: "cut-button",
          command: "cmd_cut",
          l10nId: "toolbar-button-cut",
          shortcutId: "key_cut",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "copy-button",
          command: "cmd_copy",
          l10nId: "toolbar-button-copy",
          shortcutId: "key_copy",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "paste-button",
          command: "cmd_paste",
          l10nId: "toolbar-button-paste",
          shortcutId: "key_paste",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
      ];

      let node = aDocument.createXULElement("toolbaritem");
      node.setAttribute("id", "edit-controls");
      aDocument.l10n.setAttributes(node, "toolbar-edit-controls");
      // Set this as an attribute in addition to the property to make sure we can style correctly.
      node.setAttribute("removable", "true");
      node.classList.add("chromeclass-toolbar-additional");
      node.classList.add("toolbaritem-combined-buttons");

      buttons.forEach(function (aButton, aIndex) {
        if (aIndex != 0) {
          node.appendChild(aDocument.createXULElement("separator"));
        }
        let btnNode = aDocument.createXULElement("toolbarbutton");
        setAttributes(btnNode, aButton);
        node.appendChild(btnNode);
      });

      let listener = {
        onWidgetInstanceRemoved: (aWidgetId, aDoc) => {
          if (aWidgetId != this.id || aDoc != aDocument) {
            return;
          }
          lazy.CustomizableUI.removeListener(listener);
        },
        onWidgetOverflow(aWidgetNode) {
          if (aWidgetNode == node) {
            node.documentGlobal.updateEditUIVisibility();
          }
        },
        onWidgetUnderflow(aWidgetNode) {
          if (aWidgetNode == node) {
            node.documentGlobal.updateEditUIVisibility();
          }
        },
      };
      lazy.CustomizableUI.addListener(listener);

      return node;
    },
  },
  {
    id: "characterencoding-button",
    l10nId: "repair-text-encoding-button",
    onCommand(aEvent) {
      aEvent.view.BrowserCommands.forceEncodingDetection();
    },
  },
  {
    id: "email-link-button",
    l10nId: "toolbar-button-email-link",
    onCommand(aEvent) {
      let win = aEvent.view;
      win.MailIntegration.sendLinkForBrowser(win.gBrowser.selectedBrowser);
    },
  },
  {
    id: "logins-button",
    l10nId: "toolbar-button-logins",
    onCommand(aEvent) {
      let window = aEvent.view;
      lazy.LoginHelper.openPasswordManager(window, { entryPoint: "Toolbar" });
    },
  },
];

if (
  Services.prefs.getBoolPref("browser.toolbars.share-button.enabled", false)
) {
  CustomizableWidgets.push({
    id: "share-tab-button",
    type: "custom",
    onBuild(aDocument) {
      let node = aDocument.createXULElement("toolbarbutton");
      node.setAttribute("id", "share-tab-button");
      aDocument.l10n.setAttributes(node, "toolbar-button-share-tab");

      node.classList.add("toolbarbutton-1");

      if (AppConstants.platform == "macosx") {
        node.classList.add("share-toolbar-picker");
        node.addEventListener("command", () => {
          let browser = aDocument.defaultView.gBrowser.selectedBrowser;
          node.contextBrowserToShare = Cu.getWeakReference(browser);
          node.browsersToShare = null;
          lazy.SharingUtils.shareOnMacPicker(node, { injectQR: true });
        });
        return node;
      }

      // share-tab-url-item lets BrowserUsageTelemetry find the node carrying
      // browsersToShare via .closest(".share-tab-url-item") when a
      // .share-copy-link item inside the popup is clicked.
      node.classList.add("share-tab-url-item");
      node.setAttribute("type", "menu");

      let popup = aDocument.createXULElement("menupopup");
      popup.setAttribute("id", "share-tab-popup");
      popup.addEventListener("popupshowing", () => {
        let browser = aDocument.defaultView.gBrowser.selectedBrowser;
        node.contextBrowserToShare = Cu.getWeakReference(browser);
        node.browsersToShare = null;

        lazy.SharingUtils.populateSharePopup(popup);
      });

      node.appendChild(popup);

      return node;
    },
  });
}

if (Services.prefs.getBoolPref("identity.fxaccounts.enabled")) {
  CustomizableWidgets.push({
    id: "sync-button",
    l10nId: "toolbar-button-synced-tabs",
    type: "view",
    viewId: "PanelUI-remotetabs",
    onViewShowing(aEvent) {
      let panelview = aEvent.target;
      let doc = panelview.ownerDocument;

      let syncNowBtn = panelview.querySelector(".syncnow-label");
      let l10nId = syncNowBtn.getAttribute(
        panelview.documentGlobal.gSync._isCurrentlySyncing
          ? "syncing-data-l10n-id"
          : "sync-now-data-l10n-id"
      );
      doc.l10n.setAttributes(syncNowBtn, l10nId);

      let SyncedTabsPanelList = doc.defaultView.SyncedTabsPanelList;
      panelview.syncedTabsPanelList = new SyncedTabsPanelList(
        panelview,
        lazy.PanelMultiView.getViewNode(doc, "PanelUI-remotetabs-deck"),
        lazy.PanelMultiView.getViewNode(doc, "PanelUI-remotetabs-tabslist")
      );
      panelview.addEventListener("command", this);
      panelview.addEventListener("click", this);
      let syncNowButton = lazy.PanelMultiView.getViewNode(
        aEvent.target.ownerDocument,
        "PanelUI-remotetabs-syncnow"
      );
      syncNowButton.addEventListener("mouseover", this);
    },
    onViewHiding(aEvent) {
      let panelview = aEvent.target;
      panelview.syncedTabsPanelList.destroy();
      panelview.syncedTabsPanelList = null;
      panelview.removeEventListener("command", this);
      panelview.removeEventListener("click", this);
      let syncNowButton = lazy.PanelMultiView.getViewNode(
        aEvent.target.ownerDocument,
        "PanelUI-remotetabs-syncnow"
      );
      syncNowButton.removeEventListener("mouseover", this);
    },
    handleEvent(aEvent) {
      let button = aEvent.target;
      let { gSync } = button.documentGlobal;
      switch (aEvent.type) {
        case "mouseover":
          gSync.refreshSyncButtonsTooltip();
          break;
        case "command": {
          switch (button.id) {
            case "PanelUI-remotetabs-syncnow":
              gSync.doSync();
              break;
            case "PanelUI-remotetabs-view-managedevices":
              gSync.openDevicesManagementPage("syncedtabs-menupanel");
              break;
          }
          break;
        }
        case "click": {
          switch (button.id) {
            case "PanelUI-remotetabs-tabsdisabledpane-button":
            case "PanelUI-remotetabs-setupsync-button":
            case "PanelUI-remotetabs-syncdisabled-button":
            case "PanelUI-remotetabs-reauthsync-button":
            case "PanelUI-remotetabs-unverified-button":
              gSync.openPrefs("synced-tabs");
              break;
            case "PanelUI-remotetabs-connect-device-button":
              gSync.openConnectAnotherDevice("synced-tabs");
              break;
          }
          break;
        }
      }
    },
  });

  CustomizableWidgets.push({
    id: "send-tab-button",
    l10nId: "toolbar-button-send-tab",
    type: "custom",
    tabSpecific: true,
    locationSpecific: true,
    onBuild(aDocument) {
      const node = aDocument.createXULElement("toolbarbutton");
      node.setAttribute("id", this.id);
      aDocument.l10n.setAttributes(node, "toolbar-button-send-tab");

      node.classList.add("toolbarbutton-1");

      node.setAttribute("type", "menu");

      const enableDisableButton = () => {
        node.disabled =
          !aDocument.documentGlobal.gSync.sendTabToolbarButtonShouldBeEnabled(
            aDocument.documentGlobal.gBrowser.currentURI
          );
      };

      Services.obs.addObserver(
        enableDisableButton,
        "fxaccounts:devicelist_updated"
      );
      Services.obs.addObserver(enableDisableButton, "sync-ui-state:update");
      Services.prefs.addObserver(
        "identity.fxaccounts.enabled",
        enableDisableButton
      );
      aDocument.documentGlobal.addEventListener(
        "TabSelect",
        enableDisableButton
      );

      const locationListener = {
        onLocationChange(_browser, webProgress, _request, _uri) {
          if (webProgress.isTopLevel) {
            enableDisableButton();
          }
        },
      };
      aDocument.documentGlobal.gBrowser.addTabsProgressListener(
        locationListener
      );

      enableDisableButton();

      const widgetListener = {
        onWidgetInstanceRemoved: (aWidgetId, aDoc) => {
          if (aWidgetId != this.id || aDoc != aDocument) {
            return;
          }
          lazy.CustomizableUI.removeListener(widgetListener);
          Services.obs.removeObserver(
            enableDisableButton,
            "fxaccounts:devicelist_updated"
          );
          Services.obs.removeObserver(
            enableDisableButton,
            "sync-ui-state:update"
          );
          Services.prefs.removeObserver(
            "identity.fxaccounts.enabled",
            enableDisableButton
          );
          aDocument.documentGlobal.removeEventListener(
            "TabSelect",
            enableDisableButton
          );
          aDocument.documentGlobal.gBrowser.removeTabsProgressListener(
            locationListener
          );
        },
      };
      lazy.CustomizableUI.addListener(widgetListener);

      const popup = aDocument.createXULElement("menupopup");
      popup.setAttribute("id", "send-tab-popup");
      popup.addEventListener("popupshowing", () =>
        aDocument.documentGlobal.gSync.populateSendTabToolbarButton(popup)
      );

      node.appendChild(popup);

      return node;
    },
  });
}

let preferencesButton = {
  id: "preferences-button",
  l10nId: "toolbar-settings-button",
  onCommand(aEvent) {
    let win = aEvent.target.documentGlobal;
    win.openPreferences(undefined);
  },
};
if (AppConstants.platform == "macosx") {
  preferencesButton.shortcutId = "key_preferencesCmdMac";
}
CustomizableWidgets.push(preferencesButton);

if (Services.prefs.getBoolPref("privacy.panicButton.enabled")) {
  CustomizableWidgets.push({
    id: "panic-button",
    type: "view",
    viewId: "PanelUI-panicView",
    l10nId: "toolbar-button-forget",

    forgetButtonCalled(aEvent) {
      let doc = aEvent.target.ownerDocument;
      let group = doc.getElementById("PanelUI-panic-timeSpan");
      let itemsToClear = [
        "cookies",
        "history",
        "openWindows",
        "formdata",
        "sessions",
        "cache",
        "downloads",
        "offlineApps",
      ];
      let newWindowPrivateState = PrivateBrowsingUtils.isWindowPrivate(
        doc.defaultView
      )
        ? "private"
        : "non-private";
      let promise = lazy.Sanitizer.sanitize(itemsToClear, {
        ignoreTimespan: false,
        range: lazy.Sanitizer.getClearRange(+group.value),
        privateStateForNewWindow: newWindowPrivateState,
      });
      promise.then(function () {
        let otherWindow = Services.wm.getMostRecentWindow("navigator:browser");
        if (otherWindow.closed) {
          console.error("Got a closed window!");
        }
        if (otherWindow.PanicButtonNotifier) {
          otherWindow.PanicButtonNotifier.notify();
        } else {
          otherWindow.PanicButtonNotifierShouldNotify = true;
        }
      });
    },
    handleEvent(aEvent) {
      switch (aEvent.type) {
        case "command":
          this.forgetButtonCalled(aEvent);
          break;
      }
    },
    onViewShowing(aEvent) {
      let win = aEvent.target.documentGlobal;
      let doc = win.document;
      let eventBlocker = null;
      eventBlocker = doc.l10n.translateElements([aEvent.target]);

      let forgetButton = aEvent.target.querySelector(
        "#PanelUI-panic-view-button"
      );
      let group = doc.getElementById("PanelUI-panic-timeSpan");
      group.selectedItem = doc.getElementById("PanelUI-panic-5min");
      forgetButton.addEventListener("command", this);

      if (eventBlocker) {
        aEvent.detail.addBlocker(eventBlocker);
      }
    },
    onViewHiding(aEvent) {
      let forgetButton = aEvent.target.querySelector(
        "#PanelUI-panic-view-button"
      );
      forgetButton.removeEventListener("command", this);
    },
  });
}

const kFireButtonNimbusFeature = "fireButton";
const kFireButtonFlowVariable = "flow";
// Show the confirmation panel, with its own delete action and a way into the
// clear-data dialog.
const kFireButtonFlowPanel = "panel";
// Go straight to the clear-data dialog, which does its own confirming.
const kFireButtonFlowDirect = "direct";
// The event that last activated the fire button in each window, for the
// direct flow's dialog, since ViewShowing doesn't carry it.
const gFireButtonTriggeringEvents = new WeakMap();

if (Services.prefs.getBoolPref("browser.fireButton.enabled", false)) {
  CustomizableWidgets.push({
    id: "fire-button",
    l10nId: "fire-button-toolbar-button2",
    type: "view",
    viewId: "fire-button-panel",
    defaultArea: "nav-bar",
    // The button ships disabled, so it is introduced into existing profiles
    // when the pref is flipped rather than at a fixed kVersion.
    _introducedByPref: "browser.fireButton.enabled",
    // Private windows have reset-pbm-toolbar-button, which clears the private
    // session. Clearing the normal session from there would be surprising.
    showInPrivateBrowsing: false,

    /**
     * The same set the panic button clears, but over all of history rather
     * than a chosen time span. "openWindows" must stay in the list: it is what
     * closes every browser window and opens a fresh one.
     */
    itemsToClear: [
      "cookies",
      "history",
      "openWindows",
      "formdata",
      "sessions",
      "cache",
      "downloads",
      "offlineApps",
    ],

    /**
     * Open confirmation panels in which the user has not picked an action yet.
     * Whatever is still here when the panel hides was dismissed.
     */
    _panelsAwaitingChoice: new WeakSet(),

    // Called without this.
    onBeforeCommand(aEvent, aNode) {
      gFireButtonTriggeringEvents.set(aNode.documentGlobal, aEvent);
    },

    onCreated(aNode) {
      let doc = aNode.ownerDocument;
      let box = doc.createXULElement("box");
      box.className = "toolbarbutton-animatable-box";
      for (let layer of ["rest", "flare", "settled"]) {
        let flame = doc.createXULElement("box");
        flame.className = `fire-button-flame fire-button-flame-${layer}`;
        box.appendChild(flame);
      }
      aNode.appendChild(box);
    },

    handleEvent(aEvent) {
      let panelview = aEvent.target.closest("panelview");
      // The footer buttons are moz-buttons, which fire click. The "Choose…"
      // link is a XUL button, which fires command.
      if (aEvent.type == "command") {
        if (aEvent.target.id == "fire-button-panel-choose-link") {
          this._panelsAwaitingChoice.delete(panelview);
          Glean.fireButton.chooseClicked.record();
          this.onChoose(aEvent).catch(console.error);
        }
        return;
      }
      switch (aEvent.target.id) {
        case "fire-button-panel-cancel-button":
          this._panelsAwaitingChoice.delete(panelview);
          this.recordFlowCompleted(kFireButtonFlowPanel, "cancelled", "panel");
          lazy.CustomizableUI.hidePanelForNode(aEvent.target);
          break;
        case "fire-button-panel-confirm-button":
          this._panelsAwaitingChoice.delete(panelview);
          // Not awaited: sanitizing closes the window this handler is running
          // in, so nothing here can report the failure to the user.
          this.onConfirm(aEvent.target).catch(console.error);
          break;
      }
    },
    /**
     * Which flow activating the button starts. Read fresh each time rather
     * than cached, so an enrollment or an about:config change takes effect
     * without a restart.
     *
     * @returns {string} One of the kFireButtonFlow* values.
     */
    get flow() {
      return (
        lazy.NimbusFeatures[kFireButtonNimbusFeature].getVariable(
          kFireButtonFlowVariable
        ) ?? kFireButtonFlowPanel
      );
    },

    onViewShowing(aEvent) {
      let panelview = aEvent.target;
      let win = panelview.documentGlobal;
      let triggeringEvent = gFireButtonTriggeringEvents.get(win);
      gFireButtonTriggeringEvents.delete(win);

      let flow = this.flow;
      Glean.fireButton.activated.record({ flow });

      if (flow == kFireButtonFlowDirect) {
        // Stop the panel from opening; the dialog is the confirmation.
        aEvent.preventDefault();
        lazy.CustomizableUI.hidePanelForNode(panelview);
        this.openClearDataDialog(panelview, flow, triggeringEvent).catch(
          console.error
        );
        return;
      }

      this._panelsAwaitingChoice.add(panelview);
      panelview.addEventListener("click", this);
      panelview.addEventListener("command", this);
    },
    onViewHiding(aEvent) {
      let panelview = aEvent.target;
      panelview.removeEventListener("click", this);
      panelview.removeEventListener("command", this);
      if (this._panelsAwaitingChoice.delete(panelview)) {
        this.recordFlowCompleted(kFireButtonFlowPanel, "dismissed", "panel");
      }
    },

    /**
     * @param {string} aFlow - One of the kFireButtonFlow* values.
     * @param {string} aOutcome - "deleted", "cancelled", "dismissed" or "aborted".
     * @param {string} aSurface - "panel" or "dialog".
     */
    recordFlowCompleted(aFlow, aOutcome, aSurface) {
      Glean.fireButton.flowCompleted.record({
        flow: aFlow,
        outcome: aOutcome,
        surface: aSurface,
      });
    },

    async onConfirm(aButton) {
      lazy.CustomizableUI.hidePanelForNode(aButton);
      let progress = {};
      try {
        await this.clearSession(progress);
      } catch (ex) {
        if (progress.openWindows != "aborted") {
          throw ex;
        }
        this.recordFlowCompleted(kFireButtonFlowPanel, "aborted", "panel");
        return;
      }
      this.recordFlowCompleted(kFireButtonFlowPanel, "deleted", "panel");
      this.notifyComplete();
    },

    /**
     * Hand off to the "Delete browsing data" dialog so the user can
     * delete a subset instead of everything. The dialog does its own clearing,
     * so all this adds is the same completion toast the panel's own confirm
     * button shows.
     *
     * @param {Event} aEvent - The command event of the "Choose…" link.
     */
    onChoose(aEvent) {
      lazy.CustomizableUI.hidePanelForNode(aEvent.target);
      return this.openClearDataDialog(
        aEvent.target,
        kFireButtonFlowPanel,
        aEvent
      );
    },

    /**
     * Open the clear-data dialog and report completion. Shared by the panel's
     * "Choose…" link and by the direct flow, which has no panel.
     *
     * @param {Element} aNode - Node identifying the window to parent to.
     * @param {string} aFlow - The kFireButtonFlow* value of this activation.
     * @param {Event} [aTriggeringEvent] - See Sanitizer.showUI().
     */
    async openClearDataDialog(aNode, aFlow, aTriggeringEvent) {
      let win = aNode.documentGlobal;

      // Armed before the dialog opens because the dialog reports "accept" from
      // its dialogaccept listener, which runs while its clear is still in
      // flight. Waiting for the sanitizer's own notification keeps the toast
      // from claiming the data is gone before it is.
      let observer;
      let cleared = new Promise(resolve => {
        observer = resolve;
        Services.obs.addObserver(observer, "sanitizer-sanitization-complete");
      });

      try {
        let result = await lazy.Sanitizer.showUI(win, "clearSiteData", {
          entryPoint: lazy.Sanitizer.DIALOG_ENTRY_POINTS.FIRE_BUTTON,
          triggeringEvent: aTriggeringEvent,
        });
        if (result != "accept") {
          this.recordFlowCompleted(aFlow, "cancelled", "dialog");
          return;
        }
        await cleared;
        this.recordFlowCompleted(aFlow, "deleted", "dialog");
        this.notifyComplete();
      } finally {
        Services.obs.removeObserver(
          observer,
          "sanitizer-sanitization-complete"
        );
      }
    },

    /**
     * Clear the whole browsing session: history, cookies, cache, downloads and
     * other site data, then close every browser window and open a new one
     * that keeps the pinned tabs.
     *
     * @param {object} aProgress - Receives the Sanitizer's progress.
     * @returns {Promise<object>} The Sanitizer's progress object.
     */
    clearSession(aProgress) {
      return lazy.Sanitizer.sanitize(this.itemsToClear, {
        ignoreTimespan: true,
        privateStateForNewWindow: "non-private",
        keepPinnedTabs: true,
        progress: aProgress,
      });
    },

    /**
     * Show the completion toast. Clearing from the panel replaces every browser
     * window, so this anchors in whichever window is current now rather than
     * the one the action was triggered from. Sanitizer.sanitize only resolves once that
     * window has finished its delayed startup.
     */
    notifyComplete() {
      let win = Services.wm.getMostRecentWindow("navigator:browser");
      let node = win?.closed
        ? null
        : lazy.CustomizableUI.getWidget(this.id)?.forWindow(win)?.node;
      if (!node) {
        return;
      }
      // If the button overflowed we can't anchor off it, so fall back to the
      // overflow button that cui-anchorid points at.
      let anchorId = node.getAttribute("cui-anchorid");
      let anchor = anchorId ? win.document.getElementById(anchorId) : null;
      win.ConfirmationHint.show(anchor ?? node, "fire-button-panel-complete", {
        position: "bottomright topright",
      });
    },
  });
}

/**
 * Records fire button exposure at the first startup after enrollment, in every
 * branch. That is when the button is first created or left out: an enrollment
 * only changes the toolbar at the next startup, so exposure in the session
 * that enrolled would count treatment clients who never had the button.
 */
export const FireButtonExposure = {
  kEnrollmentPref: "browser.fireButton.enrollmentSlug",

  /**
   * @param {string} aSlugAtStartup - The kEnrollmentPref value read when the
   *   toolbar was built, before Nimbus could enroll anything this session.
   */
  async init(aSlugAtStartup) {
    let feature = lazy.NimbusFeatures[kFireButtonNimbusFeature];
    await feature.ready();
    this.recordExposure(aSlugAtStartup);
    feature.onUpdate(() => this.update());
  },

  /**
   * Records exposure if the current enrollment already existed at startup.
   *
   * @param {string} aSlugAtStartup - See init().
   */
  recordExposure(aSlugAtStartup) {
    let feature = lazy.NimbusFeatures[kFireButtonNimbusFeature];
    let slug = feature.getEnrollmentMetadata()?.slug;
    if (slug && slug == aSlugAtStartup) {
      feature.recordExposureEvent();
    }
    this.update();
  },

  /**
   * Remembers the current enrollment, so the next startup knows it predates
   * that session.
   */
  update() {
    let slug =
      lazy.NimbusFeatures[kFireButtonNimbusFeature].getEnrollmentMetadata()
        ?.slug;
    if (slug) {
      Services.prefs.setStringPref(this.kEnrollmentPref, slug);
    } else {
      Services.prefs.clearUserPref(this.kEnrollmentPref);
    }
  },
};

FireButtonExposure.init(
  Services.prefs.getStringPref(FireButtonExposure.kEnrollmentPref, "")
).catch(console.error);

if (PrivateBrowsingUtils.enabled) {
  CustomizableWidgets.push({
    id: "privatebrowsing-button",
    l10nId: "toolbar-button-new-private-window",
    shortcutId: "key_privatebrowsing",
    onCommand(e) {
      let win = e.target.documentGlobal;
      win.OpenBrowserWindow({ private: true });
    },
  });
}

if (Services.prefs.getBoolPref("browser.toolbars.tab-groups.enabled", false)) {
  CustomizableWidgets.push({
    id: "tab-groups-button",
    type: "view",
    viewId: "toolbar-tabGroupsListView",
    l10nId: "toolbar-button-tab-groups",
  });
}

CustomizableWidgets.push({
  id: "firefox-view-button",
  l10nId: "toolbar-button-firefox-view-3",
  onCreated(node) {
    node.setAttribute("role", "button");
    node.setAttribute("aria-pressed", "false");
  },
});

if (Services.prefs.getBoolPref("browser.mini-window.enabled", false)) {
  CustomizableWidgets.push({
    id: "mini-window-button",
    l10nId: "toolbar-button-mini-window",
    onCommand(aEvent) {
      let win = aEvent.currentTarget.documentGlobal;
      lazy.ScreenshotsUtils.toggle(
        win.gBrowser.selectedBrowser,
        "MiniWindowToolbarButton",
        { mode: lazy.SELECTION_MODES.MINI_WINDOW }
      );
    },
  });
}

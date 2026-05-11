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
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  RecentlyClosedTabsAndWindowsMenuUtils:
    "resource:///modules/sessionstore/RecentlyClosedTabsAndWindowsMenuUtils.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
  SharingUtils: "resource:///modules/SharingUtils.sys.mjs",
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
 * A helper method to synchronize aNode's DOM attributes with the properties and
 * values in aAttrs. If aNode has an attribute that is false-y in aAttrs,
 * then this attribute is removed.
 *
 * If aAttrs includes "shortcutId", the value is never set on aNode, but is
 * instead used when setting the "label" or "tooltiptext" attributes to include
 * the shortcut key combo. shortcutId should refer to the ID of the XUL <key>
 * element that acts as the shortcut.
 *
 * @param {Element} aNode
 *   The element to change the attributes of.
 * @param {object} aAttrs
 *   A set of key-value pairs where the key is set as the attribute name, and
 *   the value is set as the attribute value.
 */
function setAttributes(aNode, aAttrs) {
  let doc = aNode.ownerDocument;
  for (let [name, value] of Object.entries(aAttrs)) {
    if (!value) {
      if (aNode.hasAttribute(name)) {
        aNode.removeAttribute(name);
      }
    } else {
      if (name == "shortcutId") {
        continue;
      }
      if (name == "label" || name == "tooltiptext") {
        let stringId = typeof value == "string" ? value : name;
        let additionalArgs = [];
        if (aAttrs.shortcutId) {
          let shortcut = doc.getElementById(aAttrs.shortcutId);
          if (shortcut) {
            additionalArgs.push(lazy.ShortcutUtils.prettifyShortcut(shortcut));
          }
        }
        value = lazy.CustomizableUI.getLocalizedProperty(
          { id: aAttrs.id },
          stringId,
          additionalArgs
        );
      }
      aNode.setAttribute(name, value);
    }
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
    shortcutId: "key_gotoHistory",
    tooltiptext: "history-panelmenu.tooltiptext2",
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
    shortcutId: "key_find",
    tooltiptext: "find-button.tooltiptext3",
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
    tooltiptext: "zoom-controls.tooltiptext2",
    onBuild(aDocument) {
      let buttons = [
        {
          id: "zoom-out-button",
          command: "cmd_fullZoomReduce",
          label: true,
          closemenu: "none",
          tooltiptext: "tooltiptext2",
          shortcutId: "key_fullZoomReduce",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "zoom-reset-button",
          command: "cmd_fullZoomReset",
          closemenu: "none",
          tooltiptext: "tooltiptext2",
          shortcutId: "key_fullZoomReset",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "zoom-in-button",
          command: "cmd_fullZoomEnlarge",
          closemenu: "none",
          label: true,
          tooltiptext: "tooltiptext2",
          shortcutId: "key_fullZoomEnlarge",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
      ];

      let node = aDocument.createXULElement("toolbaritem");
      node.setAttribute("id", "zoom-controls");
      node.setAttribute(
        "label",
        lazy.CustomizableUI.getLocalizedProperty(this, "label")
      );
      node.setAttribute(
        "title",
        lazy.CustomizableUI.getLocalizedProperty(this, "tooltiptext")
      );
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
    tooltiptext: "edit-controls.tooltiptext2",
    onBuild(aDocument) {
      let buttons = [
        {
          id: "cut-button",
          command: "cmd_cut",
          label: true,
          tooltiptext: "tooltiptext2",
          shortcutId: "key_cut",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "copy-button",
          command: "cmd_copy",
          label: true,
          tooltiptext: "tooltiptext2",
          shortcutId: "key_copy",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
        {
          id: "paste-button",
          command: "cmd_paste",
          label: true,
          tooltiptext: "tooltiptext2",
          shortcutId: "key_paste",
          class: "toolbarbutton-1 toolbarbutton-combined",
        },
      ];

      let node = aDocument.createXULElement("toolbaritem");
      node.setAttribute("id", "edit-controls");
      node.setAttribute(
        "label",
        lazy.CustomizableUI.getLocalizedProperty(this, "label")
      );
      node.setAttribute(
        "title",
        lazy.CustomizableUI.getLocalizedProperty(this, "tooltiptext")
      );
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

      // share-tab-url-item is needed so BrowserUsageTelemetry can find the
      // node carrying browsersToShare via .closest(".share-tab-url-item").
      node.classList.add("toolbarbutton-1", "share-tab-url-item");

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

if (Services.prefs.getBoolPref("browser.tabs.groups.alternateMenu", false)) {
  CustomizableWidgets.push({
    id: "tab-groups-button",
    type: "view",
    viewId: "toolbar-tabGroupsListView",
    l10nId: "toolbar-button-tab-groups",
  });
}

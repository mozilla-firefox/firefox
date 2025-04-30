/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/browser-window */

document.addEventListener(
  "DOMContentLoaded",
  () => {
    const lazy = {};
    ChromeUtils.defineESModuleGetters(lazy, {
      TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
    });
    let mainPopupSet = document.getElementById("mainPopupSet");
    // eslint-disable-next-line complexity
    mainPopupSet.addEventListener("command", event => {
      switch (event.target.id) {
        // == tabContextMenu ==
        case "context_openANewTab":
          gBrowser.addAdjacentNewTab(TabContextMenu.contextTab);
          break;
        case "context_moveTabToNewGroup":
          TabContextMenu.moveTabsToNewGroup();
          break;
        case "context_ungroupTab":
          TabContextMenu.ungroupTabs();
          break;
        case "context_reloadTab":
          gBrowser.reloadTab(TabContextMenu.contextTab);
          break;
        case "context_reloadSelectedTabs":
          gBrowser.reloadMultiSelectedTabs();
          break;
        case "context_playTab":
          TabContextMenu.contextTab.resumeDelayedMedia();
          break;
        case "context_playSelectedTabs":
          gBrowser.resumeDelayedMediaOnMultiSelectedTabs(
            TabContextMenu.contextTab
          );
          break;
        case "context_toggleMuteTab":
          TabContextMenu.contextTab.toggleMuteAudio();
          break;
        case "context_toggleMuteSelectedTabs":
          gBrowser.toggleMuteAudioOnMultiSelectedTabs(
            TabContextMenu.contextTab
          );
          break;
        case "context_pinTab":
          gBrowser.pinTab(TabContextMenu.contextTab);
          break;
        case "context_unpinTab":
          gBrowser.unpinTab(TabContextMenu.contextTab);
          break;
        case "context_pinSelectedTabs":
          gBrowser.pinMultiSelectedTabs();
          break;
        case "context_unpinSelectedTabs":
          gBrowser.unpinMultiSelectedTabs();
          break;
        case "context_duplicateTab":
          duplicateTabIn(TabContextMenu.contextTab, "tab");
          break;
        case "context_duplicateTabs":
          TabContextMenu.duplicateSelectedTabs();
          break;
        case "context_bookmarkSelectedTabs":
          PlacesCommandHook.bookmarkTabs(gBrowser.selectedTabs);
          break;
        case "context_bookmarkTab":
          PlacesCommandHook.bookmarkTabs([TabContextMenu.contextTab]);
          break;
        case "context_moveToStart":
          gBrowser.moveTabsToStart(TabContextMenu.contextTab);
          break;
        case "context_moveToEnd":
          gBrowser.moveTabsToEnd(TabContextMenu.contextTab);
          break;
        case "context_openTabInWindow":
          gBrowser.replaceTabsWithWindow(TabContextMenu.contextTab);
          break;
        case "context_selectAllTabs":
          gBrowser.selectAllTabs();
          break;
        case "context_closeTab":
          TabContextMenu.closeContextTabs();
          break;
        case "context_closeDuplicateTabs":
          gBrowser.removeDuplicateTabs(TabContextMenu.contextTab);
          break;
        case "context_closeTabsToTheStart":
          gBrowser.removeTabsToTheStartFrom(TabContextMenu.contextTab);
          break;
        case "context_closeTabsToTheEnd":
          gBrowser.removeTabsToTheEndFrom(TabContextMenu.contextTab);
          break;
        case "context_closeOtherTabs":
          gBrowser.removeAllTabsBut(TabContextMenu.contextTab);
          break;
        case "context_unloadTab":
          TabContextMenu.explicitUnloadTabs();
          break;
        case "context_fullscreenAutohide":
          FullScreen.setAutohide();
          break;
        case "context_fullscreenExit":
          BrowserCommands.fullScreen();
          break;

        // == open-tab-group-context-menu ==
        case "open-tab-group-context-menu_moveToNewWindow":
          {
            let { tabGroupId } = event.target.parentElement.triggerNode.dataset;
            let tabGroup = gBrowser.getTabGroupById(tabGroupId);
            tabGroup.ownerGlobal.gBrowser.replaceGroupWithWindow(tabGroup);
          }
          break;
        case "open-tab-group-context-menu_moveToThisWindow":
          {
            let { tabGroupId } = event.target.parentElement.triggerNode.dataset;
            let otherTabGroup = gBrowser.getTabGroupById(tabGroupId);
            let adoptedTabGroup = gBrowser.adoptTabGroup(
              otherTabGroup,
              gBrowser.tabs.length
            );
            adoptedTabGroup.select();
          }
          break;
        case "open-tab-group-context-menu_delete":
          {
            let { tabGroupId } = event.target.parentElement.triggerNode.dataset;
            let tabGroup = gBrowser.getTabGroupById(tabGroupId);
            // Tabs need to be removed by their owning `Tabbrowser` or else
            // there are errors.
            tabGroup.ownerGlobal.gBrowser.removeTabGroup(
              tabGroup,
              lazy.TabMetrics.userTriggeredContext(
                lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU
              )
            );
          }
          break;

        // == saved-tab-group-context-menu ==
        case "saved-tab-group-context-menu_openInThisWindow":
          {
            let { tabGroupId } = event.target.parentElement.triggerNode.dataset;
            SessionStore.openSavedTabGroup(tabGroupId, window, {
              source: lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
            });
          }
          break;
        case "saved-tab-group-context-menu_openInNewWindow":
          {
            // TODO Bug 1940112: "Open Group in New Window" should directly restore saved tab groups into a new window
            let { tabGroupId } = event.target.parentElement.triggerNode.dataset;
            let tabGroup = SessionStore.openSavedTabGroup(tabGroupId, window, {
              source: lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU,
            });
            gBrowser.replaceGroupWithWindow(tabGroup);
          }
          break;
        case "saved-tab-group-context-menu_delete":
          {
            let { tabGroupId } = event.target.parentElement.triggerNode.dataset;
            SessionStore.forgetSavedTabGroup(tabGroupId);
          }
          break;
        // == editBookmarkPanel ==
        case "editBookmarkPanel_showForNewBookmarks":
          StarUI.onShowForNewBookmarksCheckboxCommand();
          break;
        case "editBookmarkPanelDoneButton":
          StarUI.panel.hidePopup();
          break;
        case "editBookmarkPanelRemoveButton":
          StarUI.removeBookmarkButtonCommand();
          break;

        // == sidebarMenu-popup ==
        case "sidebar-switcher-bookmarks":
          SidebarController.show("viewBookmarksSidebar");
          break;
        case "sidebar-switcher-history":
          SidebarController.show("viewHistorySidebar");
          break;
        case "sidebar-switcher-tabs":
          SidebarController.show("viewTabsSidebar");
          break;
        case "sidebar-reverse-position":
          SidebarController.reversePosition();
          break;
        case "sidebar-menu-close":
          // Close the sidebar UI, but leave it otherwise in its current state
          SidebarController.hide({ dismissPanel: false });
          break;

        // == toolbar-context-menu ==
        case "toolbar-context-manage-extension":
          ToolbarContextMenu.openAboutAddonsForContextAction(
            event.target.parentElement
          );
          break;
        case "toolbar-context-remove-extension":
          ToolbarContextMenu.removeExtensionForContextAction(
            event.target.parentElement
          );
          break;
        case "toolbar-context-report-extension":
          ToolbarContextMenu.reportExtensionForContextAction(
            event.target.parentElement,
            "toolbar_context_menu"
          );
          break;
        case "toolbar-context-move-to-panel":
          gCustomizeMode.addToPanel(
            event.target.parentNode.triggerNode,
            "toolbar-context-menu"
          );
          break;
        case "toolbar-context-autohide-downloads-button":
          ToolbarContextMenu.onDownloadsAutoHideChange(event);
          break;
        case "toolbar-context-remove-from-toolbar":
          gCustomizeMode.removeFromArea(
            event.target.parentNode.triggerNode,
            "toolbar-context-menu"
          );
          break;
        case "toolbar-context-pin-to-toolbar":
          gUnifiedExtensions.onPinToToolbarChange(
            event.target.parentElement,
            event
          );
          break;
        case "toolbar-context-always-open-downloads-panel":
          ToolbarContextMenu.onDownloadsAlwaysOpenPanelChange(event);
          break;
        case "toolbar-context-reloadSelectedTab":
        case "toolbar-context-reloadSelectedTabs":
          gBrowser.reloadMultiSelectedTabs();
          break;
        case "toolbar-context-bookmarkSelectedTab":
        case "toolbar-context-bookmarkSelectedTabs":
          PlacesCommandHook.bookmarkTabs(gBrowser.selectedTabs);
          break;
        case "toolbar-context-selectAllTabs":
          gBrowser.selectAllTabs();
          break;
        case "toolbar-context-customize":
          gCustomizeMode.enter();
          break;
        case "toolbar-context-toggle-vertical-tabs":
          SidebarController.toggleVerticalTabs();
          break;
        case "toolbar-context-customize-sidebar":
          SidebarController.show("viewCustomizeSidebar");
          break;
        case "toolbar-context-full-screen-autohide":
          FullScreen.setAutohide();
          break;
        case "toolbar-context-full-screen-exit":
          BrowserCommands.fullScreen();
          break;

        // == pictureInPictureToggleContextMenu ==
        case "context_HidePictureInPictureToggle":
          PictureInPicture.hideToggle();
          break;
        case "context_MovePictureInPictureToggle":
          PictureInPicture.moveToggle();
          break;

        // == pageActionContextMenu ==
        case "pageActionContextMenuManageExtension":
          BrowserPageActions.openAboutAddonsForContextAction();
          break;
        case "pageActionContextMenuRemoveExtension":
          BrowserPageActions.removeExtensionForContextAction();
          break;

        // == SyncedTabsSidebarContext ==
        case "syncedTabsManageDevices":
          gSync.openDevicesManagementPage("syncedtabs-sidebar");
          break;

        // == unified-extensions-context-menu ==
        case "unified-extensions-context-menu-pin-to-toolbar":
          gUnifiedExtensions.onPinToToolbarChange(
            event.target.parentElement,
            event
          );
          break;
        case "unified-extensions-context-menu-move-widget-up":
          gUnifiedExtensions.moveWidget(event.target.parentElement, "up");
          break;
        case "unified-extensions-context-menu-move-widget-down":
          gUnifiedExtensions.moveWidget(event.target.parentElement, "down");
          break;
        case "unified-extensions-context-menu-manage-extension":
          gUnifiedExtensions.manageExtension(event.target.parentElement);
          break;
        case "unified-extensions-context-menu-remove-extension":
          gUnifiedExtensions.removeExtension(event.target.parentElement);
          break;
        case "unified-extensions-context-menu-report-extension":
          gUnifiedExtensions.reportExtension(event.target.parentElement);
          break;

        // == full-page-translations-panel-settings-menupopup ==
        case "translations-panel-settings-always-offer-translation":
          FullPageTranslationsPanel.onAlwaysOfferTranslations();
          break;
        case "translations-panel-settings-always-translate":
          FullPageTranslationsPanel.onAlwaysTranslateLanguage();
          break;
        case "translations-panel-settings-never-translate":
          FullPageTranslationsPanel.onNeverTranslateLanguage();
          break;
        case "translations-panel-settings-never-translate-site":
          FullPageTranslationsPanel.onNeverTranslateSite();
          break;
        case "translations-panel-manage-languages":
          FullPageTranslationsPanel.openManageLanguages();
          break;
        case "translations-panel-about":
          FullPageTranslationsPanel.onAboutTranslations();
          break;

        // == select-translations-panel-settings-menupopup ==
        case "select-translations-panel-open-settings-page-menuitem":
          SelectTranslationsPanel.openTranslationsSettingsPage();
          break;
        case "select-translations-panel-about-translations-menuitem":
          SelectTranslationsPanel.onAboutTranslations();
          break;

        // == customizationPanelItemContextMenu ==
        case "customizationPanelItemContextMenuManageExtension":
          ToolbarContextMenu.openAboutAddonsForContextAction(
            event.target.parentElement
          );
          break;

        case "customizationPanelItemContextMenuRemoveExtension":
          ToolbarContextMenu.removeExtensionForContextAction(
            event.target.parentElement
          );
          break;

        case "customizationPanelItemContextMenuReportExtension":
          ToolbarContextMenu.reportExtensionForContextAction(
            event.target.parentElement,
            "toolbar_context_menu"
          );
          break;

        case "customizationPanelItemContextMenuPin":
          gCustomizeMode.addToPanel(
            event.target.parentNode.triggerNode,
            "panelitem-context"
          );
          break;

        case "customizationPanelItemContextMenuUnpin":
          gCustomizeMode.addToToolbar(
            event.target.parentNode.triggerNode,
            "panelitem-context"
          );
          break;

        case "customizationPanelItemContextMenuRemove":
          gCustomizeMode.removeFromArea(
            event.target.parentNode.triggerNode,
            "panelitem-context"
          );
          break;

        // == sharing-tabs-warning-panel ==
        case "sharing-warning-proceed-to-tab":
          gSharedTabWarning.allowSharedTabSwitch();
          break;
      }
    });

    const containerHistoryPopup = document.getElementById(
      "sidebar-history-context-menu-container-popup"
    );
    containerHistoryPopup.addEventListener("command", event =>
      PlacesUIUtils.openInContainerTab(event)
    );
    containerHistoryPopup.addEventListener("popupshowing", event =>
      PlacesUIUtils.createContainerTabMenu(event)
    );

    document
      .getElementById("context_reopenInContainerPopupMenu")
      .addEventListener("command", event => {
        // Handle commands on the descendant <menuitem>s with different containers.
        TabContextMenu.reopenInContainer(event);
      });

    document
      .getElementById("context_moveTabToGroupPopupMenu")
      .addEventListener("command", event => {
        if (event.target.id == "context_moveTabToGroupNewGroup") {
          TabContextMenu.moveTabsToNewGroup();
          return;
        }

        const tabGroupId = event.target.getAttribute("tab-group-id");
        const group = gBrowser.getTabGroupById(tabGroupId);
        if (!group) {
          return;
        }

        TabContextMenu.moveTabsToGroup(group);
      });

    document
      .getElementById("backForwardMenu")
      .addEventListener("command", event => {
        // Handle commands on the descendant <menuitem>s with history entries.
        // Note: See duplicate code in SetClickAndHoldHandlers.
        BrowserCommands.gotoHistoryIndex(event);
        // Prevent propagation to the back/forward button.
        event.stopPropagation();
      });

    document
      .getElementById("unified-extensions-context-menu")
      .addEventListener("command", event => {
        gUnifiedExtensions.onContextMenuCommand(event.currentTarget, event);
      });

    document
      .getElementById("webRTC-selectWindow-menulist")
      .addEventListener("command", event => {
        webrtcUI.updateWarningLabel(event.currentTarget);
      });

    mainPopupSet.addEventListener("popupshowing", event => {
      switch (event.target.id) {
        case "context_sendTabToDevicePopupMenu":
          gSync.populateSendTabToDevicesMenu(
            event.target,
            TabContextMenu.contextTab.linkedBrowser.currentURI,
            TabContextMenu.contextTab.linkedBrowser.contentTitle,
            TabContextMenu.contextTab.multiselected
          );
          break;
        case "context_reopenInContainerPopupMenu":
          TabContextMenu.createReopenInContainerMenu(event);
          break;
        case "backForwardMenu":
          FillHistoryMenu(event);
          break;
        case "new-tab-button-popup":
          CreateContainerTabMenu(event);
          break;
        case "toolbar-context-menu":
          ToolbarContextMenu.onViewToolbarsPopupShowing(
            event,
            document.getElementById("viewToolbarsMenuSeparator")
          );
          ToolbarContextMenu.updateDownloadsAutoHide(event.target);
          ToolbarContextMenu.updateDownloadsAlwaysOpenPanel(event.target);
          ToolbarContextMenu.updateExtension(event.target, event);
          break;
        case "pageActionContextMenu":
          BrowserPageActions.onContextMenuShowing(event, event.target);
          break;
        case "tabbrowser-tab-tooltip":
          gBrowser.createTooltip(event);
          break;
        case "dynamic-shortcut-tooltip":
          UpdateDynamicShortcutTooltipText(event.target);
          break;
        case "SyncedTabsOpenSelectedInContainerTabMenu":
          createUserContextMenu(event, { isContextMenu: true });
          break;
        case "unified-extensions-context-menu":
          gUnifiedExtensions.updateContextMenu(event.target, event);
          break;
        case "customizationPanelItemContextMenu":
          gCustomizeMode.onPanelContextMenuShowing(event);
          ToolbarContextMenu.updateExtension(event.target);
          break;
        case "bhTooltip":
          BookmarksEventHandler.fillInBHTooltip(event.target, event);
          break;
      }
    });

    document
      .getElementById("tabContextMenu")
      .addEventListener("popupshowing", event => {
        if (event.target.id == "tabContextMenu") {
          TabContextMenu.updateContextMenu(event.target);
        }
      });

    // Enable/disable some `open-tab-group-context-menu` options based on the
    // specific tab group context.
    document
      .getElementById("open-tab-group-context-menu")
      .addEventListener("popupshowing", event => {
        if (event.target.id == "open-tab-group-context-menu") {
          // Disable "Move Group to This Window" menu option for tab groups
          // that are open in the current window.
          let { tabGroupId } = event.target.triggerNode.dataset;
          let tabGroup = gBrowser.getTabGroupById(tabGroupId);
          let tabGroupIsInThisWindow = tabGroup.ownerDocument == document;
          event.target.querySelector(
            "#open-tab-group-context-menu_moveToThisWindow"
          ).disabled = tabGroupIsInThisWindow;

          // Disable "Move Group to New Window" menu option for tab groups
          // that are the only things in their respective window.
          let groupAloneInWindow =
            tabGroup.tabs.length ==
            tabGroup.ownerGlobal.gBrowser.openTabs.length;
          event.target.querySelector(
            "#open-tab-group-context-menu_moveToNewWindow"
          ).disabled = groupAloneInWindow;
        }
      });

    mainPopupSet.addEventListener("popupshown", event => {
      switch (event.target.id) {
        case "sharing-tabs-warning-panel":
          gSharedTabWarning.sharedTabWarningShown();
          break;
        case "full-page-translations-panel-settings-menupopup":
          FullPageTranslationsPanel.handleSettingsPopupShownEvent();
          break;
      }
    });

    mainPopupSet.addEventListener("popuphiding", event => {
      switch (event.target.id) {
        case "tabbrowser-tab-tooltip":
        case "bhTooltip":
          event.target.removeAttribute("position");
          break;
      }
    });

    mainPopupSet.addEventListener("popuphidden", event => {
      switch (event.target.id) {
        case "full-page-translations-panel-settings-menupopup":
          FullPageTranslationsPanel.handleSettingsPopupHiddenEvent();
          break;
      }
    });
  },
  { once: true }
);

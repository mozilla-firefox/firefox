/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

document.addEventListener(
  "DOMContentLoaded",
  () => {
    const lazy = {};
    ChromeUtils.defineESModuleGetters(lazy, {
      AIWindow:
        "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
      ContentSharingUtils:
        "moz-src:///browser/components/sharing/ContentSharingUtils.sys.mjs",
      MiniWindowManager:
        "moz-src:///browser/components/miniwindow/MiniWindowManager.sys.mjs",
      TabMetrics: "moz-src:///browser/components/tabbrowser/TabMetrics.sys.mjs",
      TabNotes: "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs",
    });
    let mainPopupSet = document.getElementById("mainPopupSet");
    // The trigger node may be a descendant of the tab group row carrying
    // `data-tab-group-id`, e.g. its label or icon.
    let getContextTabGroupId = popup =>
      popup.triggerNode?.closest("[data-tab-group-id]")?.dataset.tabGroupId;
    // A panel that keeps its own list of groups up to date opts out.
    let dismissTabGroupPanel = popup =>
      popup.triggerNode
        ?.closest("panel:not([keepopenongroupdelete])")
        ?.hidePopup();
    // Determines which panel a groups-list context menu was opened from, for
    // metrics purposes.
    let getGroupsListSource = popup => {
      switch (popup.triggerNode?.closest("panelview")?.id) {
        case "appMenu-tabGroupsListView":
          return lazy.TabMetrics.METRIC_SOURCE.APP_MENU_GROUP_LIST;
        case "allTabsMenu-allTabsView":
          return lazy.TabMetrics.METRIC_SOURCE.TAB_OVERFLOW_MENU;
        default:
          return lazy.TabMetrics.METRIC_SOURCE.UNKNOWN;
      }
    };
    // eslint-disable-next-line complexity
    mainPopupSet.addEventListener("command", event => {
      switch (event.target.id) {
        // == tabContextMenu ==
        case "context_openANewTab":
          // The tab context menu can be invoked on a window that isn't the
          // OS-level frontmost window (most reproducibly on macOS in a
          // multi-monitor setup). Raise the window so the new tab's
          // focusUrlBar request can actually land OS keyboard focus on the
          // address bar. Bug 2039674 tracks routing this through
          // URILoadingHelper instead.
          window.focus();
          gBrowser.addAdjacentNewTab(TabContextMenu.contextTab);
          break;
        case "context_moveTabToNewGroup":
          TabContextMenu.moveTabsToNewGroup();
          break;
        case "context_ungroupTab":
        case "context_ungroupSplitView":
          TabContextMenu.ungroupTabsAndSplitViews();
          break;
        case "context_moveSplitViewToNewGroup":
          TabContextMenu.moveSplitViewToNewGroup();
          break;
        case "context_moveTabToSplitView":
          TabContextMenu.moveTabsToSplitView();
          break;
        case "context_separateSplitView":
          TabContextMenu.unsplitTabs();
          break;
        case "context_reverseSplitView":
          TabContextMenu.reverseSplitView();
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
          gBrowser.resumeDelayedMediaOnMultiSelectedTabs();
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
          gBrowser.pinTab(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_unpinTab":
          gBrowser.unpinTab(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_pinSelectedTabs":
          gBrowser.pinMultiSelectedTabs({
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_unpinSelectedTabs":
          gBrowser.unpinMultiSelectedTabs({
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
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
        case "context_shareSelectedTabs":
          lazy.ContentSharingUtils.handleShareTabs(TabContextMenu.contextTabs);
          break;
        case "context_createAITab":
          lazy.AIWindow.createAITab(
            window,
            TabContextMenu.contextTabs.map(
              tab => tab.linkedBrowser.currentURI.spec
            ),
            "tab_list"
          );
          break;
        case "context_bookmarkTab":
          PlacesCommandHook.bookmarkTabs([TabContextMenu.contextTab]);
          break;
        case "context_addNote":
        case "context_editNote":
          gBrowser.tabNoteMenu.openPanel(TabContextMenu.contextTab, {
            telemetrySource: lazy.TabNotes.TELEMETRY_SOURCE.TAB_CONTEXT_MENU,
          });
          Services.prefs.setBoolPref(
            "browser.tabs.notes.newBadge.enabled",
            false
          );
          break;
        case "context_moveToStart":
          gBrowser.moveTabsToStart(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_moveToEnd":
          gBrowser.moveTabsToEnd(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_openTabInWindow":
          gBrowser.replaceTabsWithWindow(TabContextMenu.contextTab, {
            metricsContext: gBrowser.TabMetrics.userTriggeredContext(
              gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_openTabInMiniWindow":
          lazy.MiniWindowManager.popTabs(
            TabContextMenu.contextTabs,
            "tab_context_menu"
          );
          break;
        case "context_selectAllTabs":
          gBrowser.selectAllTabs();
          break;
        case "context_closeTab":
          TabContextMenu.closeContextTabs();
          break;
        case "context_closeDuplicateTabs":
          gBrowser.removeDuplicateTabs(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_closeTabsToTheStart":
          gBrowser.removeTabsToTheStartFrom(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_closeTabsToTheEnd":
          gBrowser.removeTabsToTheEndFrom(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
          break;
        case "context_closeOtherTabs":
          gBrowser.removeAllTabsBut(TabContextMenu.contextTab, {
            metricsContext: lazy.TabMetrics.userTriggeredContext(
              lazy.TabMetrics.METRIC_SOURCE.TAB_MENU
            ),
          });
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
            let popup = event.target.parentElement;
            let tabGroupId = getContextTabGroupId(popup);
            let tabGroup = gBrowser.getTabGroupById(tabGroupId);
            let source = getGroupsListSource(popup);
            tabGroup.documentGlobal.gBrowser.replaceGroupWithWindow(tabGroup, {
              metricsContext: lazy.TabMetrics.userTriggeredContext(source),
            });
          }
          break;
        case "open-tab-group-context-menu_moveToThisWindow":
          {
            let tabGroupId = getContextTabGroupId(event.target.parentElement);
            let otherTabGroup = gBrowser.getTabGroupById(tabGroupId);
            let adoptedTabGroup = gBrowser.adoptTabGroup(otherTabGroup, {
              tabIndex: gBrowser.tabs.length,
            });
            adoptedTabGroup.select();
          }
          break;
        case "open-tab-group-context-menu_share":
          {
            let popup = event.target.parentElement;
            let tabGroupId = getContextTabGroupId(popup);
            let tabGroup = gBrowser.getTabGroupById(tabGroupId);
            // Close the panel this context menu was opened from (e.g. the
            // "List all tabs" panel) so it doesn't overlap the share dialog.
            popup.triggerNode?.closest("panel")?.hidePopup();
            lazy.ContentSharingUtils.handleShareTabGroup(tabGroup);
          }
          break;
        case "open-tab-group-context-menu_delete":
          {
            let popup = event.target.parentElement;
            let tabGroupId = getContextTabGroupId(popup);
            let tabGroup = gBrowser.getTabGroupById(tabGroupId);
            let source = getGroupsListSource(popup);
            // Unlike the other actions in this menu, deleting the group
            // doesn't dismiss the containing panel on its own.
            dismissTabGroupPanel(popup);
            // Tabs need to be removed by their owning `Tabbrowser` or else
            // there are errors.
            tabGroup.documentGlobal.gBrowser.removeTabGroup(tabGroup, {
              metricsContext: lazy.TabMetrics.userTriggeredContext(source),
            });
          }
          break;

        // == saved-tab-group-context-menu ==
        case "saved-tab-group-context-menu_openInThisWindow":
          {
            let popup = event.target.parentElement;
            let tabGroupId = getContextTabGroupId(popup);
            let source = getGroupsListSource(popup);
            SessionStore.openSavedTabGroup(tabGroupId, window, {
              source,
            });
          }
          break;
        case "saved-tab-group-context-menu_openInNewWindow":
          {
            // TODO Bug 1940112: "Open Group in New Window" should directly restore saved tab groups into a new window
            let popup = event.target.parentElement;
            let tabGroupId = getContextTabGroupId(popup);
            let source = getGroupsListSource(popup);
            let tabGroup = SessionStore.openSavedTabGroup(tabGroupId, window, {
              source,
            });
            gBrowser.replaceGroupWithWindow(tabGroup, {
              metricsContext: lazy.TabMetrics.userTriggeredContext(source),
            });
          }
          break;
        case "saved-tab-group-context-menu_delete":
          {
            let popup = event.target.parentElement;
            let tabGroupId = getContextTabGroupId(popup);
            dismissTabGroupPanel(popup);
            SessionStore.forgetSavedTabGroup(tabGroupId, "user-deleted");
          }
          break;
        // == editBookmarkPanel ==
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
        case "toolbar-context-always-show-extensions-button":
          if (event.target.hasAttribute("checked")) {
            gUnifiedExtensions.showExtensionsButtonInToolbar();
          } else {
            gUnifiedExtensions.hideExtensionsButtonFromToolbar();
          }
          break;
        case "toolbar-context-remove-from-toolbar":
          if (
            event.target.parentNode.triggerNode === gUnifiedExtensions.button
          ) {
            gUnifiedExtensions.hideExtensionsButtonFromToolbar();
            break;
          }
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
          gCustomizeMode.enter("toolbar_context_menu");
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

    const userContextIcons = document.getElementById("userContext-icons");
    userContextIcons.addEventListener("click", event => {
      if (event.button !== 0) {
        return;
      }
      document
        .getElementById("userContext-indicator-menu")
        .openPopup(userContextIcons, "after_start", 0, 0, false, false, event);
    });

    const containerHistoryPopup = document.getElementById(
      "sidebar-history-context-menu-container-popup"
    );
    containerHistoryPopup.addEventListener("command", event => {
      PlacesUIUtils.openInContainerTab(event, "sidebar_history_context_menu");
      Glean.browserUiInteraction.sidebarHistory.open_in_new_container_tab.add(
        1
      );
    });
    containerHistoryPopup.addEventListener("popupshowing", event =>
      PlacesUIUtils.createContainerTabMenu(
        event,
        "sidebar_history_context_menu"
      )
    );

    const containerSyncedTabsPopup = document.getElementById(
      "sidebar-synced-tabs-context-menu-container-popup"
    );
    containerSyncedTabsPopup.addEventListener("command", event =>
      PlacesUIUtils.openInContainerTab(
        event,
        "sidebar_synced_tabs_context_menu"
      )
    );
    containerSyncedTabsPopup.addEventListener("popupshowing", event =>
      PlacesUIUtils.createContainerTabMenu(
        event,
        "sidebar_synced_tabs_context_menu"
      )
    );

    document
      .getElementById("sidebar-bookmarks-context-container-tab-popup")
      .addEventListener("popupshowing", event =>
        PlacesUIUtils.createContainerTabMenu(
          event,
          "sidebar_bookmarks_context_menu"
        )
      );

    document
      .getElementById("context_reopenInContainerPopupMenu")
      .addEventListener("command", event => {
        // Handle commands on the descendant <menuitem>s with different containers.
        TabContextMenu.reopenInContainer(event);
      });

    // when pref browser.tabs.contextmenu.altstructure.enabled is enabled, the
    // group items live in the #context_addTabToGroup menu
    for (let menu of [
      document.getElementById("context_moveTabToGroupPopupMenu"),
      document.getElementById("context_addTabToGroup"),
    ]) {
      menu.addEventListener("command", event => {
        if (
          event.target.id == "context_moveTabToGroupNewGroup" ||
          event.target.id == "context_addTabToGroupNewGroup"
        ) {
          TabContextMenu.moveTabsToNewGroup();
          return;
        }

        const tabGroupId = event.target.getAttribute("tab-group-id");
        if (!tabGroupId) {
          return;
        }
        const group = gBrowser.getTabGroupById(tabGroupId);
        if (group) {
          TabContextMenu.moveTabsToGroup(group);
        }

        if (SessionStore.getSavedTabGroup(tabGroupId)) {
          TabContextMenu.addTabsToSavedGroup(tabGroupId);
        }
      });
    }

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
            {
              multiselected: TabContextMenu.contextTab.multiselected,
              contextMenuType: "tab",
            }
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
          ToolbarContextMenu.updateExtensionsButtonContextMenu(event.target);
          ToolbarContextMenu.updateExtension(event.target);

          // The following methods must be called last after updating the menu items above,
          // as they may change which items are visible.
          ToolbarContextMenu.updateCustomizationItemsVisibility(event.target);
          ToolbarContextMenu.hideLeadingSeparatorIfNeeded(event.target);
          break;
        case "pageActionContextMenu":
          BrowserPageActions.onContextMenuShowing(event, event.target);
          break;
        case "tabbrowser-tab-tooltip":
          gBrowser.createTooltip(event);
          break;
        case "dynamic-shortcut-tooltip":
          DynamicShortcutTooltip.updateText(event.target);
          break;
        case "SyncedTabsOpenSelectedInContainerTabMenu":
          createUserContextMenu(event, {
            isContextMenu: true,
            containerSource: "synced_tabs_context_menu",
          });
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
        case "moveTabOptionsMenu":
          gProfiles.populateMoveTabMenu(event.target);
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
          let tabGroupId = getContextTabGroupId(event.target);
          let tabGroup = gBrowser.getTabGroupById(tabGroupId);
          let tabGroupIsInThisWindow = tabGroup.ownerDocument == document;
          event.target.querySelector(
            "#open-tab-group-context-menu_moveToThisWindow"
          ).disabled = tabGroupIsInThisWindow;

          // Disable "Move Group to New Window" menu option for tab groups
          // that are the only things in their respective window.
          let groupAloneInWindow =
            tabGroup.tabs.length ==
            tabGroup.documentGlobal.gBrowser.openTabs.length;
          event.target.querySelector(
            "#open-tab-group-context-menu_moveToNewWindow"
          ).disabled = groupAloneInWindow;

          // Only show "Share Group" when content sharing is enabled.
          event.target.querySelector(
            "#open-tab-group-context-menu_share"
          ).hidden = !lazy.ContentSharingUtils.isEnabled;
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
        case "tabContextMenu":
          // Reset Send Tab exposure tracking when tab context menu closes
          gSync._resetSendTabExposureTracking();
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

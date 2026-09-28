/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var TabContextMenu = {
  contextTab: null,

  /*
   * Declared layout for the tab context menu and its managed submenus, keyed by
   * mode. Each mode maps a popup id to the ordered sections describing that
   * popup's contents, using the same grammar at every level. MenuSectionLayout
   * resolves each selector anywhere under #tabContextMenu and moves the node
   * into the popup that declares it, so one descriptor can both reorder a popup
   * and relocate items between popups. Applied on first show and re-applied when
   * the pref changes (see _ensureMenuArranged); each descriptor is complete over
   * the popups it manages, so re-applying it restores the structure from any
   * prior state.
   *
   * Selectors grouped in an inner array are kept contiguous as a unit (typically
   * mutually-exclusive twins toggled by updateContextMenu, where one is visible
   * at a time). Existing separators are listed explicitly rather than generated.
   * The share submenu (.share-tab-url-item) is created lazily by SharingUtils
   * after #context_moveTabOptions, so it is declared optional: positioned when
   * present, skipped when absent. Runtime-inserted items (tab groups, profiles)
   * are positioned against their anchor separators by their own code and are
   * exempt from the layout (see DYNAMIC_MENU_ITEM_SELECTORS).
   */
  MENU_SECTIONS: {
    classic: {
      tabContextMenu: [
        {
          name: "priority-items",
          items: [
            "#context_openANewTab",
            [
              "#context_moveTabToNewGroup",
              "#context_moveSplitViewToNewGroup",
              "#context_moveTabToGroup",
              "#context_ungroupTab",
              "#context_ungroupSplitView",
            ],
            [
              "#context_moveTabToSplitView",
              "#context_separateSplitView",
              "#context_reverseSplitView",
            ],
            "#context_openAndOrganizeSeparator",
          ],
        },
        {
          name: "action-items",
          items: [
            ["#context_reloadTab", "#context_reloadSelectedTabs"],
            ["#context_playTab", "#context_playSelectedTabs"],
            ["#context_toggleMuteTab", "#context_toggleMuteSelectedTabs"],
            [
              "#context_pinTab",
              "#context_unpinTab",
              "#context_pinSelectedTabs",
              "#context_unpinSelectedTabs",
            ],
            "#context_unloadTab",
            ["#context_duplicateTab", "#context_duplicateTabs"],
            "#context_tabStateSeparator",
          ],
        },
        {
          name: "ai",
          /*
           * #context_askChatSummarize is the alt layout's flat summarize item;
           * placed after the separator (hidden here) so #context_askChat keeps
           * #context_aiSeparator as its next sibling, which buildTabMenu toggles
           * together with the submenu.
           */
          items: [
            "#context_askChat",
            "#context_aiSeparator",
            "#context_askChatSummarize",
            "#context_createAITab",
          ],
        },
        {
          name: "tab-tools",
          items: [
            ["#context_bookmarkSelectedTabs", "#context_bookmarkTab"],
            ["#context_addNote", "#context_editNote"],
            "#context_moveTabOptions",
            { selector: ".share-tab-url-item", optional: true },
            "#context_reopenInContainer",
            "#context_selectAllTabs",
            "#context_tabToolsSeparator",
          ],
        },
        {
          name: "sending-items",
          items: [
            "#context_shareSelectedTabs",
            "#context_shareSelectedTabsSeparator",
            "#context_sendTabToDevice",
            "#context_sendTabToDeviceSeparator",
          ],
        },
        {
          name: "close",
          items: [
            "#context_closeTab",
            "#context_closeDuplicateTabs",
            "#context_closeTabOptions",
            "#context_undoCloseTab",
          ],
        },
        {
          name: "fullscreen",
          items: [
            "#context_fullscreenSeparator",
            "#context_fullscreenAutohide",
            "#context_fullscreenExit",
          ],
        },
        {
          /*
           * WebExtension menu items are appended to the end of the tab context
           * menu by ext-menus.js; this open section lets them sit there without
           * tripping the layout's coverage check.
           */
          name: "extensions",
          open: true,
          items: [],
        },
      ],
      /*
       * The "Move tabs to" submenu keeps its authored contents; the two extra
       * separators (hidden here) only become visible in the alt layout.
       */
      moveTabOptionsMenu: [
        {
          name: "positions",
          items: [
            "#context_moveToStart",
            "#context_moveToEnd",
            "#context_openTabInWindow",
            "#context_openTabInMiniWindow",
            "#moveTabSeparator",
            "#context_moveTabToGroupSeparator",
            "#context_selectAllSeparator",
          ],
        },
      ],
      context_moveTabToGroupPopupMenu: [
        {
          name: "groups",
          items: [
            "#context_moveTabToGroupNewGroup",
            "#open-tab-groups-separator-upper",
            "#open-tab-groups-separator-lower",
            "#context_moveTabToSavedGroup",
          ],
        },
      ],
      closeTabOptions: [
        {
          name: "close-multiple",
          items: [
            "#context_closeTabsToTheStart",
            "#context_closeTabsToTheEnd",
            "#context_closeOtherTabs",
          ],
        },
      ],
    },
    /*
     * Alternate menu structure, preserves the top few items as-is but attempts to
     * group together functions and uses shorter labels. Every managed child is placed
     * (relocated or hidden by per-show logic) so test coverage passes.
     */
    altstructure: {
      tabContextMenu: [
        {
          name: "priority-items",
          items: [
            "#context_openANewTab",
            ["#context_duplicateTab", "#context_duplicateTabs"],
            "#context_openAndOrganizeSeparator",
          ],
        },
        {
          name: "action-items",
          items: [
            [
              "#context_pinTab",
              "#context_unpinTab",
              "#context_pinSelectedTabs",
              "#context_unpinSelectedTabs",
            ],
            ["#context_toggleMuteTab", "#context_toggleMuteSelectedTabs"],
            ["#context_playTab", "#context_playSelectedTabs"],
            ["#context_reloadTab", "#context_reloadSelectedTabs"],
            "#context_unloadTab",
            "#context_tabStateSeparator",
          ],
        },
        {
          name: "spatial-items",
          items: [
            [
              "#context_moveTabToSplitView",
              "#context_separateSplitView",
              "#context_reverseSplitView",
            ],
            /*
             * "Move Tab to" reuses #context_moveTabOptions; the tab-group entries
             * and Select All Tabs are flattened into its submenu (see the
             * moveTabOptionsMenu layout below). The top-level group items stay
             * listed here for coverage but are hidden by per-show logic.
             */
            "#context_moveTabOptions",
            [
              "#context_moveTabToNewGroup",
              "#context_moveSplitViewToNewGroup",
              "#context_moveTabToGroup",
              "#context_ungroupTab",
              "#context_ungroupSplitView",
            ],
            "#context_reopenInContainer",
            /*
             * The AI submenu separator is unused in this layout (the AI feature is
             * the flat "Summarize Page" item in remembering-items), so reuse it
             * as the divider after the open-and-organize group. Shown by the AI
             * branch of updateContextMenu.
             */
            "#context_aiSeparator",
          ],
        },
        {
          name: "remembering-items",
          items: [
            ["#context_bookmarkSelectedTabs", "#context_bookmarkTab"],
            ["#context_addNote", "#context_editNote"],
            "#context_askChatSummarize",
            "#context_createAITab",
            "#context_tabToolsSeparator",
          ],
        },
        {
          name: "sending-items",
          items: [
            "#context_shareSelectedTabs",
            /*
             * Created lazily by SharingUtils (after #context_moveTabOptions); the
             * layout positions it here when present. Share placement overlaps
             * bug 2019778 and may be revisited.
             */
            { selector: ".share-tab-url-item", optional: true },
            "#context_shareSelectedTabsSeparator",
            "#context_sendTabToDevice",
            "#context_sendTabToDeviceSeparator",
          ],
        },
        {
          name: "destructive-items",
          /* Close Duplicates is flattened into the #closeTabOptions submenu. */
          items: [
            "#context_closeTab",
            "#context_closeTabOptions",
            "#context_undoCloseTab",
          ],
        },
        {
          name: "fullscreen",
          items: [
            "#context_fullscreenSeparator",
            "#context_fullscreenAutohide",
            "#context_fullscreenExit",
          ],
        },
        {
          /* Items that have no place in this layout and should always be hidden */
          name: "unused",
          items: ["#context_askChat"],
        },
        {
          /*
           * WebExtension menu items are appended to the end of the tab context
           * menu by ext-menus.js; this open section lets them sit there without
           * tripping the layout's validation check.
           */
          name: "extensions",
          open: true,
          items: [],
        },
      ],
      /*
       * "Move Tab to": #context_moveTabOptions with the tab-group entries (from
       * #context_moveTabToGroupPopupMenu) and Select All Tabs flattened in. Move
       * to Start/End/New Window are already here; profile items are inserted at
       * runtime after #moveTabSeparator.
       */
      moveTabOptionsMenu: [
        {
          name: "groups",
          items: [
            "#context_moveTabToGroupNewGroup",
            "#open-tab-groups-separator-upper",
            "#open-tab-groups-separator-lower",
            "#context_moveTabToSavedGroup",
            "#context_moveTabToGroupSeparator",
          ],
        },
        {
          name: "positions",
          items: [
            "#context_moveToStart",
            "#context_moveToEnd",
            "#context_openTabInWindow",
            "#context_openTabInMiniWindow",
            "#moveTabSeparator",
          ],
        },
        {
          name: "select-all",
          items: ["#context_selectAllSeparator", "#context_selectAllTabs"],
        },
      ],
      /* Emptied here: its entries are flattened into moveTabOptionsMenu above. */
      context_moveTabToGroupPopupMenu: [],
      /*
       * Close Duplicates joins the other close-multiple options in the "Close
       * Multiple" submenu in this layout (relocated from the top level).
       */
      closeTabOptions: [
        {
          name: "close-multiple",
          items: [
            "#context_closeTabsToTheStart",
            "#context_closeTabsToTheEnd",
            "#context_closeOtherTabs",
            "#context_closeDuplicateTabs",
          ],
        },
      ],
    },
  },

  /* Runtime-inserted menu items, exempt from the layout's coverage check. */
  DYNAMIC_MENU_ITEM_SELECTORS: ["[tab-group-id]", "[profileid]"],

  _tabContextMenuArranged: false,
  _altTabContextMenuPrefObserved: false,

  /*
   * Arrange the tab context menu into its declared section order the first time
   * it is shown, and again after the alt context menu pref changes. Deferring to first show
   * keeps this work out of startup.
   */
  _ensureMenuArranged(aPopupMenu) {
    if (!this._altTabContextMenuPrefObserved) {
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_altTabContextMenu",
        "browser.tabs.contextmenu.altstructure.enabled",
        false,
        () => {
          this._tabContextMenuArranged = false;
        }
      );
      this._altTabContextMenuPrefObserved = true;
    }
    if (this._tabContextMenuArranged) {
      return;
    }
    /*
     * Reading the pref selects the structure and arms the observer that resets
     * _tabContextMenuArranged when the pref changes.
     */
    let layout =
      (this._altTabContextMenu && this.MENU_SECTIONS.altstructure) ||
      this.MENU_SECTIONS.classic;
    // MenuSectionLayout throws if the markup and layout disagree and a menuitem is unaccounted for.
    // Catch these exceptions and log them out.
    try {
      new this.MenuSectionLayout(layout, {
        dynamicItemSelectors: this.DYNAMIC_MENU_ITEM_SELECTORS,
      }).arrange(aPopupMenu);
      this._hideUnusedSectionItems(layout);
      this._updateL10nIds(aPopupMenu, this._altTabContextMenu);
    } catch (ex) {
      console.error(ex);
    }
    this._tabContextMenuArranged = true;
  },

  /*
   * Swap in alternate Fluent ids for items that need different wording in the
   * alt layout. We cache the authored id so a pref toggle back to classic can
   * restore it. Runs only when the layout is (re)applied.
   */
  _updateL10nIds(aPopupMenu, altEnabled) {
    for (let item of aPopupMenu.querySelectorAll("[data-alt-l10n-id]")) {
      if (item._classicL10nId == null) {
        item._classicL10nId =
          item.getAttribute("data-lazy-l10n-id") ??
          item.getAttribute("data-l10n-id");
      }
      let id = altEnabled ? item.dataset.altL10nId : item._classicL10nId;
      if (id) {
        item.setAttribute("data-l10n-id", id);
      }
    }
  },

  /* Hide every item the active layout parks in its "unused" section; */
  _hideUnusedSectionItems(layout) {
    for (let section of layout.tabContextMenu) {
      if (section.name !== "unused") {
        continue;
      }
      for (let { selector } of this.MenuSectionLayout.placementsFor([
        section,
      ])) {
        let item = document.querySelector(selector);
        if (item) {
          item.hidden = true;
        }
      }
    }
  },

  /*
   * Set the per-show visibility of the nodes shared by the "Move tabs to"
   * submenu in both layouts. In the alt layout the tab-group entries and Select
   * All Tabs are flattened into that submenu (see MENU_SECTIONS.altstructure),
   * so the top-level group wrapper and the simplified new-group items are hidden
   * and the submenu's own separators are shown as needed. In the classic layout
   * these nodes are reset to their authored state (the top-level group items are
   * managed by the tab-groups block above). Runs every show so neither layout's
   * visibility leaks across a pref toggle.
   */
  _updateMoveTabToFlattenedVisibility(
    groupsEnabled,
    hasOpenGroups,
    hasSavedGroups
  ) {
    let byId = id => document.getElementById(id);
    let newGroup = byId("context_moveTabToGroupNewGroup");
    let upperSeparator = byId("open-tab-groups-separator-upper");
    let lowerSeparator = byId("open-tab-groups-separator-lower");
    let savedGroups = byId("context_moveTabToSavedGroup");
    let groupSeparator = byId("context_moveTabToGroupSeparator");
    let selectAllSeparator = byId("context_selectAllSeparator");

    if (!this._altTabContextMenu) {
      /* Classic: restore the shared submenu nodes; hide the alt-only separators. */
      newGroup.hidden = false;
      upperSeparator.hidden = false;
      savedGroups.hidden = false;
      groupSeparator.hidden = true;
      selectAllSeparator.hidden = true;
      return;
    }

    /*
     * Alt: the group entries are flattened into the submenu, so hide the
     * top-level wrapper and simplified new-group items (ungroup stays at top
     * level, managed by the tab-groups block).
     */
    byId("context_moveTabToGroup").hidden = true;
    byId("context_moveTabToNewGroup").hidden = true;
    byId("context_moveSplitViewToNewGroup").hidden = true;

    newGroup.hidden = !groupsEnabled;
    upperSeparator.hidden = !(
      groupsEnabled &&
      (hasOpenGroups || hasSavedGroups)
    );
    lowerSeparator.hidden = true;
    savedGroups.hidden = !(groupsEnabled && hasSavedGroups);
    groupSeparator.hidden = !groupsEnabled;
    selectAllSeparator.hidden = false;
  },

  _updateToggleMuteMenuItems(aTab, aConditionFn) {
    ["muted", "soundplaying"].forEach(attr => {
      if (!aConditionFn || aConditionFn(attr)) {
        if (aTab.hasAttribute(attr)) {
          aTab.toggleMuteMenuItem.setAttribute(attr, "true");
          aTab.toggleMultiSelectMuteMenuItem.setAttribute(attr, "true");
        } else {
          aTab.toggleMuteMenuItem.removeAttribute(attr);
          aTab.toggleMultiSelectMuteMenuItem.removeAttribute(attr);
        }
      }
    });
  },
  // eslint-disable-next-line complexity
  updateContextMenu(aPopupMenu) {
    this._ensureMenuArranged(aPopupMenu);

    let triggerTab =
      aPopupMenu.triggerNode &&
      (aPopupMenu.triggerNode.tab || aPopupMenu.triggerNode.closest("tab"));
    this.contextTab = triggerTab || gBrowser.selectedTab;
    this.contextTab.addEventListener("TabAttrModified", this);
    aPopupMenu.addEventListener("popuphidden", this);

    this.multiselected = this.contextTab.multiselected;
    this.contextTabs = this.multiselected
      ? gBrowser.selectedTabs
      : [this.contextTab];

    let splitViews = new Set();
    // bug1973996: This call is not guaranteed to complete
    // before the saved groups menu is populated
    for (let tab of this.contextTabs) {
      TabContextMenu.TabStateFlusher.flush(tab.linkedBrowser);

      // Add unique split views for count info below
      if (tab.splitview) {
        splitViews.add(tab.splitview);
      }
    }

    document.getElementById("context_openTabInMiniWindow").hidden =
      !Services.prefs.getBoolPref("browser.mini-window.enabled", false);

    let disabled = gBrowser.tabs.length == 1;
    let tabCountInfo = JSON.stringify({
      tabCount: this.contextTabs.length,
    });
    let splitViewCountInfo = JSON.stringify({
      splitViewCount: splitViews.size,
    });

    var menuItems = aPopupMenu.getElementsByAttribute(
      "tbattr",
      "tabbrowser-multiple"
    );
    for (let menuItem of menuItems) {
      menuItem.disabled = disabled;
    }

    disabled = gBrowser.visibleTabs.length == 1;
    menuItems = aPopupMenu.getElementsByAttribute(
      "tbattr",
      "tabbrowser-multiple-visible"
    );
    for (let menuItem of menuItems) {
      menuItem.disabled = disabled;
    }

    let contextNewTabButton = document.getElementById("context_openANewTab");
    // update context menu item strings for vertical tabs
    document.l10n.setAttributes(
      contextNewTabButton,
      gBrowser.tabContainer?.verticalMode
        ? "tab-context-new-tab-open-vertical"
        : "tab-context-new-tab-open"
    );

    // Session store
    let closedCount = SessionStore.getLastClosedTabCount(window);
    document
      .getElementById("History:UndoCloseTab")
      .toggleAttribute("disabled", closedCount == 0);
    document.l10n.setArgs(document.getElementById("context_undoCloseTab"), {
      tabCount: closedCount,
    });

    // Show/hide fullscreen context menu items and set the
    // autohide item's checked state to mirror the autohide pref.
    showFullScreenViewContextMenuItems(aPopupMenu);

    // #context_moveTabToNewGroup is a simplified context menu item that only
    // appears if there are no existing tab groups available to move the tab to.
    let contextMoveTabToNewGroup = document.getElementById(
      "context_moveTabToNewGroup"
    );
    let contextMoveTabToGroup = document.getElementById(
      "context_moveTabToGroup"
    );
    let contextUngroupTab = document.getElementById("context_ungroupTab");
    let contextMoveSplitViewToNewGroup = document.getElementById(
      "context_moveSplitViewToNewGroup"
    );
    let contextUngroupSplitView = document.getElementById(
      "context_ungroupSplitView"
    );
    let isAllSplitViewTabs = this.contextTabs.every(
      contextTab => contextTab.splitview
    );
    let openGroupsToMoveTo = [];
    let savedGroupsToMoveTo = [];

    if (TabContextMenu.Tabbrowser.prefs.tabGroupsEnabled) {
      let {
        openGroups,
        savedGroups,
        groupCount: selectedGroupCount,
      } = this.TabGroupMenu.getGroupsToMoveTo(this.contextTabs);
      openGroupsToMoveTo = openGroups;
      savedGroupsToMoveTo = savedGroups;

      if (!openGroupsToMoveTo.length && !savedGroupsToMoveTo.length) {
        if (isAllSplitViewTabs) {
          contextMoveTabToGroup.hidden = true;
          contextMoveTabToNewGroup.hidden = true;
          contextMoveSplitViewToNewGroup.hidden = false;
          contextMoveSplitViewToNewGroup.setAttribute(
            "data-l10n-args",
            splitViewCountInfo
          );
        } else {
          contextMoveTabToGroup.hidden = true;
          contextMoveSplitViewToNewGroup.hidden = true;
          contextMoveTabToNewGroup.hidden = false;
          contextMoveTabToNewGroup.setAttribute("data-l10n-args", tabCountInfo);
        }
      } else {
        if (isAllSplitViewTabs) {
          contextMoveTabToNewGroup.hidden = true;
          contextMoveSplitViewToNewGroup.hidden = true;
          contextMoveTabToGroup.hidden = false;
          contextMoveTabToGroup.setAttribute(
            "data-l10n-id",
            "tab-context-move-split-view-to-group"
          );
          contextMoveTabToGroup.setAttribute(
            "data-l10n-args",
            splitViewCountInfo
          );
        } else {
          contextMoveTabToNewGroup.hidden = true;
          contextMoveSplitViewToNewGroup.hidden = true;
          contextMoveTabToGroup.hidden = false;
          contextMoveTabToGroup.setAttribute(
            "data-l10n-id",
            "tab-context-move-tab-to-group"
          );
          contextMoveTabToGroup.setAttribute("data-l10n-args", tabCountInfo);
        }

        // Resolve the group anchors by id rather than relative to the group
        // submenu, so the open/saved group items land correctly whether these
        // nodes live in their own popup (classic) or are flattened into the
        // "Move Tab to" submenu (alt layout).
        const upperSeparator = document.getElementById(
          "open-tab-groups-separator-upper"
        );
        const lowerSeparator = document.getElementById(
          "open-tab-groups-separator-lower"
        );
        const openGroupsMenu = upperSeparator.parentNode;
        openGroupsMenu
          .querySelectorAll("[tab-group-id]")
          .forEach(el => el.remove());

        lowerSeparator.hidden = !openGroupsToMoveTo.length;

        openGroupsToMoveTo.toReversed().forEach(group => {
          let item = this._createTabGroupMenuItem(group, false);
          upperSeparator.after(item);
        });

        const savedGroupsMenu = document.getElementById(
          "context_moveTabToSavedGroup"
        );
        const savedGroupsMenuPopup = savedGroupsMenu.querySelector("menupopup");

        savedGroupsMenuPopup
          .querySelectorAll("[tab-group-id]")
          .forEach(el => el.remove());
        if (savedGroupsToMoveTo.length) {
          savedGroupsMenu.disabled = false;

          savedGroupsToMoveTo.forEach(group => {
            let item = this._createTabGroupMenuItem(group, true);
            savedGroupsMenuPopup.appendChild(item);
          });
        } else {
          savedGroupsMenu.disabled = true;
        }
      }

      let groupInfo = JSON.stringify({
        groupCount: selectedGroupCount,
      });
      if (isAllSplitViewTabs) {
        contextUngroupSplitView.hidden = !selectedGroupCount;
        contextUngroupTab.hidden = true;
        contextUngroupSplitView.setAttribute("data-l10n-args", groupInfo);
      } else {
        contextUngroupTab.hidden = !selectedGroupCount;
        contextUngroupSplitView.hidden = true;
        contextUngroupTab.setAttribute("data-l10n-args", groupInfo);
      }
    } else {
      contextMoveTabToNewGroup.hidden = true;
      contextMoveTabToGroup.hidden = true;
      contextUngroupTab.hidden = true;
      contextMoveSplitViewToNewGroup.hidden = true;
      contextUngroupSplitView.hidden = true;
    }

    this._updateMoveTabToFlattenedVisibility(
      TabContextMenu.Tabbrowser.prefs.tabGroupsEnabled,
      !!openGroupsToMoveTo.length,
      !!savedGroupsToMoveTo.length
    );

    let contextAddNote = document.getElementById("context_addNote");
    let contextEditNote = document.getElementById("context_editNote");
    if (this._tabNotesEnabled) {
      // Tab notes behaviour is disabled if a user has a selection of tabs that
      // contains more than one canonical URL.
      let multiselectingDiverseUrls =
        this.multiselected &&
        !this.contextTabs.every(
          t => t.canonicalUrl === this.contextTabs[0].canonicalUrl
        );

      contextAddNote.disabled =
        multiselectingDiverseUrls || !this.TabNotes.isEligible(this.contextTab);
      contextEditNote.disabled = multiselectingDiverseUrls;

      this.removeNewBadge(contextAddNote);
      if (
        Services.prefs.getBoolPref("browser.tabs.notes.newBadge.enabled", true)
      ) {
        this.addNewBadge(contextAddNote);
      }

      this.TabNotes.has(this.contextTab).then(hasNote => {
        contextAddNote.hidden = hasNote;
        contextEditNote.hidden = !hasNote;
      });
    } else {
      contextAddNote.hidden = true;
      contextEditNote.hidden = true;
    }

    // Split View
    let splitViewEnabled = Services.prefs.getBoolPref(
      "browser.tabs.splitView.enabled",
      false
    );
    let contextMoveTabToNewSplitView = document.getElementById(
      "context_moveTabToSplitView"
    );
    let contextSeparateSplitView = document.getElementById(
      "context_separateSplitView"
    );
    let contextReverseSplitView = document.getElementById(
      "context_reverseSplitView"
    );
    let hasSplitViewTab = this.contextTabs.some(tab => tab.splitview);
    contextMoveTabToNewSplitView.hidden = !splitViewEnabled || hasSplitViewTab;
    contextSeparateSplitView.hidden = !splitViewEnabled || !hasSplitViewTab;
    contextReverseSplitView.hidden =
      !splitViewEnabled || !hasSplitViewTab || this.multiselected;
    if (splitViewEnabled) {
      contextMoveTabToNewSplitView.removeAttribute("data-l10n-id");
      let splitViewStringId =
        this._altTabContextMenu || this.contextTabs.length >= 2
          ? "tab-context-open-in-split-view"
          : "tab-context-add-split-view";
      contextMoveTabToNewSplitView.setAttribute(
        "data-l10n-id",
        splitViewStringId
      );

      let pinnedTabs = this.contextTabs.filter(t => t.pinned);
      let customizeTabs = this.contextTabs.filter(t =>
        t.hasAttribute("customizemode")
      );
      contextMoveTabToNewSplitView.disabled =
        this.contextTabs.length > 2 ||
        pinnedTabs.length ||
        customizeTabs.length;
    }

    // Only one of Reload_Tab/Reload_Selected_Tabs should be visible.
    document.getElementById("context_reloadTab").hidden = this.multiselected;
    document.getElementById("context_reloadSelectedTabs").hidden =
      !this.multiselected;
    let unloadTabItem = document.getElementById("context_unloadTab");
    if (this._unloadTabInContextMenu) {
      // linkedPanel is false if the tab is already unloaded
      // Cannot unload about: pages, etc., so skip browsers that are not remote
      let unloadableTabs = this.contextTabs.filter(
        t => t.linkedPanel && t.linkedBrowser?.isRemoteBrowser
      );
      unloadTabItem.hidden = unloadableTabs.length === 0;
      unloadTabItem.setAttribute(
        "data-l10n-args",
        JSON.stringify({ tabCount: unloadableTabs.length })
      );
    } else {
      unloadTabItem.hidden = true;
    }

    // Show Play Tab menu item if the tab has attribute activemedia-blocked
    document.getElementById("context_playTab").hidden = !(
      this.contextTab.activeMediaBlocked && !this.multiselected
    );
    document.getElementById("context_playSelectedTabs").hidden = !(
      this.contextTab.activeMediaBlocked && this.multiselected
    );

    // Only one of pin/unpin/multiselect-pin/multiselect-unpin should be visible
    let hasAboutOpenTabsTab = this.contextTabs.some(
      t => t.linkedBrowser.currentURI.spec === "about:opentabs"
    );
    let contextPinTab = document.getElementById("context_pinTab");
    contextPinTab.hidden =
      this.contextTab.pinned || this.multiselected || hasAboutOpenTabsTab;
    let contextUnpinTab = document.getElementById("context_unpinTab");
    contextUnpinTab.hidden = !this.contextTab.pinned || this.multiselected;
    let contextPinSelectedTabs = document.getElementById(
      "context_pinSelectedTabs"
    );
    contextPinSelectedTabs.hidden =
      this.contextTab.pinned || !this.multiselected || hasAboutOpenTabsTab;
    let contextUnpinSelectedTabs = document.getElementById(
      "context_unpinSelectedTabs"
    );
    contextUnpinSelectedTabs.hidden =
      !this.contextTab.pinned || !this.multiselected;

    // Build Ask Chat items. The classic layout shows the full ask-chat submenu
    // (#context_askChat); the alt layout shows only a flat "Summarize Page" item
    // (#context_askChatSummarize). Hide whichever one this layout doesn't use so
    // a pref toggle reverses cleanly.
    if (!this._altTabContextMenu) {
      document.getElementById("context_askChatSummarize").hidden = true;
      TabContextMenu.GenAI.buildTabMenu(
        document.getElementById("context_askChat"),
        this
      );
    } else {
      document.getElementById("context_askChat").hidden = true;
      // In the alt layout #context_aiSeparator is reused as the divider after
      // the open-and-organize group, so keep it shown (the classic ask-chat
      // path would otherwise hide it together with #context_askChat).
      document.getElementById("context_aiSeparator").hidden = false;
      TabContextMenu.GenAI.buildTabSummarizeItem(
        document.getElementById("context_askChatSummarize"),
        this
      );
    }

    document.getElementById("context_createAITab").hidden = !(
      TabContextMenu.AITAB_ENABLED &&
      TabContextMenu.AIWindow.isAIWindowActiveAndEnabled(window) &&
      this.contextTabs.some(tab =>
        ["http", "https"].includes(tab.linkedBrowser.currentURI.scheme)
      )
    );

    // Move Tab items
    let contextMoveTabOptions = document.getElementById(
      "context_moveTabOptions"
    );
    // gBrowser.visibleTabs excludes tabs in collapsed groups,
    // which we want to include in calculations for Move Tab items
    let visibleOrCollapsedTabs = gBrowser.tabs.filter(
      t => t.isOpen && !t.hidden
    );
    let allTabsSelected =
      visibleOrCollapsedTabs.length == 1 ||
      visibleOrCollapsedTabs.every(t => t.multiselected);
    contextMoveTabOptions.setAttribute("data-l10n-args", tabCountInfo);
    // Label: "Move Tabs" (classic) / "Move Tab to" (alt), or "Move Split View
    // to" when the context tab is part of a split view in the alt layout. The
    // label is context-dependent, so it can't use the static data-alt-l10n-id.
    let moveTabOptionsL10nId = "tab-context-move-tabs";
    if (this._altTabContextMenu) {
      moveTabOptionsL10nId = this.contextTab.splitview
        ? "tab-context-move-split-view"
        : "tab-context-move-tabs2";
    }
    contextMoveTabOptions.setAttribute("data-l10n-id", moveTabOptionsL10nId);
    // In the alt layout this submenu also holds New Group and Select All Tabs,
    // so it stays enabled even when the move-position items aren't actionable.
    contextMoveTabOptions.disabled =
      this.contextTab.hidden || (allTabsSelected && !this._altTabContextMenu);
    let selectedTabs = gBrowser.selectedTabs;
    let contextMoveTabToEnd = document.getElementById("context_moveToEnd");
    let allSelectedTabsAdjacent = selectedTabs.every(
      (element, index, array) => {
        return array.length > index + 1
          ? element.index + 1 == array[index + 1].index
          : true;
      }
    );

    let lastVisibleTab = visibleOrCollapsedTabs.at(-1);
    let lastTabToMove = this.contextTabs.at(-1);

    let isLastPinnedTab = false;
    if (lastTabToMove.pinned) {
      let sibling = gBrowser.tabContainer.findNextTab(lastTabToMove);
      isLastPinnedTab = !sibling || !sibling.pinned;
    }

    let isSplit = !!this.contextTab.splitview;
    let firstInSplit = isSplit ? this.contextTab.splitview.tabs[0] : null;
    let lastInSplit = isSplit ? this.contextTab.splitview.tabs.at(-1) : null;
    let splitAtEnd = isSplit && lastInSplit === lastVisibleTab;
    contextMoveTabToEnd.disabled =
      (lastTabToMove === lastVisibleTab || isLastPinnedTab || splitAtEnd) &&
      !lastTabToMove.group &&
      allSelectedTabsAdjacent;

    let contextMoveTabToStart = document.getElementById("context_moveToStart");
    let isFirstTab =
      !this.contextTabs[0].group &&
      (this.contextTabs[0] === visibleOrCollapsedTabs[0] ||
        this.contextTabs[0] ===
          visibleOrCollapsedTabs[gBrowser.pinnedTabCount]);
    let splitAtStart =
      isSplit &&
      (firstInSplit === visibleOrCollapsedTabs[0] ||
        firstInSplit === visibleOrCollapsedTabs[gBrowser.pinnedTabCount]);
    contextMoveTabToStart.disabled =
      (isFirstTab || splitAtStart) && allSelectedTabsAdjacent;

    document.getElementById("context_openTabInWindow").disabled =
      this.contextTab.hasAttribute("customizemode");

    // Only one of "Duplicate Tab"/"Duplicate Tabs" should be visible.
    document.getElementById("context_duplicateTab").hidden = this.multiselected;
    document.getElementById("context_duplicateTabs").hidden =
      !this.multiselected;

    let closeTabsToTheStartItem = document.getElementById(
      "context_closeTabsToTheStart"
    );

    // update context menu item strings for vertical tabs
    document.l10n.setAttributes(
      closeTabsToTheStartItem,
      gBrowser.tabContainer?.verticalMode
        ? "close-tabs-to-the-start-vertical"
        : "close-tabs-to-the-start"
    );

    let closeTabsToTheEndItem = document.getElementById(
      "context_closeTabsToTheEnd"
    );

    // update context menu item strings for vertical tabs
    document.l10n.setAttributes(
      closeTabsToTheEndItem,
      gBrowser.tabContainer?.verticalMode
        ? "close-tabs-to-the-end-vertical"
        : "close-tabs-to-the-end"
    );

    // Disable "Close Tabs to the Left/Right" if there are no tabs
    // preceding/following it.
    let noTabsToStart = !gBrowser._getTabsToTheStartFrom(this.contextTab)
      .length;
    closeTabsToTheStartItem.disabled = noTabsToStart;

    let noTabsToEnd = !gBrowser._getTabsToTheEndFrom(this.contextTab).length;
    closeTabsToTheEndItem.disabled = noTabsToEnd;

    // Disable "Close other Tabs" if there are no unpinned tabs.
    let unpinnedTabsToClose = this.multiselected
      ? gBrowser.openTabs.filter(
          t => !t.multiselected && !t.pinned && !t.hidden
        ).length
      : gBrowser.openTabs.filter(
          t => t != this.contextTab && !t.pinned && !t.hidden
        ).length;
    let closeOtherTabsItem = document.getElementById("context_closeOtherTabs");
    closeOtherTabsItem.disabled = unpinnedTabsToClose < 1;

    // Update the close item with how many tabs will close.
    document
      .getElementById("context_closeTab")
      .setAttribute("data-l10n-args", tabCountInfo);

    let closeDuplicateTabsItem = document.getElementById(
      "context_closeDuplicateTabs"
    );
    closeDuplicateTabsItem.disabled = !gBrowser.getDuplicateTabsToClose(
      this.contextTab
    ).length;

    // Disable "Close Multiple Tabs" if all sub menuitems are disabled. In the
    // alt layout Close Duplicates is one of those sub menuitems.
    document.getElementById("context_closeTabOptions").disabled =
      closeTabsToTheStartItem.disabled &&
      closeTabsToTheEndItem.disabled &&
      closeOtherTabsItem.disabled &&
      (!this._altTabContextMenu || closeDuplicateTabsItem.disabled);

    // Hide "Bookmark Tab…" for multiselection.
    // Update its state if visible.
    let bookmarkTab = document.getElementById("context_bookmarkTab");
    bookmarkTab.hidden = this.multiselected;

    // Show "Bookmark Selected Tabs" in a multiselect context and hide it otherwise.
    let bookmarkMultiSelectedTabs = document.getElementById(
      "context_bookmarkSelectedTabs"
    );
    bookmarkMultiSelectedTabs.hidden = !this.multiselected;

    let contentSharingShareTabs = document.getElementById(
      "context_shareSelectedTabs"
    );

    const hideContentSharing =
      !this.multiselected || !ContentSharingUtils.isEnabled;
    contentSharingShareTabs.hidden = hideContentSharing;
    document.getElementById("context_shareSelectedTabsSeparator").hidden =
      hideContentSharing;
    if (!contentSharingShareTabs.hidden) {
      this.addNewBadge(contentSharingShareTabs);
    }

    let toggleMute = document.getElementById("context_toggleMuteTab");
    let toggleMultiSelectMute = document.getElementById(
      "context_toggleMuteSelectedTabs"
    );

    // Only one of mute_unmute_tab/mute_unmute_selected_tabs should be visible
    toggleMute.hidden = this.multiselected;
    toggleMultiSelectMute.hidden = !this.multiselected;

    const isMuted = this.contextTab.hasAttribute("muted");
    if (this._altTabContextMenu) {
      document.l10n.setAttributes(
        toggleMute,
        isMuted
          ? "tabbrowser-context-unmute-tab2"
          : "tabbrowser-context-mute-tab2"
      );
    } else {
      document.l10n.setAttributes(
        toggleMute,
        isMuted
          ? "tabbrowser-context-unmute-tab"
          : "tabbrowser-context-mute-tab"
      );
    }
    document.l10n.setAttributes(
      toggleMultiSelectMute,
      isMuted
        ? "tabbrowser-context-unmute-selected-tabs"
        : "tabbrowser-context-mute-selected-tabs"
    );

    this.contextTab.toggleMuteMenuItem = toggleMute;
    this.contextTab.toggleMultiSelectMuteMenuItem = toggleMultiSelectMute;
    this._updateToggleMuteMenuItems(this.contextTab);

    let selectAllTabs = document.getElementById("context_selectAllTabs");
    selectAllTabs.disabled = gBrowser.allTabsSelected();

    gSync.updateTabContextMenu(aPopupMenu, this.contextTab);
    if (this._altTabContextMenu) {
      // gSync sets #context_sendTabToDevice's label dynamically (device / mobile
      // / count), so the static data-alt-l10n-id mechanism can't own it; apply
      // the alt wording here, after gSync.
      document
        .getElementById("context_sendTabToDevice")
        .setAttribute("data-l10n-id", "tab-context-send-to-device2");
    }

    let reopenInContainer = document.getElementById(
      "context_reopenInContainer"
    );
    reopenInContainer.hidden =
      !Services.prefs.getBoolPref("privacy.userContext.enabled", false) ||
      PrivateBrowsingUtils.isWindowPrivate(window);
    reopenInContainer.disabled = this.contextTab.hidden;

    // Anchor the share submenu where the active layout declares it: after
    // #context_moveTabOptions in the classic layout, or in the sending section
    // (after #context_shareSelectedTabs) in the alt layout. Matching the layout
    // keeps ensureShareMenu from re-inserting it (or duplicating it) elsewhere.
    SharingUtils.ensureShareMenu(
      this.contextTab.linkedBrowser,
      this.multiselected ? this.contextTabs.map(t => t.linkedBrowser) : null,
      document.getElementById(
        this._altTabContextMenu
          ? "context_shareSelectedTabs"
          : "context_moveTabOptions"
      )
    );
  },

  _createTabGroupMenuItem(group, isSaved) {
    let item = document.createXULElement("menuitem");
    item.setAttribute("tab-group-id", group.id);

    // Open groups have labels, and saved groups have names
    let label = group.label ?? group.name;
    if (label) {
      item.setAttribute("label", label);
    } else {
      document.l10n.setAttributes(item, "tab-context-unnamed-group");
    }

    item.classList.add("menuitem-iconic", "tab-group-icon");
    if (isSaved) {
      item.classList.add("tab-group-icon-closed");
    }

    for (let [property, value] of Object.entries(
      this.TabGroupMenu.colorStyles(group.color)
    )) {
      item.style.setProperty(property, value);
    }

    return item;
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "popuphidden":
        if (aEvent.target.id == "tabContextMenu") {
          this.contextTab.removeEventListener("TabAttrModified", this);
          this.contextTab = null;
          this.contextTabs = null;
        }
        break;
      case "TabAttrModified": {
        let tab = aEvent.target;
        this._updateToggleMuteMenuItems(tab, attr =>
          aEvent.detail.changed.includes(attr)
        );
        break;
      }
    }
  },

  createReopenInContainerMenu(event) {
    createUserContextMenu(event, {
      isContextMenu: true,
      excludeUserContextId: this.contextTab.getAttribute("usercontextid"),
      containerSource: "tab_context_menu",
    });
  },
  duplicateSelectedTabs() {
    let newIndex = this.contextTabs.at(-1).index + 1;
    for (let tab of this.contextTabs) {
      let newTab = SessionStore.duplicateTab(window, tab);
      if (tab.group) {
        Glean.tabgroup.tabInteractions.duplicate.add();
      }
      gBrowser.moveTabTo(newTab, { tabIndex: newIndex++ });
    }
  },
  reopenInContainer(event) {
    let userContextId = parseInt(
      event.target.getAttribute("data-usercontextid")
    );

    for (let tab of this.contextTabs) {
      if (tab.getAttribute("usercontextid") == userContextId) {
        continue;
      }

      /* Create a triggering principal that is able to load the new tab
         For content principals that are about: chrome: or resource: we need system to load them.
         Anything other than system principal needs to have the new userContextId.
      */
      let triggeringPrincipal;

      if (tab.linkedPanel) {
        triggeringPrincipal = tab.linkedBrowser.contentPrincipal;
      } else {
        // For lazy tab browsers, get the original principal
        // from SessionStore
        let tabState = JSON.parse(SessionStore.getTabState(tab));
        try {
          triggeringPrincipal = E10SUtils.deserializePrincipal(
            tabState.triggeringPrincipal_base64
          );
        } catch (ex) {
          continue;
        }
      }

      if (!triggeringPrincipal || triggeringPrincipal.isNullPrincipal) {
        // Ensure that we have a null principal if we couldn't
        // deserialize it (for lazy tab browsers) ...
        // This won't always work however is safe to use.
        triggeringPrincipal =
          Services.scriptSecurityManager.createNullPrincipal({ userContextId });
      } else if (triggeringPrincipal.isContentPrincipal) {
        triggeringPrincipal = Services.scriptSecurityManager.principalWithOA(
          triggeringPrincipal,
          {
            userContextId,
          }
        );
      }

      let newTab = gBrowser.addTab(tab.linkedBrowser.currentURI.spec, {
        userContextId,
        pinned: tab.pinned,
        tabIndex: tab.index + 1,
        triggeringPrincipal,
        eventDetail: { containerSource: "tab_context_menu" },
      });

      Glean.containers.tabAssignedContainer.record({
        from_container_id: tab.getAttribute("usercontextid"),
        to_container_id: userContextId,
      });

      if (gBrowser.selectedTab == tab) {
        gBrowser.selectedTab = newTab;
      }
      if (tab.muted && !newTab.muted) {
        newTab.toggleMuteAudio(tab.muteReason);
      }
    }
  },

  closeContextTabs() {
    if (this.contextTab.multiselected) {
      gBrowser.removeMultiSelectedTabs({
        metricsContext: gBrowser.TabMetrics.userTriggeredContext(
          gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
        ),
      });
    } else {
      gBrowser.removeTab(this.contextTab, {
        animate: true,
        metricsContext: gBrowser.TabMetrics.userTriggeredContext(
          gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
        ),
      });
    }
  },

  explicitUnloadTabs() {
    gBrowser.explicitUnloadTabs(this.contextTabs);
  },

  moveTabsToNewGroup() {
    this.TabGroupMenu.createGroupFromTabs(this.contextTabs, {
      anchorTab: this.contextTab,
      metricsContext: gBrowser.TabMetrics.userTriggeredContext(
        gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
      ),
    });
    gBrowser.selectedTab = this.contextTabs[0];

    // When using the tab context menu to create a group from the all tabs
    // panel, make sure we close that panel so that it doesn't obscure the tab
    // group creation panel.
    gTabsPanel.hideAllTabsPanel();
  },

  moveSplitViewToNewGroup() {
    let insertBefore = this.contextTab;
    if (insertBefore.index < gBrowser.pinnedTabCount) {
      insertBefore = gBrowser.tabs[gBrowser.pinnedTabCount];
    } else if (this.contextTab.group) {
      insertBefore = this.contextTab.group;
    } else if (this.contextTab.splitview) {
      insertBefore = this.contextTab.splitview;
    }
    let tabsAndSplitViews = [];
    for (const contextTab of this.contextTabs) {
      if (contextTab.splitView) {
        if (!tabsAndSplitViews.includes(contextTab.splitView)) {
          tabsAndSplitViews.push(contextTab.splitView);
        }
      } else {
        tabsAndSplitViews.push(contextTab);
      }
    }
    gBrowser.addTabGroup(tabsAndSplitViews, {
      insertBefore,
      metricsContext: gBrowser.TabMetrics.userTriggeredContext(
        gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
      ),
    });
    gBrowser.selectedTab = this.contextTabs[0];

    // When using the tab context menu to create a group from the all tabs
    // panel, make sure we close that panel so that it doesn't obscure the tab
    // group creation panel.
    gTabsPanel.hideAllTabsPanel();
  },

  /**
   * @param {MozTabbrowserTabGroup} group
   */
  moveTabsToGroup(group) {
    this.TabGroupMenu.addTabsToGroup(
      this.contextTabs,
      group,
      gBrowser.TabMetrics.userTriggeredContext(
        gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
      )
    );
  },

  addTabsToSavedGroup(groupId) {
    this.TabGroupMenu.addTabsToSavedGroup(
      this.contextTabs,
      groupId,
      gBrowser.TabMetrics.userTriggeredContext(
        gBrowser.TabMetrics.METRIC_SOURCE.TAB_MENU
      )
    );
  },

  ungroupTabsAndSplitViews() {
    this.TabGroupMenu.ungroupTabs(this.contextTabs);
  },

  moveTabsToSplitView() {
    let insertBefore = this.contextTabs.includes(gBrowser.selectedTab)
      ? gBrowser.selectedTab
      : this.contextTabs[0];
    let tabsToAdd = this.contextTabs;

    // Ensure selected tab is always first in split view
    const selectedTabIndex = tabsToAdd.indexOf(gBrowser.selectedTab);
    if (selectedTabIndex > -1 && selectedTabIndex != 0) {
      const [removed] = tabsToAdd.splice(selectedTabIndex, 1);
      tabsToAdd.unshift(removed);
    }

    let newTab = null;
    let trigger;
    if (this.contextTabs.length < 2) {
      // Open new tab to split with context tab
      newTab = gBrowser.addTrustedTab("about:opentabs");
      tabsToAdd = [this.contextTabs[0], newTab];
      trigger = "menu_add";
    } else {
      trigger = "menu_open";
    }

    gBrowser.addTabSplitView(tabsToAdd, {
      insertBefore,
      trigger,
    });

    if (newTab) {
      gBrowser.selectedTab = newTab;
    }
  },

  unsplitTabs() {
    const splitviews = new Set(
      this.contextTabs.map(tab => tab.splitview).filter(Boolean)
    );
    splitviews.forEach(splitview => splitview.unsplitTabs("menu_separate"));
  },

  reverseSplitView() {
    this.contextTab.splitview?.reverseTabs("menu");
  },

  /**
   * @param {MozMenuItem} menuItem
   */
  addNewBadge(menuItem) {
    menuItem.setAttribute(
      "badge",
      gBrowser.tabLocalization.formatValueSync("tab-context-badge-new")
    );
    menuItem.classList.add("badge-new");
  },

  /**
   * @param {MozMenuItem} menuItem
   */
  removeNewBadge(menuItem) {
    menuItem.removeAttribute("badge");
    menuItem.classList.remove("badge-new");
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  TabContextMenu,
  "_tabNotesEnabled",
  "browser.tabs.notes.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  TabContextMenu,
  "_unloadTabInContextMenu",
  "browser.tabs.unloadTabInContextMenu",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  TabContextMenu,
  "AITAB_ENABLED",
  "browser.smartwindow.aitab.enabled",
  false
);

ChromeUtils.defineESModuleGetters(TabContextMenu, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  GenAI: "moz-src:///browser/components/genai/GenAI.sys.mjs",
  MenuSectionLayout: "resource:///modules/MenuSectionLayout.sys.mjs",
  Tabbrowser: "moz-src:///browser/components/tabbrowser/Tabbrowser.sys.mjs",
  TabGroupMenu: "moz-src:///browser/components/tabbrowser/TabGroupMenu.sys.mjs",
  TabNotes: "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs",
  TabStateFlusher:
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
});

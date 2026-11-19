/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

internal object TabsTrayTestTag {
    const val TABS_TRAY = "tabstray"

    // Tabs Tray Banner
    const val BANNER_ROOT = "$TABS_TRAY.banner"
    const val BANNER_HANDLE = "$BANNER_ROOT.handle"
    const val NORMAL_TABS_PAGE_BUTTON = "$BANNER_ROOT.normalTabsPageButton"
    const val PRIVATE_TABS_PAGE_BUTTON = "$BANNER_ROOT.privateTabsPageButton"
    const val TAB_GROUPS_PAGE_BUTTON = "$BANNER_ROOT.tabGroupsPageButton"
    const val SYNCED_TABS_PAGE_BUTTON = "$BANNER_ROOT.syncedTabsPageButton"

    const val SELECTION_COUNTER = "$BANNER_ROOT.selectionCounter"
    const val SHARE_BUTTON = "$BANNER_ROOT.share"

    // Tabs Tray Banner three dot menu
    const val THREE_DOT_BUTTON = "$BANNER_ROOT.threeDotButton"

    const val ACCOUNT_SETTINGS = "$THREE_DOT_BUTTON.accountSettings"
    const val CLOSE_ALL_TABS = "$THREE_DOT_BUTTON.closeAllTabs"
    const val NEW_TAB_GROUP = "$THREE_DOT_BUTTON.newTabGroup"
    const val RECENTLY_CLOSED_TABS = "$THREE_DOT_BUTTON.recentlyClosedTabs"
    const val SELECT_TABS = "$THREE_DOT_BUTTON.selectTabs"
    const val SELECT_ALL_TABS = "$THREE_DOT_BUTTON.selectAllTabs"
    const val SHARE_ALL_TABS = "$THREE_DOT_BUTTON.shareAllTabs"
    const val TAB_SETTINGS = "$THREE_DOT_BUTTON.tabSettings"

    // FAB
    const val FAB = "$TABS_TRAY.fab"

    // Tab lists
    private const val TAB_LIST_ROOT = "$TABS_TRAY.tabList"
    const val NORMAL_TABS_LIST = "$TAB_LIST_ROOT.normal"
    const val PRIVATE_TABS_LIST = "$TAB_LIST_ROOT.private"
    const val TAB_GROUPS_LIST = "$TAB_LIST_ROOT.groups"
    const val SYNCED_TABS_LIST = "$TAB_LIST_ROOT.synced"

    const val EMPTY_NORMAL_TABS_LIST = "$NORMAL_TABS_LIST.empty"
    const val EMPTY_PRIVATE_TABS_LIST = "$PRIVATE_TABS_LIST.empty"
    const val EMPTY_TAB_GROUPS_LIST = "$TAB_GROUPS_LIST.empty"
    const val COLLECTIONS_MIGRATION_CARD = "$TAB_GROUPS_LIST.collectionsMigrationCard"
    const val UNAUTHENTICATED_SYNCED_TABS_PAGE = "$SYNCED_TABS_LIST.unauthenticated"

    // Tab items
    const val TAB_ITEM_ROOT = "$TABS_TRAY.tabItem"
    const val TAB_ITEM_CLOSE = "$TAB_ITEM_ROOT.close"
    const val TAB_ITEM_THUMBNAIL = "$TAB_ITEM_ROOT.thumbnail"
    const val TAB_ITEM_MEDIA_INDICATOR = "$TAB_ITEM_ROOT.mediaIndicator"
    const val TAB_GROUP_ONBOARDING_ITEM = "$TABS_TRAY.tabGroupOnboardingItem"
    const val TAB_GROUP_ONBOARDING_GRID_ITEM = "$TAB_GROUP_ONBOARDING_ITEM.grid"
    const val TAB_GROUP_ONBOARDING_LIST_ITEM = "$TAB_GROUP_ONBOARDING_ITEM.list"
    const val TAB_GROUP_ONBOARDING_ILLUSTRATION = "$TAB_GROUP_ONBOARDING_ITEM.illustration"
    const val TAB_GROUP_ONBOARDING_ITEM_DISMISS = "$TAB_GROUP_ONBOARDING_ITEM.dismiss"

    // Group Items
    const val TAB_GROUP_ROOT = "$TABS_TRAY.tabGroups"
    private const val TAB_GROUP_THUMBNAIL_ROOT = "$TAB_GROUP_ROOT.thumbnail"
    const val TAB_GROUP_THUMBNAIL_FIRST = "$TAB_GROUP_THUMBNAIL_ROOT.1"
    const val TAB_GROUP_THUMBNAIL_SECOND = "$TAB_GROUP_THUMBNAIL_ROOT.2"
    const val TAB_GROUP_THUMBNAIL_THIRD = "$TAB_GROUP_THUMBNAIL_ROOT.3"
    const val TAB_GROUP_THUMBNAIL_FOURTH = "$TAB_GROUP_THUMBNAIL_ROOT.4"
    const val TAB_GROUP_TITLE = "$TAB_GROUP_ROOT.title"

    // Bottom sheet group items
    const val TAB_GROUP_BOTTOM_SHEET_ROOT = "$TAB_GROUP_ROOT.bottomSheet"
    const val BOTTOM_SHEET_ADD_TAB_BUTTON = "$TAB_GROUP_BOTTOM_SHEET_ROOT.addTab"
    const val BOTTOM_SHEET_CIRCLE = "$TAB_GROUP_BOTTOM_SHEET_ROOT.circle"
    const val BOTTOM_SHEET_COLOR_LIST = "$TAB_GROUP_BOTTOM_SHEET_ROOT.colors"
    const val EDIT_BOTTOM_SHEET_SAVE = "$TAB_GROUP_BOTTOM_SHEET_ROOT.save"

    const val GROUP_NAME = "$TAB_GROUP_BOTTOM_SHEET_ROOT.name"
    const val ADD_TO_TAB_GROUP_ROOT = "$TAB_GROUP_BOTTOM_SHEET_ROOT.addToGroup"
    const val ADD_TO_NEW_TAB_GROUP = "$ADD_TO_TAB_GROUP_ROOT.newGroup"

    // Tab group three dot menu
    const val TAB_GROUP_THREE_DOT_BUTTON = "$TAB_GROUP_ROOT.threeDotButton"
    const val EDIT_TAB_GROUP = "$TAB_GROUP_THREE_DOT_BUTTON.editGroup"
    const val CLOSE_TAB_GROUP = "$TAB_GROUP_THREE_DOT_BUTTON.closeGroup"
    const val UNGROUP_TAB_GROUP = "$TAB_GROUP_THREE_DOT_BUTTON.ungroup"
    const val SHARE_TAB_GROUP = "$TAB_GROUP_THREE_DOT_BUTTON.shareGroup"
    const val DELETE_TAB_GROUP = "$TAB_GROUP_THREE_DOT_BUTTON.deleteGroup"

    // Bottom app bar
    const val TAB_SEARCH_ICON = "$TABS_TRAY.tabSearchIcon"

    // Tab Search
    const val TAB_SEARCH_ROOT = "$TABS_TRAY.tab_search"
    const val TAB_SEARCH_BACK_BUTTON = "$TAB_SEARCH_ROOT.back_button"

    // Tab grid
    const val TAB_GRID = "$TABS_TRAY.grid"

    // Tab list
    const val TAB_LIST = "$TABS_TRAY.list"
}

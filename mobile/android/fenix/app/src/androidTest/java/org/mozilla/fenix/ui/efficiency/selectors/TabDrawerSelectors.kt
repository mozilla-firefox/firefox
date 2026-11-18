/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.compose.base.R as composeBaseR
import mozilla.components.compose.base.snackbar.SNACKBAR_BUTTON_TEST_TAG
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getPluralStringResource
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag.CLOSE_TAB_GROUP
import org.mozilla.fenix.tabstray.TabsTrayTestTag.GROUP_NAME
import org.mozilla.fenix.trackingprotection.PROTECTION_STATUS_PILL_TEST_TAG
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object TabDrawerSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        EMPTY_NORMAL_BROWSING_TAB_DRAWER_VIEW,
        NORMAL_BROWSING_TAB_DRAWER_VIEW,
        TAB_DRAWER_BANNER_BUTTONS,
        TAB_DRAWER_UNAUTHENTICATED_SYNCED_TABS,
        EMPTY_TAB_GROUPS_TAB_DRAWER_VIEW,
        TAB_DRAWER_THREE_DOT_MAIN_MENU,
        TAB_SELECTION_VIEW,
        TAB_SELECTION_THREE_DOT_MAIN_MENU,
        CREATE_TAB_GROUP_VIEW,
        ADD_TO_TAB_GROUP_VIEW,
        TAB_GROUP_ITEM,
        TAB_GROUPS_MORE_OPTIONS_MENU,
        DELETE_TAB_GROUP_DIALOG,
        TAB_GROUP_BOTTOM_SHEET,
        EMPTY_PRIVATE_TABS_LIST,
        PRIVATE_TABS_LIST,
        TAB_ITEM,
        TAB_SEARCH_NO_RESULTS,
    }

    val TABS_TRAY =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.TABS_TRAY,
            description = "Tabs tray container",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val NORMAL_BROWSING_EMPTY_TABS_PAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.EMPTY_NORMAL_TABS_LIST,
            description = "Normal browsing empty tabs tray page",
            groups = setOf(Group.EMPTY_NORMAL_BROWSING_TAB_DRAWER_VIEW),
        )

    val NORMAL_BROWSING_TABS_PAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.NORMAL_TABS_LIST,
            description = "Normal browsing tabs tray page",
            groups = setOf(Group.NORMAL_BROWSING_TAB_DRAWER_VIEW),
        )

    val NORMAL_BROWSING_OPEN_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.NORMAL_TABS_PAGE_BUTTON,
            description = "Normal browsing tabs tray button",
            groups = setOf(Group.TAB_DRAWER_BANNER_BUTTONS),
        )

    val NORMAL_TABS_LIST =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.NORMAL_TABS_LIST,
            description = "Normal tabs list grid view",
        )

    val EMPTY_NORMAL_TABS_LIST =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.EMPTY_NORMAL_TABS_LIST,
            description = "Empty normal tabs list placeholder",
        )

    // "Tabs closed: N" base Compose snackbar; its message isn't on the UIAutomator text tree, so read it on Compose.
    fun TABS_CLOSED_SNACKBAR(count: Int = 2) =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.snackbar_num_tabs_closed, count.toString()),
            description = "Tabs closed snackbar for $count tabs",
        )

    // "Private tab closed" base Compose snackbar shown after closing a single private tab; read on Compose.
    val PRIVATE_TAB_CLOSED_SNACKBAR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.snackbar_private_tab_closed),
            description = "Private tab closed snackbar",
        )

    // The base snackbar's action button carries SNACKBAR_BUTTON_TEST_TAG, like the download snackbar's action button.
    val SNACKBAR_UNDO_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SNACKBAR_BUTTON_TEST_TAG,
            description = "Snackbar undo action button",
        )

    val TAB_ITEM_ROOT =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.TAB_ITEM_ROOT,
            description = "Tab item root",
        )

    val SYNCED_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.SYNCED_TABS_PAGE_BUTTON,
            description = "Synced tabs button",
            groups = setOf(Group.TAB_DRAWER_BANNER_BUTTONS),
        )

    val SIGN_IN_TO_SYNC_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.tab_manager_empty_synced_tabs_page_sign_in_cta),
            description = "Sign in to sync button",
            groups = setOf(Group.TAB_DRAWER_UNAUTHENTICATED_SYNCED_TABS),
        )

    val UNAUTHENTICATED_SYNCED_TABS_PAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.UNAUTHENTICATED_SYNCED_TABS_PAGE,
            description = "Unauthenticated synced tabs page",
            groups = setOf(Group.TAB_DRAWER_UNAUTHENTICATED_SYNCED_TABS),
        )

    val UNAUTHENTICATED_SYNCED_TABS_PAGE_HEADER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.tab_manager_empty_synced_tabs_page_header),
            description = "Unauthenticated synced tabs page header",
            groups = setOf(Group.TAB_DRAWER_UNAUTHENTICATED_SYNCED_TABS),
        )

    val UNAUTHENTICATED_SYNCED_TABS_PAGE_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.tab_manager_empty_synced_tabs_page_description),
            description = "Unauthenticated synced tabs page description",
            groups = setOf(Group.TAB_DRAWER_UNAUTHENTICATED_SYNCED_TABS),
        )

    val PRIVATE_TABS_PAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.PRIVATE_TABS_PAGE_BUTTON,
            description = "Private browsing tabs tray button",
            groups = setOf(Group.TAB_DRAWER_BANNER_BUTTONS),
        )

    val TAB_GROUPS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.TAB_GROUPS_PAGE_BUTTON,
            description = "Tab groups button",
            groups = setOf(Group.TAB_DRAWER_BANNER_BUTTONS),
        )

    val EMPTY_TAB_GROUP_PAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.EMPTY_TAB_GROUPS_LIST,
            description = "Empty tab groups page",
            groups = setOf(Group.EMPTY_TAB_GROUPS_TAB_DRAWER_VIEW),
        )

    val THREE_DOT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.THREE_DOT_BUTTON,
            description = "Three dot menu button",
        )

    val SELECT_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.SELECT_TABS,
            description = "Three dot menu select tabs button",
            groups = setOf(Group.TAB_DRAWER_THREE_DOT_MAIN_MENU),
        )
    val CLOSE_ALL_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.CLOSE_ALL_TABS,
            description = "Close all tabs menu button",
        )

    val RECENTLY_CLOSED_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.RECENTLY_CLOSED_TABS,
            description = "Recently closed tabs menu button",
        )

    val TAB_SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.TAB_SETTINGS,
            description = "Tab settings menu button",
        )

    // "Close all tabs" only arms a confirmation dialog; this is its confirm button. Skipping it leaves every
    // tab open, which silently invalidates any later "no tabs" or search-group assertion.
    val CLOSE_ALL_TABS_CONFIRM_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.tab_manager_close_all_tabs_dialog_confirm),
            description = "Close tabs confirmation button",
        )

    val SELECT_ALL_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.SELECT_ALL_TABS,
            description = "Three dot menu select all tabs button",
            groups = setOf(Group.TAB_DRAWER_THREE_DOT_MAIN_MENU),
        )

    val TAB_SELECTION_THREE_DOT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.THREE_DOT_BUTTON,
            description = "Tab selection view three dot menu button",
            groups = setOf(Group.TAB_SELECTION_VIEW),
        )

    val EXIT_MULTISELECT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.tab_tray_close_multiselect_content_description),
            description = "Exit multiselect mode button",
            groups = setOf(Group.TAB_SELECTION_VIEW),
        )

    val CLOSE_SELECTED_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.tab_manager_multiselect_menu_item_close_content_description),
            description = "Close selected tabs button",
            groups = setOf(Group.TAB_SELECTION_VIEW),
        )

    val ADD_TO_GROUP_THREE_DOT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.tab_manager_multiselect_menu_item_add_to_tab_group),
            description = "Tab selection view three dot menu add to group button",
            groups = setOf(Group.TAB_SELECTION_THREE_DOT_MAIN_MENU),
        )

    val CREATE_TAB_GROUP_NAME_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = GROUP_NAME,
            description = "Create tab group name text field",
            groups = setOf(Group.CREATE_TAB_GROUP_VIEW),
        )

    fun CREATE_TAB_GROUP_COLOR_BUTTON(color: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = color,
            description = "Create tab group color: $color button",
            groups = setOf(Group.CREATE_TAB_GROUP_VIEW),
        )

    val CREATE_TAB_GROUP_SAVE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.create_tab_group_save_button),
            description = "Create tab group save button",
            groups = setOf(Group.CREATE_TAB_GROUP_VIEW),
        )

    val ADD_TO_NEW_TAB_GROUP_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.ADD_TO_NEW_TAB_GROUP,
            description = "Add to new tab group button",
            groups = setOf(Group.ADD_TO_TAB_GROUP_VIEW),
        )

    fun ADD_TO_EXISTING_TAB_GROUP_BUTTON(title: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = title,
            description = "Add to existing tab group button",
            groups = setOf(Group.ADD_TO_TAB_GROUP_VIEW),
        )

    fun TAB_GROUP_ITEM(
        tabGroupTitle: String = "",
        numberOfTabs: Int = 1,
        tabGroupColor: String = "",
    ): Selector {
        val generatedDescription =
            getPluralStringResource(
                id = R.plurals.add_to_exiting_tab_group_content_description,
                quantity = numberOfTabs,
                tabGroupTitle,
                numberOfTabs,
                tabGroupColor,
            )

        return Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = generatedDescription,
            description = "Tab group with description: $generatedDescription",
            groups = setOf(Group.TAB_GROUP_ITEM),
        )
    }

    val TAB_GROUP_MORE_OPTIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "More options",
            description = "Tab group more options button",
            groups = setOf(Group.TAB_GROUP_ITEM),
        )

    val TAB_GROUP_MORE_OPTIONS_DELETE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Delete",
            description = "Tab group more options menu delete button",
            groups = setOf(Group.TAB_GROUPS_MORE_OPTIONS_MENU),
        )

    val TAB_GROUP_MORE_OPTIONS_CLOSE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = CLOSE_TAB_GROUP,
            description = "Tab group more options menu close button",
            groups = setOf(Group.TAB_GROUPS_MORE_OPTIONS_MENU),
        )

    val DELETE_TAB_GROUP_DIALOG_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.delete_tab_group_confirmation_dialog_title),
            description = "Delete tab group dialog title",
            groups = setOf(Group.DELETE_TAB_GROUP_DIALOG),
        )

    val DELETE_TAB_GROUP_DIALOG_MESSAGE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.delete_tab_group_confirmation_dialog_body),
            description = "Delete tab group dialog message",
            groups = setOf(Group.DELETE_TAB_GROUP_DIALOG),
        )

    val DELETE_TAB_GROUP_DIALOG_CANCEL_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.close_tab_and_delete_group_confirmation_dialog_cancel),
            description = "Delete tab group dialog cancel button",
            groups = setOf(Group.DELETE_TAB_GROUP_DIALOG),
        )

    val DELETE_TAB_GROUP_DIALOG_DELETE_GROUP_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.close_tab_and_delete_group_confirmation_dialog_confirm),
            description = "Delete tab group dialog delete group button",
            groups = setOf(Group.DELETE_TAB_GROUP_DIALOG),
        )

    val TAB_GROUP_BOTTOM_SHEET_HANDLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.tab_group_sheet_dismiss_description),
            description = "Tab group bottom sheet handle",
            groups = setOf(Group.TAB_GROUP_BOTTOM_SHEET),
        )

    val FAB =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.FAB,
            description = "Floating action button",
        )

    val EMPTY_PRIVATE_TABS_LIST =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.EMPTY_PRIVATE_TABS_LIST,
            description = "Empty private tabs list",
            groups = setOf(Group.EMPTY_PRIVATE_TABS_LIST),
        )

    val PRIVATE_TABS_LIST =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.PRIVATE_TABS_LIST,
            description = "Private tabs list",
            groups = setOf(Group.PRIVATE_TABS_LIST),
        )

    val TAB_ITEM_CLOSE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
            value = TabsTrayTestTag.TAB_ITEM_CLOSE,
            description = "Tab close button",
            groups = setOf(Group.TAB_ITEM),
        )

    val TAB_ITEM_THUMBNAIL =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
            value = TabsTrayTestTag.TAB_ITEM_THUMBNAIL,
            description = "Tab thumbnail",
            groups = setOf(Group.TAB_ITEM),
        )

    // "Playing media" indicator, present only while playbackState == PLAYING. Kept out of Group.TAB_ITEM:
    // being state-dependent, a group check would wrongly require it on every tab.
    val TAB_ITEM_MEDIA_INDICATOR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.TAB_ITEM_MEDIA_INDICATOR,
            description = "Tab item media playing indicator",
        )

    fun TAB_ITEM_WITH_TITLE(tabTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_WITH_CHILD_TEXT_ON_FIRST,
            value = TabsTrayTestTag.TAB_ITEM_ROOT,
            secondaryValue = tabTitle,
            description = "Tab with title: $tabTitle",
            groups = setOf(Group.TAB_ITEM),
        )

    // The "Share" item in the tab-selection three dot menu. It carries the SHARE_BUTTON tag (not
    // SHARE_ALL_TABS), same as the banner share button in the multiselect menu.
    val SELECT_TABS_SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.SHARE_BUTTON,
            description = "Tab selection view three dot menu share button",
            groups = setOf(Group.TAB_SELECTION_THREE_DOT_MAIN_MENU),
        )

    fun SELECTION_COUNTER(numberOfTabs: Int = 0) =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.tab_tray_multi_select_title, numberOfTabs),
            description = "Multi-selection counter: $numberOfTabs selected",
        )

    val TAB_SEARCH_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TabsTrayTestTag.TAB_SEARCH_ICON,
            description = "Open tab search button",
        )

    // Tab-manager footer "widget" (TrackersBlockedCard pill); tapping it opens the protections dashboard.
    val TRACKERS_BLOCKED_DASHBOARD_PILL =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = PROTECTION_STATUS_PILL_TEST_TAG,
            description = "Trackers blocked dashboard pill (tab manager footer widget)",
        )

    // The tab search input is a Material3 SearchBar InputField, which exposes no testTag; its default
    // "Search" content description is the only stable handle, matching how the legacy robot located it.
    val TAB_SEARCH_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Search",
            description = "Tab search input field",
        )

    val TAB_SEARCH_CLEAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(composeBaseR.string.text_field_cross_trailing_icon_default_content_description),
            description = "Clear tab search text button",
        )

    val TAB_SEARCH_NO_RESULTS_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.tab_manager_no_search_results),
            description = "Tab search no matches found message",
            groups = setOf(Group.TAB_SEARCH_NO_RESULTS),
        )

    val TAB_SEARCH_NO_RESULTS_SUBTITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.tab_manager_no_search_results_additional_text),
            description = "Tab search try another search message",
            groups = setOf(Group.TAB_SEARCH_NO_RESULTS),
        )

    // Tab search results render via FaviconListItem (title passed as a merged `label`), so the
    // tag+child-text TAB_ITEM_WITH_TITLE can't locate them the way it does grid tab items; match the
    // title text directly, as the legacy robot did with onNodeWithText.
    fun TAB_SEARCH_RESULT(tabTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = tabTitle,
            description = "Tab search result with title: $tabTitle",
        )
}

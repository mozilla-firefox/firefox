/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.BookmarksTestTag
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorId
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object BookmarksSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        EMPTY_BOOKMARKS_MENU_VIEW,
        BOOKMARKED_ITEM_THREE_DOT_MENU,
        BOOKMARK_FOLDER_THREE_DOT_MENU,
        EDIT_BOOKMARK_FOLDER,
        EDIT_BOOKMARKS_VIEW,
        BOOKMARKS_SORTING_OPTIONS,
    }

    val TOOLBAR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.BOOKMARK_TOOLBAR,
            description = "Bookmarks toolbar",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Bookmarks",
            description = "Bookmarks Toolbar Title",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
        )

    val SORT_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_sort_menu_content_desc),
            description = "Bookmarks sorting options button",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
        )

    val SORT_BY_CUSTOM_ORDER_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_sort_menu_custom),
            description = "Bookmarks sort by custom order button",
            groups = setOf(Group.BOOKMARKS_SORTING_OPTIONS),
        )

    val SORT_BY_NEWEST_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_sort_menu_newest),
            description = "Bookmarks sort by newest button",
            groups = setOf(Group.BOOKMARKS_SORTING_OPTIONS),
        )

    val SORT_BY_OLDEST_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_sort_menu_oldest),
            description = "Bookmarks sort by oldest button",
            groups = setOf(Group.BOOKMARKS_SORTING_OPTIONS),
        )

    val SORT_A_TO_Z_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_sort_menu_a_to_z),
            description = "Bookmarks sort by A to Z button",
            groups = setOf(Group.BOOKMARKS_SORTING_OPTIONS),
        )

    val SORT_Z_TO_A_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_sort_menu_z_to_a),
            description = "Bookmarks sort by Z to A button",
            groups = setOf(Group.BOOKMARKS_SORTING_OPTIONS),
        )

    val EMPTY_BOOKMARKS_LIST_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_empty_list_root_title),
            description = "Empty bookmarks list title",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
        )

    val IMPORT_BOOKMARKS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_import_bookmarks_button_content_description),
            description = "Import bookmarks from file button",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
        )

    val IMPORT_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_import_menu_button),
            description = "Import from file item in the bookmarks overflow menu",
        )

    val OPEN_IN_NEW_TAB_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_open_in_new_tab_button),
            description = "Open in new tab bookmarks three dot menu button",
            groups = setOf(Group.BOOKMARKED_ITEM_THREE_DOT_MENU),
        )

    val NAVIGATE_UP_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_navigate_back_button_content_description),
            description = "Bookmark edit navigate up button",
            groups = setOf(Group.EDIT_BOOKMARKS_VIEW, Group.EMPTY_BOOKMARKS_MENU_VIEW, Group.EDIT_BOOKMARK_FOLDER),
        )

    val EDIT_BOOKMARKS_TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.edit_bookmark_fragment_title),
            description = "Bookmark edit toolbar title",
            groups = setOf(Group.EDIT_BOOKMARKS_VIEW),
        )

    val EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD,
            description = "Bookmark edit title field",
            groups = setOf(Group.EDIT_BOOKMARKS_VIEW),
        )

    val EDIT_BOOKMARK_FOLDER_TITLE_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.EDIT_BOOKMARK_FOLDER_TITLE_TEXT_FIELD,
            description = "Bookmark edit folder title field",
            groups = setOf(Group.EDIT_BOOKMARK_FOLDER),
        )

    val EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD,
            description = "Bookmark edit URL field",
            groups = setOf(Group.EDIT_BOOKMARKS_VIEW),
        )

    val DELETE_BOOKMARK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_delete_bookmark_content_description),
            description = "Delete bookmark button",
            groups = setOf(Group.EDIT_BOOKMARKS_VIEW),
        )

    val DEFAULT_BOOKMARKS_FOLDER_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Bookmarks",
            description = "Default bookmarks folder title",
            groups = setOf(Group.EDIT_BOOKMARKS_VIEW),
        )

    val BOOKMARK_TITLE_TEXT =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Bookmark title",
            description = "Bookmark title text",
        )

    val SIGN_IN_TO_SYNC_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Sign in to sync",
            description = "Sign in to sync button",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
            id = SelectorId("SIGN_IN_TO_SYNC_BUTTON"),
        )

    val SIGN_IN_WITH_CAMERA_TEXT =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Sign in with your camera",
            description = "Sign in with your camera text",
            appearsAfter = setOf(SelectorId("SIGN_IN_TO_SYNC_BUTTON")),
        )

    val ADD_FOLDER_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_add_new_folder_button_content_description),
            description = "Add new bookmark folder button",
            groups = setOf(Group.EMPTY_BOOKMARKS_MENU_VIEW),
        )

    val ADD_FOLDER_NAME_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.ADD_BOOKMARK_FOLDER_NAME_TEXT_FIELD,
            description = "Add bookmark folder name text field",
        )

    val EDIT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_edit_button),
            description = "Edit bookmark button in three dot menu",
            groups = setOf(Group.BOOKMARKED_ITEM_THREE_DOT_MENU, Group.BOOKMARK_FOLDER_THREE_DOT_MENU),
        )

    val DELETE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_delete_button),
            description = "Delete bookmark button",
            groups = setOf(Group.BOOKMARKED_ITEM_THREE_DOT_MENU, Group.BOOKMARK_FOLDER_THREE_DOT_MENU),
        )

    val SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_share_button),
            description = "Share bookmark button in three dot menu",
            groups = setOf(Group.BOOKMARKED_ITEM_THREE_DOT_MENU),
        )

    val SELECT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_select_button),
            description = "Select bookmark button in three dot menu",
            groups = setOf(Group.BOOKMARK_FOLDER_THREE_DOT_MENU),
        )

    val MOVE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_move_button),
            description = "Move bookmark button in three dot menu",
            groups = setOf(Group.BOOKMARK_FOLDER_THREE_DOT_MENU),
        )

    val OPEN_ALL_IN_NEW_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_open_all_in_tabs_button),
            description = "Open all in new tabs button in three dot menu",
            groups = setOf(Group.BOOKMARK_FOLDER_THREE_DOT_MENU),
        )

    val OPEN_ALL_IN_PRIVATE_TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_open_all_in_private_tabs_button),
            description = "Open all in private tabs button in three dot menu",
            groups = setOf(Group.BOOKMARK_FOLDER_THREE_DOT_MENU),
        )

    val CANCEL_FOLDER_DELETION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_delete_negative),
            description = "Cancel folder deletion button",
        )

    val SEARCH_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_search_button_content_description),
            description = "Search bookmarks button",
        )

    val MULTI_SELECTION_THREE_DOT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.content_description_menu),
            description = "Multi-selection three dot button",
        )

    val DELETE_BOOKMARK_FOLDER_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_delete_folder_content_description),
            description = "Delete bookmark folder button",
            groups = setOf(Group.EDIT_BOOKMARK_FOLDER),
        )

    val EDIT_FOLDER_TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.edit_bookmark_folder_fragment_title),
            description = "Edit bookmark folder toolbar title",
            groups = setOf(Group.EDIT_BOOKMARK_FOLDER),
        )

    @Suppress("FunctionName")
    fun MULTI_SELECTION_COUNTER(count: Int = 0) =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "$count selected",
            description = "Multi-selection counter: $count selected",
        )

    @Suppress("FunctionName")
    fun ITEM_MENU(title: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Item Menu for $title",
            description = "Three dot menu button for bookmark item: $title",
        )

    @Suppress("FunctionName")
    fun BOOKMARK_ITEM_TITLE(title: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = title,
            description = "Bookmark item or folder with title: $title",
        )

    @Suppress("FunctionName")
    fun BOOKMARK_ITEM_URL(url: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = url,
            description = "Bookmark item or folder with URL: $url",
        )

    @Suppress("FunctionName")
    fun EXPAND_FOLDER_BUTTON(folderTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_select_folder_expand_folder_content_description, folderTitle),
            description = "Expand folder button for: $folderTitle",
        )
}

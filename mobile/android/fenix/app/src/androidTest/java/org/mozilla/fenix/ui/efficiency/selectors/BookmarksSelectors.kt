/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.BookmarksTestTag
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object BookmarksSelectors {
    val TOOLBAR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.BOOKMARK_TOOLBAR,
            description = "Bookmarks toolbar",
            groups = listOf("emptyBookmarksMenuView"),
        )

    val TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Bookmarks",
            description = "Bookmarks Toolbar Title",
            groups = listOf("requiredForPage", "emptyBookmarksMenuView"),
        )

    val SORT_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_sort_menu_content_desc),
            description = "Bookmarks sorting options button",
            groups = listOf("emptyBookmarksMenuView"),
        )

    val EMPTY_BOOKMARKS_LIST_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_empty_list_root_title),
            description = "Empty bookmarks list title",
            groups = listOf("emptyBookmarksMenuView"),
        )

    val IMPORT_BOOKMARKS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_import_bookmarks_button_content_description),
            description = "Import bookmarks from file button",
            groups = listOf("emptyBookmarksMenuView"),
        )

    val IMPORT_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_import_menu_button),
            description = "Import from file item in the bookmarks overflow menu",
            groups = listOf(),
        )

    val OPEN_IN_NEW_TAB_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_open_in_new_tab_button),
            description = "Open in new tab bookmarks three dot menu button",
            groups = listOf("bookmarksThreeDotMenu"),
        )

    val NAVIGATE_UP_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_navigate_back_button_content_description),
            description = "Bookmark edit navigate up button",
            groups = listOf("editBookmarksView", "emptyBookmarksMenuView"),
        )

    val EDIT_BOOKMARKS_TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.edit_bookmark_fragment_title),
            description = "Bookmark edit toolbar title",
            groups = listOf("editBookmarksView"),
        )

    val EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD,
            description = "Bookmark edit title field",
            groups = listOf("editBookmarksView"),
        )

    val EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD,
            description = "Bookmark edit URL field",
            groups = listOf("editBookmarksView"),
        )

    val DELETE_BOOKMARK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_delete_bookmark_content_description),
            description = "Delete bookmark button",
            groups = listOf("editBookmarksView"),
        )

    val DEFAULT_BOOKMARKS_FOLDER_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Bookmarks",
            description = "Default bookmarks folder title",
            groups = listOf("editBookmarksView"),
        )

    val BOOKMARK_TITLE_TEXT =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Bookmark title",
            description = "Bookmark title text",
            groups = listOf(),
        )

    val SIGN_IN_TO_SYNC_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "Sign in to sync",
            description = "Sign in to sync button",
            groups = listOf("emptyBookmarksMenuView"),
            name = "SIGN_IN_TO_SYNC_BUTTON",
        )

    val SIGN_IN_WITH_CAMERA_TEXT =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Sign in with your camera",
            description = "Sign in with your camera text",
            groups = listOf("resultOf:SIGN_IN_TO_SYNC_BUTTON"),
        )

    val ADD_FOLDER_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_add_new_folder_button_content_description),
            description = "Add new bookmark folder button",
            groups = listOf("emptyBookmarksMenuView"),
        )

    val ADD_FOLDER_NAME_TEXT_FIELD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = BookmarksTestTag.ADD_BOOKMARK_FOLDER_NAME_TEXT_FIELD,
            description = "Add bookmark folder name text field",
            groups = listOf(),
        )

    val EDIT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_edit_button),
            description = "Edit bookmark button in three dot menu",
            groups = listOf("bookmarksThreeDotMenu"),
        )

    val DELETE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_delete_button),
            description = "Delete bookmark button",
            groups = listOf("bookmarksThreeDotMenu"),
        )

    val SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_menu_share_button),
            description = "Share bookmark button in three dot menu",
            groups = listOf("bookmarksThreeDotMenu"),
        )

    val CANCEL_FOLDER_DELETION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.bookmark_delete_negative),
            description = "Cancel folder deletion button",
            groups = listOf(),
        )

    val SEARCH_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_search_button_content_description),
            description = "Search bookmarks button",
            groups = listOf(),
        )

    val MULTI_SELECTION_THREE_DOT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.content_description_menu),
            description = "Multi-selection three dot button",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun MULTI_SELECTION_COUNTER(count: Int = 0) =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = "$count selected",
            description = "Multi-selection counter: $count selected",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun ITEM_MENU(title: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Item Menu for $title",
            description = "Three dot menu button for bookmark item: $title",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun BOOKMARK_ITEM(title: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = title,
            description = "Bookmark item or folder with title: $title",
            groups = listOf(),
        )

    @Suppress("FunctionName")
    fun EXPAND_FOLDER_BUTTON(folderTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.bookmark_select_folder_expand_folder_content_description, folderTitle),
            description = "Expand folder button for: $folderTitle",
            groups = listOf(),
        )

    val all =
        listOf(
            TOOLBAR,
            TOOLBAR_TITLE,
            SORT_MENU_BUTTON,
            EMPTY_BOOKMARKS_LIST_TITLE,
            IMPORT_BOOKMARKS_BUTTON,
            IMPORT_MENU_BUTTON,
            OPEN_IN_NEW_TAB_BUTTON,
            NAVIGATE_UP_BUTTON,
            EDIT_BOOKMARKS_TOOLBAR_TITLE,
            EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD,
            EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD,
            DELETE_BOOKMARK_BUTTON,
            DEFAULT_BOOKMARKS_FOLDER_TITLE,
            BOOKMARK_TITLE_TEXT,
            SIGN_IN_TO_SYNC_BUTTON,
            SIGN_IN_WITH_CAMERA_TEXT,
            ADD_FOLDER_BUTTON,
            ADD_FOLDER_NAME_TEXT_FIELD,
            EDIT_BUTTON,
            DELETE_BUTTON,
            SHARE_BUTTON,
            CANCEL_FOLDER_DELETION_BUTTON,
            SEARCH_BUTTON,
            MULTI_SELECTION_THREE_DOT_BUTTON,
            MULTI_SELECTION_COUNTER(),
            ITEM_MENU(),
            BOOKMARK_ITEM(),
            EXPAND_FOLDER_BUTTON(),
        )
}

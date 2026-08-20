/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.BookmarksSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors

class BookmarksPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "BookmarksPage"

    init {
        NavigationRegistry.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.BOOKMARKS_BUTTON),
                ),
        )

        NavigationRegistry.register(
            from = "MainMenuPage",
            to = pageName,
            steps = listOf(NavigationStep.Click(MainMenuSelectors.BOOKMARKS_BUTTON)),
        )

        NavigationRegistry.register(
            from = pageName,
            to = "BookmarkSearchPage",
            steps =
                listOf(
                    /* TODO: the search button only renders when at least one bookmark exists, so we add
                    one here as a navigation precondition. This adds a hidden "Mozilla" bookmark in any
                    test navigating through this path — safe as long as no test searches for "moz", "org", etc. */
                    NavigationStep.Action { createBookmarkItem("https://www.mozilla.org", "Mozilla", null) },
                    NavigationStep.Click(BookmarksSelectors.SEARCH_BUTTON),
                ),
        )
    }

    override fun navigateToPage(url: String, forceNavigation: Boolean): BookmarksPage {
        super.navigateToPage(url, forceNavigation)
        return this
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return BookmarksSelectors.all.filter { it.groups.contains(group) }
    }

    fun createFolder(name: String): BookmarksPage {
        mozClick(BookmarksSelectors.ADD_FOLDER_BUTTON)
        mozClearAndEnterText(name, BookmarksSelectors.ADD_FOLDER_NAME_TEXT_FIELD)
        mozClick(BookmarksSelectors.NAVIGATE_UP_BUTTON)
        return this
    }

    fun openItemMenu(title: String): BookmarksPage {
        mozClick(BookmarksSelectors.ITEM_MENU(title))
        return this
    }

    fun importBookmarksFromFile(): BookmarksPage {
        mozClick(BookmarksSelectors.IMPORT_BOOKMARKS_BUTTON)
        mozClick(BookmarksSelectors.IMPORT_MENU_BUTTON)
        return this
    }

    fun setParentFolder(folderName: String): BookmarksPage {
        mozClick(BookmarksSelectors.DEFAULT_BOOKMARKS_FOLDER_TITLE)
        mozClick(BookmarksSelectors.EXPAND_FOLDER_BUTTON("Bookmarks"))
        mozClick(BookmarksSelectors.BOOKMARK_ITEM(folderName))
        mozClick(BookmarksSelectors.NAVIGATE_UP_BUTTON)
        return this
    }

    fun saveEditBookmark(): BookmarksPage {
        mozClick(BookmarksSelectors.NAVIGATE_UP_BUTTON)
        return this
    }

    fun longClickBookmarkedItem(title: String): BookmarksPage {
        mozLongClick(BookmarksSelectors.BOOKMARK_ITEM(title))
        return this
    }

    fun selectBookmarkedItem(title: String): BookmarksPage {
        mozClick(BookmarksSelectors.BOOKMARK_ITEM(title))
        return this
    }

    fun verifyMultiSelectionCounter(count: Int): BookmarksPage {
        mozVerify(BookmarksSelectors.MULTI_SELECTION_COUNTER(count))
        return this
    }

    fun clickMultiSelectThreeDotButton(): BookmarksPage {
        mozClick(BookmarksSelectors.MULTI_SELECTION_THREE_DOT_BUTTON)
        return this
    }

    fun verifyBookmarkTitle(title: String): BookmarksPage {
        mozVerify(BookmarksSelectors.BOOKMARK_ITEM(title))
        return this
    }
}

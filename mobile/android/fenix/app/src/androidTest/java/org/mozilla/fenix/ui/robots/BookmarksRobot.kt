package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasAnyDescendant
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onChildAt
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextClearance
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performTouchInput
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.BookmarksTestTag.ADD_BOOKMARK_FOLDER_NAME_TEXT_FIELD
import org.mozilla.fenix.bookmarks.BookmarksTestTag.BOOKMARK_TOOLBAR
import org.mozilla.fenix.bookmarks.BookmarksTestTag.EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD
import org.mozilla.fenix.bookmarks.BookmarksTestTag.EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD
import org.mozilla.fenix.helpers.Constants.LONG_CLICK_DURATION
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime

class BookmarksRobot(private val composeTestRule: ComposeTestRule) {

    @OptIn(ExperimentalTestApi::class)
    fun verifyEmptyBookmarksMenuView() {
        Log.i(TAG, "verifyBookmarksMenuView: Waiting for bookmarks toolbar elements to exist.")
        composeTestRule.waitUntilAtLeastOneExists(
            hasTestTag(BOOKMARK_TOOLBAR)
                .and(hasAnyDescendant(hasText("Bookmarks")))
                .and(hasAnyDescendant(hasContentDescription("Navigate back")))
                .and(hasAnyDescendant(hasContentDescription(getStringResource(R.string.bookmark_sort_menu_content_desc))))
                .and(hasAnyDescendant(hasContentDescription(getStringResource(R.string.bookmark_add_new_folder_button_content_description)))),
        )
        Log.i(TAG, "verifyBookmarksMenuView: Waited for bookmarks toolbar elements to exist.")
        Log.i(TAG, "verifyBookmarksMenuView: Trying to verify the empty bookmarks list is displayed.")
        composeTestRule.onNodeWithText(
            getStringResource(R.string.bookmark_empty_list_root_title),
        ).assertIsDisplayed()
        Log.i(TAG, "verifyBookmarksMenuView: Verified the empty bookmarks list is displayed.")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyBookmarkedURL(url: String) {
        Log.i(TAG, "verifyBookmarkedURL: Waiting for $waitingTime ms for bookmarked url: $url to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText(url), waitingTime)
        Log.i(TAG, "verifyBookmarkedURL: Waited for $waitingTime ms for bookmarked url: $url to exist")
        Log.i(TAG, "verifyBookmarkedURL: Trying to verify bookmarks url: $url is displayed")
        composeTestRule.onNodeWithText(url).assertIsDisplayed()
        Log.i(TAG, "verifyBookmarkedURL: Verified bookmarks url: $url is displayed")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyFolderTitle(title: String) {
        Log.i(TAG, "verifyFolderTitle: Waiting for $waitingTime for bookmark folder with title: $title to exist.")
        composeTestRule.waitUntilAtLeastOneExists(hasText(title), waitingTime)
        Log.i(TAG, "verifyFolderTitle: Waited for $waitingTime for bookmark folder with title: $title to exist.")
        Log.i(TAG, "verifyFolderTitle: Trying to verify bookmarks folder with title: $title is displayed")
        composeTestRule.onNodeWithText(title).assertIsDisplayed()
        Log.i(TAG, "verifyFolderTitle: Verified bookmarks folder with title: $title is displayed")
    }

    fun verifyBookmarkFolderDescription(numberOfBookmarksInFolder: String) {
        Log.i(TAG, "verifyBookmarkFolderDescription: Trying to verify the number of bookmarks in folder: $numberOfBookmarksInFolder is displayed")
        composeTestRule.onNodeWithText(text = getStringResource(R.string.bookmarks_folder_description, numberOfBookmarksInFolder)).assertIsDisplayed()
        Log.i(TAG, "verifyBookmarkFolderDescription: Verified the number of bookmarks in folder: $numberOfBookmarksInFolder is displayed")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyBookmarkTitle(title: String) {
        Log.i(TAG, "verifyBookmarkTitle: Waiting for $waitingTime for bookmark with title: $title to exist.")
        composeTestRule.waitUntilExactlyOneExists(
            hasText(title),
            waitingTime,
        )
        Log.i(TAG, "verifyBookmarkTitle: Waited for $waitingTime for bookmark with title: $title to exist.")
        Log.i(TAG, "verifyBookmarkTitle: Waited for bookmarks header to exist.")
        Log.i(TAG, "verifyBookmarkTitle: Trying to verify bookmark with title: $title is displayed")
        composeTestRule.onNodeWithText(title).assertIsDisplayed()
        Log.i(TAG, "verifyBookmarkTitle: Verified bookmark with title: $title is displayed")
    }

    fun verifyBookmarkIsDeleted(expectedTitle: String) {
        Log.i(TAG, "verifyBookmarkIsDeleted: Trying to verify that the bookmarked item : $expectedTitle is not displayed")
        composeTestRule.onNodeWithText(expectedTitle).assertIsNotDisplayed()
        Log.i(TAG, "verifyBookmarkIsDeleted: Verified that the bookmarked item : $expectedTitle is not displayed")
    }

    fun verifyEditBookmarksView() {
        Log.i(TAG, "verifyEditBookmarksView: Trying to verify that the edit bookmark view items are displayed")
        composeTestRule.onNodeWithContentDescription(getStringResource(R.string.bookmark_navigate_back_button_content_description))
            .assertIsDisplayed()
        composeTestRule.onNodeWithText(getStringResource(R.string.edit_bookmark_fragment_title))
            .assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription(getStringResource(R.string.bookmark_delete_bookmark_content_description))
            .assertIsDisplayed()
        composeTestRule.onNodeWithTag(EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD)
            .assertIsDisplayed()
        composeTestRule.onNodeWithTag(EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD)
            .assertIsDisplayed()
        composeTestRule.onNodeWithText("Bookmarks").assertIsDisplayed()
        Log.i(TAG, "verifyEditBookmarksView: Verified that the edit bookmark view items are displayed")
    }

    fun clickDeleteBookmarkButtonInEditMode() {
        Log.i(TAG, "clickDeleteBookmark: Trying to click delete bookmark button in edit mode.")
        composeTestRule.onNodeWithContentDescription(getStringResource(R.string.bookmark_delete_bookmark_content_description))
            .performClick()
        Log.i(TAG, "clickDeleteBookmark: Clicked delete bookmark button in edit mode")
    }

    fun verifyAddFolderView() {
        Log.i(TAG, "verifyAddFolderView: Trying to verify that the folder name title field is displayed")
        composeTestRule.addFolderTitleField().assertIsDisplayed()
        Log.i(TAG, "verifyAddFolderView: Verified that the folder name title field is displayed")
        Log.i(TAG, "verifyAddFolderView: Trying to verify that the bookmark folder selector is displayed")
        composeTestRule.bookmarkFolderSelector().assertIsDisplayed()
        Log.i(TAG, "verifyAddFolderView: Verified that the bookmark folder selector is displayed")
    }

    fun cancelFolderDeletion() {
        Log.i(TAG, "cancelFolderDeletion: Trying to click \"Cancel\" bookmarks folder deletion dialog button")
        composeTestRule.onNodeWithText(getStringResource(R.string.bookmark_delete_negative)).performClick()
        Log.i(TAG, "cancelFolderDeletion: Clicked \"Cancel\" bookmarks folder deletion dialog button")
    }

    fun createFolder(name: String, parent: String? = null) {
        clickAddFolderButton()
        addNewFolderName(name)
        if (!parent.isNullOrBlank()) {
            setParentFolder(parent)
        }
        saveNewFolder()
    }

    fun setParentFolder(parentName: String) {
        clickParentFolderSelector()
        selectFolder(parentName)
        navigateUp()
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickAddFolderButton() {
        Log.i(TAG, "clickAddFolderButton: Waiting for $waitingTime for the add bookmarks folder button to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(getStringResource(R.string.bookmark_add_new_folder_button_content_description)), waitingTime)
        Log.i(TAG, "clickAddFolderButton: Waited for $waitingTime for the add bookmarks folder button to exist")
        Log.i(TAG, "clickAddFolderButton: Trying to click add bookmarks folder button")
        composeTestRule.addFolderButton().performClick()
        Log.i(TAG, "clickAddFolderButton: Clicked add bookmarks folder button")
        composeTestRule.waitForIdle()
    }

    fun clickSelectFolderNewFolderButton() {
        Log.i(TAG, "clickSelectFolderNewFolderButton: Trying to click the select new folder button")
        composeTestRule.selectFolderNewFolderButton().performClick()
        Log.i(TAG, "clickSelectFolderNewFolderButton: Clicked the select new folder button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun addNewFolderName(name: String) {
        Log.i(TAG, "addNewFolderName: Waiting for $waitingTime for the folder name text field to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADD_BOOKMARK_FOLDER_NAME_TEXT_FIELD), waitingTime)
        Log.i(TAG, "addNewFolderName: Waited for $waitingTime for the folder name text field to exist")
        composeTestRule.waitForIdle()
        Log.i(TAG, "addNewFolderName: Trying to set bookmarks folder name to: $name")
        composeTestRule.addFolderTitleField().performTextReplacement(name)
        Log.i(TAG, "addNewFolderName: Bookmarks folder name was set to: $name")
        composeTestRule.waitForIdle()
    }

    @OptIn(ExperimentalTestApi::class)
    fun saveNewFolder() {
        Log.i(TAG, "saveNewFolder: Waiting for $waitingTime for the navigate up toolbar button to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(getStringResource(R.string.bookmark_navigate_back_button_content_description)), waitingTime)
        Log.i(TAG, "saveNewFolder: Waited for $waitingTime for the navigate up toolbar button to exist")
        Log.i(TAG, "saveNewFolder: Trying to click navigate up toolbar button")
        composeTestRule.navigateUpButton().performClick()
        Log.i(TAG, "saveNewFolder: Clicked the navigate up toolbar button")
        composeTestRule.waitForIdle()
    }

    fun navigateUp() {
        Log.i(TAG, "navigateUp: Trying to click navigate up toolbar button")
        composeTestRule.navigateUpButton().performClick()
        Log.i(TAG, "navigateUp: Clicked navigate up toolbar button")
        composeTestRule.waitForIdle()
    }

    fun changeBookmarkTitle(newTitle: String) {
        Log.i(TAG, "changeBookmarkTitle: Trying to clear bookmark name text box")
        composeTestRule.bookmarkNameEditBox().performTextClearance()
        Log.i(TAG, "changeBookmarkTitle: Cleared bookmark name text box")
        Log.i(TAG, "changeBookmarkTitle: Trying to set bookmark title to: $newTitle")
        composeTestRule.bookmarkNameEditBox().performTextInput(newTitle)
        Log.i(TAG, "changeBookmarkTitle: Bookmark title was set to: $newTitle")
    }

    fun changeBookmarkUrl(newUrl: String) {
        Log.i(TAG, "changeBookmarkUrl: Trying to clear bookmark url text box")
        composeTestRule.bookmarkURLEditBox().performTextClearance()
        Log.i(TAG, "changeBookmarkUrl: Cleared bookmark url text box")
        Log.i(TAG, "changeBookmarkUrl: Trying to set bookmark url to: $newUrl")
        composeTestRule.bookmarkURLEditBox().performTextInput(newUrl)
        Log.i(TAG, "changeBookmarkUrl: Bookmark url was set to: $newUrl")
    }

    fun saveEditBookmark() {
        Log.i(TAG, "saveEditBookmark: Trying to click navigate up toolbar button")
        composeTestRule.navigateUpButton().performClick()
        Log.i(TAG, "saveEditBookmark: Clicked navigate up toolbar button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickParentFolderSelector() {
        Log.i(TAG, "clickParentFolderSelector: Trying to click folder selector")
        composeTestRule.bookmarkFolderSelector().performClick()
        Log.i(TAG, "clickParentFolderSelector: Clicked folder selector")
        Log.i(TAG, "clickParentFolderSelector: Waiting for the Bookmarks folder to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText("Bookmarks"), waitingTime)
        Log.i(TAG, "clickParentFolderSelector: Waited for the Bookmarks folder to exist")
    }

    @OptIn(ExperimentalTestApi::class)
    fun expandSelectableFolder(title: String) {
        Log.i(TAG, "expandSelectableFolder: Waiting for $waitingTime for expand button for folder: $title to exist")
        composeTestRule.waitUntilAtLeastOneExists(
            hasContentDescription(getStringResource(R.string.bookmark_select_folder_expand_folder_content_description, title)),
            waitingTime,
        )
        Log.i(TAG, "expandSelectableFolder: Waited for $waitingTime for expand button for folder: $title to exist")
        Log.i(TAG, "expandSelectableFolder: Trying to click expand select folder selector")
        composeTestRule.expandBookmarkFolderSelector(title).performClick()
        Log.i(TAG, "expandSelectableFolder: Clicked expand select folder selector")
        composeTestRule.waitForIdle()
    }

    @OptIn(ExperimentalTestApi::class)
    fun selectFolder(title: String) {
        Log.i(TAG, "selectFolder: Waiting for $waitingTime for folder with title: $title to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText(title), waitingTime)
        Log.i(TAG, "selectFolder: Waited for $waitingTime for folder with title: $title to exist")
        Log.i(TAG, "selectFolder: Trying to click folder with title: $title")
        composeTestRule.onNodeWithText(title).performClick()
        Log.i(TAG, "selectFolder: Clicked folder with title: $title")
        composeTestRule.waitForIdle()
    }

    @OptIn(ExperimentalTestApi::class)
    fun longClickBookmarkedItem(title: String) {
        Log.i(TAG, "longClickBookmarkedItem: Waiting for $waitingTime for bookmark with title: $title to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText(title), waitingTime)
        Log.i(TAG, "longClickBookmarkedItem: Waited for $waitingTime for bookmark with title: $title to exist")
        Log.i(TAG, "longClickBookmarkedItem: Trying to long click bookmark with title: $title")
        composeTestRule.onNodeWithText(title).performTouchInput { longClick(durationMillis = LONG_CLICK_DURATION) }
        Log.i(TAG, "longClickBookmarkedItem: Long clicked bookmark with title: $title")
    }

    fun selectBookmarkedItem(title: String) {
        Log.i(TAG, "selectBookmarkedItem: Trying to click and select bookmark with title: $title")
        composeTestRule.onNodeWithText(title).performClick()
        Log.i(TAG, "selectBookmarkedItem: Clicked and selected bookmark with title: $title")
    }

    fun confirmDeletion() {
        Log.i(TAG, "confirmDeletion: Trying to click \"Delete\" bookmarks deletion button")
        composeTestRule.onNodeWithText("Delete").performClick()
        Log.i(TAG, "confirmDeletion: Clicked \"Delete\" bookmarks deletion button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun waitForBookmarksSnackBarToBeGone(snackbarText: String) {
        Log.i(TAG, "waitForBookmarksSnackBarToBeGone: Waiting for $waitingTime for snackbar: $snackbarText to be gone")
        composeTestRule.waitUntilDoesNotExist(hasText(snackbarText), waitingTime)
        Log.i(TAG, "waitForBookmarksSnackBarToBeGone: Waited for $waitingTime for snackbar: $snackbarText to be gone")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        @OptIn(ExperimentalTestApi::class)
        fun openThreeDotMenu(bookmarkedItem: String, interact: ThreeDotMenuBookmarksRobot.() -> Unit): ThreeDotMenuBookmarksRobot.Transition {
            Log.i(TAG, "openThreeDotMenu: Waiting for $waitingTime for the bookmarked item $bookmarkedItem three dot button to exist")
            composeTestRule.waitUntilAtLeastOneExists(hasContentDescription("Item Menu for $bookmarkedItem"), waitingTime)
            Log.i(TAG, "openThreeDotMenu: Waited for $waitingTime for the bookmarked item $bookmarkedItem three dot button to exist")
            Log.i(TAG, "openThreeDotMenu: Trying to click three dot button for bookmark item: $bookmarkedItem")
            composeTestRule.threeDotMenuButton(bookmarkedItem).performClick()
            Log.i(TAG, "openThreeDotMenu: Clicked three dot button for bookmark item: $bookmarkedItem")

            ThreeDotMenuBookmarksRobot().interact()
            return ThreeDotMenuBookmarksRobot.Transition(composeTestRule)
        }

        fun openBookmarkWithTitle(bookmarkTitle: String, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openBookmarkWithTitle: Trying to click bookmark with title: $bookmarkTitle")
            composeTestRule.onNodeWithText(bookmarkTitle).performClick()
            Log.i(TAG, "openBookmarkWithTitle: Clicked bookmark with title: $bookmarkTitle")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickSearchButton(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "clickSearchButton: Waiting for the search bookmarks button to exist")
            composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(getStringResource(R.string.bookmark_search_button_content_description)), waitingTime)
            Log.i(TAG, "clickSearchButton: Waited for the search bookmarks button to exist")
            Log.i(TAG, "clickSearchButton: Trying to click search bookmarks button")
            composeTestRule.onNodeWithContentDescription(getStringResource(R.string.bookmark_search_button_content_description)).performClick()
            Log.i(TAG, "clickSearchButton: Clicked search bookmarks button")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun goBackToBrowserScreen(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goBackToBrowserScreen: Trying to click go back button")
            composeTestRule.onNodeWithContentDescription(getStringResource(R.string.bookmark_navigate_back_button_content_description)).performClick()
            Log.i(TAG, "goBackToBrowserScreen: Clicked go back button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click go back button")
            composeTestRule.onNodeWithContentDescription(getStringResource(R.string.bookmark_navigate_back_button_content_description)).performClick()
            Log.i(TAG, "goBackToHomeScreen: Clicked go back button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }
    }
}

fun composeBookmarksMenu(composeTestRule: ComposeTestRule, interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
    BookmarksRobot(composeTestRule).interact()
    return BookmarksRobot.Transition(composeTestRule)
}

private fun ComposeTestRule.addFolderButton() =
    onNodeWithContentDescription(getStringResource(R.string.bookmark_add_new_folder_button_content_description))

private fun ComposeTestRule.addFolderTitleField() =
    onNodeWithTag(ADD_BOOKMARK_FOLDER_NAME_TEXT_FIELD)

private fun ComposeTestRule.navigateUpButton() =
    onNodeWithContentDescription(getStringResource(R.string.bookmark_navigate_back_button_content_description))

private fun ComposeTestRule.threeDotMenuButton(bookmarkedItem: String) =
    onNodeWithContentDescription("Item Menu for $bookmarkedItem")

private fun ComposeTestRule.bookmarkNameEditBox() =
    onNodeWithTag(EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD)

private fun ComposeTestRule.bookmarkFolderSelector() =
    onNodeWithText("Bookmarks")

private fun ComposeTestRule.expandBookmarkFolderSelector(title: String) =
    onNodeWithContentDescription(
        getStringResource(R.string.bookmark_select_folder_expand_folder_content_description, title),
        useUnmergedTree = true,
    )

private fun ComposeTestRule.bookmarkURLEditBox() =
    onNodeWithTag(EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD)

private fun ComposeTestRule.selectFolderNewFolderButton() =
    onNodeWithText(getStringResource(R.string.bookmark_select_folder_new_folder_button_title))

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.espresso.Espresso.openActionBarOverflowOrOptionsMenu
import androidx.test.espresso.Espresso.pressBack
import androidx.test.filters.SdkSuppress
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.registerAndCleanupIdlingResources
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.generateBookmarkFolder
import org.mozilla.fenix.helpers.RecyclerViewIdlingResource
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.longTapSelectItem
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.bookmarksMenu
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.multipleSelectionToolbar
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying basic functionality of bookmarks
 */
class BookmarksTest : TestSetup() {
    private val bookmarksFolderName = "New Folder"
    private val testBookmark = object {
        var title: String = "Bookmark title"
        var url: String = "https://www.example.com"
    }

    @get:Rule(order = 0)
    val activityTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule(order = 1)
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule(order = 2)
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/522919
    @Test
    @SkipLeaks
    fun verifyEmptyBookmarksMenuTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 1),
            ) {
                verifyBookmarksMenuView()
                verifyAddFolderButton()
                verifyCloseButton()
                verifyBookmarkTitle("Desktop Bookmarks")
                selectFolder("Desktop Bookmarks")
                verifyFolderTitle("Bookmarks Menu")
                verifyFolderTitle("Bookmarks Toolbar")
                verifyFolderTitle("Other Bookmarks")
                verifySyncSignInButton()
            }
        }.clickSingInToSyncButton {
            verifyTurnOnSyncToolbarTitle()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/522920
    @Test
    fun cancelCreateBookmarkFolderTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            clickAddFolderButton()
            addNewFolderName(bookmarksFolderName)
            navigateUp()
            verifyKeyboardHidden(isExpectedToBeVisible = false)
            verifyBookmarkFolderIsNotCreated(bookmarksFolderName)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2299619
    @Test
    fun cancelingChangesInEditModeAreNotSavedTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.bookmarkPage {
            clickSnackbarButton(activityTestRule, "EDIT")
        }
        bookmarksMenu {
            verifyEditBookmarksView()
            changeBookmarkTitle(testBookmark.title)
            changeBookmarkUrl(testBookmark.url)
        }.closeEditBookmarkSection {
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            verifyBookmarkTitle(defaultWebPage.title)
            verifyBookmarkedURL(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view//325633
    @SmokeTest
    @Test
    fun editBookmarksNameAndUrlTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        browserScreen {
            createBookmark(defaultWebPage.url)
        }.openThreeDotMenu {
        }.editBookmarkPage {
            verifyEditBookmarksView()
            changeBookmarkTitle(testBookmark.title)
            changeBookmarkUrl(testBookmark.url)
            saveEditBookmark()
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
            verifyBookmarkTitle(testBookmark.title)
            verifyBookmarkedURL(testBookmark.url)
        }.openBookmarkWithTitle(testBookmark.title) {
            verifyUrl("example.com")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view//341696
    @Test
    fun copyBookmarkURLTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickCopy {
            verifySnackBarText(expectedText = "URL copied")
            navigateUp()
        }

        navigationToolbar {
        }.clickUrlbar {
            longClickToolbar()
            clickPasteText()
            verifyTypedToolbarText(defaultWebPage.url.toString(), exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325634
    @Test
    fun shareBookmarkTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickShare {
            verifyShareOverlay()
            verifyShareBookmarkFavicon()
            verifyShareBookmarkTitle()
            verifyShareBookmarkUrl()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325636
    @Test
    fun openBookmarkInNewTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickOpenInNewTab(activityTestRule) {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1919261
    @Test
    fun verifyOpenAllInNewTabsOptionTest() {
        val webPages = listOf(
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
            TestAssetHelper.getGenericAsset(mockWebServer, 3),
            TestAssetHelper.getGenericAsset(mockWebServer, 4),
        )

        val rootFolderGuid = generateBookmarkFolder(title = "root", position = null)
        val subFolderGuid = generateBookmarkFolder(rootFolderGuid, "sub", null)

        generateBookmarkFolder(rootFolderGuid, "empty", null)
        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null, rootFolderGuid)
        createBookmarkItem(webPages[2].url.toString(), webPages[2].title, null, subFolderGuid)
        createBookmarkItem(webPages[3].url.toString(), webPages[2].title, null, rootFolderGuid)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.openThreeDotMenu("root") {
        }.clickOpenAllInTabs(activityTestRule) {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()

            verifyExistingOpenTabs("Test_Page_2", "Test_Page_3", "Test_Page_4")

            // Bookmark that is not under the root folder should not be opened
            verifyNoExistingOpenTabs("Test_Page_1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1919262
    @Test
    @SkipLeaks
    fun verifyOpenAllInPrivateTabsTest() {
        val webPages = listOf(
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
        )

        val rootFolderGuid = generateBookmarkFolder(title = "root", position = null)
        val subFolderGuid = generateBookmarkFolder(rootFolderGuid, "sub", null)

        generateBookmarkFolder(rootFolderGuid, "empty", null)
        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null, rootFolderGuid)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null, subFolderGuid)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.openThreeDotMenu("root") {
        }.clickOpenAllInPrivateTabs(activityTestRule) {
            verifyTabTrayIsOpen()
            verifyPrivateBrowsingButtonIsSelected()

            verifyExistingOpenTabs("Test_Page_1", "Test_Page_2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325637
    @Test
    @SkipLeaks
    fun openBookmarkInPrivateTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickOpenInPrivateTab(activityTestRule) {
            verifyTabTrayIsOpen()
            verifyPrivateBrowsingButtonIsSelected()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325635
    @Test
    @SkipLeaks
    fun deleteBookmarkTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickDelete {
            verifyUndoDeleteSnackBarButton()
            clickSnackbarButton(activityTestRule, "UNDO")
            verifySnackBarHidden()
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {
                verifyBookmarkedURL(defaultWebPage.url.toString())
            }
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickDelete {
            verifyBookmarkIsDeleted(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2300275
    @Test
    fun bookmarksMultiSelectionToolbarItemsTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {
                longTapSelectItem(firstWebPage.url)
                selectItem(secondWebPage.url.toString())
            }
        }

        multipleSelectionToolbar {
            verifyMultiSelectionCheckmark(firstWebPage.url)
            verifyMultiSelectionCheckmark(secondWebPage.url)
            verifyMultiSelectionCounter(2)
            verifyShareBookmarksButton()
            verifyCloseToolbarButton()
        }.closeToolbarReturnToBookmarks {
            verifyBookmarksMenuView()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2300276
    @SmokeTest
    @Test
    fun openMultipleSelectedBookmarksInANewTabTest() {
        val webPages = listOf(
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {
                longTapSelectItem(webPages[0].url)
                longTapSelectItem(webPages[1].url)
                openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
            }
        }

        multipleSelectionToolbar {
        }.clickOpenNewTab(activityTestRule) {
            verifyTabTrayIsOpen()
            verifyNormalBrowsingButtonIsSelected()
            verifyNormalTabsList()
            verifyExistingOpenTabs("Test_Page_1", "Test_Page_2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2300277
    @Test
    fun openMultipleSelectedBookmarksInPrivateTabTest() {
        val webPages = listOf(
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {
                longTapSelectItem(webPages[0].url)
                longTapSelectItem(webPages[1].url)
                openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
            }
        }

        multipleSelectionToolbar {
        }.clickOpenPrivateTab(activityTestRule) {
            verifyPrivateBrowsingButtonIsSelected()
            verifyPrivateTabsList()
            verifyExistingOpenTabs("Test_Page_1", "Test_Page_2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325644
    @SmokeTest
    @Test
    fun deleteMultipleSelectedBookmarksTest() {
        val webPages = listOf(
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
        )

        createBookmarkItem(webPages[0].url.toString(), webPages[0].title, null)
        createBookmarkItem(webPages[1].url.toString(), webPages[1].title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 3),
            ) {
                longTapSelectItem(webPages[0].url)
                longTapSelectItem(webPages[1].url)
            }
            openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
        }

        multipleSelectionToolbar {
            clickMultiSelectionDelete()
        }

        bookmarksMenu {
            verifySnackBarText(expectedText = "Bookmarks deleted")
            clickSnackbarButton(activityTestRule, "UNDO")
            verifyBookmarkedURL(webPages[0].url.toString())
            verifyBookmarkedURL(webPages[1].url.toString())
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 3),
            ) {
                longTapSelectItem(webPages[0].url)
                longTapSelectItem(webPages[1].url)
            }
            openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
        }

        multipleSelectionToolbar {
            clickMultiSelectionDelete()
        }

        bookmarksMenu {
            verifySnackBarText(expectedText = "Bookmarks deleted")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2301355
    @Test
    fun shareMultipleSelectedBookmarksTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {
                longTapSelectItem(defaultWebPage.url)
            }
        }

        multipleSelectionToolbar {
            clickShareBookmarksButton()
            verifyShareOverlay()
            verifyShareTabFavicon()
            verifyShareTabTitle()
            verifyShareTabUrl()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325639
    @Test
    fun createBookmarkFolderTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickEdit {
            clickParentFolderSelector()
            clickAddNewFolderButtonFromSelectFolderView()
            addNewFolderName(bookmarksFolderName)
            saveNewFolder()
            navigateUp()
            saveEditBookmark()
            selectFolder(bookmarksFolderName)
            verifyBookmarkedURL(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325645
    @Test
    fun navigateBookmarksFoldersTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            createFolder("1")
            waitForBookmarksFolderContentToExist("Bookmarks", "1")
            selectFolder("1")
            verifyCurrentFolderTitle("1")
            createFolder("2")
            waitForBookmarksFolderContentToExist("1", "2")
            selectFolder("2")
            verifyCurrentFolderTitle("2")
            navigateUp()
            waitForBookmarksFolderContentToExist("1", "2")
            verifyCurrentFolderTitle("1")
            mDevice.pressBack()
            verifyBookmarksMenuView()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/374855
    @Test
    fun cantSelectDefaultFoldersTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list)),
            ) {
                longTapDesktopFolder("Desktop Bookmarks")
                verifySnackBarText(expectedText = "Can’t edit default folders")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2299703
    @Test
    fun deleteBookmarkInEditModeTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {}
        }.openThreeDotMenu(defaultWebPage.title) {
        }.clickEdit {
            clickDeleteInEditModeButton()
            cancelDeletion()
            clickDeleteInEditModeButton()
            confirmDeletion()
            verifySnackBarText(expectedText = "Deleted")
            verifyBookmarkIsDeleted("Test_Page_1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1715710
    @Test
    fun verifySearchBookmarksViewTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.clickSearchButton {
            verifySearchView()
            verifySearchToolbar(true)
            verifySearchSelectorButton()
            verifySearchEngineIcon("Bookmarks")
            verifySearchBarPlaceholder("Search bookmarks")
            verifySearchBarPosition(true)
            tapOutsideToDismissSearchBar()
            verifySearchToolbar(false)
        }
        bookmarksMenu {
        }.goBackToBrowserScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
        }

        exitMenu()

        browserScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.clickSearchButton {
            verifySearchToolbar(true)
            verifySearchEngineIcon("Bookmarks")
            verifySearchBarPosition(false)
            pressBack()
            verifySearchToolbar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1715712
    @Test
    fun verifySearchForBookmarkedItemsTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getHTMLControlsFormAsset(mockWebServer)

        val newFolder = generateBookmarkFolder(title = bookmarksFolderName, position = null)
        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null, newFolder)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.clickSearchButton {
            // Search for a valid term
            typeSearch(firstWebPage.title)
            verifySearchSuggestionsAreDisplayed(activityTestRule, firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(activityTestRule, secondWebPage.url.toString())
            // Search for invalid term
            typeSearch("Android")
            verifySuggestionsAreNotDisplayed(activityTestRule, firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(activityTestRule, secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1715711
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyVoiceSearchInBookmarksTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        createBookmarkItem(defaultWebPage.url.toString(), defaultWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.clickSearchButton {
            verifySearchToolbar(true)
            verifySearchEngineIcon("Bookmarks")
            startVoiceSearch()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1715714
    @Test
    fun verifyDeletedBookmarksAreNotDisplayedAsSearchResultsTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)
        val thirdWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 3)

        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)
        createBookmarkItem(thirdWebPage.url.toString(), thirdWebPage.title, null)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.openThreeDotMenu(firstWebPage.title) {
        }.clickDelete {
            verifyBookmarkIsDeleted(firstWebPage.title)
        }.openThreeDotMenu(secondWebPage.title) {
        }.clickDelete {
            verifyBookmarkIsDeleted(secondWebPage.title)
        }.clickSearchButton {
            // Search for a valid term
            typeSearch("generic")
            verifySuggestionsAreNotDisplayed(activityTestRule, firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(activityTestRule, secondWebPage.url.toString())
            verifySponsoredSuggestionsResults(activityTestRule, thirdWebPage.url.toString(), searchTerm = "generic")
            pressBack()
        }
        bookmarksMenu {
        }.openThreeDotMenu(thirdWebPage.title) {
        }.clickDelete {
            verifyBookmarkIsDeleted(thirdWebPage.title)
        }.clickSearchButton {
            // Search for a valid term
            typeSearch("generic")
            verifySuggestionsAreNotDisplayed(activityTestRule, thirdWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/325642
    // Verifies that deleting a Bookmarks folder also removes the item from inside it.
    @SmokeTest
    @Test
    fun deleteBookmarkFoldersTest() {
        val website = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        val myFolder = generateBookmarkFolder(title = "My Folder", position = null)
        generateBookmarkFolder(myFolder, title = "My Folder 2", position = null)
        createBookmarkItem(website.url.toString(), website.title, null, myFolder)

        homeScreen {
        }.openThreeDotMenu {
        }.openBookmarks {
        }.openThreeDotMenu("My Folder") {
        }.clickDelete {
            cancelFolderDeletion()
            verifyFolderTitle("My Folder")
        }.openThreeDotMenu("My Folder") {
        }.clickDelete {
            confirmDeletion()
            verifySnackBarText(expectedText = "Deleted")
            clickSnackbarButton(activityTestRule, "UNDO")
            verifyFolderTitle("My Folder")
        }.openThreeDotMenu("My Folder") {
        }.clickDelete {
            confirmDeletion()
            verifySnackBarText(expectedText = "Deleted")
            verifyBookmarkIsDeleted("My Folder")
            verifyBookmarkIsDeleted("My Folder 2")
            verifyBookmarkIsDeleted("Test_Page_1")
            navigateUp()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2301370
    @Test
    fun verifyAddBookmarkButtonTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.bookmarkPage {
            verifySnackBarText("Saved in “Bookmarks”")
        }.openThreeDotMenu {
            verifyEditBookmarkButton()
        }.openBookmarks {
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.bookmark_list), 2),
            ) {
                verifyBookmarksMenuView()
                verifyBookmarkTitle(defaultWebPage.title)
                verifyBookmarkedURL(defaultWebPage.url.toString())
                verifyBookmarkFavicon(defaultWebPage.url)
            }
        }
    }
}

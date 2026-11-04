/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.net.Uri
import android.util.Log
import android.view.KeyEvent
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assertAny
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performTouchInput
import androidx.test.espresso.AppNotIdleException
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.action.ViewActions.pressImeActionButton
import androidx.test.espresso.assertion.PositionAssertions.isCompletelyAbove
import androidx.test.espresso.assertion.PositionAssertions.isPartiallyBelow
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.isRoot
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.By.textContains
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import mozilla.components.browser.menu.R as menuR
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.NAVIGATION_BAR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.SEARCH_SELECTOR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.TABS_COUNTER
import org.junit.Assert.assertTrue
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.LONG_CLICK_DURATION
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectIsGone
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.SessionLoadedIdlingResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.ext.waitNotNull
import org.mozilla.fenix.helpers.matchers.hasItemsCount

/** Implementation of Robot Pattern for the URL toolbar. */
class NavigationToolbarRobot(private val composeTestRule: ComposeTestRule) {
    fun verifyUrl(url: String) {
        Log.i(TAG, "verifyUrl: Trying to verify toolbar text matches $url")
        onView(withId(toolbarR.id.mozac_browser_toolbar_url_view)).check(matches(withText(url)))
        Log.i(TAG, "verifyUrl: Verified toolbar text matches $url")
    }

    fun verifyTabButtonShortcutMenuItems() {
        Log.i(TAG, "verifyTabButtonShortcutMenuItems: Trying to verify tab counter shortcut options")
        composeTestRule.onNodeWithText("Close tab", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("New private tab", useUnmergedTree = true).assertIsDisplayed()
        composeTestRule.onNodeWithText("New tab", useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyTabButtonShortcutMenuItems: Verified tab counter shortcut options")
    }

    fun verifyTabButtonShortcutMenuItemsForNormalHomescreen() {
        Log.i(TAG, "verifyTabButtonShortcutMenuItemsForNormalHomescreen: Trying to verify tab counter shortcut options")
        onView(withId(menuR.id.mozac_browser_menu_recyclerView))
            .check(matches(hasItemsCount(2)))
            .check(matches(hasDescendant(withText("New tab"))))
            .check(matches(hasDescendant(withText("New private tab"))))
        Log.i(TAG, "verifyTabButtonShortcutMenuItemsForNormalHomescreen: Verified tab counter shortcut options")
    }

    fun verifyTabButtonShortcutMenuItemsForPrivateHomescreen() {
        Log.i(
            TAG,
            "verifyTabButtonShortcutMenuItemsForPrivateHomescreen: Trying to verify tab counter shortcut options",
        )
        onView(withId(menuR.id.mozac_browser_menu_recyclerView))
            .check(matches(hasItemsCount(2)))
            .check(matches(hasDescendant(withText("New tab"))))
            .check(matches(hasDescendant(withText("New private tab"))))
        Log.i(TAG, "verifyTabButtonShortcutMenuItemsForPrivateHomescreen: Verified tab counter shortcut options")
    }

    /** Matches the reader view toolbar button, whose label also offers Listen to Page when that is turned on. */
    private fun readerViewToolbarButtonMatcher() =
        hasContentDescription(getStringResource(R.string.browser_menu_read)) or
            hasContentDescription(getStringResource(R.string.browser_menu_read_or_listen))

    fun verifyReaderViewToolbarButton(isDisplayed: Boolean = false) {
        waitForAppWindowToBeUpdated()
        if (isDisplayed) {
            Log.i(
                TAG,
                "verifyReaderViewToolbarButton: Trying to verify that the reader view toolbar button is displayed",
            )
            composeTestRule.onNode(readerViewToolbarButtonMatcher(), useUnmergedTree = true).assertIsDisplayed()
            Log.i(TAG, "verifyReaderViewToolbarButton: Verified that the reader view toolbar button is displayed")
        } else {
            Log.i(
                TAG,
                "verifyReaderViewToolbarButton: Trying to verify that the reader view toolbar button is not displayed",
            )
            composeTestRule.onNode(readerViewToolbarButtonMatcher(), useUnmergedTree = true).assertIsNotDisplayed()
            Log.i(TAG, "verifyReaderViewToolbarButton: Verified that the reader view toolbar button is not displayed")
        }
    }

    fun clickReaderViewToolbarButton(isReaderViewEnabled: Boolean) {
        if (isReaderViewEnabled) {
            Log.i(TAG, "clickReaderViewToolbarButton: Trying to click the \"Close reader view\" toolbar button")
            composeTestRule
                .onNodeWithContentDescription(
                    getStringResource(R.string.browser_menu_read_close),
                    useUnmergedTree = true,
                )
                .performClick()
            Log.i(TAG, "clickReaderViewToolbarButton: Clicked the \"Close reader view\" toolbar button")
        } else {
            Log.i(TAG, "clickReaderViewToolbarButton: Trying to click the \"Reader view\" toolbar button")
            composeTestRule.onNode(readerViewToolbarButtonMatcher(), useUnmergedTree = true).performClick()
            Log.i(TAG, "clickReaderViewToolbarButton: Clicked the \"Reader view\" toolbar button")
        }
        waitForAppWindowToBeUpdated()
    }

    fun verifyClipboardSuggestionsAreDisplayed(shouldBeDisplayed: Boolean) {
        if (shouldBeDisplayed) {
            composeTestRule.onNodeWithText(getStringResource(R.string.awesomebar_clipboard_title)).assertIsDisplayed()
        } else {
            composeTestRule
                .onNodeWithText(getStringResource(R.string.awesomebar_clipboard_title))
                .assertIsNotDisplayed()
        }
    }

    fun longClickEditModeToolbar() {
        Log.i(TAG, "longClickEditModeToolbar: Trying to long click the edit mode toolbar")
        mDevice.findObject(By.res("$packageName:id/mozac_browser_toolbar_edit_url_view")).click(LONG_CLICK_DURATION)
        Log.i(TAG, "longClickEditModeToolbar: Long clicked the edit mode toolbar")
    }

    fun clickContextMenuItem(item: String) {
        mDevice.waitNotNull(
            Until.findObject(By.text(item)),
            waitingTime,
        )
        Log.i(TAG, "clickContextMenuItem: Trying click context menu item: $item")
        mDevice.findObject(By.text(item)).click()
        Log.i(TAG, "clickContextMenuItem: Clicked context menu item: $item")
    }

    fun clickClearToolbarButton() {
        Log.i(TAG, "clickClearToolbarButton: Trying click the clear address button")
        clearAddressBarButton().click()
        Log.i(TAG, "clickClearToolbarButton: Clicked the clear address button")
    }

    fun verifyToolbarIsEmpty() =
        assertUIObjectExists(
            itemWithResIdContainingText(
                "$packageName:id/mozac_browser_toolbar_edit_url_view",
                getStringResource(R.string.search_hint),
            )
        )

    fun verifySearchBarPlaceholder(searchHint: String) {
        Log.i(TAG, "verifySearchBarPlaceholder: Trying to verify that the search bar place holder is \"$searchHint\"")
        composeTestRule.onAllNodesWithTag(ADDRESSBAR_URL_BOX).assertAny(hasContentDescription(searchHint))
        Log.i(TAG, "verifySearchBarPlaceholder: Verified that the search bar place holder is \"$searchHint\"")
    }

    fun verifyDefaultSearchEngine(engineName: String) {
        Log.i(
            TAG,
            "verifyDefaultSearchEngine: Trying to verify that default search engine is: $engineName is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(R.string.search_engine_selector_content_description, engineName)
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyDefaultSearchEngine: Verified that default search engine is: $engineName is displayed")
    }

    fun verifyTextSelectionOptions(vararg textSelectionOptions: String) {
        for (textSelectionOption in textSelectionOptions) {
            mDevice.waitNotNull(Until.findObject(textContains(textSelectionOption)), waitingTime)
        }
    }

    fun verifyRedesignedNavigationToolbarItems() {
        assertUIObjectExists(
            itemWithDescription(getStringResource(R.string.browser_menu_back)),
            itemWithDescription(getStringResource(R.string.browser_menu_forward)),
            itemWithDescription(getStringResource(R.string.search_hint)),
            itemWithDescription("More options"),
            itemWithResId("$packageName:id/counter_box"),
        )
    }

    fun verifyTranslationButton(
        isPageTranslated: Boolean,
        originalLanguage: String = "",
        translatedLanguage: String = "",
    ) {
        if (isPageTranslated) {
            for (i in 1..RETRY_COUNT) {
                Log.i(TAG, "verifyTranslationButton: Started try #$i")
                try {
                    assertUIObjectExists(
                        itemWithDescription("Page translated from $originalLanguage to $translatedLanguage.")
                    )

                    break
                } catch (e: AssertionError) {
                    Log.i(TAG, "verifyTranslationButton: AssertionError caught, executing fallback methods")
                    browserScreen(composeTestRule) {}.openThreeDotMenu {}.clickRefreshButton {}
                }
            }
        } else {
            assertUIObjectExists(itemWithDescription(getStringResource(R.string.browser_toolbar_translate)))
        }
    }

    fun verifyReaderViewNavigationToolbarButton(isReaderViewEnabled: Boolean) {
        if (isReaderViewEnabled) {
            assertUIObjectExists(itemWithDescription(getStringResource(R.string.browser_menu_read_close)))
        } else {
            assertUIObjectExists(itemWithDescription(getStringResource(R.string.browser_menu_read)))
        }
    }

    fun longTapNavButton(buttonDescription: String) {
        Log.i(TAG, "longTapNavButton: Waiting to find the nav bar $buttonDescription button.")
        mDevice.findObject(UiSelector().description("Back")).waitForExists(waitingTime)
        Log.i(TAG, "longTapNavButton: Trying to long click the nav bar $buttonDescription button.")
        composeTestRule
            .onNode(
                hasContentDescription(buttonDescription).and(hasAnyAncestor(hasTestTag(NAVIGATION_BAR))),
                useUnmergedTree = true,
            )
            .performTouchInput {
                longClick(durationMillis = LONG_CLICK_DURATION)
            }
        Log.i(TAG, "longTapNavButton: Long clicked the nav bar $buttonDescription button.")
    }

    fun verifyTabHistorySheetIsDisplayed(isDisplayed: Boolean) {
        assertUIObjectExists(
            itemWithResId("$packageName:id/tabHistoryRecyclerView"),
            exists = isDisplayed,
        )
    }

    fun verifyTabHistoryContainsWebsite(websiteUrl: String, isDisplayed: Boolean) {
        assertUIObjectExists(
            itemWithResIdAndText("$packageName:id/site_list_item", websiteUrl),
            exists = isDisplayed,
        )
    }

    // Verifies that the address bar is displayed separately, or merged with the navbar in landscape mode.
    fun verifyAddressBarIsDisplayedSeparately(isSeparate: Boolean, isAtTop: Boolean) {
        val addressBar = "$packageName:id/toolbar"

        if (isSeparate) {
            assertUIObjectExists(itemWithResId(addressBar))
            composeTestRule.onNodeWithTag(NAVIGATION_BAR).assertIsDisplayed()
        } else if (isAtTop) {
            assertUIObjectExists(itemWithResId(addressBar))
            composeTestRule.onNodeWithTag(NAVIGATION_BAR).assertIsNotDisplayed()
        } else {
            assertUIObjectIsGone(itemWithResId(addressBar))
            composeTestRule.onNodeWithTag(NAVIGATION_BAR).assertIsDisplayed()
        }
    }

    fun verifyAddressBarPosition(isAtTop: Boolean) {
        Log.i(
            TAG,
            "verifyAddressBarPosition: Trying to verify the toolbar address bar position is at the top: $isAtTop.",
        )
        onView(withId(R.id.toolbar))
            .check(
                if (isAtTop) {
                    isCompletelyAbove(withId(R.id.engineView))
                } else {
                    isPartiallyBelow(withId(R.id.engineView))
                }
            )
        Log.i(TAG, "verifyAddressBarPosition: Verified the toolbar address bar position is at the top: $isAtTop.")
    }

    fun verifyNavBarBarPosition(isAtBottom: Boolean) {
        Log.i(
            TAG,
            "verifyNavBarBarPosition: Trying to verify the toolbar navbar position is at the bottom: $isAtBottom.",
        )
        assertNavBarIsPositioned(referenceResourceId = "$packageName:id/engineView")
        Log.i(TAG, "verifyNavBarBarPosition: Verified the toolbar navbar position is at the bottom: $isAtBottom.")
    }

    fun verifyNavBarPosition() {
        Log.i(TAG, "verifyNavBarPosition: Trying to verify the toolbar navbar position is at the bottom.")
        assertNavBarIsPositioned(referenceResourceId = "$packageName:id/composable_toolbar")
        Log.i(TAG, "verifyNavBarPosition: Verified the toolbar navbar position is at the bottom.")
    }

    /**
     * Verifies navigation bar exists and is correctly positioned when Edge-to-Edge is enabled. Edge-to-Edge mode
     * extends content to screen edges, so we verify it's at the very edge.
     */
    fun verifyNavBarPositionForEdgeToEdge(isAtBottom: Boolean = true) {
        Log.i(TAG, "verifyNavBarPositionForEdgeToEdge: Verifying navbar is docked to the edge (isAtBottom=$isAtBottom)")

        composeTestRule.waitForIdle()
        mDevice.waitForIdle()

        val navBarBounds = composeTestRule.onNodeWithTag(NAVIGATION_BAR).fetchSemanticsNode().boundsInWindow

        val displayHeight = mDevice.displayHeight

        // Allows enough pixels to account for the System Navigation Bar or Status Bar
        val maxSystemInsetsPx = 150

        if (isAtBottom) {
            val bottomEdge = navBarBounds.bottom.toInt()
            val distanceToBottom = displayHeight - bottomEdge

            assertTrue(
                "Navigation bar should be docked at the bottom, accounting for system insets. " +
                    "Distance to bottom was $distanceToBottom px (expected <= $maxSystemInsetsPx px). Bounds: $navBarBounds",
                distanceToBottom in 0..maxSystemInsetsPx,
            )
        } else {
            val topEdge = navBarBounds.top.toInt()

            assertTrue(
                "Navigation bar should be docked at the top, accounting for system insets. " +
                    "Distance to top was $topEdge px (expected <= $maxSystemInsetsPx px). Bounds: $navBarBounds",
                topEdge in 0..maxSystemInsetsPx,
            )
        }
        Log.i(TAG, "verifyNavBarPositionForEdgeToEdge: Verified navbar is at the expected edge within safe insets")
    }

    /**
     * Asserts that the navigation bar compose node is positioned relative to the view identified by
     * [referenceResourceId].
     */
    private fun assertNavBarIsPositioned(referenceResourceId: String) {
        val navBarBounds = composeTestRule.onNodeWithTag(NAVIGATION_BAR).fetchSemanticsNode().boundsInWindow
        val reference = mDevice.findObject(UiSelector().resourceId(referenceResourceId))

        assertTrue(
            "Reference view ($referenceResourceId) must be present in the view hierarchy",
            reference.waitForExists(waitingTime),
        )

        val referenceBounds = reference.visibleBounds

        assertTrue(
            "Navigation bar is below the reference view",
            navBarBounds.top >= referenceBounds.bottom,
        )
    }

    fun verifyTheTabCounter(numberOfOpenTabs: String, isPrivateBrowsingEnabled: Boolean = false) {
        if (isPrivateBrowsingEnabled) {
            Log.i(TAG, "verifyTabCounter: Trying to verify that the number of open private tabs is : $numberOfOpenTabs")
            composeTestRule
                .onNodeWithContentDescription("Private Tabs Open: $numberOfOpenTabs. Tap to switch tabs.")
                .assertIsDisplayed()
            Log.i(TAG, "verifyTabCounter: Verified that the number of open private tabs is : $numberOfOpenTabs")
        } else {
            Log.i(TAG, "verifyTabCounter: Trying to verify that the number of open tabs is : $numberOfOpenTabs")
            composeTestRule
                .onNodeWithContentDescription("Non-private Tabs Open: $numberOfOpenTabs. Tap to switch tabs.")
                .assertIsDisplayed()
            Log.i(TAG, "verifyTabCounter: Verified that the number of open tabs is : $numberOfOpenTabs")
        }
    }

    fun verifyTheMainMenuButton() {
        Log.i(TAG, "verifyTheMainMenuButton: Trying to verify that the main menu button is displayed")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.content_description_menu))
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheMainMenuButton: Verified that the main menu button is displayed")
    }

    fun verifyTheNewTabButton(isPrivateModeEnabled: Boolean = false) {
        if (isPrivateModeEnabled) {
            Log.i(TAG, "verifyTheNewTabButton: Trying to verify that the \"New private tab\" button is displayed.")
            composeTestRule.onNodeWithContentDescription("New private tab").assertIsDisplayed()
            Log.i(TAG, "verifyTheNewTabButton: Verified that the \"New private tab\" button is displayed.")
        } else {
            Log.i(TAG, "verifyTheNewTabButton: Trying to verify that the \"New tab\" button is displayed.")
            composeTestRule.onNodeWithContentDescription("New tab").assertIsDisplayed()
            Log.i(TAG, "verifyTheNewTabButton: Verified that the \"New tab\" button is displayed.")
        }
    }

    fun verifyTheTabStripOpenTab(tabName: String) {
        Log.i(TAG, "verifyTheTabStripOpenTab: Trying to verify that the opened tab is displayed in the tab strip")
        composeTestRule.onNodeWithText(tabName, ignoreCase = true).assertIsDisplayed()
        Log.i(TAG, "verifyTheTabStripOpenTab: Verified that the opened tab is displayed in the tab strip")
    }

    fun verifyTheTabStripCloseTabButton(tabName: String) {
        Log.i(TAG, "verifyTheTabStripCloseTabButton: Trying to verify close button for tab in tab strip is displayed")
        composeTestRule.onNodeWithContentDescription("Close tab $tabName").assertIsDisplayed()
        Log.i(TAG, "verifyTheTabStripCloseTabButton: Verified close button for tab in tab strip is displayed")
    }

    fun verifyTheTabStripAddTabButton() {
        Log.i(TAG, "verifyTheTabStripAddTabButton: Trying to verify the tab strip \"Add tab\" button is displayed")
        composeTestRule.onNodeWithContentDescription("Add tab").assertIsDisplayed()
        Log.i(TAG, "verifyTheTabStripAddTabButton: Verified the tab strip \"Add tab\" button is displayed")
    }

    fun verifyTheBackButton() {
        Log.i(
            TAG,
            "verifyTheBackButton: Trying to verify that the \"Back\" nav bar button is displayed in landscape mode.",
        )
        composeTestRule.onNodeWithContentDescription("Back").assertIsDisplayed()
        Log.i(TAG, "verifyTheBackButton: Verified that the \"Back\" nav bar button is displayed in landscape mode.")
    }

    fun verifyTheForwardButton() {
        Log.i(
            TAG,
            "verifyTheForwardButton: Trying to verify that the \"Forward\" nav bar button is displayed in landscape mode.",
        )
        composeTestRule.onNodeWithContentDescription("Forward").assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheForwardButton: Verified that the \"Forward\" nav bar button is displayed in landscape mode.",
        )
    }

    fun verifyTheRefreshButton() {
        Log.i(
            TAG,
            "verifyTheRefreshButton: Trying to verify that the \"Refresh\" nav bar button is displayed in landscape mode.",
        )
        composeTestRule.onNodeWithContentDescription("Refresh").assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheRefreshButton: Verified that the \"Refresh\" nav bar button is displayed in landscape mode.",
        )
    }

    fun clickTheNavigationBarAddBookmarkButton() {
        Log.i(
            TAG,
            "clickTheNavigationBarAddBookmarkButton: Trying to click the \"Bookmark page\" button in the navigation bar.",
        )
        composeTestRule.onNodeWithContentDescription("Bookmark page").performClick()
        Log.i(
            TAG,
            "clickTheNavigationBarAddBookmarkButton: Clicked the \"Bookmark page\" button in the navigation bar.",
        )
    }

    fun verifyTheNavigationBarAddBookmarkButton() {
        Log.i(
            TAG,
            "verifyTheNavigationBarAddBookmarkButton: Trying to verify that the bookmark page button is displayed in the navigation bar",
        )
        composeTestRule.onNodeWithContentDescription("Bookmark page").assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheNavigationBarAddBookmarkButton: Verified that the bookmark page button is displayed in the navigation bar",
        )
    }

    fun verifyTheNavigationBarShareButton() {
        Log.i(
            TAG,
            "verifyTheNavigationBarShareButton: Trying to verify that the share button is displayed in the navigation bar",
        )
        composeTestRule.onNodeWithContentDescription("Share").assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheNavigationBarShareButton: Verified that the share button is displayed in the navigation bar",
        )
    }

    fun verifyTheNavigationBarTranslateButton() {
        Log.i(
            TAG,
            "verifyTheNavigationBarTranslateButton: Trying to verify that the translate button is displayed in the navigation bar",
        )
        composeTestRule.onNodeWithContentDescription("Translate page").assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheNavigationBarTranslateButton: Verified that the translate button is displayed in the navigation bar",
        )
    }

    fun verifyTheNavigationBarHomepageButton() {
        Log.i(
            TAG,
            "verifyTheNavigationBarHomepageButton: Trying to verify that the homepage button is displayed in the navigation bar",
        )
        composeTestRule.onNodeWithContentDescription("Homepage").assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheNavigationBarHomepageButton: Verified that the homepage button is displayed in the navigation bar",
        )
    }

    fun clickTheNavigationBarEditBookmarkButton() {
        Log.i(
            TAG,
            "clickTheNavigationBarEditBookmarkButton: Trying to click the \"Edit bookmark\" button in the navigation bar.",
        )
        composeTestRule.onNodeWithContentDescription("Edit bookmark").performClick()
        Log.i(
            TAG,
            "clickTheNavigationBarEditBookmarkButton: Clicked the \"Edit bookmark\" button in the navigation bar.",
        )
    }

    fun clickTheNavigationBarRefreshButton() {
        Log.i(TAG, "clickTheNavigationBarRefreshButton: Trying to click the \"Refresh\" button in the navigation bar.")
        composeTestRule.onNodeWithContentDescription("Refresh").performClick()
        Log.i(TAG, "clickTheNavigationBarRefreshButton: Clicked the \"Refresh\" button in the navigation bar.")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        private lateinit var sessionLoadedIdlingResource: SessionLoadedIdlingResource

        @OptIn(ExperimentalTestApi::class)
        fun enterURLAndEnterToBrowser(
            url: Uri,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            composeTestRule.waitForIdle()
            Log.i(TAG, "enterURLAndEnterToBrowser: Waiting for $waitingTime until the URL bar exists")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADDRESSBAR_URL_BOX), waitingTime)
            Log.i(TAG, "enterURLAndEnterToBrowser: Waited for $waitingTime until the URL bar exists")
            Log.i(TAG, "enterURLAndEnterToBrowser: Trying to click navigation toolbar")
            itemWithResId(ADDRESSBAR_URL_BOX).click()
            Log.i(TAG, "enterURLAndEnterToBrowser: Clicked navigation toolbar")

            Log.i(TAG, "enterURLAndEnterToBrowser: Waiting for search box to appear")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADDRESSBAR_SEARCH_BOX), waitingTime)
            Log.i(TAG, "enterURLAndEnterToBrowser: Search box appeared")

            Log.i(TAG, "enterURLAndEnterToBrowser: Trying to set toolbar text to: $url")
            composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTextReplacement(url.toString())
            Log.i(TAG, "enterURLAndEnterToBrowser: Toolbar text was set to: $url")

            Log.i(TAG, "enterURLAndEnterToBrowser: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "enterURLAndEnterToBrowser: Waited for compose rule to be idle")

            runCatching {
                Log.i(TAG, "enterURLAndEnterToBrowser: Trying to perform Compose IME action on the toolbar")
                composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performImeAction()
                Log.i(TAG, "enterURLAndEnterToBrowser: Compose IME action performed on the toolbar")
            }
                .onFailure { throwable ->
                    Log.e(
                        TAG,
                        "enterURLAndEnterToBrowser: Compose IME action failed with: ${throwable::class.java.simpleName} - ${throwable.message}",
                    )
                    Log.d(TAG, "enterURLAndEnterToBrowser: Falling back to UiDevice pressEnter()")
                    mDevice.pressEnter()
                    Log.d(TAG, "enterURLAndEnterToBrowser: Fallback UiDevice pressEnter() completed")
                }

            Log.i(TAG, "enterURLAndEnterToBrowser: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "enterURLAndEnterToBrowser: Waited for compose rule to be idle")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun enterURL(
            url: Uri,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            sessionLoadedIdlingResource = SessionLoadedIdlingResource()

            openEditURLView()
            Log.i(TAG, "enterURLAndEnterToBrowser: Trying to set toolbar text to: $url")
            awesomeBar().setText(url.toString())
            Log.i(TAG, "enterURLAndEnterToBrowser: Toolbar text was set to: $url")
            Log.i(TAG, "enterURLAndEnterToBrowser: Trying to press device enter button")
            pressImeActionOnAwesomeBar()
            Log.i(TAG, "enterURLAndEnterToBrowser: Pressed device enter button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun visitLinkFromClipboard(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            composeTestRule.onNodeWithText(getStringResource(R.string.awesomebar_clipboard_title)).assertIsDisplayed()
            Log.i(TAG, "visitLinkFromClipboard: Trying to click the fill link from clipboard button")
            composeTestRule.onNodeWithText(getStringResource(R.string.awesomebar_clipboard_title)).performClick()
            Log.i(TAG, "visitLinkFromClipboard: Clicked the fill link from clipboard button")
            waitForAppWindowToBeUpdated()
            Log.i(TAG, "visitLinkFromClipboard: Trying to press device enter button")
            onView(isRoot()).perform(ViewActions.pressKey(KeyEvent.KEYCODE_ENTER))
            Log.i(TAG, "visitLinkFromClipboard: Pressed device enter button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click the device back button")
            mDevice.pressBack()
            Log.i(TAG, "goBackToHomeScreen: Clicked the device back button")
            Log.i(TAG, "goBackToHomeScreen: Waiting for $waitingTimeShort ms for $packageName window to be updated")
            mDevice.waitForWindowUpdate(packageName, waitingTimeShort)
            Log.i(TAG, "goBackToHomeScreen: Waited for $waitingTimeShort ms for $packageName window to be updated")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goBackToBrowserScreen(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goBackToBrowserScreen: Trying to click the device back button")
            mDevice.pressBack()
            Log.i(TAG, "goBackToBrowserScreen: Clicked the device back button")
            Log.i(TAG, "goBackToBrowserScreen: Waiting for $waitingTimeShort ms for $packageName window to be updated")
            mDevice.waitForWindowUpdate(packageName, waitingTimeShort)
            Log.i(TAG, "goBackToBrowserScreen: Waited for $waitingTimeShort ms for $packageName window to be updated")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun openTabButtonShortcutsMenu(interact: NavigationToolbarRobot.() -> Unit): Transition {
            Log.i(TAG, "openTabButtonShortcutsMenu: Trying to long click the tab counter button")
            composeTestRule.onNodeWithTag(TABS_COUNTER).performTouchInput {
                down(center)
                advanceEventTime(10000)
                up()
            }
            Log.i(TAG, "openTabButtonShortcutsMenu: Long clicked the tab counter button")

            NavigationToolbarRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun closeTabFromShortcutsMenu(interact: NavigationToolbarRobot.() -> Unit): Transition {
            Log.i(TAG, "closeTabFromShortcutsMenu: Trying to click the \"Close tab\" button")
            composeTestRule.onNodeWithText("Close tab", useUnmergedTree = true).performClick()
            Log.i(TAG, "closeTabFromShortcutsMenu: Clicked the \"Close tab\" button")

            NavigationToolbarRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun openNewTabFromShortcutsMenu(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "openNewTabFromShortcutsMenu: Trying to click the \"New tab\" button")
            composeTestRule.onNodeWithText("New tab", useUnmergedTree = true).performClick()
            Log.i(TAG, "openNewTabFromShortcutsMenu: Clicked the \"New tab\" button")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun openNewPrivateTabFromShortcutsMenu(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "openNewPrivateTabFromShortcutsMenu: Trying to click the \"New private tab\" button")
            composeTestRule.onNodeWithText("New private tab", useUnmergedTree = true).performClick()
            Log.i(TAG, "openNewPrivateTabFromShortcutsMenu: Clicked the \"New private tab\" button")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickURLBar(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "clickURLBar: Waiting for $waitingTime until the URL bar to exist")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADDRESSBAR_URL_BOX), waitingTime)
            Log.i(TAG, "clickURLBar: Waited for $waitingTime until the URL bar to exist")
            Log.i(TAG, "clickURLBar: Trying to click navigation toolbar")
            composeTestRule.onAllNodesWithTag(ADDRESSBAR_URL_BOX).onFirst().performClick()
            Log.i(TAG, "clickURLBar: Clicked navigation toolbar")
            composeTestRule.waitForIdle()

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun clickSearchSelectorButton(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "clickSearchSelectorButton: Trying to click the search selector button")
            composeTestRule.onNodeWithTag(SEARCH_SELECTOR).performClick()
            Log.i(TAG, "clickSearchSelectorButton: Clicked the search selector button")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickTranslateButton(
            isPageTranslated: Boolean = false,
            originalLanguage: String = "",
            translatedLanguage: String = "",
            interact: TranslationsRobot.() -> Unit,
        ): TranslationsRobot.Transition {
            val buttonDescription =
                if (isPageTranslated) {
                    "Page translated from $originalLanguage to $translatedLanguage."
                } else {
                    getStringResource(R.string.browser_toolbar_translate)
                }
            Log.i(TAG, "clickTranslateButton: Waiting for $waitingTime until the translate button exists")
            composeTestRule.waitUntilAtLeastOneExists(
                hasContentDescription(buttonDescription, substring = true),
                waitingTime,
            )
            Log.i(TAG, "clickTranslateButton: Waited for $waitingTime until the translate button exists")
            Log.i(TAG, "clickTranslateButton: Trying to click the translate button")
            composeTestRule.onNodeWithContentDescription(buttonDescription, substring = true).performClick()
            Log.i(TAG, "clickTranslateButton: Clicked the translate button")

            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        // New navbar design home screen search button
        @OptIn(ExperimentalTestApi::class)
        fun clickHomeScreenSearchButton(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "clickHomeScreenSearchButton: Waiting for $waitingTime ms for the search button to exist.")
            composeTestRule.waitUntilAtLeastOneExists(hasContentDescription("Search or enter address"))
            Log.i(TAG, "clickHomeScreenSearchButton: Waited for $waitingTime ms for the search button to exist.")
            Log.i(TAG, "clickHomeScreenSearchButton: Trying to click the nav bar search button.")
            composeTestRule.onNodeWithContentDescription("Search or enter address").performClick()
            Log.i(TAG, "clickHomeScreenSearchButton: Clicked the nav bar search button.")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun openUnifiedTrustPanel(interact: UnifiedTrustPanelRobot.() -> Unit): UnifiedTrustPanelRobot.Transition {
            Log.i(
                TAG,
                "openUnifiedTrustPanel: Trying to click the site information button and wait for $waitingTime ms for a new window",
            )
            composeTestRule.onNodeWithContentDescription("Site information").performClick()
            Log.i(
                TAG,
                "openUnifiedTrustPanel: Clicked the site information button and waited for $waitingTime ms for a new window",
            )
            waitForAppWindowToBeUpdated()

            UnifiedTrustPanelRobot().interact()
            return UnifiedTrustPanelRobot.Transition()
        }

        fun clickTheNewTabButton(
            isPrivateModeEnabled: Boolean = false,
            interact: SearchRobot.() -> Unit,
        ): SearchRobot.Transition {
            if (isPrivateModeEnabled) {
                Log.i(TAG, "clickTheNewTabButton: Trying to click the \"New private tab\" button.")
                composeTestRule.onNodeWithContentDescription("New private tab").performClick()
                Log.i(TAG, "clickTheNewTabButton: Clicked the \"New private tab\" button.")
            } else {
                Log.i(TAG, "clickTheNewTabButton: Trying to click the \"New tab\" button.")
                composeTestRule.onNodeWithContentDescription("New tab").performClick()
                Log.i(TAG, "clickTheNewTabButton: Clicked the \"New tab\" button.")
            }

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun clickTheNavigationBarShareButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickTheNavigationBarShareButton: Trying to click the \"Share\" button in the navigation bar.")
            composeTestRule.onNodeWithContentDescription("Share").performClick()
            Log.i(TAG, "clickTheNavigationBarShareButton: Clicked the \"Share\" button in the navigation bar.")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickTheNavigationBarTranslatePageButton(
            interact: TranslationsRobot.() -> Unit
        ): TranslationsRobot.Transition {
            Log.i(
                TAG,
                "clickTheNavigationBarTranslatePageButton: Trying to click the \"Translate page\" button in the navigation bar.",
            )
            composeTestRule.onNodeWithContentDescription("Translate page").performClick()
            Log.i(
                TAG,
                "clickTheNavigationBarTranslatePageButton: Clicked the \"Translate page\" button in the navigation bar.",
            )

            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        fun clickTheNavigationBarHomepageButton(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(
                TAG,
                "clickTheNavigationBarHomepageButton: Trying to click the \"Homepage\" button in the navigation bar.",
            )
            composeTestRule.onNodeWithContentDescription("Homepage").performClick()
            Log.i(TAG, "clickTheNavigationBarHomepageButton: Clicked the \"Homepage\" button in the navigation bar.")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }
    }
}

fun navigationToolbar(
    composeTestRule: ComposeTestRule,
    interact: NavigationToolbarRobot.() -> Unit,
): NavigationToolbarRobot.Transition {
    NavigationToolbarRobot(composeTestRule).interact()
    return NavigationToolbarRobot.Transition(composeTestRule)
}

fun openEditURLView() {
    Log.i(TAG, "openEditURLView: Waiting for $waitingTime ms for the toolbar to exist")
    urlBar().waitForExists(waitingTime)
    Log.i(TAG, "openEditURLView: Waited for $waitingTime ms for the toolbar to exist")
    Log.i(TAG, "openEditURLView: Trying to click the toolbar")
    urlBar().click()
    Log.i(TAG, "openEditURLView: Clicked the toolbar")
    Log.i(TAG, "openEditURLView: Waiting for $waitingTime ms for the edit mode toolbar to exist")
    itemWithResId("$packageName:id/mozac_browser_toolbar_edit_url_view").waitForExists(waitingTime)
    Log.i(TAG, "openEditURLView: Waited for $waitingTime ms for the edit mode toolbar to exist")
}

private fun urlBar() = mDevice.findObject(UiSelector().resourceId("$packageName:id/toolbar"))

private fun awesomeBar() =
    mDevice.findObject(UiSelector().resourceId("$packageName:id/mozac_browser_toolbar_edit_url_view"))

private fun pressImeActionOnAwesomeBar() {
    val context = appContext
    val resId =
        context.resources.getIdentifier(
            "mozac_browser_toolbar_edit_url_view",
            "id",
            packageName,
        )

    try {
        Log.i(TAG, "pressImeActionOnAwesomeBar: Trying pressImeActionButton via Espresso")
        onView(withId(resId)).perform(pressImeActionButton())
        Log.i(TAG, "pressImeActionOnAwesomeBar: Espresso IME action completed successfully")
    } catch (e: AppNotIdleException) {
        Log.w(
            TAG,
            "pressImeActionOnAwesomeBar: IME action failed (AppNotIdleException); falling back to UiAutomator pressEnter()",
            e,
        )
        try {
            val field = awesomeBar()
            if (field.exists()) {
                field.click()
                mDevice.pressEnter()
                Log.i(TAG, "pressImeActionOnAwesomeBar: Fallback UiAutomator pressEnter() executed successfully")
            } else {
                Log.w(TAG, "pressImeActionOnAwesomeBar: Fallback failed: awesomeBar() not found")
            }
        } catch (fallbackEx: Exception) {
            Log.e(TAG, "pressImeActionOnAwesomeBar: Fallback UiAutomator pressEnter() failed", fallbackEx)
        }
    }
}

private fun clearAddressBarButton() = itemWithResId("$packageName:id/mozac_browser_toolbar_clear_view")

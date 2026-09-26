/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.graphics.drawable.ColorDrawable
import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.filter
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasAnyDescendant
import androidx.compose.ui.test.hasAnySibling
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isDialog
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onLast
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeUp
import androidx.core.content.ContextCompat
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.PositionAssertions.isPartiallyBelow
import androidx.test.espresso.matcher.RootMatchers
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiObjectNotFoundException
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import kotlin.test.assertNotNull
import mozilla.components.browser.menu.R as menuR
import mozilla.components.compose.base.R as composeBaseR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.TABS_COUNTER
import org.hamcrest.CoreMatchers.allOf
import org.junit.Assert.assertTrue
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.DataGenerationHelper.sponsoredShortcuts
import org.mozilla.fenix.helpers.MatcherHelper.assertItemIsChecked
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithClassNameAndIndex
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithIndex
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndIndex
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.openMainMenuAndAwaitBottomSheet
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.home.topsites.TopSitesTestTag
import org.mozilla.fenix.home.topsites.TopSitesTestTag.TOP_SITE_CARD_FAVICON
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_PRIVATE_BROWSING_LEARN_MORE_LINK
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_STORY
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_WORDMARK_LOGO
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_WORDMARK_TEXT
import org.mozilla.fenix.home.ui.HomepageTestTag.POCKET_STORIES
import org.mozilla.fenix.home.ui.HomepageTestTag.PRIVATE_BROWSING_HOMEPAGE_BUTTON
import org.mozilla.fenix.tabstray.TabsTrayTestTag

/** Implementation of Robot Pattern for the home screen menu. */
class HomeScreenRobot(private val composeTestRule: ComposeTestRule) {
    fun verifyNavigationToolbar() = assertUIObjectExists(navigationToolbar())

    fun verifyHomeScreen() = assertUIObjectExists(homeScreen())

    fun verifyPrivateBrowsingHomeScreenItems() {
        // The redesigned private homepage header deliberately hides the wordmark, so we don't call the
        // shared verifyHomeScreenAppBarItems() here (it still checks the wordmark for normal-mode tests).
        assertUIObjectExists(homeScreen(), privateBrowsingButton())
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
        assertUIObjectExists(itemContainingText(getStringResource(R.string.felt_privacy_desc_card_title)))
    }

    fun verifyHomeScreenAppBarItems() =
        assertUIObjectExists(homeScreen(), privateBrowsingButton(), homepageWordmarkLogo(), homepageWordmarkText())

    fun verifyHomePrivateBrowsingButton() = assertUIObjectExists(privateBrowsingButton())

    fun verifyHomeMenuButton() = assertUIObjectExists(menuButton())

    fun verifyHomeWordmark() {
        Log.i(TAG, "verifyHomeWordmark: Scrolled 3x to the beginning of the home screen")
        assertUIObjectExists(homepageWordmarkLogo(), homepageWordmarkText())
    }

    fun verifyHomeComponent() {
        Log.i(TAG, "verifyHomeComponent: Trying to verify home screen view is visible")
        this@HomeScreenRobot.composeTestRule.onNodeWithTag(HOMEPAGE).assertIsDisplayed()
        Log.i(TAG, "verifyHomeComponent: Verified home screen view is visible")
    }

    fun verifyTabCounter(numberOfOpenTabs: String, isPrivateBrowsingEnabled: Boolean = false) {
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

    fun <A : androidx.activity.ComponentActivity, R : org.junit.rules.TestRule> verifyWindowBackgroundDrawable(
        composeTestRule: AndroidComposeTestRule<R, A>,
        expectedDrawableRes: Int,
        timeoutMillis: Long = 5_000,
    ) {
        val activity = composeTestRule.activity

        composeTestRule.waitUntil(timeoutMillis) {
            val actualBg = activity.window?.decorView?.background ?: return@waitUntil false
            val expectedBg = ContextCompat.getDrawable(activity, expectedDrawableRes) ?: return@waitUntil false

            when {
                // If both are ColorDrawables, strictly compare their actual color integers
                actualBg is ColorDrawable && expectedBg is ColorDrawable -> {
                    actualBg.color == expectedBg.color
                }
                else -> {
                    actualBg.constantState == expectedBg.constantState
                }
            }
        }
    }

    fun <A : androidx.activity.ComponentActivity, R : org.junit.rules.TestRule> verifyEdgeToEdgeWallpaperApplied(
        composeTestRule: AndroidComposeTestRule<R, A>
    ) {
        Log.i(TAG, "verifyEdgeToEdgeWallpaperApplied: Verifying edge-to-edge wallpaper background")
        verifyWindowBackgroundDrawable(composeTestRule, R.drawable.home_background_gradient)
        Log.i(TAG, "verifyEdgeToEdgeWallpaperApplied: Verified edge-to-edge wallpaper background")
    }

    fun <A : androidx.activity.ComponentActivity, R : org.junit.rules.TestRule> verifyDefaultWallpaperApplied(
        composeTestRule: AndroidComposeTestRule<R, A>
    ) {
        Log.i(TAG, "verifyDefaultWallpaperApplied: Verifying default wallpaper background")
        verifyWindowBackgroundDrawable(composeTestRule, R.color.fx_mobile_surface)
        Log.i(TAG, "verifyDefaultWallpaperApplied: Verified default wallpaper background")
    }

    fun <A : androidx.activity.ComponentActivity, R : org.junit.rules.TestRule> verifyPrivateModeBackgroundApplied(
        composeTestRule: AndroidComposeTestRule<R, A>
    ) {
        Log.i(TAG, "verifyPrivateModeBackgroundApplied: Verifying private mode background is applied")
        verifyWindowBackgroundDrawable(composeTestRule, R.color.fx_mobile_private_surface)
        Log.i(TAG, "verifyPrivateModeBackgroundApplied: Verified private mode background is applied")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyExistingTopSitesList() {
        Log.i(TAG, "verifyExistingTopSitesList: Waiting for $waitingTime ms until the top sites list exists")
        this@HomeScreenRobot.composeTestRule.waitUntilAtLeastOneExists(
            hasTestTag(TopSitesTestTag.TOP_SITES),
            timeoutMillis = waitingTime,
        )
        Log.i(TAG, "verifyExistingTopSitesList: Waited for $waitingTime ms until the top sites list to exists")
        Log.i(TAG, "verifyExistingTopSitesList: Trying to verify that the top sites list is displayed")
        this@HomeScreenRobot.composeTestRule.onNodeWithTag(TopSitesTestTag.TOP_SITES).assertIsDisplayed()
        Log.i(TAG, "verifyExistingTopSitesList: Verified that the top sites list is displayed")
    }

    fun verifyNotExistingTopSiteItem(vararg titles: String) {
        titles.forEach { title ->
            Log.i(
                TAG,
                "verifyNotExistingTopSiteItem: Waiting for $waitingTime ms for top site with title: $title to disappear",
            )
            itemContainingText(title).waitUntilGone(waitingTime)
            Log.i(
                TAG,
                "verifyNotExistingTopSiteItem: Waited for $waitingTime ms for top site with title: $title to disappear",
            )
            Log.i(TAG, "verifyNotExistingTopSiteItem: Trying to verify that top site with title: $title does not exist")
            this@HomeScreenRobot.composeTestRule.topSiteItem(title).assertDoesNotExist()
            Log.i(TAG, "verifyNotExistingTopSiteItem: Verified that top site with title: $title does not exist")
        }
    }

    fun verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle: String, position: Int) =
        assertUIObjectExists(
            itemWithResIdAndIndex("$packageName:id/top_site_item", index = position - 1)
                .getChild(UiSelector().textContains(sponsoredShortcutTitle)),
            exists = false,
        )

    fun verifyNotExistingSponsoredTopSitesList() =
        assertUIObjectExists(
            mDevice
                .findObject(UiSelector().resourceId("top_sites_list.top_site_item"))
                .getChild(UiSelector().textContains(getStringResource(R.string.top_sites_sponsored_label))),
            exists = false,
        )

    @OptIn(ExperimentalTestApi::class)
    fun verifyExistingTopSitesTabs(vararg titles: String) {
        titles.forEach { title ->
            Log.i(
                TAG,
                "verifyExistingTopSiteItem: Waiting for $waitingTime ms until the top site with title: $title exists",
            )
            this@HomeScreenRobot.composeTestRule.waitUntilAtLeastOneExists(
                hasTestTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).and(hasAnyChild(hasText(title))),
                timeoutMillis = waitingTimeLong,
            )
            Log.i(
                TAG,
                "verifyExistingTopSiteItem: Waited for $waitingTimeLong ms until the top site with title: $title exists",
            )
            Log.i(TAG, "verifyExistingTopSiteItem: Trying to verify that the top site with title: $title exists")
            this@HomeScreenRobot.composeTestRule.topSiteItem(title).assertExists()
            Log.i(TAG, "verifyExistingTopSiteItem: Verified that the top site with title: $title exists")
        }
    }

    fun verifyAddShortcutButtonExists() {
        Log.i(TAG, "verifyAddShortcutButtonExists: Trying to verify that the \"Add shortcut\" button exists")
        this@HomeScreenRobot.composeTestRule.onNodeWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertExists()
        this@HomeScreenRobot.composeTestRule
            .onNodeWithTag(TopSitesTestTag.ADD_SHORTCUT_TITLE, useUnmergedTree = true)
            .assert(hasText("Add shortcut"))
        Log.i(TAG, "verifyAddShortcutButtonExists: Verified that the \"Add shortcut\" button exists")
    }

    /**
     * Asserts the [ordinal]th sponsored shortcut has a favicon and the expected title.
     *
     * @param sponsoredShortcutTitle The shortcut title which is expected.
     * @param ordinal One-based index among the sponsored shortcuts only, matching
     *   [org.mozilla.fenix.helpers.DataGenerationHelper.getSponsoredShortcutTitle].
     */
    fun verifySponsoredShortcutDetails(sponsoredShortcutTitle: String, ordinal: Int) {
        val sponsoredShortcut = sponsoredShortcuts()[ordinal - 1]
        assertNotNull(
            sponsoredShortcut.findObject(By.res(TOP_SITE_CARD_FAVICON)),
            "Sponsored shortcut #$ordinal has no favicon",
        )
        assertNotNull(
            sponsoredShortcut.findObject(By.textContains(sponsoredShortcutTitle)),
            "Sponsored shortcut #$ordinal is not titled $sponsoredShortcutTitle",
        )
    }

    fun verifyTopSiteContextMenuItems() {
        verifyTopSiteContextMenuOpenInPrivateTabButton()
        verifyTopSiteContextMenuRemoveButton()
        verifyTopSiteContextMenuEditButton()
    }

    fun verifyTopSiteContextMenuOpenInPrivateTabButton() {
        Log.i(
            TAG,
            "verifyTopSiteContextMenuOpenInPrivateTabButton: Trying to verify that the \"Open in private tab\" menu button exists",
        )
        this@HomeScreenRobot.composeTestRule.contextMenuItemOpenInPrivateTab().assertExists()
        Log.i(
            TAG,
            "verifyTopSiteContextMenuOpenInPrivateTabButton: Verified that the \"Open in private tab\" menu button exists",
        )
    }

    fun verifyTopSiteContextMenuEditButton() {
        Log.i(TAG, "verifyTopSiteContextMenuEditButton: Trying to verify that the \"Edit\" menu button exists")
        this@HomeScreenRobot.composeTestRule.contextMenuItemEdit().assertExists()
        Log.i(TAG, "verifyTopSiteContextMenuEditButton: Verified that the \"Edit\" menu button exists")
    }

    fun verifyTopSiteContextMenuRemoveButton() {
        Log.i(TAG, "verifyTopSiteContextMenuRemoveButton: Trying to verify that the \"Remove\" menu button exists")
        this@HomeScreenRobot.composeTestRule.contextMenuItemRemove().assertExists()
        Log.i(TAG, "verifyTopSiteContextMenuRemoveButton: Verified that the \"Remove\" menu button exists")
    }

    fun verifyTopSiteContextMenuUrlErrorMessage() {
        assertUIObjectExists(itemContainingText(getStringResource(R.string.top_sites_edit_dialog_url_error)))
    }

    fun verifyContinueSectionIsDisplayed() {
        assertUIObjectExists(itemContainingText(getStringResource(R.string.recent_tabs_header_2)))
    }

    fun verifyContinueSectionIsNotDisplayed() =
        this@HomeScreenRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.recent_tabs_header_2))
            .assertIsNotDisplayed()

    fun verifyContinueItemTitle(testRule: ComposeTestRule, itemTitle: String) {
        Log.i(TAG, "verifyContinueItemTitle: Trying to verify continue item with title: $itemTitle")
        testRule.onNodeWithTag("recent.tab.title", useUnmergedTree = true).assert(hasText(itemTitle))
        Log.i(TAG, "verifyContinueItemTitle: Verified continue item with title: $itemTitle")
    }

    fun verifyContinueItemWithUrl(testRule: ComposeTestRule, itemUrl: String) {
        Log.i(TAG, "verifyContinueItemWithUrl: Trying to verify continue item with URL: $itemUrl")
        testRule.onNodeWithTag("recent.tab.url", useUnmergedTree = true).assert(hasText(itemUrl))
        Log.i(TAG, "verifyContinueItemWithUrl: Verified continue item with URL: $itemUrl")
    }

    fun verifyRecentlyVisitedSectionIsDisplayed(exists: Boolean) =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.history_metadata_header_2)), exists = exists)

    fun verifyBookmarksSectionIsDisplayed(exists: Boolean) =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.home_bookmarks_title)), exists = exists)

    fun verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed: Boolean, searchTerm: String, groupSize: Int) {
        // checks if the search group exists in the Recently visited section
        if (shouldBeDisplayed) {
            Log.i(
                TAG,
                "verifyRecentlyVisitedSearchGroupDisplayed: Trying to verify that the \"Recently visited\" section is displayed",
            )
            this@HomeScreenRobot.composeTestRule.onNodeWithText("Recently visited").assertIsDisplayed()
            Log.i(
                TAG,
                "verifyRecentlyVisitedSearchGroupDisplayed: Verified that the \"Recently visited\" section is displayed",
            )
            Log.i(
                TAG,
                "verifyRecentlyVisitedSearchGroupDisplayed: Trying to verify that the search group: $searchTerm has $groupSize pages",
            )
            this@HomeScreenRobot.composeTestRule
                .onNodeWithText(searchTerm, useUnmergedTree = true)
                .assert(hasAnySibling(hasText("$groupSize pages")))
            Log.i(
                TAG,
                "verifyRecentlyVisitedSearchGroupDisplayed: Verified that the search group: $searchTerm has $groupSize pages",
            )
        } else {
            Log.i(
                TAG,
                "verifyRecentlyVisitedSearchGroupDisplayed: Trying to verify that the search group: $searchTerm is not displayed",
            )
            this@HomeScreenRobot.composeTestRule
                .onNodeWithText(searchTerm, useUnmergedTree = true)
                .assertIsNotDisplayed()
            Log.i(
                TAG,
                "verifyRecentlyVisitedSearchGroupDisplayed: Verified that the search group: $searchTerm is not displayed",
            )
        }
    }

    // Collections elements
    @OptIn(ExperimentalTestApi::class)
    fun verifyCollectionIsDisplayed(title: String, collectionExists: Boolean = true) {
        if (collectionExists) {
            Log.i(
                TAG,
                "verifyCollectionIsDisplayed: Waiting for $waitingTime until collection with title: $title exist",
            )
            composeTestRule.waitUntilExactlyOneExists(hasText(title), waitingTime)
            Log.i(TAG, "verifyCollectionIsDisplayed: Waited for $waitingTime until collection with title: $title exist")
        } else {
            Log.i(
                TAG,
                "verifyCollectionIsDisplayed: Waiting for $waitingTime until collection with title: $title does not exist",
            )
            composeTestRule.waitUntilDoesNotExist(hasText(title), waitingTime)
            Log.i(
                TAG,
                "verifyCollectionIsDisplayed: Waited for $waitingTime until collection with title: $title does not exist",
            )
        }
    }

    fun togglePrivateBrowsingModeOnOff() {
        Log.i(TAG, "togglePrivateBrowsingModeOnOff: Trying to click private browsing home screen button")
        this@HomeScreenRobot.composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.content_description_private_browsing))
            .performClick()
        Log.i(TAG, "togglePrivateBrowsingModeOnOff: Clicked private browsing home screen button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyThoughtProvokingStories(enabled: Boolean) {
        if (enabled) {
            Log.i(
                TAG,
                "verifyThoughtProvokingStories: Trying to wait $waitingTimeLong ms for the \"$POCKET_STORIES\" node to appear in the semantics tree",
            )
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(POCKET_STORIES), timeoutMillis = waitingTimeLong)
            Log.i(TAG, "verifyThoughtProvokingStories: The \"$POCKET_STORIES\" node appeared in the semantics tree")
            Log.i(TAG, "verifyThoughtProvokingStories: Trying to scroll to the \"$POCKET_STORIES\" node")
            composeTestRule.onNodeWithTag(HOMEPAGE).performScrollToNode(hasTestTag(POCKET_STORIES))
            Log.i(TAG, "verifyThoughtProvokingStories: Scrolled to the \"$POCKET_STORIES\" node")
            Log.i(TAG, "verifyThoughtProvokingStories: Trying to verify the Pocket stories header is displayed")
            assertUIObjectExists(itemContainingText(getStringResource(R.string.pocket_stories_header_2)))
            Log.i(TAG, "verifyThoughtProvokingStories: Verified the Pocket stories header is displayed")
        } else {
            Log.i(TAG, "verifyThoughtProvokingStories: Trying to verify the Pocket stories header does not exist")
            assertUIObjectExists(
                itemContainingText(getStringResource(R.string.pocket_stories_header_2)),
                exists = false,
            )
            Log.i(TAG, "verifyThoughtProvokingStories: Verified the Pocket stories header does not exist")
        }
    }

    fun verifyPocketRecommendedStoriesItems() {
        Log.i(TAG, "verifyPocketRecommendedStoriesItems: Trying to scroll into view the \"Stories\" pocket section")
        this@HomeScreenRobot.composeTestRule.onNodeWithTag(HOMEPAGE).performScrollToNode(hasTestTag(POCKET_STORIES))
        Log.i(TAG, "verifyPocketRecommendedStoriesItems: Scrolled into view the \"Stories\" pocket section")
        for (position in 0..7) {
            Log.i(
                TAG,
                "verifyPocketRecommendedStoriesItems: Trying to scroll into view the featured pocket story from position: $position",
            )
            pocketStoriesList().scrollIntoView(UiSelector().index(position))
            Log.i(
                TAG,
                "verifyPocketRecommendedStoriesItems: Scrolled into view the featured pocket story from position: $position",
            )
            assertUIObjectExists(itemWithIndex(position))
        }
    }

    fun verifyToolbarPosition(bottomPosition: Boolean) {
        Log.i(TAG, "verifyToolbarPosition: Trying to verify toolbar is set to bottom: $bottomPosition")
        val toolbar = mDevice.findObject(UiSelector().resourceId("$packageName:id/composable_toolbar"))
        assertTrue(
            "Toolbar must be present in the view hierarchy",
            toolbar.waitForExists(waitingTime),
        )
        val toolbarCenterY = toolbar.visibleBounds.centerY()
        val screenCenter = mDevice.displayHeight / 2
        if (bottomPosition) {
            assertTrue(
                "Toolbar should be positioned at the bottom of the screen",
                toolbarCenterY > screenCenter,
            )
        } else {
            assertTrue(
                "Toolbar should be positioned at the top of the screen",
                toolbarCenterY < screenCenter,
            )
        }
        Log.i(TAG, "verifyToolbarPosition: Verified toolbar position is set to bottom: $bottomPosition")
    }

    fun verifyNavigationToolbarIsSetToTheBottomOfTheHomeScreen() {
        Log.i(TAG, "verifyAddressBarPosition: Trying to verify that the navigation toolbar is set to bottom")
        onView(withId(R.id.navigation_bar)).check(isPartiallyBelow(withId(R.id.homepageView)))
        Log.i(TAG, "verifyAddressBarPosition: Verified that the navigation toolbar is set to bottom")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyNimbusMessageCard(title: String, text: String, action: String) {
        for (str in listOf(title, text, action)) {
            composeTestRule.waitUntil(waitingTime) {
                composeTestRule
                    .onAllNodes(hasText(str), useUnmergedTree = true)
                    .fetchSemanticsNodes(atLeastOneRootRequired = false)
                    .isNotEmpty()
            }
        }
    }

    fun verifyIfInPrivateOrNormalMode(privateBrowsingEnabled: Boolean) {
        Log.i(TAG, "verifyIfInPrivateOrNormalMode: Trying to verify private browsing mode is enabled")
        assert(isPrivateModeEnabled() == privateBrowsingEnabled)
        Log.i(TAG, "verifyIfInPrivateOrNormalMode: Verified private browsing mode is enabled: $privateBrowsingEnabled")
    }

    fun verifyTheSetAsDefaultBrowserSystemDialog() {
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
        assertUIObjectExists(
            itemContainingText("Set Firefox Fenix as your default browser app?"),
            itemContainingText(appName),
            itemContainingText("Cancel"),
            itemContainingText("Set as default"),
        )
        assertItemIsChecked(
            firefoxOptionSetAsDefaultBrowserDialogRadioButton(),
            isChecked = false,
        )
    }

    fun clickTheSetAsDefaultBrowserDialogCancelButton() {
        Log.i(
            TAG,
            "clickTheSetAsDefaultBrowserDialogCancelButton: Trying to click the \"Set as default browser\" \"Cancel\" dialog button",
        )
        itemContainingText("Cancel").click()
        Log.i(
            TAG,
            "clickTheSetAsDefaultBrowserDialogCancelButton: Clicked the \"Set as default browser\" \"Cancel\" dialog button",
        )
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTheTermsOfUseOnboardingCard() {
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.onboarding_welcome_to_firefox)),
            waitingTime,
        )
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Trying to verify the \"Terms of use\" title is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.onboarding_welcome_to_firefox)).assertIsDisplayed()
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Verified the \"Terms of use\" title is displayed")
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Trying to verify the \"Terms of use\" subtitle is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.nova_onboarding_tou_subtitle)).assertIsDisplayed()
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Verified the \"Terms of use\" subtitle is displayed")
        Log.i(
            TAG,
            "verifyTheTermsOfUseOnboardingCard: Trying to verify the \"Terms of use\" first message is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(
                    R.string.nova_onboarding_tou_body_line_1,
                    argument = getStringResource(R.string.nova_onboarding_tou_body_line_1_link_text),
                ) + " " + getStringResource(composeBaseR.string.mozac_compose_base_link_text_links_available),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Verified the \"Terms of use\" first message is displayed")
        Log.i(
            TAG,
            "verifyTheTermsOfUseOnboardingCard: Trying to verify the \"Terms of use\" second message is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(
                    R.string.nova_onboarding_tou_body_line_2,
                    argument = getStringResource(R.string.nova_onboarding_tou_body_line_2_link_text),
                ) + " " + getStringResource(composeBaseR.string.mozac_compose_base_link_text_links_available),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Verified the \"Terms of use\" second message is displayed")
        Log.i(
            TAG,
            "verifyTheTermsOfUseOnboardingCard: Trying to verify the \"Terms of use\" third message is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(
                    R.string.nova_onboarding_tou_body_line_3,
                    argument = getStringResource(R.string.nova_onboarding_tou_body_line_3_link_text),
                ) + " " + getStringResource(composeBaseR.string.mozac_compose_base_link_text_links_available),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Verified the \"Terms of use\" third message is displayed")
        Log.i(
            TAG,
            "verifyTheTermsOfUseOnboardingCard: Trying to verify the \"Terms of use\" \"Continue\" button is displayed",
        )
        composeTestRule
            .onAllNodesWithText(getStringResource(R.string.nova_onboarding_continue_button))
            .onFirst()
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheTermsOfUseOnboardingCard: Verified the \"Terms of use\" \"Continue\" button is displayed")
    }

    fun clickTheOnboardingCardContinueButton() {
        Log.i(TAG, "clickTheOnboardingCardContinueButton: Trying to click the \"Continue\" button")
        composeTestRule
            .onAllNodesWithText(getStringResource(R.string.nova_onboarding_continue_button), useUnmergedTree = true)
            .onFirst()
            .performClick()
        Log.i(TAG, "clickTheOnboardingCardContinueButton: Clicked the \"Continue\" button")
        Log.i(TAG, "clickTheOnboardingCardContinueButton: Waiting for compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickTheOnboardingCardContinueButton: Waited for compose rule to be idle")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickContinueIfMarketingCardShown() {
        val marketingCardExists =
            composeTestRule
                .onAllNodes(hasText(getStringResource(R.string.nova_onboarding_marketing_title), substring = true))
                .fetchSemanticsNodes()
                .isNotEmpty()

        if (marketingCardExists) {
            Log.i(TAG, "Onboarding marketing card shown, clicking continue")
            verifyTheHelpUsBuildABetterInternetOnboardingCard()
            clickTheOnboardingCardContinueButton()
        } else {
            Log.i(TAG, "Onboarding marketing card not shown, skipping")
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTheSetAsDefaultBrowserOnboardingCard() {
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.nova_onboarding_set_to_default_title_2)),
            waitingTime,
        )
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Trying to verify the \"Set as default browser\" onboarding card title is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.nova_onboarding_set_to_default_title_2))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Verified the \"Set as default browser\" onboarding card title is displayed",
        )
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Trying to verify the \"Set as default browser\" onboarding card subtitle is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.nova_onboarding_set_to_default_subtitle))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Verified the \"Set as default browser\" onboarding cardsubtitle is displayed",
        )
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Trying to verify the \"Set as default browser\" onboarding card button is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.nova_onboarding_set_to_default_button))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Verified the \"Set as default browser\" onboarding card button is displayed",
        )
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Trying to verify the \"Set as default browser\" onboarding card \"Not now\" button is displayed",
        )
        assertUIObjectExists(itemContainingText(getStringResource(R.string.nova_onboarding_negative_button)))
        Log.i(
            TAG,
            "verifyTheSetAsDefaultBrowserOnboardingCard: Verified the \"Set as default browser\" onboarding card \"Not now\" button is displayed",
        )
    }

    fun clickTheSetAsDefaultBrowserOnboardingCardButton() {
        Log.i(
            TAG,
            "clickTheSetAsDefaultBrowserOnboardingCardButton: Trying to click the \"Set as default browser\" onboarding card button",
        )
        composeTestRule.onNodeWithText(getStringResource(R.string.nova_onboarding_set_to_default_button)).performClick()
        Log.i(
            TAG,
            "clickTheSetAsDefaultBrowserOnboardingCardButton: Clicked the \"Set as default browser\" onboarding card button",
        )
        Log.i(TAG, "clickTheSetAsDefaultBrowserOnboardingCardButton: Waiting for compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickTheSetAsDefaultBrowserOnboardingCardButton: Waited for compose rule to be idle")
    }

    fun clickNotNowOnboardingCardButton() {
        Log.i(TAG, "clickNotNowOnboardingCardButton: Trying to click the \"Not now\" onboarding card button")
        itemContainingText(getStringResource(R.string.nova_onboarding_negative_button)).click()
        Log.i(TAG, "clickNotNowOnboardingCardButton: Clicked the \"Not now\" onboarding card button")
        Log.i(TAG, "clickNotNowOnboardingCardButton: Waiting for compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickNotNowOnboardingCardButton: Waited for compose rule to be idle")
    }

    fun verifyTheHelpUsBuildABetterInternetOnboardingCard() {
        Log.i(
            TAG,
            "verifyTheHelpUsBuildABetterInternetOnboardingCard: Trying to verify the \"Help us build a better internet\" onboarding card title is displayed",
        )
        composeTestRule.onNodeWithText(getStringResource(R.string.nova_onboarding_marketing_title)).assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheHelpUsBuildABetterInternetOnboardingCard: Verified the \"Help us build a better internet\" onboarding card title is displayed",
        )
        Log.i(
            TAG,
            "verifyTheHelpUsBuildABetterInternetOnboardingCard: Trying to verify the \"Help us build a better internet\" onboarding card \"Continue\" button is displayed",
        )
        composeTestRule
            .onAllNodesWithText(getStringResource(R.string.nova_onboarding_continue_button))
            .onFirst()
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheHelpUsBuildABetterInternetOnboardingCard: Verified the \"Help us build a better internet\" onboarding card \"Continue\" button is displayed",
        )
    }

    fun swipeRightTheTermsOfUseOnboardingCard() {
        Log.i(
            TAG,
            "swipeRightTheTermsOfUseOnboardingCard: Trying to perform swipe right action on the \"Terms of use\" onboarding card",
        )
        mDevice
            .findObject(UiSelector().textContains(getStringResource(R.string.onboarding_welcome_to_firefox)))
            .swipeRight(3)
        Log.i(
            TAG,
            "swipeRightTheTermsOfUseOnboardingCard: Performed swipe right action on the \"Terms of use\" onboarding card",
        )
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyAddToHomepageBottomSheet() {
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.homepage_shortcuts_add_to_homepage)),
            waitingTime,
        )

        Log.i(TAG, "verifyAddToHomepageBottomSheet: Trying to verify the \"Add to homepage\" title is displayed")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.homepage_shortcuts_add_to_homepage))
            .assertIsDisplayed()
        Log.i(TAG, "verifyAddToHomepageBottomSheet: Verified the \"Add to homepage\" title is displayed")

        Log.i(TAG, "verifyAddToHomepageBottomSheet: Trying to verify the \"Add website\" option is displayed")
        composeTestRule.onNodeWithTag(TopSitesTestTag.ADD_WEBSITE).assertIsDisplayed()
        Log.i(TAG, "verifyAddToHomepageBottomSheet: Verified the \"Add website\" option is displayed")

        Log.i(TAG, "verifyAddToHomepageBottomSheet: Trying to verify the \"Popular sites\" section header is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.homepage_shortcuts_popular_sites)).assertIsDisplayed()
        Log.i(TAG, "verifyAddToHomepageBottomSheet: Verified the \"Popular sites\" section header is displayed")

        Log.i(TAG, "verifyAddToHomepageBottomSheet: Trying to verify the \"Facebook\" popular site item is displayed")
        composeTestRule.onNodeWithText("Facebook").assertIsDisplayed()
        Log.i(TAG, "verifyAddToHomepageBottomSheet: Verified the \"Facebook\" popular site item is displayed")
    }

    fun clickOnPopularWebsite(siteName: String) {
        Log.i(TAG, "clickOnPopularWebsite: Waiting for bottom sheet animations to settle")
        composeTestRule.waitForIdle()

        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "clickOnPopularWebsite: Started try #$i")
                Log.i(TAG, "clickOnPopularWebsite: Trying to click the '$siteName' popular site item")

                composeTestRule.onNodeWithText(siteName).performClick()

                Log.i(TAG, "clickOnPopularWebsite: Successfully clicked the '$siteName' popular site item")
                composeTestRule.waitForIdle()
                break
            } catch (e: AssertionError) {
                Log.i(TAG, "clickOnPopularWebsite: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(TAG, "clickOnPopularWebsite: Trying to swipe up the bottom sheet drag handle")
                    composeTestRule.onNodeWithContentDescription("Drag handle", ignoreCase = true).performTouchInput {
                        swipeUp()
                    }
                    Log.i(TAG, "clickOnPopularWebsite: Swiped up the bottom sheet drag handle")

                    Log.i(TAG, "clickOnPopularWebsite: Waiting for device to be idle")
                    composeTestRule.waitForIdle()
                    Log.i(TAG, "clickOnPopularWebsite: Waited for device to be idle")
                }
            }
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyEnterAWebsiteUrlDialog() {
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.homepage_shortcuts_add_website_title)),
            waitingTime,
        )

        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Trying to verify the \"Enter a Website URL\" title is displayed")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.homepage_shortcuts_add_website_title))
            .assertIsDisplayed()
        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Verified the \"Enter a Website URL\" title is displayed")

        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Trying to verify the \"URL\" input field is displayed")
        composeTestRule.onNodeWithText("URL").assertIsDisplayed()
        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Verified the \"URL\" input field is displayed")

        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Trying to verify the \"Shortcut name\" input field is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.shortcut_name_hint)).assertIsDisplayed()
        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Verified the \"Shortcut name\" input field is displayed")

        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Trying to verify the \"Cancel\" button is displayed")
        composeTestRule.addWebsiteDialogCancelButton().assertIsDisplayed()
        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Verified the \"Cancel\" button is displayed")

        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Trying to verify the \"Save\" button is displayed")
        composeTestRule.addWebsiteDialogSaveButton().assertIsDisplayed()
        Log.i(TAG, "verifyEnterAWebsiteUrlDialog: Verified the \"Save\" button is displayed")
    }

    fun enterWebsiteUrl(url: String) {
        Log.i(TAG, "enterWebsiteUrl: Trying to enter URL '$url' in Enter a Website URL dialog")
        composeTestRule.onNodeWithText("URL").performTextReplacement(url)
        Log.i(TAG, "enterWebsiteUrl: Successfully entered URL: $url")
    }

    fun enterShortcutName(shortcutName: String) {
        Log.i(TAG, "enterShortcutName: Trying to enter shortcut name '$shortcutName' in Enter a Website URL dialog")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.shortcut_name_hint))
            .performTextReplacement(shortcutName)
        Log.i(TAG, "enterShortcutName: Successfully entered shortcut name: $shortcutName")
    }

    fun clickCancelInAddWebsiteDialog() {
        Log.i(TAG, "clickCancelInAddWebsiteDialog: Trying to click the Enter a Website URL dialog \"Cancel\" button")
        composeTestRule.addWebsiteDialogCancelButton().performClick()
        Log.i(TAG, "clickCancelInAddWebsiteDialog: Clicked the Enter a Website URL dialog \"Cancel\" button")
    }

    fun clickSaveInAddWebsiteDialog() {
        Log.i(TAG, "clickSaveInAddWebsiteDialog: Clicking the Enter a Website URL dialog \"Save\" button")
        composeTestRule.addWebsiteDialogSaveButton().performClick()
        Log.i(TAG, "clickSaveInAddWebsiteDialog: Clicked the Enter a Website URL dialog \"Save\" button")
    }

    fun verifyInvalidUrlError() {
        Log.i(TAG, "verifyInvalidUrlError: Verifying the invalid URL error message is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.top_sites_edit_dialog_url_error)).assertIsDisplayed()
        Log.i(TAG, "verifyInvalidUrlError: Verified the invalid URL error message is displayed")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {

        fun openTabDrawerFromRedesignedToolbar(interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            for (i in 1..RETRY_COUNT) {
                try {
                    Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Started try #$i")
                    assertUIObjectExists(tabsCounterFromRedesignedToolbar())
                    Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Trying to click the tab counter button")
                    tabsCounter().click()
                    Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Clicked the tab counter button")
                    Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Trying to verify the tabs tray exists")
                    composeTestRule.onNodeWithTag(TabsTrayTestTag.TABS_TRAY).assertExists()
                    Log.i(TAG, "openTabDrawer: Verified the tabs tray exists")

                    break
                } catch (e: AssertionError) {
                    Log.i(TAG, "openTabDrawerFromRedesignedToolbar: AssertionError caught, executing fallback methods")
                    if (i == RETRY_COUNT) {
                        throw e
                    } else {
                        Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Waiting for device to be idle")
                        mDevice.waitForIdle()
                        Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Waited for device to be idle")
                    }
                }
            }
            Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Trying to verify the tabs tray new tab FAB button exists")
            composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).assertExists()
            Log.i(TAG, "openTabDrawerFromRedesignedToolbar: Verified the tabs tray new tab FAB button exists")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun openTabDrawer(interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            Log.i(TAG, "openTabDrawer: Trying to click the tab counter button")
            composeTestRule.onNodeWithTag(TABS_COUNTER).performClick()
            Log.i(TAG, "openTabDrawer: Clicked the tab counter button")
            Log.i(TAG, "openTabDrawer: Trying to verify the tabs tray exists")
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TABS_TRAY).assertExists()
            Log.i(TAG, "openTabDrawer: Verified the tabs tray exists")
            Log.i(TAG, "openTabDrawer: Trying to verify the tabs tray new tab FAB button exists")
            composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).assertExists()
            Log.i(TAG, "openTabDrawer: Verified the tabs tray new tab FAB button exists")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun openThreeDotMenu(interact: ThreeDotMenuMainRobot.() -> Unit): ThreeDotMenuMainRobot.Transition {
            openMainMenuAndAwaitBottomSheet(composeTestRule)

            ThreeDotMenuMainRobot(composeTestRule).interact()
            return ThreeDotMenuMainRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun openSearch(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "openSearch: Waiting for $waitingTime until the URL bar exists")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADDRESSBAR_URL_BOX), waitingTime)
            Log.i(TAG, "openSearch: Waited for $waitingTime until the URL bar exists")
            Log.i(TAG, "openSearch: Trying to click navigation toolbar")
            composeTestRule.onAllNodesWithTag(ADDRESSBAR_URL_BOX).onLast().performClick()
            Log.i(TAG, "openSearch: Clicked navigation toolbar")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun togglePrivateBrowsingMode(switchPBModeOn: Boolean = true) {
            // Ensure home screen is loaded first
            composeTestRule.waitForIdle()
            mDevice.waitForIdle()

            Log.i(TAG, "togglePrivateBrowsingMode: Waiting for $waitingTime ms for private browsing button to exist")
            if (!privateBrowsingButton().waitForExists(waitingTime)) {
                throw AssertionError(
                    "togglePrivateBrowsingMode: Private browsing button not found after $waitingTime ms"
                )
            }
            Log.i(TAG, "togglePrivateBrowsingMode: Waited for $waitingTime ms for private browsing button to exist")

            // Switch to private browsing homescreen
            if (switchPBModeOn && !isPrivateModeEnabled()) {
                Log.i(TAG, "togglePrivateBrowsingMode: Trying to click private browsing button")
                privateBrowsingButton().click()
                Log.i(TAG, "togglePrivateBrowsingMode: Clicked private browsing button")
                composeTestRule.waitForIdle()
                mDevice.waitForIdle()
            }

            // Switch to normal browsing homescreen
            if (!switchPBModeOn && isPrivateModeEnabled()) {
                Log.i(TAG, "togglePrivateBrowsingMode: Trying to click private browsing button")
                privateBrowsingButton().click()
                Log.i(TAG, "togglePrivateBrowsingMode: Clicked private browsing button")
                composeTestRule.waitForIdle()
                mDevice.waitForIdle()
            }
        }

        fun triggerPrivateBrowsingShortcutPrompt(
            interact: AddToHomeScreenRobot.() -> Unit
        ): AddToHomeScreenRobot.Transition {
            // Loop to press the PB icon for 5 times to display the Add the Private Browsing Shortcut CFR
            for (i in 1..5) {
                Log.i(
                    TAG,
                    "triggerPrivateBrowsingShortcutPrompt: Waiting for $waitingTime ms for private browsing button to exist",
                )
                mDevice
                    .findObject(UiSelector().resourceId("$packageName:id/privateBrowsingButton"))
                    .waitForExists(waitingTime)
                Log.i(
                    TAG,
                    "triggerPrivateBrowsingShortcutPrompt: Waited for $waitingTime ms for private browsing button to exist",
                )
                Log.i(TAG, "triggerPrivateBrowsingShortcutPrompt: Trying to click private browsing button")
                privateBrowsingButton().click()
                Log.i(TAG, "triggerPrivateBrowsingShortcutPrompt: Clicked private browsing button")
            }

            AddToHomeScreenRobot(composeTestRule).interact()
            return AddToHomeScreenRobot.Transition(composeTestRule)
        }

        fun pressBack() {
            Log.i(TAG, "pressBack: Trying to click device back button")
            onView(ViewMatchers.isRoot()).perform(ViewActions.pressBack())
            Log.i(TAG, "pressBack: Clicked device back button")
        }

        fun openContextMenuOnTopSitesWithTitle(
            title: String,
            interact: HomeScreenRobot.() -> Unit,
        ): Transition {
            Log.i(TAG, "openContextMenuOnTopSitesWithTitle: Trying to scroll to top site with title: $title")
            composeTestRule.topSiteItem(title).performScrollTo()
            Log.i(TAG, "openContextMenuOnTopSitesWithTitle: Scrolled to top site with title: $title")
            Log.i(TAG, "openContextMenuOnTopSitesWithTitle: Trying to long click top site with title: $title")
            composeTestRule.topSiteItem(title).performTouchInput { longClick() }
            Log.i(TAG, "openContextMenuOnTopSitesWithTitle: Long clicked top site with title: $title")

            HomeScreenRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun openTopSiteTabWithTitle(
            title: String,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(TAG, "openTopSiteTabWithTitle: Trying to scroll to top site with title: $title")
            composeTestRule.topSiteItem(title).performScrollTo()
            Log.i(TAG, "openTopSiteTabWithTitle: Scrolled to top site with title: $title")
            Log.i(TAG, "openTopSiteTabWithTitle: Trying to click top site with title: $title")
            composeTestRule.topSiteItem(title).performClick()
            Log.i(TAG, "openTopSiteTabWithTitle: Clicked top site with title: $title")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun editTopSite(
            title: String,
            url: String,
            interact: HomeScreenRobot.() -> Unit,
        ): Transition {
            Log.i(TAG, "editTopSite: Trying to click the \"Edit\" menu button")
            composeTestRule.contextMenuItemEdit().performClick()
            Log.i(TAG, "editTopSite: Clicked the \"Edit\" menu button")
            itemWithResId("$packageName:id/top_site_title").also {
                Log.i(TAG, "editTopSite: Waiting for $waitingTimeShort ms for top site name text box to exist")
                it.waitForExists(waitingTimeShort)
                Log.i(TAG, "editTopSite: Waited for $waitingTimeShort ms for top site name text box to exist")
                Log.i(TAG, "editTopSite: Trying to set top site name text box text to: $title")
                it.setText(title)
                Log.i(TAG, "editTopSite: Top site name text box text was set to: $title")
            }
            itemWithResId("$packageName:id/top_site_url").also {
                Log.i(TAG, "editTopSite: Waiting for $waitingTimeShort ms for top site url text box to exist")
                it.waitForExists(waitingTimeShort)
                Log.i(TAG, "editTopSite: Waited for $waitingTimeShort ms for top site url text box to exist")
                Log.i(TAG, "editTopSite: Trying to set top site url text box text to: $url")
                it.setText(url)
                Log.i(TAG, "editTopSite: Top site url text box text was set to: $url")
            }
            Log.i(TAG, "editTopSite: Trying to click the \"Save\" dialog button")
            itemWithResIdContainingText("android:id/button1", "Save").click()
            Log.i(TAG, "editTopSite: Clicked the \"Save\" dialog button")

            HomeScreenRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun removeTopSite(interact: HomeScreenRobot.() -> Unit): Transition {
            Log.i(TAG, "removeTopSite: Trying to click the \"Remove\" menu button")
            composeTestRule.contextMenuItemRemove().performClick()
            Log.i(TAG, "removeTopSite: Clicked the \"Remove\" menu button")
            Log.i(TAG, "removeTopSite: Waiting for $waitingTime ms until the \"Remove\" menu button does not exist")
            composeTestRule.waitUntilDoesNotExist(hasTestTag(TopSitesTestTag.REMOVE), waitingTime)
            Log.i(TAG, "removeTopSite: Waited for $waitingTime ms until the \"Remove\" menu button does not exist")

            HomeScreenRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun openTopSiteInPrivateTab(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openTopSiteInPrivateTab: Trying to click the \"Open in private tab\" menu button")
            composeTestRule.contextMenuItemOpenInPrivateTab().performClick()
            Log.i(TAG, "openTopSiteInPrivateTab: Clicked the \"Open in private tab\" menu button")
            composeTestRule.waitForIdle()

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickSponsorsAndPrivacyButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(
                TAG,
                "clickSponsorsAndPrivacyButton: Trying to click \"Our sponsors & your privacy\" context menu button and wait for $waitingTime ms for a new window",
            )
            composeTestRule.onNodeWithText(getStringResource(R.string.top_sites_menu_sponsor_privacy)).performClick()
            Log.i(
                TAG,
                "clickSponsorsAndPrivacyButton: Clicked \"Our sponsors & your privacy\" context menu button and waited for $waitingTime ms for a new window",
            )

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickSponsoredShortcutsSettingsButton(
            interact: SettingsSubMenuHomepageRobot.() -> Unit
        ): SettingsSubMenuHomepageRobot.Transition {
            Log.i(
                TAG,
                "clickSponsoredShortcutsSettingsButton: Trying to click \"Settings\" context menu button and wait for $waitingTime for a new window",
            )
            composeTestRule.onNodeWithText(getStringResource(R.string.top_sites_menu_settings)).performClick()
            Log.i(
                TAG,
                "clickSponsoredShortcutsSettingsButton: Clicked \"Settings\" context menu button and waited for $waitingTime for a new window",
            )

            SettingsSubMenuHomepageRobot().interact()
            return SettingsSubMenuHomepageRobot.Transition()
        }

        fun openPrivateBrowsingModeLearnMoreLink(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openPrivateBrowsingModeLearnMoreLink: Trying to click private browsing home screen link")
            composeTestRule.onNodeWithTag(HOMEPAGE_PRIVATE_BROWSING_LEARN_MORE_LINK).performClick()
            Log.i(TAG, "openPrivateBrowsingModeLearnMoreLink: Clicked private browsing home screen link")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickSaveTabsToCollectionButton(interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            Log.i(TAG, "clickSaveTabsToCollectionButton: Trying to click save tabs to collection button")
            saveTabsToCollectionButton(composeTestRule).performClick()
            Log.i(TAG, "clickSaveTabsToCollectionButton: Clicked save tabs to collection button")
            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun expandCollection(title: String, interact: CollectionRobot.() -> Unit): CollectionRobot.Transition {
            Log.i(TAG, "expandCollection: Trying to click collection with title: $title")
            itemContainingText(title).click()
            Log.i(TAG, "expandCollection: Clicked collection with title: $title")

            CollectionRobot(composeTestRule).interact()
            return CollectionRobot.Transition(composeTestRule)
        }

        fun openRecentlyVisitedSearchGroupHistoryList(
            title: String,
            interact: HistoryRobot.() -> Unit,
        ): HistoryRobot.Transition {
            Log.i(
                TAG,
                "openRecentlyVisitedSearchGroupHistoryList: Trying to click recently visited search group with title: $title",
            )
            composeTestRule.onNodeWithText(title).performClick()
            Log.i(
                TAG,
                "openRecentlyVisitedSearchGroupHistoryList: Clicked recently visited search group with title: $title",
            )

            HistoryRobot().interact()
            return HistoryRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickPocketStoryItem(position: Int, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickPocketStoryItem: Trying to scroll to the \"$POCKET_STORIES\" section")
            composeTestRule.onNodeWithTag(HOMEPAGE).performScrollToNode(hasTestTag(POCKET_STORIES))
            Log.i(TAG, "clickPocketStoryItem: Scrolled to the \"$POCKET_STORIES\" section")
            composeTestRule.waitForIdle()
            Log.i(
                TAG,
                "clickPocketStoryItem: Trying to wait $waitingTimeLong ms for at least one \"$HOMEPAGE_STORY\" node to appear",
            )
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(HOMEPAGE_STORY), timeoutMillis = waitingTimeLong)
            val storyNodes = composeTestRule.onAllNodesWithTag(HOMEPAGE_STORY)
            val storyNodeList = storyNodes.fetchSemanticsNodes()
            check(position in 1..storyNodeList.size) {
                "clickPocketStoryItem: requested position $position but only ${storyNodeList.size} \"$HOMEPAGE_STORY\" nodes found"
            }
            Log.i(
                TAG,
                "clickPocketStoryItem: \"$HOMEPAGE_STORY\" nodes are present, scrolling item at position $position into view",
            )
            storyNodes[position - 1].performScrollTo()
            composeTestRule.waitForIdle()
            Log.i(TAG, "clickPocketStoryItem: Trying to click pocket story item at position $position")
            storyNodes[position - 1].performClick()
            Log.i(TAG, "clickPocketStoryItem: Clicked pocket story item at position $position")
            composeTestRule.waitForIdle()
            mDevice.waitForIdle()

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickSetAsDefaultBrowserOnboardingButton(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(
                TAG,
                "clickSetAsDefaultBrowserOnboardingButton: Trying to click \"Set as default browser\" onboarding button",
            )
            composeTestRule
                .onNodeWithText(getStringResource(R.string.nova_onboarding_set_to_default_button))
                .performClick()
            Log.i(TAG, "clickSetAsDefaultBrowserOnboardingButton: Clicked \"Set as default browser\" onboarding button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickAddShortcutButton(interact: HomeScreenRobot.() -> Unit): Transition {
            Log.i(TAG, "clickAddShortcutButton: Trying to click the \"Add shortcut\" shortcut button")
            composeTestRule.onNodeWithTag(TopSitesTestTag.ADD_SHORTCUT_TITLE, useUnmergedTree = true).performClick()
            Log.i(TAG, "clickAddShortcutButton: Clicked the \"Add shortcut\" shortcut button")

            HomeScreenRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickAddWebsiteButton(interact: HomeScreenRobot.() -> Unit): Transition {
            Log.i(TAG, "clickAddWebsiteButton: Trying to click the \"Add website\" button")
            composeTestRule.onNodeWithTag(TopSitesTestTag.ADD_WEBSITE, useUnmergedTree = true).performClick()
            Log.i(TAG, "clickAddWebsiteButton: Clicked the \"Add website\" button")

            HomeScreenRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }
    }
}

fun homeScreen(composeTestRule: ComposeTestRule, interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
    HomeScreenRobot(composeTestRule).interact()
    return HomeScreenRobot.Transition(composeTestRule)
}

private fun saveTabsToCollectionButton(composeTestRule: ComposeTestRule) =
    composeTestRule.onNodeWithText(getStringResource(R.string.tabs_menu_save_to_collection1))

private fun tabsCounterFromRedesignedToolbar() = itemWithResId("$packageName:id/counter_box")

private fun tabsCounter() = mDevice.findObject(By.res("$packageName:id/counter_root"))

private fun homeScreen() = itemWithResId("$packageName:id/homepageView")

private fun privateBrowsingButton() = itemWithResId(PRIVATE_BROWSING_HOMEPAGE_BUTTON)

private fun isPrivateModeEnabled(): Boolean {
    return try {
        itemWithResId(PRIVATE_BROWSING_HOMEPAGE_BUTTON).isChecked
    } catch (e: UiObjectNotFoundException) {
        false
    }
}

private fun homepageWordmarkLogo() = itemWithResId(HOMEPAGE_WORDMARK_LOGO)

private fun homepageWordmarkText() = itemWithResId(HOMEPAGE_WORDMARK_TEXT)

private fun navigationToolbar() = itemWithResId("$packageName:id/composable_toolbar")

private fun menuButton() = itemWithDescription(getStringResource(R.string.content_description_menu))

fun deleteFromHistory() =
    onView(
            allOf(
                withId(menuR.id.simple_text),
                withText(R.string.delete_from_history),
            )
        )
        .inRoot(RootMatchers.isPlatformPopup())

private fun pocketStoriesList() = UiScrollable(UiSelector().resourceId(POCKET_STORIES)).setAsHorizontalList()

private fun firefoxOptionSetAsDefaultBrowserDialogRadioButton() =
    itemWithClassNameAndIndex(
            className = "android.widget.RadioButton",
            index = 2,
        )
        .getFromParent(UiSelector().className("android.widget.LinearLayout").index(1))

private fun ComposeTestRule.topSiteItem(title: String) =
    onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).filter(hasAnyChild(hasText(title))).onFirst()

private fun ComposeTestRule.contextMenuItemOpenInPrivateTab() =
    onAllNodesWithTag(TopSitesTestTag.OPEN_IN_PRIVATE_TAB).onFirst()

private fun ComposeTestRule.contextMenuItemEdit() = onAllNodesWithTag(TopSitesTestTag.EDIT).onFirst()

private fun ComposeTestRule.contextMenuItemRemove() = onAllNodesWithTag(TopSitesTestTag.REMOVE).onFirst()

private fun ComposeTestRule.addWebsiteDialogCancelButton() =
    onNode(
        hasText("Cancel")
            .and(
                hasAnyAncestor(
                    isDialog()
                        .and(
                            hasAnyDescendant(hasText(getStringResource(R.string.homepage_shortcuts_add_website_title)))
                        )
                )
            )
    )

private fun ComposeTestRule.addWebsiteDialogSaveButton() =
    onNode(
        hasText("Save")
            .and(
                hasAnyAncestor(
                    isDialog()
                        .and(
                            hasAnyDescendant(hasText(getStringResource(R.string.homepage_shortcuts_add_website_title)))
                        )
                )
            )
    )

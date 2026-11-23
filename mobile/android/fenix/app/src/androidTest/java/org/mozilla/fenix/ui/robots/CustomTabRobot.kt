/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui.robots

import android.net.Uri
import android.util.Log
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.UiSelector
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.feature.customtabs.R as customtabsR
import org.mozilla.fenix.Config
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuTestTag
import org.mozilla.fenix.helpers.Constants.LONG_CLICK_DURATION
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.TestHelper.waitForObjects
import org.mozilla.fenix.pdf.PdfToolsTestTag

/** Implementation of the robot pattern for Custom tabs */
class CustomTabRobot(private val composeTestRule: ComposeTestRule) {

    fun verifyCustomTabsSiteInfoButton() {
        Log.i(TAG, "verifyCustomTabsSiteInfoButton: Trying to verify that the site info button is displayed")
        composeTestRule.onNodeWithContentDescription("Site information").assertIsDisplayed()
        Log.i(TAG, "verifyCustomTabsSiteInfoButton: Verified that the site info button is displayed")
    }

    fun verifyMainMenuButton() {
        Log.i(TAG, "verifyMainMenuButton: Trying to verify that the main menu button is displayed")
        composeTestRule.onNodeWithContentDescription("More options").assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuButton: Verified that the main menu button is displayed")
    }

    fun verifyDesktopSiteButtonExists() {
        Log.i(TAG, "verifyDesktopSiteButtonExists: Trying to verify that the request desktop site button is displayed")
        desktopSiteButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyDesktopSiteButtonExists: Verified that the request desktop site button is displayed")
    }

    fun verifyFindInPageButtonExists() {
        Log.i(TAG, "verifyFindInPageButtonExists: Trying to verify that the find in page button is displayed")
        findInPageButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyFindInPageButtonExists: Verified that the find in page button is displayed")
    }

    fun verifyPoweredByTextIsDisplayed() = assertUIObjectExists(itemContainingText("POWERED BY $appName"))

    fun verifyOpenInBrowserButtonExists() {
        Log.i(TAG, "verifyOpenInBrowserButtonExists: Trying to verify that the \"Open in Firefox\" button is displayed")
        openInBrowserButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyOpenInBrowserButtonExists: Verified that the \"Open in Firefox\" button is displayed")
    }

    fun verifyOpenInBrowserComposeButtonExists() {
        Log.i(
            TAG,
            "verifyOpenInBrowserComposeButtonExists: Trying to verify that the \"Open in Firefox\" button is displayed",
        )
        composeTestRule.openInBrowserButtonFromRedesignedToolbar().assertIsDisplayed()
        Log.i(TAG, "verifyOpenInBrowserComposeButtonExists: Verified that the \"Open in Firefox\" button is displayed")
    }

    fun verifyBackButtonExists() = assertUIObjectExists(itemWithDescription("Back"))

    fun verifyForwardButtonExists() = assertUIObjectExists(itemWithDescription("Forward"))

    fun verifyRefreshButtonExists() = assertUIObjectExists(itemWithDescription("Refresh"))

    fun verifyCustomMenuItem(label: String) = assertUIObjectExists(itemContainingText(label))

    fun verifyCustomTabCloseButton() {
        Log.i(TAG, "verifyCustomTabCloseButton: Trying to verify that the close custom tab button is displayed")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(customtabsR.string.mozac_feature_customtabs_exit_button))
            .assertIsDisplayed()
        Log.i(TAG, "verifyCustomTabCloseButton: Verified that the close custom tab button is displayed")
    }

    fun verifyCustomTabToolbarTitle(title: String) {
        Log.i(TAG, "verifyCustomTabToolbarTitle: Trying to verify that the custom tab title: $title is displayed")
        composeTestRule.waitUntil(waitingTime) {
            composeTestRule
                .onAllNodes(hasText(title, substring = true), useUnmergedTree = true)
                .fetchSemanticsNodes(atLeastOneRootRequired = false)
                .isNotEmpty()
        }
        composeTestRule.onNodeWithText(title, substring = true, useUnmergedTree = true).assertExists()
        Log.i(TAG, "verifyCustomTabToolbarTitle: Verified that the custom tab title: $title is displayed")
    }

    fun verifyCustomTabUrl(url: String) {
        val uri = Uri.parse(url)
        val expectedText = uri.host ?: url // fallback if host is null
        Log.i(TAG, "verifyCustomTabUrl: Trying to verify that the custom tab url: $expectedText is displayed")
        composeTestRule.onNodeWithText(expectedText, substring = true, useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyCustomTabUrl: Verified that the custom tab url: $expectedText is displayed")
    }

    fun longClickAndCopyToolbarUrl() {
        Log.i(TAG, "longClickAndCopyToolbarUrl: Trying to long click the custom tab toolbar")
        composeTestRule.onAllNodes(hasTestTag(ADDRESSBAR_URL_BOX)).onFirst().performTouchInput {
            down(center)
            advanceEventTime(10000)
            up()
        }
        Log.i(TAG, "longClickAndCopyToolbarUrl: Long clicked the custom tab toolbar")
        Log.i(TAG, "longClickAndCopyToolbarUrl: Waiting for compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "longClickAndCopyToolbarUrl: Waited for compose rule to be idle")
        Log.i(TAG, "longClickAndCopyToolbarUrl: Trying to click the \"Copy\" option")
        composeTestRule.onNodeWithText("Copy", useUnmergedTree = true).performClick()
        Log.i(TAG, "longClickAndCopyToolbarUrl: Clicked the \"Copy\" option")
    }

    fun fillAndSubmitLoginCredentials(userName: String, password: String) {
        Log.i(TAG, "fillAndSubmitLoginCredentials: Waiting for device to be idle for $waitingTime ms")
        mDevice.waitForIdle(waitingTime)
        Log.i(TAG, "fillAndSubmitLoginCredentials: Waited for device to be idle for $waitingTime ms")
        setPageObjectText(composeTestRule, itemWithResId("username"), userName)
        waitForAppWindowToBeUpdated()
        setPageObjectText(composeTestRule, itemWithResId("password"), password)
        waitForAppWindowToBeUpdated()
        clickPageObject(composeTestRule, itemWithResId("submit"))
        mDevice.waitForObjects(
            mDevice.findObject(UiSelector().resourceId("$packageName:id/save_confirm")),
            waitingTime,
        )
    }

    fun waitForPageToLoad(pageLoadWaitingTime: Long = waitingTime) {
        Log.i(TAG, "waitForPageToLoad: Waiting for $waitingTime ms until progress bar is gone")
        progressBar().waitUntilGone(pageLoadWaitingTime)
        Log.i(TAG, "waitForPageToLoad: Waited for $waitingTime ms until progress bar was gone")
    }

    fun clickCustomTabCloseButton() {
        Log.i(TAG, "clickCustomTabCloseButton: Trying to click close custom tab button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(customtabsR.string.mozac_feature_customtabs_exit_button))
            .performClick()
        Log.i(TAG, "clickCustomTabCloseButton: Clicked close custom tab button")
    }

    fun verifyCustomTabActionButton(customTabActionButtonDescription: String) {
        Log.i(TAG, "verifyCustomTabActionButton: Trying to verify that the custom tab action button is displayed")
        composeTestRule.onNodeWithContentDescription(customTabActionButtonDescription).assertIsDisplayed()
        Log.i(TAG, "verifyCustomTabActionButton: Verified that the custom tab action button is displayed")
    }

    fun verifyPDFReaderToolbarItems() {
        // Bug 2081247 to switch to new toolbar for testing
        if (!Config.channel.isNightlyOrDebug) {
            assertUIObjectExists(itemWithResIdContainingText("download", "Download"))
            return
        }

        Log.i(TAG, "verifyPDFReaderToolbarItems: Waiting for $waitingTime until the PDF tools bar exists")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(PdfToolsTestTag.BAR), waitingTime)
        Log.i(TAG, "verifyPDFReaderToolbarItems: Waited for $waitingTime until the PDF tools bar exists")
        listOf(
                R.string.pdf_tools_sign_content_description_2,
                R.string.pdf_tools_download_content_description,
                R.string.pdf_tools_print_content_description,
                R.string.pdf_tools_share_content_description,
            )
            .forEach { resId ->
                val description = getStringResource(resId)
                Log.i(TAG, "verifyPDFReaderToolbarItems: Trying to verify that $description is displayed")
                composeTestRule.onNodeWithContentDescription(description).assertIsDisplayed()
                Log.i(TAG, "verifyPDFReaderToolbarItems: Verified that $description is displayed")
            }
    }

    fun verifyCustomTabsMainMenuItems(
        customMenuItem: String,
        exist: Boolean,
        waitingTime: Long = TestAssetHelper.waitingTime,
    ) {
        listOf(
                R.string.browser_menu_back,
                R.string.browser_menu_forward,
                R.string.browser_menu_refresh,
                R.string.browser_menu_share,
            )
            .forEach { resId ->
                val pageControl = pageControl(getStringResource(resId))
                if (exist) {
                    composeTestRule.waitUntilAtLeastOneExists(pageControl, waitingTime)
                } else {
                    composeTestRule.waitUntilDoesNotExist(pageControl, waitingTime)
                }
            }
        assertUIObjectExists(
            itemWithText(getStringResource(R.string.browser_menu_open_in_fenix, appName)),
            itemWithText(getStringResource(R.string.browser_menu_find_in_page)),
            itemWithText(getStringResource(R.string.browser_menu_desktop_site)),
            itemContainingText(customMenuItem),
            itemContainingText("Powered by $appName"),
            exists = exist,
            waitingTime = waitingTime,
        )
    }

    fun verifyBookmarkThisPageButton() {
        Log.i(TAG, "verifyBookmarkThisPageButton: Trying to verify that the \"Bookmark this page\" button is displayed")
        composeTestRule.bookmarkPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyBookmarkThisPageButton: Verified that the \"Bookmark this page\" button is displayed")
    }

    fun verifyEditBookmarkButton() {
        Log.i(TAG, "verifyEditBookmarkButton: Trying to verify that the \"Edit bookmark\" button is displayed")
        composeTestRule.editBookmarkButton().assertIsDisplayed()
        Log.i(TAG, "verifyEditBookmarkButton: Verified that the \"Edit bookmark\" button is displayed")
    }

    fun verifySwitchToDesktopSiteButton() {
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Trying to verify that the \"Desktop site\" button is displayed.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Verified that the \"Switch to desktop site\" button is displayed.")
    }

    fun verifyDesktopSiteButtonState(isEnabled: Boolean) {
        if (isEnabled) {
            Log.i(
                TAG,
                "verifyDesktopSiteButtonState: Trying to verify that the \"Desktop site\" button is set to \"On\".",
            )
            composeTestRule.enabledDesktopSiteButton().assertIsDisplayed()
            Log.i(TAG, "verifyDesktopSiteButtonState: Verified that the \"Desktop site\" button is set to \"On\".")
        } else {
            Log.i(
                TAG,
                "verifyDesktopSiteButtonState: Trying to verify that the \"Desktop site\" button is set to \"Off\".",
            )
            composeTestRule.disabledDesktopSiteButton().assertIsDisplayed()
            Log.i(TAG, "verifyDesktopSiteButtonState: Verified that the \"Desktop site\" button is set to \"Off\".")
        }
    }

    fun verifyDesktopSiteButtonIsDisabled() {
        Log.i(TAG, "verifyDesktopSiteButtonIsDisabled: Trying to verify that the \"Desktop site\" button is disabled.")
        composeTestRule.desktopSiteButton().assertIsNotEnabled()
        Log.i(TAG, "verifyDesktopSiteButtonIsDisabled: Verified that the \"Desktop site\" button is disabled.")
    }

    fun clickSwitchToDesktopSiteButton() {
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Trying to click the \"Desktop site\" button.")
        composeTestRule.desktopSiteButton().performClick()
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Clicked the \"Desktop site\" button.")
    }

    fun clickBookmarkThisPageButton() {
        Log.i(
            TAG,
            "clickBookmarkThisPageButton: Trying to click the \"Bookmark this page\" button from the custom tab main menu.",
        )
        composeTestRule.bookmarkPageButton().performClick()
        Log.i(
            TAG,
            "clickBookmarkThisPageButton: Clicked the \"Bookmark this page\" button from the custom tab main menu.",
        )
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun openMainMenu(interact: CustomTabRobot.() -> Unit): Transition {
            Log.i(TAG, "openMainMenu: Trying to click the main menu button")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.content_description_menu))
                .performClick()
            Log.i(TAG, "openMainMenu: Clicked the main menu button")

            CustomTabRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun clickOpenInBrowserButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOpenInBrowserButton: Trying to click the \"Open in Firefox\" button")
            openInBrowserButton().perform(click())
            Log.i(TAG, "clickOpenInBrowserButton: Clicked the \"Open in Firefox\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickOpenInBrowserButtonFromRedesignedToolbar(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOpenInBrowserButtonFromRedesignedToolbar: Trying to click the \"Open in Firefox\" button")
            composeTestRule.openInBrowserButtonFromRedesignedToolbar().performClick()
            Log.i(TAG, "clickOpenInBrowserButtonFromRedesignedToolbar: Clicked the \"Open in Firefox\" button")
            Log.i(TAG, "clickOpenInBrowserButtonFromRedesignedToolbar: Waiting for device to be idle to be idle")
            mDevice.waitForIdle(waitingTime)
            Log.i(TAG, "clickOpenInBrowserButtonFromRedesignedToolbar: Waited for device to be idle")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickShareButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareButton: Trying to click the share button")
            itemWithDescription(getStringResource(customtabsR.string.mozac_feature_customtabs_share_link))
                .waitForExists(waitingTime)
            itemWithDescription(getStringResource(customtabsR.string.mozac_feature_customtabs_share_link)).click()
            Log.i(TAG, "clickShareButton: Clicked the share button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickShareButtonFromRedesignedMenu(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(
                TAG,
                "clickShareButtonFromRedesignedMenu: Trying to click the redesigned main menu share button from custom tab",
            )
            composeTestRule.shareButton().performClick()
            Log.i(
                TAG,
                "clickShareButtonFromRedesignedMenu: Clicked the redesigned main menu share button from custom tab",
            )

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickEditBookmarkButton(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(
                TAG,
                "clickEditBookmarkButton: Trying to click the \"Edit bookmark\" button from the custom tab main menu.",
            )
            composeTestRule.editBookmarkButton().performClick()
            Log.i(TAG, "clickEditBookmarkButton: Clicked the \"Edit bookmark\" button from the custom tab main menu.")

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickFindInPageButton(interact: FindInPageRobot.() -> Unit): FindInPageRobot.Transition {
            Log.i(
                TAG,
                "clickFindInPageButton: Trying to click the \"Find In Page\" button from the new main menu design.",
            )
            composeTestRule.findInPageButton().performClick()
            Log.i(TAG, "clickFindInPageButton: Clicked the \"Find In Page\" button from the new main menu design.")

            FindInPageRobot().interact()
            return FindInPageRobot.Transition()
        }

        fun clickOutsideTheMainMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOutsideTheMainMenu: Trying to click outside the main menu.")
            itemWithResId("$packageName:id/touch_outside").clickTopLeft()
            Log.i(TAG, "clickOutsideTheMainMenu: Clicked click outside the main menu.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickBackButtonFromMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickBackButtonFromMenu: Trying to click the \"Back\" button from custom tab main menu.")
            composeTestRule.backButton().performClick()
            Log.i(TAG, "clickBackButtonFromMenu: Clicked the \"Back\" button from custom tab main menu.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun longClickBackButtonFromMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "longClickBackButtonFromMenu: Trying to long-click the \"Back\" button")
            composeTestRule.backButton().performTouchInput {
                longClick(durationMillis = LONG_CLICK_DURATION)
            }
            Log.i(TAG, "longClickBackButtonFromMenu: long-clicked the \"Back\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickForwardButtonFromMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickForwardButtonFromMenu: Trying to click the \"Forward\" button from custom tab main menu.")
            composeTestRule.forwardButton().performClick()
            Log.i(TAG, "clickForwardButtonFromMenu: Clicked the \"Forward\" button from custom tab main menu.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickRefreshButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickRefreshButton: Trying to click the \"Refresh\" button from custom tab main menu.")
            composeTestRule.refreshButton().performClick()
            Log.i(TAG, "clickRefreshButton: Clicked the \"Refresh\" button from custom tab main menu.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
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
    }
}

fun customTabScreen(composeTestRule: ComposeTestRule, interact: CustomTabRobot.() -> Unit): CustomTabRobot.Transition {
    CustomTabRobot(composeTestRule).interact()
    return CustomTabRobot.Transition(composeTestRule)
}

private fun desktopSiteButton() = onView(withId(R.id.switchWidget))

private fun findInPageButton() = onView(withText("Find in page"))

private fun openInBrowserButton() = onView(withText("Open in $appName"))

private fun ComposeTestRule.openInBrowserButtonFromRedesignedToolbar() =
    onNodeWithText(getStringResource(R.string.browser_menu_open_in_fenix, appName))

private fun progressBar() =
    mDevice.findObject(UiSelector().resourceId("$packageName:id/mozac_browser_toolbar_progress"))

private fun ComposeTestRule.desktopSiteButton() = onNodeWithText(getStringResource(R.string.browser_menu_desktop_site))

private fun ComposeTestRule.enabledDesktopSiteButton() =
    onNode(
        hasText(getStringResource(R.string.browser_menu_desktop_site))
            .and(
                SemanticsMatcher.expectValue(
                    SemanticsProperties.StateDescription,
                    getStringResource(R.string.browser_feature_desktop_site_on),
                )
            )
    )

private fun ComposeTestRule.disabledDesktopSiteButton() =
    onNode(
        hasText(getStringResource(R.string.browser_menu_desktop_site))
            .and(
                SemanticsMatcher.expectValue(
                    SemanticsProperties.StateDescription,
                    getStringResource(R.string.browser_feature_desktop_site_off),
                )
            )
    )

private fun ComposeTestRule.findInPageButton() = onNodeWithText(getStringResource(R.string.browser_menu_find_in_page))

/** A page control of the main menu, excluding toolbar buttons with the same content description. */
private fun pageControl(description: String) =
    hasContentDescription(description).and(hasAnyAncestor(hasTestTag(MenuTestTag.MAIN_MENU)))

private fun ComposeTestRule.pageControlButton(description: String) = onNode(pageControl(description))

private fun ComposeTestRule.bookmarkPageButton() =
    pageControlButton(getStringResource(R.string.browser_menu_bookmark_this_page_2))

private fun ComposeTestRule.editBookmarkButton() =
    pageControlButton(getStringResource(R.string.browser_menu_edit_bookmark))

private fun ComposeTestRule.backButton() = pageControlButton(getStringResource(R.string.browser_menu_back))

private fun ComposeTestRule.forwardButton() = pageControlButton(getStringResource(R.string.browser_menu_forward))

private fun ComposeTestRule.refreshButton() = pageControlButton(getStringResource(R.string.browser_menu_refresh))

private fun ComposeTestRule.shareButton() = pageControlButton(getStringResource(R.string.browser_menu_share))

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiSelector
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.LONG_CLICK_DURATION
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescriptionAndIndex
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.TestHelper.waitForObjects
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.isChecked

/**
 *  Implementation of the robot pattern for Custom tabs
 */
class CustomTabRobot {

    fun verifyCustomTabsSiteInfoButton() =
        assertUIObjectExists(
            itemWithResId("$packageName:id/mozac_browser_toolbar_site_info_indicator"),
        )

    fun verifyCustomTabsShareButton() =
        assertUIObjectExists(
            itemWithDescription(getStringResource(R.string.mozac_feature_customtabs_share_link)),
        )

    fun verifyMainMenuButton() = assertUIObjectExists(mainMenuButton())

    fun verifyMainMenuComposeButton() = assertUIObjectExists(mainMenuButtonFromRedesignedToolbar())

    fun verifyDesktopSiteButtonExists() {
        Log.i(TAG, "verifyDesktopSiteButtonExists: Trying to verify that the request desktop site button is displayed")
        desktopSiteButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyDesktopSiteButtonExists: Verified that the request desktop site button is displayed")
    }

    fun verifyRequestDesktopSiteToggleState(isEnabled: Boolean) {
        Log.i(TAG, "verifyRequestDesktopSiteToggleState: Trying to verify that the request desktop site toggle is enabled : $isEnabled")
        desktopSiteButton().check(matches(isChecked(isEnabled)))
        Log.i(TAG, "verifyRequestDesktopSiteToggleState: Verified that the request desktop site toggle is enabled : $isEnabled")
    }

    fun verifyFindInPageButtonExists() {
        Log.i(TAG, "verifyFindInPageButtonExists: Trying to verify that the find in page button is displayed")
        findInPageButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyFindInPageButtonExists: Verified that the find in page button is displayed")
    }

    fun verifyPoweredByTextIsDisplayed() =
        assertUIObjectExists(itemContainingText("POWERED BY $appName"))

    fun verifyOpenInBrowserButtonExists() {
        Log.i(TAG, "verifyOpenInBrowserButtonExists: Trying to verify that the \"Open in Firefox\" button is displayed")
        openInBrowserButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyOpenInBrowserButtonExists: Verified that the \"Open in Firefox\" button is displayed")
    }

    fun verifyOpenInBrowserComposeButtonExists() {
        Log.i(TAG, "verifyOpenInBrowserComposeButtonExists: Trying to verify that the \"Open in Firefox\" button is displayed")
        assertUIObjectExists(openInBrowserButtonFromRedesignedToolbar())
        Log.i(TAG, "verifyOpenInBrowserComposeButtonExists: Verified that the \"Open in Firefox\" button is displayed")
    }

    fun verifyBackButtonExists() = assertUIObjectExists(itemWithDescription("Back"))

    fun verifyForwardButtonExists() = assertUIObjectExists(itemWithDescription("Forward"))

    fun verifyRefreshButtonExists() = assertUIObjectExists(itemWithDescription("Refresh"))

    fun verifyCustomMenuItem(label: String) = assertUIObjectExists(itemContainingText(label))

    fun verifyCustomTabCloseButton() {
        Log.i(TAG, "verifyCustomTabCloseButton: Trying to verify that the close custom tab button is displayed")
        closeButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTabCloseButton: Verified that the close custom tab button is displayed")
    }

    fun verifyCustomTabToolbarTitle(title: String) {
        waitForPageToLoad()

        mDevice.waitForObjects(
            mDevice.findObject(
                UiSelector()
                    .resourceId("$packageName:id/mozac_browser_toolbar_title_view")
                    .textContains(title),
            )
                .getFromParent(
                    UiSelector().resourceId("$packageName:id/mozac_browser_toolbar_origin_view"),
                ),
            waitingTime,
        )

        assertUIObjectExists(
            itemWithResIdContainingText("$packageName:id/mozac_browser_toolbar_title_view", title),
        )
    }

    fun verifyCustomTabUrl(Url: String) {
        assertUIObjectExists(
            itemWithResIdContainingText("$packageName:id/mozac_browser_toolbar_url_view", Url.drop(7)),
        )
    }

    fun longCLickAndCopyToolbarUrl() {
        mDevice.waitForObjects(
            mDevice.findObject(UiSelector().resourceId("$packageName:id/toolbar")),
            waitingTime,
        )
        Log.i(TAG, "longCLickAndCopyToolbarUrl: Trying to long click the custom tab toolbar")
        customTabToolbar().click(LONG_CLICK_DURATION)
        Log.i(TAG, "longCLickAndCopyToolbarUrl: Long clicked the custom tab toolbar")
        clickContextMenuItem("Copy")
    }

    fun fillAndSubmitLoginCredentials(userName: String, password: String) {
        Log.i(TAG, "fillAndSubmitLoginCredentials: Waiting for device to be idle for $waitingTime ms")
        mDevice.waitForIdle(waitingTime)
        Log.i(TAG, "fillAndSubmitLoginCredentials: Waited for device to be idle for $waitingTime ms")
        setPageObjectText(itemWithResId("username"), userName)
        waitForAppWindowToBeUpdated()
        setPageObjectText(itemWithResId("password"), password)
        waitForAppWindowToBeUpdated()
        clickPageObject(itemWithResId("submit"))
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
        closeButton().click()
        Log.i(TAG, "clickCustomTabCloseButton: Clicked close custom tab button")
    }

    fun verifyCustomTabActionButton(customTabActionButtonDescription: String) =
        assertUIObjectExists(itemWithDescription(customTabActionButtonDescription))

    fun verifyPDFReaderToolbarItems() =
        assertUIObjectExists(
            itemWithResIdAndText("download", "Download"),
        )

    fun verifyRedesignedCustomTabsMainMenuItemsExist(customMenuItem: String, exist: Boolean, waitingTime: Long = TestAssetHelper.waitingTime) =
        assertUIObjectExists(
            itemWithDescription(getStringResource(R.string.browser_menu_switch_to_desktop_site)),
            itemWithDescription(getStringResource(R.string.browser_menu_find_in_page_2)),
            itemWithDescriptionAndIndex("Open in $appName", 2),
            itemWithDescription(getStringResource(R.string.browser_menu_share_2)),
            itemContainingText(customMenuItem),
            exists = exist,
            waitingTime = waitingTime,
        )

    fun verifySwitchToDesktopSiteButtonIsEnabled(
        composeTestRule: ComposeTestRule,
        isEnabled: Boolean,
    ) {
        Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Trying to verify that the \"Switch to Desktop Site\" button from the new main menu design is enabled.")
        if (isEnabled) {
            composeTestRule.desktopSiteButton().assertIsEnabled()
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Verified that the \"Switch to Desktop Site\" button from the new main menu design is enabled.")
        } else {
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Trying to verify that the \"Switch to Desktop Site\" button from the new main menu design is disabled.")
            composeTestRule.desktopSiteButton().assertIsNotEnabled()
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Verified the \"Switch to Desktop Site\" button from the new main menu design is disabled.")
        }
    }

    fun verifySwitchToDesktopSiteButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Trying to verify that the \"Switch to desktop site\" button is displayed.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Verified that the \"Switch to desktop site\" button is displayed.")
    }

    fun verifySwitchToMobileSiteButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifySwitchToMobileSiteButton: Trying to verify that the \"Switch to mobile site\" button is displayed.")
        composeTestRule.mobileSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToMobileSiteButton: Verified that the \"Switch to mobile site\" button is displayed.")
    }

    fun clickSwitchToDesktopSiteButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Trying to click the \"Switch to desktop site\" button.")
        composeTestRule.desktopSiteButton().performClick()
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Clicked the \"Switch to desktop site\" button.")
    }

    fun clickSwitchToMobileSiteButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickSwitchToMobileSiteButton: Trying to click the \"Switch to mobile site\" button.")
        composeTestRule.mobileSiteButton().performClick()
        Log.i(TAG, "clickSwitchToMobileSiteButton: Clicked the \"Switch to mobile site\" button.")
    }

    class Transition {
        fun openMainMenu(interact: CustomTabRobot.() -> Unit): Transition {
            mainMenuButton().also {
                Log.i(TAG, "openMainMenu: Waiting for $waitingTime ms for the main menu button to exist")
                it.waitForExists(waitingTime)
                Log.i(TAG, "openMainMenu: Waited for $waitingTime ms for the main menu button to exist")
                Log.i(TAG, "openMainMenu: Trying to click the main menu button")
                it.click()
                Log.i(TAG, "openMainMenu: Clicked the main menu button")
            }

            CustomTabRobot().interact()
            return Transition()
        }

        fun openMainMenuFromRedesignedToolbar(interact: CustomTabRobot.() -> Unit): Transition {
            mainMenuButtonFromRedesignedToolbar().also {
                Log.i(TAG, "openMainMenuFromRedesignedToolbar: Waiting for $waitingTime ms for the main menu button to exist")
                it.waitForExists(waitingTime)
                Log.i(TAG, "openMainMenuFromRedesignedToolbar: Waited for $waitingTime ms for the main menu button to exist")
                Log.i(TAG, "openMainMenuFromRedesignedToolbar: Trying to click the main menu button")
                it.click()
                Log.i(TAG, "openMainMenuFromRedesignedToolbar: Clicked the main menu button")
            }

            CustomTabRobot().interact()
            return Transition()
        }

        fun clickOpenInBrowserButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOpenInBrowserButton: Trying to click the \"Open in Firefox\" button")
            openInBrowserButton().perform(click())
            Log.i(TAG, "clickOpenInBrowserButton: Clicked the \"Open in Firefox\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickOpenInBrowserButtonFromRedesignedToolbar(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOpenInBrowserButtonFromRedesignedToolbar: Trying to click the \"Open in Firefox\" button")
            openInBrowserButtonFromRedesignedToolbar().click()
            Log.i(TAG, "clickOpenInBrowserButtonFromRedesignedToolbar: Clicked the \"Open in Firefox\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickShareButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareButton: Trying to click the share button")
            itemWithDescription(getStringResource(R.string.mozac_feature_customtabs_share_link)).waitForExists(waitingTime)
            itemWithDescription(getStringResource(R.string.mozac_feature_customtabs_share_link)).click()
            Log.i(TAG, "clickShareButton: Clicked the share button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickShareButtonFromRedesignedMenu(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareButtonFromRedesignedMenu: Trying to click the main menu share button")
            itemWithDescription(getStringResource(R.string.browser_menu_share_2)).click()
            Log.i(TAG, "clickShareButtonFromRedesignedMenu: Clicked the main menu share button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickFindInPageButton(composeTestRule: ComposeTestRule, interact: FindInPageRobot.() -> Unit): FindInPageRobot.Transition {
            Log.i(TAG, "clickFindInPageButton: Trying to click the \"Find In Page\" button from the new main menu design.")
            composeTestRule.findInPageButton().performClick()
            Log.i(TAG, "clickFindInPageButton: Clicked the \"Find In Page\" button from the new main menu design.")

            FindInPageRobot().interact()
            return FindInPageRobot.Transition()
        }

        fun clickOutsideTheMainMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOutsideTheMainMenu: Trying to click outside the main menu.")
            itemWithResId("$packageName:id/touch_outside").clickTopLeft()
            Log.i(TAG, "clickOutsideTheMainMenu: Clicked click outside the main menu.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }
    }
}

fun customTabScreen(interact: CustomTabRobot.() -> Unit): CustomTabRobot.Transition {
    CustomTabRobot().interact()
    return CustomTabRobot.Transition()
}

private fun mainMenuButton() = itemWithResId("$packageName:id/mozac_browser_toolbar_menu")

private fun mainMenuButtonFromRedesignedToolbar() =
    itemWithDescription(getStringResource(R.string.content_description_menu))

private fun desktopSiteButton() = onView(withId(R.id.switch_widget))

private fun findInPageButton() = onView(withText("Find in page"))

private fun openInBrowserButton() = onView(withText("Open in $appName"))

private fun openInBrowserButtonFromRedesignedToolbar() = itemWithDescription("Open in $appName")

private fun closeButton() = onView(withContentDescription("Return to previous app"))

private fun customTabToolbar() = mDevice.findObject(By.res("$packageName:id/toolbar"))

private fun progressBar() =
    mDevice.findObject(
        UiSelector().resourceId("$packageName:id/mozac_browser_toolbar_progress"),
    )
private fun ComposeTestRule.desktopSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_switch_to_desktop_site))

private fun ComposeTestRule.mobileSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_switch_to_mobile_site))

private fun ComposeTestRule.findInPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_find_in_page_2))

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.content.res.Configuration
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.verifyDarkThemeApplied
import org.mozilla.fenix.helpers.TestHelper.verifyLightThemeApplied
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class SettingsCustomizeTest : TestSetup() {
    @get:Rule
    val activityTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    private fun getUiTheme(): Boolean {
        val mode =
            activityTestRule.activity.resources?.configuration?.uiMode?.and(Configuration.UI_MODE_NIGHT_MASK)

        return when (mode) {
            Configuration.UI_MODE_NIGHT_YES -> true // dark theme is set
            Configuration.UI_MODE_NIGHT_NO -> false // dark theme is not set, using light theme
            else -> false // default option is light theme
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/344212
    @Test
    fun changeThemeOfTheAppTest() {
        // Goes through the settings and changes the default search engine, then verifies it changes.
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            verifyThemes()
            selectDarkMode()
            verifyDarkThemeApplied(getUiTheme())
            selectLightMode()
            verifyLightThemeApplied(getUiTheme())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/466571
    @Test
    fun setToolbarPositionTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            verifyAddressBarPositionPreference("Bottom")
            clickTopToolbarToggle()
            verifyAddressBarPositionPreference("Top")
        }.goBack {
        }.goBack {
            verifyAddressBarPosition(bottomPosition = false)
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            clickBottomToolbarToggle()
            verifyAddressBarPositionPreference("Bottom")
            exitMenu()
        }
        homeScreen {
            verifyAddressBarPosition(bottomPosition = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1058682
    @Test
    @SkipLeaks
    fun turnOffSwipeToSwitchTabsPreferenceTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            verifySwipeToolbarGesturePrefState(true)
            clickSwipeToolbarToSwitchTabToggle()
            verifySwipeToolbarGesturePrefState(false)
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondWebPage.url.toString()) {
            swipeNavBarRight(secondWebPage.url.toString())
            verifyUrl(secondWebPage.url.toString())
            swipeNavBarLeft(secondWebPage.url.toString())
            verifyUrl(secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1992289
    @Test
    fun pullToRefreshPreferenceTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openCustomizeSubMenu {
            verifyPullToRefreshGesturePrefState(isEnabled = true)
            clickPullToRefreshToggle()
            verifyPullToRefreshGesturePrefState(isEnabled = false)
        }
    }
}

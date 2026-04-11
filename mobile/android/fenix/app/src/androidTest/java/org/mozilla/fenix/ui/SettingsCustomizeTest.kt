/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.content.res.Configuration
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.firstForeignWebPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.secondForeignWebPageAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.TestHelper.verifyDarkThemeApplied
import org.mozilla.fenix.helpers.TestHelper.verifyLightThemeApplied
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class SettingsCustomizeTest {
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer get() = fenixTestRule.mockWebServer

    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    private fun getUiTheme(): Boolean {
        val mode =
            composeTestRule.activity.resources?.configuration?.uiMode?.and(Configuration.UI_MODE_NIGHT_MASK)

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
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
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
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyAddressBarPositionPreference("Bottom")
            clickTopToolbarToggle()
            verifyAddressBarPositionPreference("Top")
        }.goBack {
        }.goBack(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickBottomToolbarToggle()
            verifyAddressBarPositionPreference("Bottom")
            exitMenu()
        }
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1058682
    @Test
    @SkipLeaks
    fun turnOffSwipeToSwitchTabsPreferenceTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifySwipeToolbarGesturePrefState(true)
            clickSwipeToolbarToSwitchTabToggle()
            verifySwipeToolbarGesturePrefState(false)
            exitMenu()
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
        }.openTabDrawer(composeTestRule) {
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
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyPullToRefreshGesturePrefState(isEnabled = true)
            clickPullToRefreshToggle()
            verifyPullToRefreshGesturePrefState(isEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186732
    @SmokeTest
    @Test
    fun verifyTheDefaultAppIconSettingTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyAppIconOption(composeTestRule, "Default")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186731
    @SmokeTest
    @Test
    fun verifyTheAppIconSelectionPageTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTheAppIconOption(composeTestRule)
            verifyAppIconSettingItems(composeTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186734
    @SmokeTest
    @Test
    fun verifyTheChangeAppIconButtonTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyAppIconOption(composeTestRule, "Default")
            clickTheAppIconOption(composeTestRule)
            clickAppIconOption(composeTestRule, appIconOptionName = "Dark")
            verifyChangeAppIconDialog(composeTestRule)
            clickTheChangeIconDialogButton(composeTestRule)
            restartApp(composeTestRule.activityRule)
        }
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyAppIconOption(composeTestRule, "Dark")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333174
    @Test
    fun verifyTheToolbarLayoutSectionTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyToolbarLayout()
            verifyToolbarLayoutPreference("Simple")
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
            scrollToAddressBarLocation()
            clickBottomToolbarToggle()
            verifyAddressBarPositionPreference("Bottom")
            verifyToolbarLayout()
            verifyToolbarLayoutPreference("Expanded")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909009
    @Test
    fun verifyTheSimpleToolbarShortcutUI() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayout()
            verifyToolbarLayoutPreference("Simple")
            scrollToTheScrollToHideToolbarOption()
            verifyTheSimpleToolbarShortcutOptions()
            scrollToAddressBarLocation()
            clickBottomToolbarToggle()
            scrollToTheScrollToHideToolbarOption()
            verifyTheSimpleToolbarShortcutOptions()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909001
    @Test
    fun verifyTheSimpleToolbarLayoutOpenANewTabShortcut() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayoutPreference("Simple")
            scrollToTheScrollToHideToolbarOption()
            verifyTheOpenANewTabToolbarShortcutIsSelected()
            exitMenu()
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
        }
        navigationToolbar(composeTestRule) {
            verifyTheNewTabButton(false)
        }.clickTheNewTabButton(false) {
            verifySearchBarPlaceholder("Search or enter address")
        }.submitQuery(secondPage.url.toString()) {
        }
        navigationToolbar(composeTestRule) {
            verifyTheNewTabButton(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909002
    @Test
    fun verifyTheSimpleToolbarLayoutShareShortcut() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayoutPreference("Simple")
            scrollToTheScrollToHideToolbarOption()
            clickTheShareToolbarShortcut()
            verifyTheShareToolbarShortcutIsSelected()
            exitMenu()
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
        }
        navigationToolbar(composeTestRule) {
            verifyTheNavigationBarShareButton()
        }

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(secondPage.url) {
            swipeNavBarRight(secondPage.url.toString())
            waitForPageToLoad()
            verifyUrl(firstPage.url.toString())
        }
        navigationToolbar(composeTestRule) {
            verifyTheNavigationBarShareButton()
        }.clickTheNavigationBarShareButton {
            verifyShareTabLayout()
            verifySharingWithSelectedApp(
                appName = "Gmail",
                content = secondPage.url.toString(),
                subject = secondPage.title,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909003
    @Test
    fun verifyTheSimpleToolbarLayoutAddBookmarkShortcut() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayoutPreference("Simple")
            scrollToTheScrollToHideToolbarOption()
            clickTheAddBookmarkToolbarShortcut()
            verifyTheAddBookmarkToolbarShortcutIsSelected()
            exitMenu()
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }

        navigationToolbar(composeTestRule) {
            verifyTheNavigationBarAddBookmarkButton()
            clickTheNavigationBarAddBookmarkButton()
        }

        browserScreen(composeTestRule) {
            waitUntilSnackbarGone()
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkedURL(defaultWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909004
    @Test
    fun verifyTheSimpleToolbarLayoutTranslateShortcut() {
        val firstPage = mockWebServer.firstForeignWebPageAsset
        val secondPage = mockWebServer.secondForeignWebPageAsset

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayoutPreference("Simple")
            scrollToTheScrollToHideToolbarOption()
            clickTheTranslateToolbarShortcut()
            verifyTheTranslateToolbarShortcutIsSelected()
            exitMenu()
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
        }
        navigationToolbar(composeTestRule) {
            verifyTheNavigationBarTranslateButton()
        }.clickTheNavigationBarTranslatePageButton {
            verifyTranslationSheetIsDisplayed(isDisplayed = true)
        }.clickTranslateButton {
            waitForPageToLoad()
            verifyPageContent("Article of the day")
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(secondPage.url) {
        }
        navigationToolbar(composeTestRule) {
            verifyTheNavigationBarTranslateButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3909005
    @Test
    fun verifyTheSimpleToolbarLayoutHomepageShortcut() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayoutPreference("Simple")
            scrollToTheScrollToHideToolbarOption()
            clickTheHomepageToolbarShortcut()
            verifyTheHomepageToolbarShortcutIsSelected()
            exitMenu()
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
        }
        navigationToolbar(composeTestRule) {
            verifyTheNavigationBarHomepageButton()
        }.clickTheNavigationBarHomepageButton {
            verifyHomeWordmark()
            navigationToolbar(composeTestRule) {
            }.enterURLAndEnterToBrowser(secondPage.url) {
            }
            navigationToolbar(composeTestRule) {
                verifyTheNavigationBarHomepageButton()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3908992
    @Test
    fun verifyTheExpandedToolbarShortcutUI() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyToolbarLayout()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
            verifyToolbarLayoutPreference("Expanded")
            scrollToTheScrollToHideToolbarOption()
            verifyTheExpandedToolbarShortcutOptions()
            scrollToAddressBarLocation()
            clickBottomToolbarToggle()
            selectDarkMode()
            verifyDarkThemeApplied(getUiTheme())
            scrollToTheScrollToHideToolbarOption()
            verifyTheExpandedToolbarShortcutOptions()
        }
    }
}

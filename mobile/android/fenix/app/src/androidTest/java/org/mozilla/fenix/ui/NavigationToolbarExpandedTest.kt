/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("DEPRECATION")

package org.mozilla.fenix.ui

import android.content.pm.ActivityInfo
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.AppAndSystemHelper.setScreenOrientation
import org.mozilla.fenix.helpers.AppAndSystemHelper.verifyKeyboardVisibility
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.refreshAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.composeBookmarksMenu
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests for verifying basic functionality of browser navigation in expanded toolbar layout and page related
 * interactions
 *
 * Including:
 * - Visiting a URL
 * - Back and Forward navigation
 * - Refresh
 * - Find in page
 */
class NavigationToolbarExpandedTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule(
                isWallpaperOnboardingEnabled = false,
                isOpenInAppBannerEnabled = false,
                isMicrosurveyEnabled = false,
                isTermsOfServiceAccepted = true,
                shouldUseExpandedToolbar = true,
            )
        ) {
            it.activity
        }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333207
    @Test
    fun verifyTheExpandedToolbarItemsWebsiteViewTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
                verifyUrl(website.url.toString())
                verifyETPShieldIconIsDisplayed(composeTestRule)
            }
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyNavBarPosition()
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton()
            verifyTheTabCounter("1")
            verifyTheMainMenuButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333211
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheExpandedToolbarAddBookmarkButtonTest"
            ],
        bug = 2060480,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarAddBookmarkButtonTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(website.url) {}
        navigationToolbar(composeTestRule) {
            clickTheNavigationBarAddBookmarkButton()
        }
        browserScreen(composeTestRule) {
            waitUntilSnackbarGone()
        }
        navigationToolbar(composeTestRule) {
            clickTheNavigationBarEditBookmarkButton()
        }
        composeBookmarksMenu(composeTestRule) {
            verifyEditBookmarksView()
            clickDeleteBookmarkButtonInEditMode()
        }
        browserScreen(composeTestRule) {
            verifyPageContent(website.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333212
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheExpandedToolbarShareButtonTest"
            ],
        bug = 2060480,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarShareButtonTest() {
        val website = mockWebServer.getGenericAsset(1)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
        navigationToolbar(composeTestRule) {}
            .clickTheNavigationBarShareButton {
                verifyShareTabLayout()
                verifySharingWithSelectedApp(
                    appName = "Gmail",
                    content = website.url.toString(),
                    subject = website.title,
                )
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333213
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.ToolbarTest#verifyTheExpandedToolbarNewTabButtonTest"],
        bug = 2057957,
        since = "2026-07",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarNewTabButtonTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
        navigationToolbar(composeTestRule) {}
            .clickTheNewTabButton {
                verifySearchBarPlaceholder("Search or enter address")
                verifyKeyboardVisibility(true)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333214
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.ToolbarTest#verifyTheExpandedToolbarTabTrayButtonTest"],
        bug = 2057419,
        since = "2026-07",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarTabTrayButtonTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
            .openTabDrawer(composeTestRule) {
                verifyExistingOpenTabs(website.title)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333215
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.ToolbarTest#verifyTheExpandedToolbarMainMenuButtonTest"],
        bug = 2057419,
        since = "2026-07",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarMainMenuButtonTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
            .openThreeDotMenu {
                verifyPageMainMenuItems()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333220
    @Test
    fun verifyTheExpandedToolbarTabsCounterShortcutMenuNewTabTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar(composeTestRule) {}
            .openTabButtonShortcutsMenu {
                verifyTabButtonShortcutMenuItems()
            }
            .openNewTabFromShortcutsMenu {
                verifySearchBarPlaceholder("Search or enter address")
            }
            .dismissSearchBar {
                verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333221
    @Test
    fun verifyTheExpandedToolbarTabsCounterShortcutMenuNewPrivateTabTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar(composeTestRule) {}
            .openTabButtonShortcutsMenu {
                verifyTabButtonShortcutMenuItems()
            }
            .openNewPrivateTabFromShortcutsMenu {
                verifySearchBarPlaceholder("Search or enter address")
            }
            .dismissSearchBar {
                verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333222
    @Test
    fun verifyTheExpandedToolbarTabsCounterShortcutMenuCloseTabTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar(composeTestRule) {}
            .openTabButtonShortcutsMenu {
                verifyTabButtonShortcutMenuItems()
            }
            .closeTabFromShortcutsMenu {
                verifySnackBarText(composeTestRule, "Tab closed")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3911787
    @Test
    fun verifyTheExpandedToolbarHomepageItemsInLandscapeModeTest() {
        homeScreen(composeTestRule) {
            verifyHomeWordmark()
            verifyToolbarPosition(bottomPosition = false)
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333175
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheExpandedToolbarItemsInLandscapeModeTest"
            ],
        bug = 2060480,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarItemsInLandscapeModeTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(defaultWebPage.url) {
                verifyPageContent(defaultWebPage.content)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        browserScreen(composeTestRule) {
            verifyUrl(defaultWebPage.url.toString())
            verifyETPShieldIconIsDisplayed(composeTestRule)
        }
        navigationToolbar(composeTestRule) {
            verifyTheBackButton()
            verifyTheForwardButton()
            verifyTheRefreshButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton(false)
            verifyTheTabCounter("1")
            verifyTheMainMenuButton()
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333213
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheExpandedToolbarNewTabButtonInLandscapeModeTest"
            ],
        bug = 2060480,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarNewTabButtonInLandscapeModeTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        navigationToolbar(composeTestRule) {}
            .clickTheNewTabButton {
                verifySearchBarPlaceholder("Search or enter address")
                verifyKeyboardVisibility(true)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333214
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheExpandedToolbarTabTrayButtonInLandscapeModeTest"
            ],
        bug = 2060480,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarTabTrayButtonInLandscapeModeTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        homeScreen(composeTestRule) {}
            .openTabDrawer {
                verifyExistingOpenTabs(website.title)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333178
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheExpandedToolbarMainMenuButtonInLandscapeModeTest"
            ],
        bug = 2060480,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheExpandedToolbarMainMenuButtonInLandscapeModeTest() {
        val website = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(website.url) {
                verifyPageContent(website.content)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {
                verifyPageMainMenuItemsInLandscapeMode()
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333178
    @Test
    fun verifyTheExpandedToolbarRefreshButtonInLandscapeModeTest() {
        val refreshWebPage = mockWebServer.refreshAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(refreshWebPage.url) {
                verifyPageContent("DEFAULT")
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        navigationToolbar(composeTestRule) {
            clickTheNavigationBarRefreshButton()
        }
        browserScreen(composeTestRule) {
            verifyPageContent("REFRESHED")
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333212
    @Test
    fun verifyTheExpandedToolbarShareButtonInLandscapeModeTest() {
        val website = mockWebServer.getGenericAsset(1)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(website.url) {}
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        navigationToolbar(composeTestRule) {}
            .clickTheNavigationBarShareButton {
                verifyShareTabLayoutInLandscapeMode()
                verifySharingWithSelectedApp(
                    appName = "Gmail",
                    content = website.url.toString(),
                    subject = website.title,
                )
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333206
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyHomepageItemsWithTabStripTest"],
        bug = 2060482,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyHomepageItemsWithTabStripTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickShowTabBarToggle()
            }
            .goBack {}
            .goBack(composeTestRule) {
                verifyToolbarPosition(bottomPosition = false)
            }
        navigationToolbar(composeTestRule) {
            verifyNavBarPosition()
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton()
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333193
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheTabStripUITest"],
        bug = 2060482,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheTabStripUITest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickShowTabBarToggle()
            }
            .goBack {}
            .goBack(composeTestRule) {}
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(defaultWebPage.url) {
                verifyPageContent(defaultWebPage.content)
                verifyUrl(defaultWebPage.url.toString())
                verifyETPShieldIconIsDisplayed(composeTestRule)
            }
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyTheTabStripOpenTab("Test_Page_1")
            verifyTheTabStripCloseTabButton("Test_Page_1")
            verifyNavBarPosition()
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton(false)
            verifyTheTabCounter("1")
            verifyTheMainMenuButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333194
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTheNewTabButtonWithTabStripEnabledTest"
            ],
        bug = 2060482,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheNewTabButtonWithTabStripEnabledTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickShowTabBarToggle()
            }
            .goBack {}
            .goBack(composeTestRule) {}
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(defaultWebPage.url) {
                verifyTabCounter("1")
            }
        navigationToolbar(composeTestRule) {
                verifyTheNewTabButton(false)
            }
            .clickTheNewTabButton(false) {
                verifySearchBarPlaceholder("Search or enter address")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333195
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.NavigationToolbarExpandedTest#verifyTabsTrayWithTabStripEnabledTest"
            ],
        bug = 2060482,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTabsTrayWithTabStripEnabledTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickShowTabBarToggle()
            }
            .goBack {}
            .goBack(composeTestRule) {
                navigationToolbar(composeTestRule) {}
                    .enterURLAndEnterToBrowser(defaultWebPage.url) {}
                    .openTabDrawer(composeTestRule) {
                        verifyExistingOpenTabs(defaultWebPage.title)
                    }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333173
    @Test
    fun verifyHomepageItemsWithTabStripLandscapeTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickShowTabBarToggle()
            }
            .goBack {}
            .goBack(composeTestRule) {
                verifyToolbarPosition(bottomPosition = false)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333201
    @Test
    fun verifyTheTabStripUILandscapeTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickShowTabBarToggle()
            }
            .goBack {}
            .goBack(composeTestRule) {}
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(defaultWebPage.url) {
                verifyPageContent(defaultWebPage.content)
            }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        browserScreen(composeTestRule) {
            verifyUrl(defaultWebPage.url.toString())
            verifyETPShieldIconIsDisplayed(composeTestRule)
        }
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyTheTabStripOpenTab("Test_Page_1")
            verifyTheTabStripCloseTabButton("Test_Page_1")
            verifyTheBackButton()
            verifyTheForwardButton()
            verifyTheRefreshButton()
            verifyTheTabStripAddTabButton()
            verifyTheTabCounter("1")
            verifyTheMainMenuButton()
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3911787
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.SettingsCustomizeExpandedToolbarTest#verifyTheExpandedToolbarHomepageItemsTest"
            ],
        bug = 2084095,
        since = "2026-10",
    )
    @Critical
    @Test
    fun verifyTheExpandedToolbarHomepageItemsTest() {
        homeScreen(composeTestRule) {
            verifyGradientWallpaperApplied(composeTestRule)
            verifyHomeWordmark()
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyNavBarPositionForEdgeToEdge()
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton()
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                clickBottomToolbarToggle()
                verifyAddressBarPositionPreference("Bottom")
                exitMenu()
            }
        navigationToolbar(composeTestRule) {
            verifyNavBarPositionForEdgeToEdge()
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton()
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
    }
}

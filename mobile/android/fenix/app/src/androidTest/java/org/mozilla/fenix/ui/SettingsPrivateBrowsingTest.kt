/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.helpers.AppAndSystemHelper
import org.mozilla.fenix.helpers.AppAndSystemHelper.clickSystemHomeScreenShortcutAddButton
import org.mozilla.fenix.helpers.DataGenerationHelper
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.addToHomeScreen
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class SettingsPrivateBrowsingTest : TestSetup() {
    private val pageShortcutName = DataGenerationHelper.generateRandomString(5)

    @get:Rule
    val activityTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(
                skipOnboarding = true,
            ),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/555822
    @Test
    fun verifyPrivateBrowsingMenuItemsTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openPrivateBrowsingSubMenu {
            verifyAddPrivateBrowsingShortcutButton()
            verifyOpenLinksInPrivateTab()
            verifyOpenLinksInPrivateTabOff()
        }.goBack {
            verifySettingsView()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/420086
    @Test
    fun launchLinksInAPrivateTabTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        setOpenLinksInPrivateOn()

        AppAndSystemHelper.openAppFromExternalLink(firstWebPage.url.toString())

        browserScreen {
            verifyUrl(firstWebPage.url.toString())
        }.openTabDrawer(activityTestRule) {
            verifyPrivateBrowsingButtonIsSelected()
        }.closeTabDrawer {
        }.goToHomescreen { }

        setOpenLinksInPrivateOff()

        // We need to open a different link, otherwise it will open the same session
        AppAndSystemHelper.openAppFromExternalLink(secondWebPage.url.toString())

        browserScreen {
            verifyUrl(secondWebPage.url.toString())
        }.openTabDrawer(activityTestRule) {
            verifyNormalBrowsingButtonIsSelected()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/555776
    @Test
    fun launchPageShortcutInPrivateBrowsingTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        setOpenLinksInPrivateOn()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openThreeDotMenu {
        }.openAddToHomeScreen {
            addShortcutName(pageShortcutName)
            clickAddShortcutButton()
            clickSystemHomeScreenShortcutAddButton()
            verifyShortcutAdded(pageShortcutName)
        }

        mDevice.waitForIdle()
        // We need to close the existing tab here, to open a different session
        restartApp(activityTestRule.activityRule)

        browserScreen {
        }.openTabDrawer(activityTestRule) {
            verifyNormalBrowsingButtonIsSelected()
            closeTab()
        }

        addToHomeScreen {
        }.searchAndOpenHomeScreenShortcut(pageShortcutName) {
        }.openTabDrawer(activityTestRule) {
            verifyPrivateBrowsingButtonIsSelected()
            closeTab()
        }

        setOpenLinksInPrivateOff()

        addToHomeScreen {
        }.searchAndOpenHomeScreenShortcut(pageShortcutName) {
        }.openTabDrawer(activityTestRule) {
            verifyNormalBrowsingButtonIsSelected()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/414583
    @Test
    fun addPrivateBrowsingShortcutFromSettingsTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openPrivateBrowsingSubMenu {
            cancelPrivateShortcutAddition()
            addPrivateShortcutToHomescreen()
            verifyPrivateBrowsingShortcutIcon()
        }.openPrivateBrowsingShortcut {
            verifySearchView()
        }.openBrowser {
        }.openTabDrawer(activityTestRule) {
            verifyPrivateBrowsingButtonIsSelected()
        }
    }
}

private fun setOpenLinksInPrivateOn() {
    homeScreen {
    }.openThreeDotMenu {
    }.openSettings {
    }.openPrivateBrowsingSubMenu {
        verifyOpenLinksInPrivateTabEnabled()
        clickOpenLinksInPrivateTabSwitch()
    }.goBack {
    }.goBack {
        verifyHomeComponent()
    }
}

private fun setOpenLinksInPrivateOff() {
    homeScreen {
    }.openThreeDotMenu {
    }.openSettings {
    }.openPrivateBrowsingSubMenu {
        clickOpenLinksInPrivateTabSwitch()
        verifyOpenLinksInPrivateTabOff()
    }.goBack {
    }.goBack {
        verifyHomeComponent()
    }
}

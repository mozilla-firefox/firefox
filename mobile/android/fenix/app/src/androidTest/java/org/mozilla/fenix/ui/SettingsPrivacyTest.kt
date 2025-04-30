/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade
import org.mozilla.fenix.utils.DURATION_MS_TRANSLATIONS
import org.mozilla.fenix.utils.exitMenu

/**
 *  Tests for verifying the the privacy and security section of the Settings menu
 *
 */

class SettingsPrivacyTest : TestSetup() {
    @get:Rule
    val activityTestRule = HomeActivityTestRule.withDefaultSettingsOverrides(skipOnboarding = true)

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2092698
    @Test
    fun settingsPrivacyItemsTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
            verifySettingsToolbar()
            verifyPrivacyHeading()
            verifyPrivateBrowsingButton()
            verifyHTTPSOnlyModeButton()
            verifySettingsOptionSummary("HTTPS-Only Mode", "Off")
            verifySettingsOptionSummary("Cookie Banner Blocker in private browsing", "")
            verifyEnhancedTrackingProtectionButton()
            verifySettingsOptionSummary("Enhanced Tracking Protection", "Standard")
            verifySiteSettingsButton()
            verifyDeleteBrowsingDataButton()
            verifyDeleteBrowsingDataOnQuitButton()
            verifySettingsOptionSummary("Delete browsing data on quit", "Off")
            verifyNotificationsButton()
            verifySettingsOptionSummary("Notifications", "Allowed")
            verifyDataCollectionButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243362
    @Test
    fun verifyDataCollectionSettingsTest() {
        homeScreen {}.openThreeDotMenu {}.openSettings {}.openSettingsSubMenuDataCollection {
            // Studies depends on the telemetry switch,  if telemetry is off studies will be
            // turned off as well, and will require the app to be restarted.
            // Daily usage ping should default to telemetry pref value
            verifyDataCollectionView(
                isUsageAndTechnicalDataEnabled = true,
                isDailyUsagePingEnabled = true,
                studiesSummary = "On",
                isAutomaticallySendCrashReportsEnabled = false,
            )
            clickUsageAndTechnicalDataToggle()
            verifyDataCollectionView(
                isUsageAndTechnicalDataEnabled = false,
                isDailyUsagePingEnabled = true,
                studiesSummary = "Off",
                isAutomaticallySendCrashReportsEnabled = false,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1024594
    @Test
    fun allowAppToSendNotifications() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        // Clear all existing notifications
        notificationShade {
            mDevice.openNotification()
            clearNotifications()
        }

        homeScreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openNotificationShade {
            verifySystemNotificationExists("Close private tabs?")
        }.closeNotificationTray {
        }.openThreeDotMenu {
        }.openSettings {
            verifySettingsOptionSummary("Notifications", "Allowed")
        }.openSettingsSubMenuNotifications {
            verifyAllSystemNotificationsToggleState(true)
            verifyPrivateBrowsingSystemNotificationsToggleState(true)
            exitMenu(DURATION_MS_TRANSLATIONS)
        }
    }
}

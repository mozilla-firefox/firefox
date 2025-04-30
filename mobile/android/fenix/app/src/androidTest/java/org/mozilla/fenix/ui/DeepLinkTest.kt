/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.DeepLinkRobot

/**
 *  Tests for verifying basic functionality of deep links
 *  - fenix://home
 *  - fenix://open
 *  - fenix://settings_notifications — take the user to the notification settings page
 *  - fenix://settings_privacy — take the user to the privacy settings page.
 *  - fenix://settings_search_engine — take the user to the search engine page, to set the default search engine.
 *  - fenix://home_collections — take the user to the home screen to see the list of collections.
 *  - fenix://urls_history — take the user to the history list.
 *  - fenix://urls_bookmarks — take the user to the bookmarks list
 *  - fenix://settings_logins — take the user to the settings page to do with logins (not the saved logins).
 **/

class DeepLinkTest : TestSetup() {
    private val robot = DeepLinkRobot()

    @get:Rule
    val activityIntentTestRule = HomeActivityIntentTestRule(
        isHomeOnboardingDialogEnabled = false,
        isNavigationBarCFREnabled = false,
        isNavigationToolbarEnabled = false,
        isMenuRedesignEnabled = false,
        isMenuRedesignCFREnabled = false,
    )

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Test
    fun openHomeScreen() {
        robot.openHomeScreen {
            verifyHomeComponent()
        }
        robot.openSettings { /* move away from the home screen */ }
        robot.openHomeScreen {
            verifyHomeComponent()
        }
    }

    @Test
    fun openURL() {
        val genericURL =
            "https://support.mozilla.org/en-US/products/mobile"
        robot.openURL(genericURL) {
            verifyUrl("support.mozilla.org/en-US/products/mobile")
        }
    }

    @Test
    fun openBookmarks() {
        robot.openBookmarks {
            // verify we can see headings.
            verifyFolderTitle("Desktop Bookmarks")
        }
    }

    @Test
    fun openHistory() {
        robot.openHistory {
            verifyHistoryMenuView()
        }
    }

    @Test
    fun openCollections() {
        robot.openCollections {
            verifyCollectionsHeader()
        }
    }

    @Test
    fun openSettings() {
        robot.openSettings {
            verifyGeneralHeading()
            verifyAdvancedHeading()
        }
    }

    @Test
    fun openSettingsLogins() {
        robot.openSettingsLogins {
            verifyDefaultView()
            verifyDefaultValueAutofillLogins(InstrumentationRegistry.getInstrumentation().targetContext)
        }
    }

    @Test
    fun openSettingsPrivacy() {
        robot.openSettingsPrivacy {
            verifyPrivacyHeading()
        }
    }

    @Test
    fun openSettingsTrackingProtection() {
        robot.openSettingsTrackingProtection {
            verifyEnhancedTrackingProtectionSummary()
        }
    }

    @Test
    fun openSettingsSearchEngine() {
        robot.openSettingsSearchEngine {
            verifyDefaultSearchEngineHeader()
        }
    }

    @Test
    fun openSettingsNotifications() {
        robot.openSettingsNotification {
            verifyNotifications()
        }
    }

    @Test
    fun openMakeDefaultBrowser() {
        robot.openMakeDefaultBrowser {
            verifyMakeDefaultBrowser()
        }
    }
}

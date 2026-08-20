/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSiteSettingsPermissionsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSiteSettingsSelectors

class SettingsSiteSettingsPermissionsPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "SettingsSiteSettingsPermissionsPage"

    init {
        NavigationRegistry.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.SETTINGS_BUTTON),
                    NavigationStep.Swipe(SettingsSelectors.SITE_SETTINGS_BUTTON),
                    NavigationStep.Click(SettingsSelectors.SITE_SETTINGS_BUTTON),
                    NavigationStep.Click(SettingsSiteSettingsSelectors.CAMERA_BUTTON),
                ),
        )

        NavigationRegistry.register(
            from = pageName,
            to = "HomePage",
            steps = listOf(NavigationStep.PressBackUntilGone(SettingsSelectors.NAVIGATION_TOOLBAR)),
        )
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SettingsSiteSettingsPermissionsSelectors.all.filter { it.groups.contains(group) }
    }

    // Typed override so a test can chain the verbs below straight off navigateToPage(), as the other page objects do.
    override fun navigateToPage(url: String, forceNavigation: Boolean): SettingsSiteSettingsPermissionsPage {
        super.navigateToPage(url = url, forceNavigation = forceNavigation)
        return this
    }

    /**
     * The five assertions legacy made in verifyBlockedByAndroidSection: the "Blocked by Android" heading, the "To allow
     * it:" intro, both numbered steps, and the Go to settings button.
     */
    fun verifyBlockedByAndroidSection(): SettingsSiteSettingsPermissionsPage {
        mozVerifyElementsByGroup("blockedByAndroid")
        return this
    }

    /**
     * Android no longer blocks this permission. Waits the blocked-state container out before asserting the heading is
     * gone, which is what legacy did: the heading is one child of that container, so the container is the stronger
     * signal and the heading assertion alone could pass mid-teardown.
     */
    fun verifyUnblockedByAndroid(): SettingsSiteSettingsPermissionsPage {
        mozWaitUntilAbsent(SettingsSiteSettingsPermissionsSelectors.BLOCKED_BY_ANDROID_CONTAINER)
        mozVerifyElementAbsent(SettingsSiteSettingsPermissionsSelectors.BLOCKED_BY_ANDROID_HEADING)
        return this
    }

    /** Leave this permission's screen for the Site settings list it was opened from. */
    fun goBackToSiteSettings(): SettingsSiteSettingsPermissionsPage {
        mozPressBack()
        return this
    }

    /**
     * Come back from the Android settings app to whichever permission screen we left. Presses back until the permission
     * radio is in front again — the harness equivalent of legacy's goBackToPermissionsSettingsSubMenu loop, and
     * depth-independent because the number of system screens entered varies by OS release.
     */
    fun returnFromSystemSettings(): SettingsSiteSettingsPermissionsPage {
        mozPressBackUntilPresent(SettingsSiteSettingsPermissionsSelectors.ASK_TO_ALLOW_RADIO_BUTTON)
        return this
    }
}

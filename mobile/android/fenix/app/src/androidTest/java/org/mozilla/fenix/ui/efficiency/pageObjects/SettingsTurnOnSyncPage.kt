/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationOptions
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsTurnOnSyncSelectors

class SettingsTurnOnSyncPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "SettingsTurnOnSyncPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        builder.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.SETTINGS_BUTTON),
                    NavigationStep.Click(
                        // The SIGN_IN_BUTTON branch can be removed once accountSettingsNewUi is enabled by default.
                        if (appContext.components.settings.accountSettingsNewUi) {
                            SettingsSelectors.SIGN_IN_PROMO_CARD
                        } else {
                            SettingsSelectors.SIGN_IN_BUTTON
                        }
                    ),
                ),
        )
    }

    override fun navigateToPage(
        url: String,
        forceNavigation: Boolean,
        navigationOptions: NavigationOptions,
    ): SettingsTurnOnSyncPage {
        super.navigateToPage(url, forceNavigation, navigationOptions)
        return this
    }

    /**
     * Click "Ready to scan" until the camera-permission dialog is up, mirroring the legacy retry.
     *
     * The retry is the mechanism, not defensiveness. With camera permission denied, the FIRST click still navigates to
     * PairFragment (Fenix only consults the permission after the one-shot pref_key_camera_permissions_needed has been
     * consumed), PairFragment's own request is auto-denied and it pops straight back — so the button reappears. Only
     * the second click reaches the dialog. Requiring the button to stay absent for a sustained window is what
     * distinguishes "the dialog is covering it" from "we bounced through PairFragment and came back".
     */
    fun clickReadyToScanUntilPermissionDialog(): SettingsTurnOnSyncPage {
        for (attempt in 1..READY_TO_SCAN_RETRY_COUNT) {
            try {
                mozVerify(SettingsTurnOnSyncSelectors.READY_TO_SCAN_BUTTON)
                mozClick(SettingsTurnOnSyncSelectors.READY_TO_SCAN_BUTTON)
                mozVerifyElementStaysAbsent(SettingsTurnOnSyncSelectors.READY_TO_SCAN_BUTTON)
                return this
            } catch (e: AssertionError) {
                if (attempt == READY_TO_SCAN_RETRY_COUNT) throw e
            }
        }
        return this
    }

    /**
     * Press back out of the Android Settings app until this screen is showing again.
     *
     * Legacy presses back a fixed three times, which assumes an exact back-stack depth in an app we do not control.
     * Pressing back until the anchor returns survives a Settings app with a different number of screens.
     */
    fun returnFromSystemSettings(): SettingsTurnOnSyncPage {
        mozPressBackUntilPresent(SettingsTurnOnSyncSelectors.READY_TO_SCAN_BUTTON, maxPresses = BACK_OUT_MAX_PRESSES)
        return this
    }

    override val selectorCatalog = SettingsTurnOnSyncSelectors

    private companion object {
        const val READY_TO_SCAN_RETRY_COUNT = 3
        const val BACK_OUT_MAX_PRESSES = 5
    }
}

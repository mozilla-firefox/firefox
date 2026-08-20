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
import org.mozilla.fenix.ui.efficiency.selectors.SettingsPrivateBrowsingSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors

class SettingsPrivateBrowsingPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "SettingsPrivateBrowsingPage"

    init {
        // Reached only via the Settings hub (SettingsPage -> SettingsPrivateBrowsingPage), like every other
        // settings subpage. This page intentionally has NO direct HomePage mega-edge: that made the Home route
        // shorter than the hub route, so BFS routed browser -> Home (via the New tab button, absent on the
        // private browser) instead of browser -> menu -> Settings -> Private browsing.
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON)),
        )

        // Return edge for when this page was reached from the browser (menu -> Settings -> Private browsing):
        // backing out twice lands on the browser the flow came from, not Home. Settings back-navigation is
        // entry-dependent, so this models the browser-entry return used by the screenshots test.
        NavigationRegistry.register(
            from = pageName,
            to = "BrowserPage",
            steps =
                listOf(
                    NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON),
                    NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON),
                ),
        )
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SettingsPrivateBrowsingSelectors.all.filter { it.groups.contains(group) }
    }

    fun toggleAllowScreenshotsInPrivateBrowsing(): SettingsPrivateBrowsingPage {
        mozClick(SettingsPrivateBrowsingSelectors.ALLOW_SCREENSHOTS_IN_PRIVATE_BROWSING)
        return this
    }

    /**
     * NOTE: Temporary stub for the Test Factory demo.
     *
     * This method exists only to illustrate how the `SettingsPrivateBrowsingTest` (and the Test Factory pattern) would
     * toggle Private Browsing in a real page object. It is **not** connected to functional UI code and should be
     * replaced with the actual implementation when Settings pages are integrated.
     *
     * The `UnsupportedOperationException` is intentional to ensure this placeholder is never used in production or
     * non-demo tests.
     */
    fun setPrivateBrowsing(on: Boolean) {
        throw UnsupportedOperationException("setPrivateBrowsing is not supported by ${this::class.simpleName}")
    }
}

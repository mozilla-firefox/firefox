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
import org.mozilla.fenix.ui.efficiency.selectors.SettingsCustomizeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors

class SettingsCustomizePage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "SettingsCustomizePage"

    init {
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON)),
        )

        NavigationRegistry.register(
            from = pageName,
            to = "BrowserPage",
            steps =
                listOf(
                    NavigationStep.Click(SettingsCustomizeSelectors.NAVIGATE_BACK_TOOLBAR_BUTTON),
                    NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON),
                ),
        )
    }

    override fun navigateToPage(url: String, forceNavigation: Boolean): SettingsCustomizePage {
        super.navigateToPage(url, forceNavigation)
        return this
    }

    fun verifyOptionIsSelected(selector: Selector): SettingsCustomizePage {
        mozVerifyElementHasCheckedSiblingByResName(selector, "radio_button")
        return this
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SettingsCustomizeSelectors.all.filter { it.groups.contains(group) }
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.navigation.NavigationFacts
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsTabsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors

class SettingsTabsPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "SettingsTabsPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        // Navigate up returns to Settings only when opened from Settings, not from the tray.
        builder.register(
            from = pageName,
            to = "SettingsPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON)),
            forbids = setOf(NavigationFacts.TAB_SETTINGS_FROM_TRAY),
        )

        // Opened from the tab manager's three-dot menu; back returns to the tray (edge below).
        builder.register(
            from = "TabDrawerPage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(TabDrawerSelectors.THREE_DOT_BUTTON),
                    NavigationStep.Click(TabDrawerSelectors.TAB_SETTINGS_BUTTON),
                ),
            provides = setOf(NavigationFacts.TAB_SETTINGS_FROM_TRAY),
        )

        builder.register(
            from = pageName,
            to = "TabDrawerPage",
            steps = listOf(NavigationStep.PressBack),
            requires = setOf(NavigationFacts.TAB_SETTINGS_FROM_TRAY),
            invalidates = setOf(NavigationFacts.TAB_SETTINGS_FROM_TRAY),
        )
    }

    override val selectorCatalog = SettingsTabsSelectors
}

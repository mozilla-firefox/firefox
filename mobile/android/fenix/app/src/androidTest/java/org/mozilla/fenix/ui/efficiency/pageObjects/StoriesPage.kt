/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.StoriesSelectors

class StoriesPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {

    override val pageName = "StoriesPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        // The stories entry point only renders when Pocket is on, so the reachability suite must launch HomePage
        // with it for STORIES_BUTTON to exist.
        builder.register(
            from = "HomePage",
            to = pageName,
            steps = listOf(NavigationStep.Click(HomeSelectors.STORIES_BUTTON)),
            launch = LaunchConfig(isPocketEnabled = true),
        )

        builder.register(
            from = pageName,
            to = "HomePage",
            steps = listOf(NavigationStep.Click(StoriesSelectors.BACK_BUTTON)),
        )
    }

    override val selectorCatalog = StoriesSelectors
}

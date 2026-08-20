/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.uiautomator.By
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchAddSearchEngineSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchDefaultSearchEngineSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors

class SettingsSearchAddSearchEnginePage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "SettingsSearchAddSearchEnginePage"

    init {
        NavigationRegistry.register(
            from = "SettingsSearchDefaultSearchEnginePage",
            to = pageName,
            steps = listOf(NavigationStep.Click(SettingsSearchDefaultSearchEngineSelectors.ADD_SEARCH_ENGINE_BUTTON)),
        )

        NavigationRegistry.register(
            from = pageName,
            to = "SettingsSearchDefaultSearchEnginePage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON)),
        )
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SettingsSearchAddSearchEngineSelectors.all.filter { it.groups.contains(group) }
    }

    // Covariant override so the page's own helpers can be chained straight off navigateToPage().
    override fun navigateToPage(url: String, forceNavigation: Boolean): SettingsSearchAddSearchEnginePage {
        super.navigateToPage(url, forceNavigation = forceNavigation)
        return this
    }

    /**
     * Fills the name and search-string fields. The fields are plain View EditTexts reached by res id; setting
     * UiObject2.text directly mirrors the legacy SettingsSubMenuSearchRobot.typeCustomEngineDetails and fires the
     * TextWatcher that enables the Save button.
     */
    fun typeCustomEngineDetails(engineName: String, engineUrl: String): SettingsSearchAddSearchEnginePage {
        mDevice.findObject(By.res("$packageName:id/edit_engine_name")).text = engineName
        mDevice.findObject(By.res("$packageName:id/edit_search_string")).text = engineUrl
        return this
    }

    fun saveNewSearchEngine(): SettingsSearchAddSearchEnginePage {
        // The Save button sits below the fields in a ScrollView, so the keyboard can cover it.
        dismissSoftKeyboard()
        mozClick(SettingsSearchAddSearchEngineSelectors.SAVE_BUTTON)
        return this
    }
}

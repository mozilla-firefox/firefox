/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import android.util.Log
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.intent.Intents.intended
import androidx.test.espresso.intent.matcher.IntentMatchers.hasAction
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import org.hamcrest.Matchers.allOf
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.hasCousin
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors

class SettingsPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "SettingsPage"

    init {
        NavigationRegistry.register(
            from = "MainMenuPage",
            to = pageName,
            steps = listOf(NavigationStep.Click(MainMenuSelectors.SETTINGS_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "HomePage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.GO_BACK_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsAccessibilityPage",
            steps =
                listOf(
                    NavigationStep.Swipe(SettingsSelectors.ACCESSIBILITY_BUTTON),
                    NavigationStep.Click(SettingsSelectors.ACCESSIBILITY_BUTTON),
                ),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsAutofillPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.AUTOFILL_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsPrivateBrowsingPage",
            steps =
                listOf(
                    // "Private browsing" sits below the fold, so scroll it into view first.
                    NavigationStep.Swipe(SettingsSelectors.PRIVATE_BROWSING_BUTTON),
                    NavigationStep.Click(SettingsSelectors.PRIVATE_BROWSING_BUTTON),
                ),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsCustomizePage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.CUSTOMIZE_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsHomepagePage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.HOMEPAGE_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsPasswordsPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.PASSWORDS_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsSearchPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.SEARCH_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsTabsPage",
            steps = listOf(NavigationStep.Click(SettingsSelectors.TABS_BUTTON)),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsPageSummariesPage",
            steps =
                listOf(
                    // "Page summaries" sits below the fold in the General section, so scroll it into view first.
                    NavigationStep.Swipe(SettingsSelectors.PAGE_SUMMARIES_BUTTON),
                    NavigationStep.Click(SettingsSelectors.PAGE_SUMMARIES_BUTTON),
                ),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "GooglePlayPage",
            steps =
                listOf(
                    NavigationStep.Swipe(SettingsSelectors.RATE_ON_GOOGLE_PLAY_BUTTON),
                    NavigationStep.Click(SettingsSelectors.RATE_ON_GOOGLE_PLAY_BUTTON),
                ),
        )
        NavigationRegistry.register(
            from = pageName,
            to = "SettingsAboutPage",
            steps =
                listOf(
                    NavigationStep.Swipe(SettingsSelectors.ABOUT_FIREFOX_BUTTON),
                    NavigationStep.Click(SettingsSelectors.ABOUT_FIREFOX_BUTTON),
                ),
        )
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return SettingsSelectors.all.filter { it.groups.contains(group) }
    }

    override fun navigateToPage(url: String, forceNavigation: Boolean): SettingsPage {
        super.navigateToPage(url, forceNavigation)
        return this
    }

    fun verifyHttpsOnlyModeOnAllTabs(): SettingsPage {
        return verifySettingOptionSummary(
            SettingsSelectors.HTTPS_ONLY_MODE_BUTTON.value,
            SettingsSelectors.HTTPS_ONLY_MODE_ON_ALL_TABS_SUMMARY.value,
        )
    }

    /**
     * Assert the "Set as default browser" preference's Switch is in the given state. Mirrors legacy
     * SettingsRobot.verifyDefaultBrowserToggle: the Switch (R.id.switch_widget) is a cousin of the preference title, so
     * it is matched via hasCousin rather than as the title itself.
     */
    fun verifyDefaultBrowserToggle(isEnabled: Boolean): SettingsPage {
        scrollToSettingText(getStringResource(R.string.preferences_set_as_default_browser))
        onView(withText(R.string.preferences_set_as_default_browser))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withId(R.id.switch_widget),
                            if (isEnabled) isChecked() else isNotChecked(),
                        )
                    )
                )
            )
        return this
    }

    fun clickDefaultBrowserSwitch(): SettingsPage {
        scrollToSettingText(getStringResource(R.string.preferences_set_as_default_browser))
        mozClick(SettingsSelectors.SET_AS_DEFAULT_BROWSER_BUTTON)
        return this
    }

    /** Assert the system default-apps chooser was launched (the REQUEST_ROLE intent fired). */
    fun verifyAndroidDefaultAppsMenuAppears(): SettingsPage {
        intended(hasAction(DEFAULT_APPS_SETTINGS_ACTION))
        return this
    }

    private fun scrollToSettingText(text: String) {
        val appView = UiScrollable(UiSelector().scrollable(true))
        appView.waitForExists(waitingTimeShort)
        if (appView.exists()) {
            try {
                appView.scrollTextIntoView(text)
            } catch (e: Exception) {
                Log.w("SettingsPage", "scrollTextIntoView failed for '$text': ${e.message}")
            }
        }
    }

    fun verifySettingOptionSummary(setting: String, summary: String): SettingsPage {
        val appView = UiScrollable(UiSelector().scrollable(true))
        appView.waitForExists(waitingTimeShort)
        if (appView.exists()) {
            try {
                appView.scrollTextIntoView(setting)
            } catch (e: Exception) {
                Log.w("SettingsPage", "scrollTextIntoView failed for '$setting': ${e.message}")
            }
        }
        onView(allOf(withText(setting), hasSibling(withText(summary))))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        return this
    }

    private companion object {
        const val DEFAULT_APPS_SETTINGS_ACTION = "android.app.role.action.REQUEST_ROLE"
    }
}

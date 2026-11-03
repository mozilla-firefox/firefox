/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.navigation.NavigationFacts
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationOptions
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsAddonsManagerSelectors

class SettingsAddonsManagerPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "SettingsAddonsManagerPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        builder.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.EXTENSIONS_BUTTON),
                ),
        )

        builder.register(
            from = "MainMenuPage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(MainMenuSelectors.EXTENSIONS_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.MANAGE_EXTENSIONS_BUTTON),
                ),
        )

        builder.register(
            from = pageName,
            to = "HomePage",
            steps = listOf(NavigationStep.Click(SettingsAddonsManagerSelectors.NAVIGATE_BACK_TOOLBAR_BUTTON)),
            requires = setOf(NavigationFacts.RETURN_SURFACE_HOME),
        )

        builder.register(
            from = pageName,
            to = "BrowserPage",
            steps = listOf(NavigationStep.Click(SettingsAddonsManagerSelectors.NAVIGATE_BACK_TOOLBAR_BUTTON)),
            requires = setOf(NavigationFacts.RETURN_SURFACE_BROWSER),
        )
    }

    override fun navigateToPage(
        url: String,
        forceNavigation: Boolean,
        navigationOptions: NavigationOptions,
    ): SettingsAddonsManagerPage {
        super.navigateToPage(url, forceNavigation, navigationOptions)
        return this
    }

    override val selectorCatalog = SettingsAddonsManagerSelectors

    /**
     * Installs [addonTitle] from the add-ons manager list, then closes the install-completed prompt. When
     * [allowInPrivateBrowsing] is true the "Allow in private browsing" checkbox is ticked before confirming. Assumes
     * the add-ons manager list is already open. Mirrors the legacy installAddon / installAddonInPrivateMode +
     * closeAddonInstallCompletePrompt flow.
     */
    fun installAddon(addonTitle: String, allowInPrivateBrowsing: Boolean = false): SettingsAddonsManagerPage {
        mozWaitUntilAbsent(SettingsAddonsManagerSelectors.ADD_ONS_PROGRESS_BAR, timeout = waitingTimeLong)
        mozClick(SettingsAddonsManagerSelectors.INSTALL_ADDON_BUTTON(addonTitle))
        mozVerify(SettingsAddonsManagerSelectors.ADDON_PERMISSION_PROMPT_TITLE(addonTitle), timeout = waitingTimeLong)
        if (allowInPrivateBrowsing) {
            mozClick(SettingsAddonsManagerSelectors.ALLOW_IN_PRIVATE_BROWSING_CHECKBOX)
        }
        // The permission dialog disables its Add button for ~1s after appearing, so wait for it to
        // become enabled before clicking (mirrors the legacy allowPermissionToInstall).
        mozClickWhenEnabled(SettingsAddonsManagerSelectors.ADDON_PERMISSION_ALLOW_BUTTON)
        mozVerify(SettingsAddonsManagerSelectors.ADDON_INSTALL_COMPLETED_TITLE(addonTitle), timeout = waitingTimeLong)
        // Closing the "<addon> was added" dialog is best-effort: some extensions auto-open an
        // onboarding tab on install that tears the dialog down first, and the legacy
        // closeAddonInstallCompletePrompt ignored the click result too.
        mozClickIfPresent(SettingsAddonsManagerSelectors.ADDON_INSTALL_COMPLETED_OK_BUTTON)
        return this
    }

    /**
     * Opens the detail screen for the installed [addonTitle] and removes it, returning to the add-ons manager list.
     * Mirrors the legacy openDetailedMenuForAddon + removeAddon flow.
     */
    fun removeInstalledExtension(addonTitle: String): SettingsAddonsManagerPage {
        mozVerify(SettingsAddonsManagerSelectors.INSTALLED_ADDON_ITEM(addonTitle))
        mozClick(SettingsAddonsManagerSelectors.INSTALLED_ADDON_ITEM(addonTitle))
        mozVerify(SettingsAddonsManagerSelectors.REMOVE_ADDON_BUTTON)
        mozClick(SettingsAddonsManagerSelectors.REMOVE_ADDON_BUTTON)
        mozVerify(SettingsAddonsManagerSelectors.REMOVE_ADDON_DIALOG_CONFIRM_BUTTON)
        mozClick(SettingsAddonsManagerSelectors.REMOVE_ADDON_DIALOG_CONFIRM_BUTTON)
        return this
    }

    /**
     * Opens the detail screen for the installed [addonTitle], toggles it off, and returns to the add-ons manager list.
     * Mirrors the legacy openDetailedMenuForAddon + disableExtension + waitUntilSnackbarGone + goBack flow.
     */
    fun disableInstalledExtension(addonTitle: String): SettingsAddonsManagerPage {
        mozVerify(SettingsAddonsManagerSelectors.INSTALLED_ADDON_ITEM(addonTitle))
        mozClick(SettingsAddonsManagerSelectors.INSTALLED_ADDON_ITEM(addonTitle))
        mozVerify(SettingsAddonsManagerSelectors.ENABLE_OR_DISABLE_EXTENSION_TOGGLE)
        mozClick(SettingsAddonsManagerSelectors.ENABLE_OR_DISABLE_EXTENSION_TOGGLE)
        // The "extension disabled" snackbar overlays the toolbar; wait it out before navigating back,
        // mirroring the legacy waitUntilSnackbarGone.
        mozWaitUntilAbsent(BrowserPageSelectors.SNACKBAR)
        mozClick(SettingsAddonsManagerSelectors.NAVIGATE_BACK_TOOLBAR_BUTTON)
        return this
    }
}

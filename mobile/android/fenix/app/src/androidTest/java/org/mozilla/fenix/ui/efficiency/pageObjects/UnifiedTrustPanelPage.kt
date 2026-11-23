/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationGraph
import org.mozilla.fenix.ui.efficiency.navigation.NavigationOptions
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.CustomTabsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.UnifiedTrustPanelSelectors

class UnifiedTrustPanelPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) :
    BasePage(composeRule) {
    override val pageName = "UnifiedTrustPanelPage"

    internal override fun registerNavigation(builder: NavigationGraph.Builder) {
        builder.register(
            from = "BrowserPage",
            to = pageName,
            // The site-info button's tag depends on the page's state (insecure connection / secure /
            // tracking protection off / unknown). Try each - ClickIfPresent opens the panel from
            // whichever one the page shows.
            steps =
                listOf(
                    NavigationStep.ClickIfPresent(ToolbarSelectors.INSECURE_CONNECTION_INFORMATION_BUTTON),
                    NavigationStep.ClickIfPresent(ToolbarSelectors.SECURE_SITE_INFORMATION_BUTTON),
                    NavigationStep.ClickIfPresent(ToolbarSelectors.TRACKING_PROTECTION_OFF_INFORMATION_BUTTON),
                    NavigationStep.ClickIfPresent(ToolbarSelectors.UNKNOWN_SITE_INFORMATION_BUTTON),
                ),
        )

        // From a custom tab, the trust panel opens via the custom-tab toolbar's "Site information" button.
        builder.register(
            from = "CustomTabsPage",
            to = pageName,
            steps = listOf(NavigationStep.Click(CustomTabsSelectors.SITE_INFO_BUTTON)),
        )

        // Dismiss the (root) panel back to the browser. The fragment's BackHandler dismisses the whole sheet from the
        // root ProtectionPanel, so a single back press returns to the page underneath. Lets tests route away after
        // interacting with the panel (e.g. toggling per-site ETP, then visiting settings).
        builder.register(
            from = pageName,
            to = "BrowserPage",
            steps = listOf(NavigationStep.PressBack),
        )
    }

    override fun navigateToPage(
        url: String,
        forceNavigation: Boolean,
        navigationOptions: NavigationOptions,
    ): UnifiedTrustPanelPage {
        super.navigateToPage(
            url = url.ifBlank { "example.com" },
            forceNavigation = forceNavigation,
            navigationOptions = navigationOptions,
        )
        return this
    }

    override val selectorCatalog = UnifiedTrustPanelSelectors

    fun verifyTheClearCookiesAndSiteDataDialog(webSite: String): UnifiedTrustPanelPage {
        mozVerify(UnifiedTrustPanelSelectors.CLEAR_COOKIES_AND_SITE_DATA_DIALOG_TITLE)
        mozVerify(UnifiedTrustPanelSelectors.CLEAR_COOKIES_AND_SITE_DATA_DIALOG_DESCRIPTION(webSite))
        mozVerify(UnifiedTrustPanelSelectors.CLEAR_COOKIES_AND_SITE_DATA_DIALOG_CLEAR_BUTTON)
        mozVerify(UnifiedTrustPanelSelectors.CLEAR_COOKIES_AND_SITE_DATA_DIALOG_CANCEL_BUTTON)

        return this
    }

    // Shared capability: assert the full trust-panel state (site identity, ETP banner + toggle,
    // trackers-blocked option, connection security). Mirrors the legacy verifyUnifiedTrustPanelItems.
    // Reused by every quick-settings / site-security test (incl. the custom-tab variants).
    fun verifyUnifiedTrustPanelItems(
        webSite: String,
        webSiteURL: String,
        isTheWebSiteSecure: Boolean,
        isEnhancedTrackingProtectionEnabled: Boolean,
        isTrackerBlockingEnabled: Boolean,
        areTrackersBlocked: Boolean,
        shouldWebSiteURLBeDisplayed: Boolean = true,
    ): UnifiedTrustPanelPage {
        mozVerify(UnifiedTrustPanelSelectors.WEBSITE_TITLE(webSite))
        if (shouldWebSiteURLBeDisplayed) {
            mozVerify(UnifiedTrustPanelSelectors.WEBSITE_URL(webSiteURL))
        }
        verifyEnhancedTrackingProtectionState(isEnhancedTrackingProtectionEnabled, isTheWebSiteSecure)
        verifyTrackersBlockedState(isTheWebSiteSecure, isTrackerBlockingEnabled, areTrackersBlocked)
        verifySiteSecurityState(isTheWebSiteSecure)
        mozVerify(UnifiedTrustPanelSelectors.CLEAR_COOKIES_AND_SITE_DATA_BUTTON)
        mozVerify(UnifiedTrustPanelSelectors.PRIVACY_SETTINGS_LINK)
        return this
    }

    fun clickTheEnhancedTrackingProtectionOption(): UnifiedTrustPanelPage {
        mozClick(UnifiedTrustPanelSelectors.ETP_TOGGLE_LABEL)
        return this
    }

    /** Assert the per-site ETP toggle badge shows ON (or OFF when [isOn] is false). */
    fun verifyEnhancedTrackingProtectionToggleState(isOn: Boolean): UnifiedTrustPanelPage {
        if (isOn) {
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_ON)
        } else {
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_OFF)
        }
        return this
    }

    /**
     * Open the "trackers blocked" details sub-panel by clicking the banner. The banner is clickable only when
     * trackers > 0, but its content description is chosen by connection security first: the tracking mock asset is
     * served over HTTP, so the banner shows the not-secure text (not "…blocked on this site", which only appears on a
     * secure page). Click it via that content description.
     */
    fun openTrackersBlockedDetails(): UnifiedTrustPanelPage {
        mozClick(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_SECURE_TITLE)
        return this
    }

    /** Assert each given category row is listed on the trackers-blocked details sub-panel. */
    fun verifyBlockedTrackerCategories(vararg categoryRows: Selector): UnifiedTrustPanelPage {
        categoryRows.forEach { mozVerify(it) }
        return this
    }

    /**
     * From the trackers-blocked details sub-panel, open a category's detail panel, assert its title and the "Blocked"
     * list header, then press back to return to the details sub-panel (the trust panel's BackHandler pops one level).
     */
    fun verifyTrackerCategoryDetails(categoryRow: Selector, categoryTitle: Selector): UnifiedTrustPanelPage {
        mozClick(categoryRow)
        mozVerify(categoryTitle)
        mozVerify(UnifiedTrustPanelSelectors.CATEGORY_DETAIL_BLOCKED_HEADER)
        mozPressBack()
        return this
    }

    private fun verifyEnhancedTrackingProtectionState(isEnabled: Boolean, isSecure: Boolean) {
        if (isEnabled) {
            if (isSecure) {
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_PROTECTED_TITLE)
            } else {
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_SECURE_TITLE)
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_SECURE_DESCRIPTION)
            }
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_LABEL)
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_ENABLED_DESCRIPTION)
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_ON)
        } else {
            if (isSecure) {
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_PROTECTED_TITLE)
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_PROTECTED_DESCRIPTION)
            } else {
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_SECURE_TITLE)
                mozVerify(UnifiedTrustPanelSelectors.ETP_BANNER_NOT_SECURE_DESCRIPTION)
            }
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_LABEL)
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_DISABLED_DESCRIPTION)
            mozVerify(UnifiedTrustPanelSelectors.ETP_TOGGLE_OFF)
        }
    }

    private fun verifyTrackersBlockedState(
        isSecure: Boolean,
        isTrackerBlockingEnabled: Boolean,
        areTrackersBlocked: Boolean,
    ) {
        if (!isTrackerBlockingEnabled) {
            mozVerify(UnifiedTrustPanelSelectors.TRACKERS_DISABLED_NONE_BLOCKED)
        } else if (isSecure) {
            if (areTrackersBlocked) {
                mozVerify(UnifiedTrustPanelSelectors.TRACKERS_BLOCKED_ON_SITE)
            } else {
                mozVerify(UnifiedTrustPanelSelectors.TRACKERS_PROTECTED_NONE_BLOCKED)
            }
        }
        // insecure + tracker-blocking enabled: legacy asserts nothing here
    }

    private fun verifySiteSecurityState(isSecure: Boolean) {
        if (isSecure) {
            mozVerify(UnifiedTrustPanelSelectors.CONNECTION_SECURE)
            mozVerify(UnifiedTrustPanelSelectors.CONNECTION_VERIFIED_BY)
        } else {
            mozVerify(UnifiedTrustPanelSelectors.CONNECTION_NOT_SECURE)
        }
    }
}

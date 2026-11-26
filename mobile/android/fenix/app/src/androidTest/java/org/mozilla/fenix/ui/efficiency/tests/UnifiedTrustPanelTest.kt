/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import androidx.core.net.toUri
import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.TestAssetHelper.enhancedTrackingProtectionAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.core.WaitPolicy
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.pageObjects.ProtectionsDashboardPage
import org.mozilla.fenix.ui.efficiency.selectors.UnifiedTrustPanelSelectors
import org.mozilla.fenix.ui.efficiency.selectors.UnifiedTrustPanelSelectors.CLEAR_COOKIES_AND_SITE_DATA_BUTTON

class UnifiedTrustPanelTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186723
    @SmokeTest
    @Test
    fun verifyClearCookiesAndSiteDataFromQuickSettingsTest() {
        val loginPage = "https://mozilla-mobile.github.io/testapp/loginForm"
        val originWebsite = "mozilla-mobile.github.io"

        on.browserPage.navigateToPage(loginPage)
        on.unifiedTrustPanel.navigateToPage().mozClick(CLEAR_COOKIES_AND_SITE_DATA_BUTTON)
        on.unifiedTrustPanel.verifyTheClearCookiesAndSiteDataDialog(originWebsite)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186718
    // Converted from legacy UnifiedTrustPanelTest.verifySecurePageConnectionFromQuickSettingsWithNoTrackersTest
    @SmokeTest
    @Test
    fun verifySecurePageConnectionFromQuickSettingsWithNoTrackersTest() {
        val firstPage = "https://mozilla-mobile.github.io/testapp"

        on.browserPage.navigateToPage(firstPage)
        on.browserPage.verifyPageContent("Lets test!")
        on.unifiedTrustPanel.navigateToPage()
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = "Test App",
            webSiteURL = "mozilla-mobile.github.io",
            isTheWebSiteSecure = true,
            isEnhancedTrackingProtectionEnabled = true,
            isTrackerBlockingEnabled = true,
            areTrackersBlocked = false,
        )
        on.unifiedTrustPanel.clickTheEnhancedTrackingProtectionOption()
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = "Test App",
            webSiteURL = "mozilla-mobile.github.io",
            isTheWebSiteSecure = true,
            isEnhancedTrackingProtectionEnabled = false,
            isTrackerBlockingEnabled = false,
            areTrackersBlocked = false,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186721
    // Converted from legacy UnifiedTrustPanelTest.verifyInsecurePageConnectionFromQuickSettingsWithTrackersTest
    @SmokeTest
    @Test
    fun verifyInsecurePageConnectionFromQuickSettingsWithTrackersTest() {
        appContext.components.settings.setStrictETP()
        val genericPage = mockWebServer.getGenericAsset(1)
        val trackingProtectionPage = mockWebServer.enhancedTrackingProtectionAsset.url

        // Load a generic page first so GeckoView is warm on a fresh run.
        on.browserPage.navigateToPage(genericPage.url.toString())
        on.browserPage.verifyPageContent(genericPage.content)
        val trackingProtectionUrl = trackingProtectionPage.toString()
        on.browserPage.navigateToPage(trackingProtectionUrl)
        // The page reports what it had blocked; these confirm the state the trust panel then claims. The
        // report is only written once trackers have been processed, so these reload between attempts —
        // waiting on the current document is not enough (same reason legacy refreshed on retry).
        on.browserPage.verifyPageContentWithReload(trackingProtectionUrl, "social blocked")
        on.browserPage.verifyPageContentWithReload(trackingProtectionUrl, "ads blocked")
        on.browserPage.verifyPageContentWithReload(trackingProtectionUrl, "analytics blocked")
        on.browserPage.verifyPageContentWithReload(trackingProtectionUrl, "Fingerprinting blocked")
        on.browserPage.verifyPageContentWithReload(trackingProtectionUrl, "Cryptomining blocked")
        on.unifiedTrustPanel.navigateToPage()
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = trackingProtectionPage.host.toString(),
            webSiteURL = trackingProtectionPage.host.toString(),
            shouldWebSiteURLBeDisplayed = false,
            isTheWebSiteSecure = false,
            isEnhancedTrackingProtectionEnabled = true,
            isTrackerBlockingEnabled = true,
            areTrackersBlocked = true,
        )
        on.unifiedTrustPanel.clickTheEnhancedTrackingProtectionOption()
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = trackingProtectionPage.host.toString(),
            webSiteURL = trackingProtectionPage.host.toString(),
            shouldWebSiteURLBeDisplayed = false,
            isTheWebSiteSecure = false,
            isEnhancedTrackingProtectionEnabled = false,
            isTrackerBlockingEnabled = false,
            areTrackersBlocked = false,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186714
    // Converted from legacy UnifiedTrustPanelTest.verifyClearCookiesAndSiteDataFromQuickSettingsInCustomTabsTest
    @SmokeTest
    @Test
    fun verifyClearCookiesAndSiteDataFromQuickSettingsInCustomTabsTest() {
        val customTabPage = "https://mozilla-mobile.github.io/testapp/loginForm"
        val originWebsite = "mozilla-mobile.github.io"

        on.customTabs.launchCustomTab(customTabPage)
        // Opens the trust panel from the custom-tab "Site information" button (CustomTabsPage -> panel edge).
        on.unifiedTrustPanel.navigateToPage().mozClick(CLEAR_COOKIES_AND_SITE_DATA_BUTTON)
        on.unifiedTrustPanel.verifyTheClearCookiesAndSiteDataDialog(originWebsite)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186711
    // Converted from legacy
    // UnifiedTrustPanelTest.verifyInsecurePageConnectionFromQuickSettingsWithNoTrackersInCustomTabsTest
    @SmokeTest
    @Test
    fun verifyInsecurePageConnectionFromQuickSettingsWithNoTrackersInCustomTabsTest() {
        val customTabPage = mockWebServer.getGenericAsset(1)

        on.customTabs.launchCustomTab(customTabPage.url.toString())
        on.unifiedTrustPanel.navigateToPage()
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = customTabPage.title,
            webSiteURL = customTabPage.url.host.toString(),
            isTheWebSiteSecure = false,
            isEnhancedTrackingProtectionEnabled = true,
            isTrackerBlockingEnabled = true,
            areTrackersBlocked = false,
        )
        on.unifiedTrustPanel.clickTheEnhancedTrackingProtectionOption()
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = customTabPage.title,
            webSiteURL = customTabPage.url.host.toString(),
            isTheWebSiteSecure = false,
            isEnhancedTrackingProtectionEnabled = false,
            isTrackerBlockingEnabled = false,
            areTrackersBlocked = false,
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3186713
    @Critical
    @Test
    fun verifyTheTrackersSubMenuInCustomTabTest() {
        // Given a user who turned on Strict Enhanced Tracking Protection (Standard blocks tracking content only in
        // private browsing, so Strict is what surfaces the tracking-content category for a normal custom tab).
        appContext.components.settings.setStrictETP()
        val genericPage = mockWebServer.getGenericAsset(1)
        val customTabPage = "https://senglehardt.com/test/trackingprotection/test_pages/tracking_protection"
        val host = customTabPage.toUri().host.toString()
        val protectionsDashboard = ProtectionsDashboardPage(composeRule)

        // The user opens a tracker-heavy, secure site in a custom tab. A generic load goes first so the ETP list is
        // ready — a cold first load slips trackers through before it initializes, and they are never blocked.
        on.customTabs.launchCustomTab(genericPage.url.toString())
        on.customTabs.verifyWebContent(genericPage.content)
        on.customTabs.launchCustomTab(customTabPage)
        // Web-content text isn't reliably matched in a custom tab, so confirm the load via the toolbar host.
        on.customTabs.verifyUrl(customTabPage)

        // Opening the trust panel shows a secure connection with trackers blocked (the count can lag, so poll for it).
        on.unifiedTrustPanel.navigateToPage()
        on.unifiedTrustPanel.mozVerify(UnifiedTrustPanelSelectors.TRACKERS_BLOCKED_ON_SITE, timeout = waitingTimeLong)
        on.unifiedTrustPanel.verifyUnifiedTrustPanelItems(
            webSite = host,
            webSiteURL = host,
            shouldWebSiteURLBeDisplayed = false,
            isTheWebSiteSecure = true,
            isEnhancedTrackingProtectionEnabled = true,
            isTrackerBlockingEnabled = true,
            areTrackersBlocked = true,
        )

        // Tapping the protection banner (the secure-page "…is on guard" title) opens the trackers-blocked sub-panel.
        on.unifiedTrustPanel.mozClick(UnifiedTrustPanelSelectors.ETP_BANNER_PROTECTED_TITLE, WaitPolicy.Poll())
        on.unifiedTrustPanel.verifyTrackersBlockedSubPanel(host)

        // Opening a blocked category shows its details; tracking content is the one senglehardt reliably blocks.
        on.unifiedTrustPanel.mozVerify(UnifiedTrustPanelSelectors.TRACKERS_DETAILS_TRACKING_CONTENT)
        on.unifiedTrustPanel.mozClick(UnifiedTrustPanelSelectors.TRACKERS_DETAILS_TRACKING_CONTENT, WaitPolicy.Poll())
        on.unifiedTrustPanel.verifyTrackerCategoryDetailPanel(
            host = host,
            categoryTitle = UnifiedTrustPanelSelectors.CATEGORY_DETAIL_TRACKING_CONTENT_TITLE,
            categoryDescription = UnifiedTrustPanelSelectors.CATEGORY_DETAIL_TRACKING_CONTENT_DESCRIPTION,
        )
        // Back returns to the trackers-blocked sub-panel.
        on.unifiedTrustPanel.mozClick(UnifiedTrustPanelSelectors.SUBMENU_BACK_BUTTON)
        on.unifiedTrustPanel.mozVerify(UnifiedTrustPanelSelectors.TRACKERS_PANEL_BLOCKED_ON_SITE_HEADER)

        // The weekly total opens the protections dashboard; it has no back arrow, so system back returns to the list.
        on.unifiedTrustPanel.mozClick(UnifiedTrustPanelSelectors.TRACKERS_BLOCKED_THIS_WEEK_ROW, WaitPolicy.Poll())
        protectionsDashboard.verifyDashboardElements()
        on.unifiedTrustPanel.mozPressBack()
        on.unifiedTrustPanel.mozVerify(UnifiedTrustPanelSelectors.TRACKERS_PANEL_BLOCKED_ON_SITE_HEADER)

        // Backing out once more returns to the trust panel.
        on.unifiedTrustPanel.mozClick(UnifiedTrustPanelSelectors.SUBMENU_BACK_BUTTON)
        on.unifiedTrustPanel.mozVerify(UnifiedTrustPanelSelectors.PRIVACY_SETTINGS_LINK)
    }
}

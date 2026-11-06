/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.content.Context
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import java.security.cert.X509Certificate
import mozilla.components.compose.base.theme.Theme
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class ProtectionPanelTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val resources
        get() = ApplicationProvider.getApplicationContext<Context>().resources

    private val appName
        get() = resources.getString(R.string.app_name_firefox)

    private val protectedOnGuardTitle
        get() = resources.getString(R.string.protection_panel_banner_protected_title, appName)

    private fun blockedTrackersDescription(count: Int): String =
        resources.getQuantityString(
            R.plurals.protection_panel_banner_protected_blocked_trackers_description,
            count,
            count,
        )

    private val qwacStatement
        get() = resources.getString(R.string.connection_security_panel_qualified_certificate)

    private fun gradientBannerContentDescription(count: Int): String =
        "$protectedOnGuardTitle. ${blockedTrackersDescription(count)}"

    @Test
    fun `WHEN multiple trackers were blocked THEN plural banner is shown`() {
        setProtectionPanel(numberOfTrackersBlocked = 5)

        composeTestRule.onNodeWithContentDescription(gradientBannerContentDescription(5)).assertIsDisplayed()
    }

    @Test
    fun `WHEN banner is shown with trackers blocked THEN clicking it invokes the trackers callback`() {
        var clicked = false
        setProtectionPanel(
            numberOfTrackersBlocked = 5,
            onTrackerBlockedMenuClick = { clicked = true },
        )

        composeTestRule.onNodeWithContentDescription(gradientBannerContentDescription(5)).performClick()
        assertTrue(clicked)
    }

    @Test
    fun `WHEN a single tracker was blocked THEN singular banner is shown`() {
        setProtectionPanel(numberOfTrackersBlocked = 1)

        composeTestRule.onNodeWithContentDescription(gradientBannerContentDescription(1)).assertIsDisplayed()
    }

    @Test
    fun `WHEN no trackers were blocked THEN gradient banner is shown with no-blocked-trackers description`() {
        setProtectionPanel(numberOfTrackersBlocked = 0)

        composeTestRule.onNodeWithContentDescription(protectedOnGuardTitle, substring = true).assertIsDisplayed()
        composeTestRule
            .onNodeWithContentDescription(blockedTrackersDescription(1), substring = true)
            .assertDoesNotExist()
    }

    @Test
    fun `WHEN tab is private THEN clear site data menu is not shown`() {
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            isPrivate = true,
        )

        composeTestRule
            .onNodeWithText(resources.getString(R.string.clear_site_data), useUnmergedTree = true)
            .assertDoesNotExist()
    }

    @Test
    fun `WHEN tab is not private THEN clear site data menu is shown`() {
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            isPrivate = false,
        )

        composeTestRule
            .onNodeWithText(resources.getString(R.string.clear_site_data), useUnmergedTree = true)
            .assertExists()
    }

    @Test
    fun `GIVEN QWAC certificate data THEN the QWAC statement is shown`() {
        setProtectionPanel(numberOfTrackersBlocked = 0, qwac = mockk(relaxed = true))

        composeTestRule.onNodeWithText(qwacStatement, substring = true).assertIsDisplayed()
    }

    @Test
    fun `GIVEN no QWAC certificate data THEN the QWAC statement is not shown`() {
        setProtectionPanel(numberOfTrackersBlocked = 0)

        composeTestRule.onNodeWithText(qwacStatement, substring = true, useUnmergedTree = true).assertDoesNotExist()
    }

    private fun setProtectionPanel(
        numberOfTrackersBlocked: Int,
        isPrivate: Boolean = false,
        onTrackerBlockedMenuClick: () -> Unit = {},
        qwac: X509Certificate? = null,
    ) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                ProtectionPanel(
                    websiteInfoState =
                        WebsiteInfoState(
                            isSecured = true,
                            websiteUrl = "https://www.mozilla.org",
                            websiteTitle = "Mozilla",
                            certificate = null,
                            qwac = qwac,
                        ),
                    ipProtectionMenuState = IPProtectionMenuState(),
                    icon = null,
                    isTrackingProtectionEnabled = true,
                    isGlobalTrackingProtectionEnabled = true,
                    isLocalPdf = false,
                    isPrivate = isPrivate,
                    showIPProtection = false,
                    numberOfTrackersBlocked = numberOfTrackersBlocked,
                    websitePermissions = emptyList(),
                    onTrackerBlockedMenuClick = onTrackerBlockedMenuClick,
                    onTrackingProtectionToggleClick = {},
                    onClearSiteDataMenuClick = {},
                    onPrivacySecuritySettingsClick = {},
                    onAutoplayValueClick = {},
                    onToggleablePermissionClick = {},
                    onViewCertificateClick = {},
                    onViewQWACClick = {},
                    onIPProtectionToggle = {},
                    onIPProtectionNavigate = {},
                )
            }
        }
    }
}

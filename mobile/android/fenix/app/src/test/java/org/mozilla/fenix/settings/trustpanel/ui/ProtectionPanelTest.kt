/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.content.Context
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsFocused
import androidx.compose.ui.test.assertIsNotFocused
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import java.security.cert.X509Certificate
import mozilla.components.compose.base.theme.Theme
import org.junit.Assert.assertEquals
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

    private val verifiedByPrefix
        get() = resources.getString(R.string.connection_security_panel_verified_by, "").trim()

    private val viewCertificateDetailsLabel
        get() = resources.getString(R.string.connection_security_panel_view_certificate_details)

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

    @Test
    fun `GIVEN the certificate details link is shown WHEN it is clicked THEN the certificate details are opened`() {
        var opened = 0
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            onSecureConnectionClick = { opened++ },
            showCertificateDetailsLink = true,
        )

        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG).assertIsDisplayed().assertHasClickAction()
        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG).performClick()

        assertEquals(1, opened)
    }

    @Test
    fun `GIVEN the certificate details link is shown THEN it is announced as a button that views the certificate details`() {
        setProtectionPanel(numberOfTrackersBlocked = 0, showCertificateDetailsLink = true)

        val link = composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG).fetchSemanticsNode()

        assertEquals(Role.Button, link.config[SemanticsProperties.Role])
        assertEquals(viewCertificateDetailsLabel, link.config[SemanticsActions.OnClick].label)
    }

    @Test
    fun `GIVEN the certificate details link is shown and a page with a QWAC THEN the old secure connection and QWAC rows are not shown`() {
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            certificate = mockk(relaxed = true),
            qwac = mockk(relaxed = true),
            showCertificateDetailsLink = true,
        )

        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG).assertIsDisplayed()
        composeTestRule.onNodeWithText(verifiedByPrefix, substring = true, useUnmergedTree = true).assertDoesNotExist()
        composeTestRule.onNodeWithText(qwacStatement, substring = true, useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the certificate details link is not shown THEN the old secure connection row is shown instead`() {
        setProtectionPanel(numberOfTrackersBlocked = 0, certificate = mockk(relaxed = true))

        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG, useUnmergedTree = true).assertDoesNotExist()
        composeTestRule.onNodeWithText(verifiedByPrefix, substring = true, useUnmergedTree = true).assertExists()
    }

    @Test
    fun `GIVEN returning from the certificate details THEN the secure connection link is focused once`() {
        var focusedCount = 0
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            showCertificateDetailsLink = true,
            focusSecureConnectionLink = true,
            onSecureConnectionLinkFocusHandled = { focusedCount++ },
        )

        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG).assertIsFocused()
        composeTestRule.runOnIdle { assertEquals(1, focusedCount) }
    }

    @Test
    fun `GIVEN not returning from the certificate details THEN the secure connection link is not focused`() {
        var focusedCount = 0
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            showCertificateDetailsLink = true,
            onSecureConnectionLinkFocusHandled = { focusedCount++ },
        )

        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG).assertIsNotFocused()
        composeTestRule.runOnIdle { assertEquals(0, focusedCount) }
    }

    @Test
    fun `GIVEN returning from the certificate details WHEN the secure connection link is hidden THEN the focus request is dropped`() {
        var handledCount = 0
        setProtectionPanel(
            numberOfTrackersBlocked = 0,
            showCertificateDetailsLink = false,
            focusSecureConnectionLink = true,
            onSecureConnectionLinkFocusHandled = { handledCount++ },
        )

        composeTestRule.onNodeWithTag(SECURE_CONNECTION_LINK_TEST_TAG, useUnmergedTree = true).assertDoesNotExist()
        composeTestRule.runOnIdle { assertEquals(1, handledCount) }
    }

    private fun setProtectionPanel(
        numberOfTrackersBlocked: Int,
        isPrivate: Boolean = false,
        onTrackerBlockedMenuClick: () -> Unit = {},
        qwac: X509Certificate? = null,
        certificate: X509Certificate? = null,
        onSecureConnectionClick: () -> Unit = {},
        showCertificateDetailsLink: Boolean = false,
        focusSecureConnectionLink: Boolean = false,
        onSecureConnectionLinkFocusHandled: () -> Unit = {},
    ) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                ProtectionPanel(
                    websiteInfoState =
                        WebsiteInfoState(
                            isSecured = true,
                            websiteUrl = "https://www.mozilla.org",
                            websiteTitle = "Mozilla",
                            certificate = certificate,
                            qwac = qwac,
                        ),
                    ipProtectionMenuState = IPProtectionMenuState(),
                    icon = null,
                    isTrackingProtectionEnabled = true,
                    isGlobalTrackingProtectionEnabled = true,
                    isLocalPdf = false,
                    isPrivate = isPrivate,
                    showCertificateDetailsLink = showCertificateDetailsLink,
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
                    onSecureConnectionClick = onSecureConnectionClick,
                    focusSecureConnectionLink = focusSecureConnectionLink,
                    onSecureConnectionLinkFocusHandled = onSecureConnectionLinkFocusHandled,
                    onIPProtectionToggle = {},
                    onIPProtectionNavigate = {},
                )
            }
        }
    }
}

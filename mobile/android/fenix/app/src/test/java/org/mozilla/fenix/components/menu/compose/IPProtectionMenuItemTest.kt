/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.Theme
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class IPProtectionMenuItemTest {
    @get:Rule val composeTestRule = createComposeRule()

    private var toggleCount = 0
    private var navigateCount = 0

    private val toggleLabel: String
        get() = testContext.getString(R.string.ip_protection_toggle_label)

    private val navigateLabel: String
        get() = testContext.getString(R.string.ip_protection_navigate_settings)

    @Test
    fun `GIVEN IP protection is enabled WHEN the menu item is displayed THEN the toggle exposes the on state as a switch`() {
        setContent(IPProtectionStatus.Enabled)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.preferences_ip_protection_on)))
            .assert(hasRole(Role.Switch))
    }

    @Test
    fun `GIVEN IP protection is enabled WHEN the menu item is displayed THEN the badge is not exposed separately`() {
        setContent(IPProtectionStatus.Enabled)

        composeTestRule
            .onNodeWithText(testContext.getString(R.string.preferences_ip_protection_on))
            .assertDoesNotExist()
    }

    @Test
    fun `GIVEN IP protection is disabled WHEN the menu item is displayed THEN the toggle exposes the off state as a switch`() {
        setContent(IPProtectionStatus.Disabled)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.preferences_ip_protection_off)))
            .assert(hasRole(Role.Switch))
    }

    @Test
    fun `GIVEN IP protection is connecting WHEN the menu item is displayed THEN the toggle exposes the connecting state as a switch`() {
        setContent(IPProtectionStatus.Activating)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.ip_protection_menu_connecting)))
            .assert(hasRole(Role.Switch))
    }

    @Test
    fun `GIVEN the data limit is reached WHEN the menu item is displayed THEN the toggle exposes the paused state as a switch`() {
        setContent(IPProtectionStatus.DataLimitReached)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.ip_protection_menu_paused)))
            .assert(hasRole(Role.Switch))
    }

    @Test
    fun `GIVEN the connection failed WHEN the menu item is displayed THEN the toggle exposes the error state as a switch`() {
        setContent(IPProtectionStatus.ConnectionError)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.ip_protection_menu_error)))
            .assert(hasRole(Role.Switch))
    }

    @Test
    fun `GIVEN authentication is required WHEN the menu item is displayed THEN the row exposes the try VPN action as a button`() {
        setContent(IPProtectionStatus.AuthRequired)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.ip_protection_menu_try_vpn_cta)))
            .assert(hasRole(Role.Button))
    }

    @Test
    fun `GIVEN the service has not accepted the signed-in account yet WHEN the menu item is displayed THEN the row exposes the connecting state as a button`() {
        setContent(IPProtectionStatus.AwaitingService)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.ip_protection_menu_connecting)))
            .assert(hasRole(Role.Button))
    }

    @Test
    fun `GIVEN IP protection has not reported its state yet WHEN the menu item is displayed THEN the toggle exposes the off state as a switch`() {
        setContent(IPProtectionStatus.Uninitialized)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assert(hasStateDescription(testContext.getString(R.string.preferences_ip_protection_off)))
            .assert(hasRole(Role.Switch))
    }

    @Test
    fun `GIVEN an IP protection menu item WHEN it is displayed THEN the navigation chevron is exposed as a clickable button`() {
        setContent(IPProtectionStatus.Disabled)

        composeTestRule.onNodeWithContentDescription(navigateLabel).assert(hasRole(Role.Button)).assertHasClickAction()
    }

    @Test
    fun `GIVEN an IP protection menu item WHEN the chevron is clicked THEN only the navigate callback is invoked`() {
        setContent(IPProtectionStatus.Disabled)

        composeTestRule.onNodeWithContentDescription(navigateLabel).performClick()

        assertEquals(1, navigateCount)
        assertEquals(0, toggleCount)
    }

    @Test
    fun `GIVEN IP protection is disabled WHEN the label row is clicked twice rapidly THEN only the toggle callback is invoked, once per click`() {
        setContent(IPProtectionStatus.Disabled)

        repeat(2) {
            composeTestRule.onNodeWithText(toggleLabel).performClick()
        }

        assertEquals(2, toggleCount)
        assertEquals(0, navigateCount)
    }

    @Test
    fun `GIVEN the data limit is reached WHEN the menu item is displayed THEN the limit description is displayed`() {
        setContent(status = IPProtectionStatus.DataLimitReached, quotaLimit = DataVolume(51L * 1024 * 1024 * 1024))

        composeTestRule
            .onNodeWithText(testContext.getString(R.string.ip_protection_menu_limit_reached, 51))
            .assertIsDisplayed()
    }

    @Test
    fun `GIVEN the data limit is reached without a known limit WHEN the menu item is displayed THEN no limit description is displayed`() {
        setContent(status = IPProtectionStatus.DataLimitReached, quotaLimit = null)

        composeTestRule
            .onNodeWithText(testContext.getString(R.string.ip_protection_menu_limit_reached, 0))
            .assertDoesNotExist()
    }

    @Test
    fun `GIVEN the data limit is reached WHEN reading the row THEN it is exposed as disabled`() {
        setContent(status = IPProtectionStatus.DataLimitReached, quotaLimit = DataVolume(50L * 1024 * 1024 * 1024))

        composeTestRule
            .onNode(
                hasText(toggleLabel) and hasText(testContext.getString(R.string.ip_protection_menu_limit_reached, 50))
            )
            .assertIsNotEnabled()
            .assert(hasStateDescription(testContext.getString(R.string.ip_protection_menu_paused)))
    }

    @Test
    fun `GIVEN IP protection can be toggled WHEN reading the row THEN it is exposed as enabled`() {
        setContent(IPProtectionStatus.Enabled)

        composeTestRule
            .onNodeWithText(toggleLabel)
            .assertIsEnabled()
            .assert(hasStateDescription(testContext.getString(R.string.preferences_ip_protection_on)))
    }

    @Test
    fun `GIVEN the data limit is reached WHEN the label row is clicked THEN the toggle callback is not invoked`() {
        setContent(status = IPProtectionStatus.DataLimitReached, quotaLimit = DataVolume(50L * 1024 * 1024 * 1024))

        composeTestRule.onNodeWithText(toggleLabel).assertHasNoClickAction().performClick()

        assertEquals(0, toggleCount)
    }

    @Test
    fun `GIVEN the data limit is reached WHEN the chevron is clicked THEN the navigate callback is invoked`() {
        setContent(status = IPProtectionStatus.DataLimitReached, quotaLimit = DataVolume(50L * 1024 * 1024 * 1024))

        composeTestRule.onNodeWithContentDescription(navigateLabel).performClick()

        assertEquals(1, navigateCount)
        assertEquals(0, toggleCount)
    }

    private fun hasStateDescription(value: String) =
        SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, value)

    private fun hasRole(role: Role) = SemanticsMatcher.expectValue(SemanticsProperties.Role, role)

    private fun setContent(status: IPProtectionStatus, quotaLimit: DataVolume? = null) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionMenuItem(
                    state = IPProtectionMenuState(status = status, dataLimit = quotaLimit),
                    onToggle = { toggleCount++ },
                    onNavigate = { navigateCount++ },
                )
            }
        }
    }
}

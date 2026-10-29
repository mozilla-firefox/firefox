/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import androidx.compose.material3.SnackbarHostState
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasProgressBarRangeInfo
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isToggleable
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.compose.base.theme.Theme
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

private const val PROMO_DATE = "September 30"

@OptIn(ExperimentalAndroidComponentsApi::class)
@RunWith(AndroidJUnit4::class)
class IPProtectionScreenTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN all data remains WHEN rendering the screen THEN the data limit progress bar is full`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        max = DataVolume.roundedGigabytes(50),
                        remaining = DataVolume.roundedGigabytes(50),
                    ),
            )

        setScreen(state = state)

        composeTestRule.onNode(hasProgressBarRangeInfo(ProgressBarRangeInfo(1f, 0f..1f))).assertExists()
    }

    @Test
    fun `GIVEN a portion of the quota is used WHEN rendering the screen THEN the progress bar shows the same percent as fraction`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        max = DataVolume.roundedGigabytes(100),
                        remaining = DataVolume.roundedGigabytes(80),
                    ),
            )

        setScreen(state = state)

        composeTestRule.onNode(hasProgressBarRangeInfo(ProgressBarRangeInfo(0.8f, 0f..1f))).assertExists()
    }

    @Test
    fun `GIVEN the data limit is reached WHEN rendering the screen THEN the data limit UI is shown`() {
        val maxDataGb = 50f
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.DataLimitReached,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        resetTime = null,
                        max = DataVolume.roundedGigabytes(maxDataGb),
                        remaining = DataVolume.Zero,
                    ),
            )

        setScreen(state = state)

        composeTestRule.onNode(hasProgressBarRangeInfo(ProgressBarRangeInfo(0f, 0f..1f))).assertExists()

        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_data_limit_label)).assertExists()

        composeTestRule
            .onNodeWithText(
                testContext.getString(
                    R.string.ip_protection_data_limit_reached_description,
                    maxDataGb.toInt(),
                )
            )
            .assertExists()

        composeTestRule
            .onNode(hasText(testContext.getString(R.string.ip_protection_toggle_label)) and isToggleable())
            .assertExists()
            .assertIsOff()
            .assertIsNotEnabled()

        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_location_section)).assertExists()

        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_get_started)).assertDoesNotExist()
    }

    @Test
    fun `GIVEN no promo is running WHEN rendering the screen THEN the description is shown instead of the promo card`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                        max = DataVolume.roundedGigabytes(50),
                        remaining = DataVolume.roundedGigabytes(40),
                    ),
            )

        setScreen(state = state)

        composeTestRule.onNode(hasVpnDescription()).assertExists()

        composeTestRule.onNode(hasPromoCardTitle()).assertDoesNotExist()
    }

    @Test
    fun `GIVEN no promo is running WHEN the description learn more link is clicked THEN the callback is invoked`() {
        var learnMoreClicks = 0
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                        max = DataVolume.roundedGigabytes(50),
                        remaining = DataVolume.roundedGigabytes(40),
                    ),
            )

        setScreen(state = state, onLearnMoreClick = { learnMoreClicks++ })

        composeTestRule.onNode(hasVpnDescription()).performSemanticsAction(SemanticsActions.OnClick)

        assertEquals(1, learnMoreClicks)
    }

    @Test
    fun `GIVEN an unlimited plan and a promo date WHEN rendering the screen THEN the promo description is shown`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                usageQuota = UsageQuota.Unmetered(resetTime = null),
            )

        setScreen(state = state, promoDate = PROMO_DATE)

        composeTestRule
            .onNode(hasContentDescription("unlimited bandwidth through $PROMO_DATE", substring = true))
            .assertExists()
        composeTestRule.onNode(hasVpnDescription()).assertDoesNotExist()
    }

    // The promo card advertises unlimited bandwidth, which must not run alongside a data cap - bug 2045899.
    @Test
    fun `GIVEN a metered plan and a promo date WHEN rendering the screen THEN the description is shown`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        resetTime = Instant.parse("2026-06-01T00:00:00Z"),
                        max = DataVolume.roundedGigabytes(50),
                        remaining = DataVolume.roundedGigabytes(40),
                    ),
            )

        setScreen(state = state, promoDate = PROMO_DATE)

        composeTestRule.onNode(hasVpnDescription()).assertExists()

        composeTestRule.onNode(hasPromoCardTitle()).assertDoesNotExist()
    }

    @Test
    fun `GIVEN an unlimited plan and a null promo date WHEN rendering the screen THEN the description is shown`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Active,
                usageQuota = UsageQuota.Unmetered(resetTime = Instant.parse("2026-06-01T00:00:00Z")),
            )

        setScreen(state = state)

        composeTestRule.onNode(hasVpnDescription()).assertExists()

        composeTestRule.onNode(hasPromoCardTitle()).assertDoesNotExist()
    }

    @Test
    fun `GIVEN usage data has not arrived and no promo is running WHEN rendering the screen THEN the description is shown`() {
        setScreen(state = IPProtectionState(eligibilityStatus = EligibilityStatus.Eligible, usageQuota = null))

        composeTestRule.onNode(hasVpnDescription()).assertExists()

        composeTestRule.onNode(hasPromoCardTitle()).assertDoesNotExist()
    }

    @Test
    fun `GIVEN usage data has not arrived and a promo is running WHEN rendering the screen THEN the promo card is shown`() {
        setScreen(state = IPProtectionState(eligibilityStatus = EligibilityStatus.Eligible), promoDate = PROMO_DATE)

        composeTestRule.onNode(hasPromoCardTitle()).assertExists()
    }

    @Test
    fun `GIVEN the proxy is activating WHEN rendering the screen THEN the location row is not selectable`() {
        val state =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Activating,
                serviceStatus = ServiceState.Ready,
                usageQuota =
                    UsageQuota.Metered(
                        max = DataVolume.roundedGigabytes(50),
                        remaining = DataVolume.roundedGigabytes(40),
                    ),
            )

        setScreen(state = state, isLocationSelectionEnabled = true)

        composeTestRule
            .onNode(hasText(testContext.getString(R.string.ip_protection_toggle_label)) and isToggleable())
            .assertIsNotEnabled()

        composeTestRule
            .onNodeWithText(testContext.getString(R.string.ip_protection_location_recommended_label))
            .assertHasNoClickAction()
    }

    @Test
    fun `GIVEN the user has to sign in WHEN rendering the screen THEN an enabled Get started button is shown`() {
        setScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unauthenticated,
                    accountState = AccountState(AccountStatus.NoAccount),
                )
        )

        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_get_started)).assertIsEnabled()
        composeTestRule
            .onNodeWithText(testContext.getString(R.string.ip_protection_location_section))
            .assertDoesNotExist()
    }

    @Test
    fun `GIVEN the service has not accepted the signed-in account yet WHEN rendering the screen THEN a disabled Connecting button is shown`() {
        setScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unauthenticated,
                    accountState = AccountState(AccountStatus.Authenticated),
                )
        )

        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_connecting)).assertIsNotEnabled()
        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_get_started)).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the service is unavailable WHEN rendering the screen THEN the Get started button is disabled`() {
        setScreen(
            state =
                IPProtectionState(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceStatus = ServiceState.Unavailable,
                )
        )

        composeTestRule.onNodeWithText(testContext.getString(R.string.ip_protection_get_started)).assertIsNotEnabled()
    }

    private fun setScreen(
        state: IPProtectionState,
        promoDate: String? = null,
        onLearnMoreClick: () -> Unit = {},
        isLocationSelectionEnabled: Boolean = false,
    ) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionScreen(
                    state = state,
                    snackbarHostState = SnackbarHostState(),
                    promoDate = promoDate,
                    onVpnToggle = {},
                    onLearnMoreClick = onLearnMoreClick,
                    onGetStartedClick = {},
                    showDebugAction = false,
                    onDebugActionClick = {},
                    onNavigateBack = {},
                    onLocationClicked = {},
                    isLocationSelectionEnabled = isLocationSelectionEnabled,
                )
            }
        }
    }
}

private fun hasPromoCardTitle() =
    hasText(testContext.getString(R.string.ip_protection_promo_headline, testContext.getString(R.string.firefox)))

private fun hasVpnDescription() =
    hasContentDescription(
        testContext.getString(
            R.string.ip_protection_promo_body_2,
            testContext.getString(R.string.ip_protection_learn_more),
        ),
        substring = true,
    )

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.R

class OnboardingMapperTest {

    private val actions = mutableListOf<OnboardingAction>()

    private fun mapToState(onboardingPageUiData: OnboardingPageUiData) =
        mapToOnboardingPageState(
            onboardingPageUiData = onboardingPageUiData,
            onAction = { action -> actions += action },
        )

    @Test
    fun `GIVEN a default browser page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.ic_onboarding_welcome,
                title = "default browser title",
                description = "default browser body with link text",
                primaryButton = Action("default browser primary button text", noOp),
                secondaryButton = Action("default browser secondary button text", noOp),
            )

        val state = mapToState(defaultBrowserPageUiData)
        assertEquals(expected, state.withNoOpCallbacks())
    }

    @Test
    fun `GIVEN a default browser page WHEN its buttons are clicked THEN the default browser actions are dispatched`() {
        val state = mapToState(defaultBrowserPageUiData)

        assertEquals(
            OnboardingAction.DefaultBrowser.SetAsDefaultClicked,
            actionDispatchedBy(state.primaryButton.onClick),
        )
        assertEquals(OnboardingAction.DefaultBrowser.SkipClicked, actionDispatchedBy(state.secondaryButton!!.onClick))
    }

    @Test
    fun `GIVEN a sync sign in page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.ic_onboarding_sync,
                title = "sync title",
                description = "sync body",
                primaryButton = Action("sync primary button text", noOp),
                secondaryButton = Action("sync secondary button text", noOp),
            )

        val state = mapToState(syncPageUiData)
        assertEquals(expected, state.withNoOpCallbacks())
    }

    @Test
    fun `GIVEN a sync sign in page WHEN its buttons are clicked THEN the sync sign in actions are dispatched`() {
        val state = mapToState(syncPageUiData)

        assertEquals(OnboardingAction.SyncSignIn.SignInClicked, actionDispatchedBy(state.primaryButton.onClick))
        assertEquals(OnboardingAction.SyncSignIn.SkipClicked, actionDispatchedBy(state.secondaryButton!!.onClick))
    }

    @Test
    fun `GIVEN a sync pairing page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_sync_qr,
                title = "sync pairing title",
                description = "sync pairing body",
                primaryButton =
                    Action(
                        text = "sync pairing scan button text",
                        onClick = noOp,
                        leadingIconRes = iconsR.drawable.mozac_ic_qr_code_24,
                    ),
                secondaryButton = Action("sync pairing email button text", noOp),
                onSkipClick = noOp,
            )

        val state = mapToState(syncPairingPageUiData)
        assertEquals(expected, state.withNoOpCallbacks())
    }

    @Test
    fun `GIVEN a sync pairing page WHEN its buttons are clicked THEN the sync pairing actions are dispatched`() {
        val state = mapToState(syncPairingPageUiData)

        assertEquals(OnboardingAction.SyncPairing.ScanClicked, actionDispatchedBy(state.primaryButton.onClick))
        assertEquals(OnboardingAction.SyncPairing.EmailClicked, actionDispatchedBy(state.secondaryButton!!.onClick))
        assertEquals(OnboardingAction.SyncPairing.SkipClicked, actionDispatchedBy(state.onSkipClick!!))
    }

    @Test
    fun `GIVEN a terms of service page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_tou,
                title = "terms of service title",
                description = "terms of service body",
                primaryButton = Action("terms of service primary button text", noOp),
                termsOfService = termsOfServicePageUiData.termsOfService,
            )

        val state = mapToState(termsOfServicePageUiData)
        assertEquals(expected, state.withNoOpCallbacks())
    }

    @Test
    fun `GIVEN a terms of service page WHEN its buttons are clicked THEN the terms of service actions are dispatched`() {
        val state = mapToState(termsOfServicePageUiData)

        assertEquals(OnboardingAction.TermsOfService.AcceptClicked, actionDispatchedBy(state.primaryButton.onClick))
    }

    @Test
    fun `GIVEN a marketing data page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.ic_high_five,
                title = "marketing data title",
                description = "marketing data body",
                primaryButton = Action("marketing data button text", noOp),
                marketingData = marketingDataPageUiData.marketingData,
            )

        val state = mapToState(marketingDataPageUiData)
        assertEquals(expected, state.withNoOpCallbacks())
    }

    @Test
    fun `GIVEN a marketing data page WHEN its primary button is clicked THEN no action is dispatched`() {
        val state = mapToState(marketingDataPageUiData)

        assertNull(actionDispatchedBy(state.primaryButton.onClick))
    }

    private fun actionDispatchedBy(click: () -> Unit): OnboardingAction? {
        actions.clear()
        click()
        return actions.singleOrNull()
    }
}

private val noOp: () -> Unit = {}

private fun OnboardingPageState.withNoOpCallbacks() =
    copy(
        primaryButton = primaryButton.copy(onClick = noOp),
        secondaryButton = secondaryButton?.copy(onClick = noOp),
        onSkipClick = onSkipClick?.let { noOp },
    )

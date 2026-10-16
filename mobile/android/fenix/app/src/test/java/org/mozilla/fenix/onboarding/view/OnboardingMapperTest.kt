/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.nimbus.MarketingCardVariant

class OnboardingMapperTest {

    @Test
    fun `GIVEN a default browser page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.ic_onboarding_welcome,
                title = "default browser title",
                description = "default browser body with link text",
                primaryButton = Action("default browser primary button text", unitLambda),
                secondaryButton = Action("default browser secondary button text", unitLambda),
            )

        val onboardingPageUiData =
            OnboardingPageUiData(
                type = OnboardingPageUiData.Type.DEFAULT_BROWSER,
                imageRes = R.drawable.ic_onboarding_welcome,
                title = "default browser title",
                description = "default browser body with link text",
                primaryButtonLabel = "default browser primary button text",
                secondaryButtonLabel = "default browser secondary button text",
            )
        val actual =
            mapToOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onMakeFirefoxDefaultClick = unitLambda,
                onMakeFirefoxDefaultSkipClick = unitLambda,
                onSignInButtonClick = {},
                onSignInSkipClick = {},
                onTermsOfServiceButtonClick = {},
            )

        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN a sync page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val expected =
            OnboardingPageState(
                imageRes = R.drawable.ic_onboarding_sync,
                title = "sync title",
                description = "sync body",
                primaryButton = Action("sync primary button text", unitLambda),
                secondaryButton = Action("sync secondary button text", unitLambda),
            )

        val onboardingPageUiData =
            OnboardingPageUiData(
                type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
                imageRes = R.drawable.ic_onboarding_sync,
                title = "sync title",
                description = "sync body",
                primaryButtonLabel = "sync primary button text",
                secondaryButtonLabel = "sync secondary button text",
            )
        val actual =
            mapToOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onMakeFirefoxDefaultClick = {},
                onMakeFirefoxDefaultSkipClick = {},
                onSignInButtonClick = unitLambda,
                onSignInSkipClick = unitLambda,
                onTermsOfServiceButtonClick = {},
            )

        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN a marketing data collection opt out page WHEN mapToOnboardingPageState is called THEN creates the expected OnboardingPageState`() {
        val marketingData =
            OnboardingMarketingData(
                marketingCardVariant = MarketingCardVariant.DEFAULT,
                bodyOneText = "marketing data body one",
                bodyOneLinkText = "marketing data body one link",
                bodyTwoText = "marketing data body two",
            )

        val expected =
            OnboardingPageState(
                imageRes = R.drawable.ic_high_five,
                title = "marketing data title",
                description = "marketing data body",
                primaryButton = Action("marketing data button text", unitLambda),
                marketingData = marketingData,
            )

        val onboardingPageUiData =
            OnboardingPageUiData(
                type = OnboardingPageUiData.Type.MARKETING_DATA,
                imageRes = R.drawable.ic_high_five,
                title = "marketing data title",
                description = "marketing data body",
                primaryButtonLabel = "marketing data button text",
                marketingData = marketingData,
            )

        val actual =
            mapToOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onMakeFirefoxDefaultClick = {},
                onMakeFirefoxDefaultSkipClick = {},
                onSignInButtonClick = {},
                onSignInSkipClick = {},
                onTermsOfServiceButtonClick = {},
                onMarketingDataContinueClick = unitLambda,
            )

        assertEquals(expected, actual)
    }
}

private val unitLambda = { dummyUnitFunc() }

private fun dummyUnitFunc() {}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.nimbus.MarketingCardVariant

class OnboardingPageUiDataTest {

    @Test
    fun `GIVEN first page in the list WHEN sequencePosition called THEN returns the index plus 1`() {
        val expected = "1"
        val actual = allKnownPages.sequencePosition(defaultBrowserPageUiData.type)

        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN last page in the list WHEN sequencePosition called THEN returns the index plus 1`() {
        val expected = "3"
        val actual = allKnownPages.sequencePosition(termsOfServicePageUiData.type)

        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN all known pages of WHEN sequenceId() called THEN should map to the correct sequence id`() {
        val expected = "default_sync_terms_of_service"
        val actual = allKnownPages.telemetrySequenceId()

        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN some of the known pages WHEN sequenceId() called THEN should map to the correct sequence id`() {
        val expected = "default_sync"
        val actual = listOf(defaultBrowserPageUiData, syncPageUiData).telemetrySequenceId()

        assertEquals(expected, actual)
    }

    @Test
    fun `GIVEN a pairing install sequence WHEN sequenceId() is called THEN should map to the correct sequence id`() {
        val pages = listOf(termsOfServicePageUiData, syncPairingPageUiData)

        assertEquals("terms_of_service_sync_pairing", pages.telemetrySequenceId())
    }

    @Test
    fun `GIVEN a single known page WHEN sequenceId() called THEN should map to the correct sequence id`() {
        val expected = "default"
        val actual = listOf(defaultBrowserPageUiData).telemetrySequenceId()

        assertEquals(expected, actual)
    }
}

val defaultBrowserPageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.DEFAULT_BROWSER,
        imageRes = R.drawable.ic_onboarding_welcome,
        title = "default browser title",
        description = "default browser body with link text",
        primaryButtonLabel = "default browser primary button text",
        secondaryButtonLabel = "default browser secondary button text",
    )

val syncPageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = "sync title",
        description = "sync body",
        primaryButtonLabel = "sync primary button text",
        secondaryButtonLabel = "sync secondary button text",
    )

val syncPairingPageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.SYNC_PAIRING,
        imageRes = R.drawable.nova_onboarding_sync_qr,
        title = "sync pairing title",
        description = "sync pairing body",
        primaryButtonLabel = "sync pairing scan button text",
        secondaryButtonLabel = "sync pairing email button text",
    )

val termsOfServicePageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.TERMS_OF_SERVICE,
        imageRes = R.drawable.nova_onboarding_tou,
        title = "terms of service title",
        description = "terms of service body",
        primaryButtonLabel = "terms of service primary button text",
        termsOfService =
            OnboardingTermsOfService(
                lineOneText = "terms of service line one text",
                lineOneLinkText = "terms of service line one link text",
                lineOneLinkUrl = "terms of service line one link url",
                lineTwoText = "terms of service line two text",
                lineTwoLinkText = "terms of service line two link text",
                lineTwoLinkUrl = "terms of service line two link url",
                lineThreeText = "terms of service line three text",
                lineThreeLinkText = "terms of service line three link text",
            ),
    )

val marketingDataPageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.MARKETING_DATA,
        imageRes = R.drawable.ic_high_five,
        title = "marketing data title",
        description = "marketing data body",
        primaryButtonLabel = "marketing data button text",
        marketingData =
            OnboardingMarketingData(
                marketingCardVariant = MarketingCardVariant.DEFAULT,
                bodyOneText = "marketing data body one",
                bodyOneLinkText = "marketing data body one link",
                bodyTwoText = "marketing data body two",
            ),
    )

private val allKnownPages =
    listOf(
        defaultBrowserPageUiData,
        syncPageUiData,
        termsOfServicePageUiData,
    )

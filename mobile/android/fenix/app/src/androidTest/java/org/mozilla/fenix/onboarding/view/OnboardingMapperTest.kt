/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.experiments.nimbus.StringHolder
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.nimbus.ExtraCardData
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.JunoOnboarding
import org.mozilla.fenix.nimbus.OnboardingCardData
import org.mozilla.fenix.nimbus.OnboardingCardType
import org.mozilla.fenix.nimbus.TermsOfServiceData

class OnboardingMapperTest {

    @get:Rule val activityTestRule = HomeActivityIntentTestRule.withDefaultSettingsOverrides()

    private lateinit var junoOnboardingFeature: JunoOnboarding
    private lateinit var jexlConditions: Map<String, String>
    private val fakeJexlEvaluator: (String) -> Boolean = { evalStringInput ->
        when (evalStringInput) {
            "true" -> true
            else -> false
        }
    }

    @Before
    fun setup() {
        junoOnboardingFeature = FxNimbus.features.junoOnboarding.value()
        jexlConditions = junoOnboardingFeature.conditions
    }

    @Test
    fun showDefaultBrowserPageTrue_pagesToDisplay_returnsSortedListOfAllConvertedPages() {
        val expected = listOf(defaultBrowserPageUiData, syncPageUiData)
        assertEquals(
            expected,
            unsortedAllKnownCardData.toPageUiData(
                showDefaultBrowserPage = true,
                jexlConditions = jexlConditions,
                jexlEvaluator = fakeJexlEvaluator,
            ),
        )
    }

    @Test
    fun showDefaultBrowserPageFalse_pagesToDisplay_returnsSortedListOfConvertedPages_withoutDefaultBrowserPage() {
        val expected = listOf(syncPageUiData)
        assertEquals(
            expected,
            unsortedAllKnownCardData.toPageUiData(
                showDefaultBrowserPage = false,
                jexlConditions = jexlConditions,
                jexlEvaluator = fakeJexlEvaluator,
            ),
        )
    }

    @Test
    fun termsOfServiceData_toPageUiData_returnsConvertedPage() {
        val imageRes = R.drawable.ic_firefox
        val title = "Terms of service card title"
        val description = "Terms of service card body"
        val primaryButtonLabel = "onboarding card primary button text"

        val lineOneText = "By continuing, you agree to the %1\$s."
        val lineOneLinkText = "Firefox Terms of Use"
        val lineOneLinkUrl = "LinkOne"
        val lineTwoText = "Firefox cares about your privacy. Learn more in our %1\$s."
        val lineTwoLinkText = "Privacy Notice"
        val lineTwoLinkUrl = "LinkTwo"
        val lineThreeText =
            "To help improve the browser, Firefox sends diagnostic and interaction data to Mozilla. %1\$s"
        val lineThreeLinkText = "Manage settings"

        val expected =
            OnboardingPageUiData(
                type = OnboardingPageUiData.Type.TERMS_OF_SERVICE,
                imageRes = imageRes,
                title = title,
                description = description,
                primaryButtonLabel = primaryButtonLabel,
                termsOfService =
                    OnboardingTermsOfService(
                        lineOneText = lineOneText,
                        lineOneLinkText = lineOneLinkText,
                        lineOneLinkUrl = lineOneLinkUrl,
                        lineTwoText = lineTwoText,
                        lineTwoLinkText = lineTwoLinkText,
                        lineTwoLinkUrl = lineTwoLinkUrl,
                        lineThreeText = lineThreeText,
                        lineThreeLinkText = lineThreeLinkText,
                    ),
            )

        val nimbusTermsOfServiceData =
            TermsOfServiceData(
                lineOneText = StringHolder(R.string.nova_onboarding_tou_body_line_1, ""),
                lineOneLinkText = StringHolder(R.string.nova_onboarding_tou_body_line_1_link_text, ""),
                lineOneLinkUrl = StringHolder(null, lineOneLinkUrl),
                lineTwoText = StringHolder(R.string.nova_onboarding_tou_body_line_2, ""),
                lineTwoLinkText = StringHolder(R.string.nova_onboarding_tou_body_line_2_link_text, ""),
                lineTwoLinkUrl = StringHolder(null, lineTwoLinkUrl),
                lineThreeText = StringHolder(R.string.nova_onboarding_tou_body_line_3, ""),
                lineThreeLinkText = StringHolder(R.string.nova_onboarding_tou_body_line_3_link_text, ""),
            )

        val termsOfServiceCardData =
            OnboardingCardData(
                cardType = OnboardingCardType.TERMS_OF_SERVICE,
                imageRes = imageRes,
                title = StringHolder(null, title),
                body = StringHolder(null, description),
                primaryButtonLabel = StringHolder(null, primaryButtonLabel),
                ordering = 30,
                extraData = ExtraCardData(termOfServiceData = nimbusTermsOfServiceData),
            )

        assertEquals(
            expected,
            listOf(defaultBrowserCardData, termsOfServiceCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                )
                .last(),
        )
    }

    @Test
    fun cardConditionsMatchJexlConditions_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("ALWAYS" to "true", "NEVER" to "false")
        val expected = listOf(defaultBrowserPageUiData)

        assertEquals(
            expected,
            listOf(defaultBrowserCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun noJexlConditionsAndNoCardConditions_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf<String, String>()
        val expected = listOf(syncPageUiData)

        assertEquals(
            expected,
            listOf(syncCardDataNoConditions)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun noJexlConditions_shouldDisplayCard_returnsNoPage() {
        val jexlConditions = mapOf<String, String>()
        val expected = emptyList<OnboardingPageUiData>()

        assertEquals(
            expected,
            listOf(defaultBrowserCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun prerequisitesMatchJexlConditions_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("ALWAYS" to "true")
        val expected = listOf(defaultBrowserPageUiData)

        assertEquals(
            expected,
            listOf(defaultBrowserCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun prerequisitesDontMatchJexlConditions_shouldDisplayCard_returnsNoPage() {
        val jexlConditions = mapOf("NEVER" to "false")
        val expected = emptyList<OnboardingPageUiData>()

        assertEquals(
            expected,
            listOf(defaultBrowserCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun noCardConditions_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("ALWAYS" to "true", "NEVER" to "false")
        val expected = listOf(syncPageUiData)

        assertEquals(
            expected,
            listOf(syncCardDataNoConditions)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun noDisqualifiers_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("ALWAYS" to "true", "NEVER" to "false")
        val expected = listOf(defaultBrowserPageUiData)

        assertEquals(
            expected,
            listOf(defaultBrowserCardDataNoDisqualifiers)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun disqualifiersMatchJexlConditions_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("NEVER" to "false")
        val expected = listOf(syncPageUiData)

        assertEquals(
            expected,
            listOf(syncCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun disqualifiersDontMatchJexlConditions_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("NEVER" to "false")
        val expected = listOf(syncPageUiData)

        assertEquals(
            expected,
            listOf(syncCardDataUnknownDisqualifier)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun noPrerequisites_shouldDisplayCard_returnsConvertedPage() {
        val jexlConditions = mapOf("ALWAYS" to "true", "NEVER" to "false")
        val expected = listOf(syncPageUiData)

        assertEquals(
            expected,
            listOf(syncCardData)
                .toPageUiData(
                    showDefaultBrowserPage = true,
                    jexlConditions = jexlConditions,
                    jexlEvaluator = fakeJexlEvaluator,
                ),
        )
    }

    @Test
    fun givenANullType_whenConvertingToPageUiData_thenCardIsDiscarded() {
        // When a config contains an unsupported card type, Nimbus builds a OnboardingCardData
        // with a null type.
        val cardDataWithoutType =
            OnboardingCardData(
                cardType = null,
                imageRes = R.drawable.ic_onboarding_sync,
                title = StringHolder(null, "unknown card title"),
                body = StringHolder(null, "unknown card body"),
                primaryButtonLabel = StringHolder(null, "unknown card primary button text"),
                secondaryButtonLabel = StringHolder(null, "unknown card secondary button text"),
                ordering = 15,
                prerequisites = listOf(),
                disqualifiers = listOf(),
            )

        val expected = listOf(defaultBrowserPageUiData)
        val cardData = listOf(defaultBrowserCardData, cardDataWithoutType)
        assertEquals(
            expected,
            cardData.toPageUiData(
                showDefaultBrowserPage = true,
                jexlConditions = jexlConditions,
                jexlEvaluator = fakeJexlEvaluator,
            ),
        )
    }
}

private val defaultBrowserPageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.DEFAULT_BROWSER,
        imageRes = R.drawable.ic_onboarding_welcome,
        title = "default browser title",
        description = "default browser body",
        primaryButtonLabel = "default browser primary button text",
        secondaryButtonLabel = "default browser secondary button text",
    )
private val syncPageUiData =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = "sync title",
        description = "sync body",
        primaryButtonLabel = "sync primary button text",
        secondaryButtonLabel = "sync secondary button text",
    )
private val defaultBrowserCardData =
    OnboardingCardData(
        cardType = OnboardingCardType.DEFAULT_BROWSER,
        imageRes = R.drawable.ic_onboarding_welcome,
        title = StringHolder(null, "default browser title"),
        body = StringHolder(null, "default browser body"),
        primaryButtonLabel = StringHolder(null, "default browser primary button text"),
        secondaryButtonLabel = StringHolder(null, "default browser secondary button text"),
        ordering = 10,
        prerequisites = listOf("ALWAYS"),
        disqualifiers = listOf("NEVER"),
    )

private val defaultBrowserCardDataNoDisqualifiers =
    OnboardingCardData(
        cardType = OnboardingCardType.DEFAULT_BROWSER,
        imageRes = R.drawable.ic_onboarding_welcome,
        title = StringHolder(null, "default browser title"),
        body = StringHolder(null, "default browser body"),
        primaryButtonLabel = StringHolder(null, "default browser primary button text"),
        secondaryButtonLabel = StringHolder(null, "default browser secondary button text"),
        ordering = 10,
        prerequisites = listOf("ALWAYS"),
        disqualifiers = listOf(),
    )

private val syncCardData =
    OnboardingCardData(
        cardType = OnboardingCardType.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = StringHolder(null, "sync title"),
        body = StringHolder(null, "sync body"),
        primaryButtonLabel = StringHolder(null, "sync primary button text"),
        secondaryButtonLabel = StringHolder(null, "sync secondary button text"),
        ordering = 20,
        prerequisites = listOf(),
        disqualifiers = listOf("NEVER"),
    )

private val syncCardDataNoConditions =
    OnboardingCardData(
        cardType = OnboardingCardType.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = StringHolder(null, "sync title"),
        body = StringHolder(null, "sync body"),
        primaryButtonLabel = StringHolder(null, "sync primary button text"),
        secondaryButtonLabel = StringHolder(null, "sync secondary button text"),
        ordering = 20,
        prerequisites = listOf(),
        disqualifiers = listOf(),
    )

private val syncCardDataUnknownDisqualifier =
    OnboardingCardData(
        cardType = OnboardingCardType.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = StringHolder(null, "sync title"),
        body = StringHolder(null, "sync body"),
        primaryButtonLabel = StringHolder(null, "sync primary button text"),
        secondaryButtonLabel = StringHolder(null, "sync secondary button text"),
        ordering = 20,
        prerequisites = listOf(),
        disqualifiers = listOf("NEVER", "OTHER"),
    )

private val unsortedAllKnownCardData =
    listOf(
        syncCardData,
        defaultBrowserCardData,
    )

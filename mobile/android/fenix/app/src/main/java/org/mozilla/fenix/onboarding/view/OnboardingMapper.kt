/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import org.mozilla.fenix.nimbus.MarketingData
import org.mozilla.fenix.nimbus.OnboardingCardData
import org.mozilla.fenix.nimbus.OnboardingCardType
import org.mozilla.fenix.nimbus.TermsOfServiceData

/** Returns a list of all the required Nimbus 'cards' that have been converted to [OnboardingPageUiData]. */
internal fun Collection<OnboardingCardData>.toPageUiData(
    showDefaultBrowserPage: Boolean,
    jexlConditions: Map<String, String>,
    jexlEvaluator: (String) -> Boolean,
): List<OnboardingPageUiData> {
    // we are first filtering the cards based on Nimbus configuration
    return asSequence()
        .filter { it.cardType != null }
        .filter { it.shouldDisplayCard(jexlEvaluator, jexlConditions) }
        // we are then filtering again based on device capabilities
        .filter { it.isCardEnabled(showDefaultBrowserPage) }
        .sortedBy { it.ordering }
        // Cards that don't declare a card type are considered misconfigured so they are discarded
        .mapNotNull { it.toPageUiData() }
        .toList()
}

/**
 * Determines whether the given [OnboardingCardData] should be displayed.
 *
 * @param jexlEvaluator Function that receives a condition as a [String] and returns its JEXL evaluation as a [Boolean].
 * @param jexlConditions A <String, String> map containing the Nimbus conditions.
 * @return True if the card should be displayed, otherwise false.
 */
private fun OnboardingCardData.shouldDisplayCard(
    jexlEvaluator: (String) -> Boolean,
    jexlConditions: Map<String, String>,
): Boolean {
    val jexlCache: MutableMap<String, Boolean> = mutableMapOf()

    // Make sure the conditions exist and have a value, and that the number
    // of valid conditions matches the number of conditions on the card's
    // respective prerequisite or disqualifier table. If these mismatch,
    // that means a card contains a condition that's not in the feature
    // conditions lookup table. JEXLs can only be evaluated on
    // supported conditions. Otherwise, consider the card invalid.
    val allPrerequisites = prerequisites.mapNotNull { jexlConditions[it] }
    val allDisqualifiers = disqualifiers.mapNotNull { jexlConditions[it] }

    val validPrerequisites =
        if (allPrerequisites.size == prerequisites.size) {
            allPrerequisites.all { condition ->
                jexlCache.getOrPut(condition) {
                    jexlEvaluator(condition)
                }
            }
        } else {
            false
        }

    val hasDisqualifiers =
        if (allDisqualifiers.isNotEmpty() && allDisqualifiers.size == disqualifiers.size) {
            allDisqualifiers.all { condition ->
                jexlCache.getOrPut(condition) {
                    jexlEvaluator(condition)
                }
            }
        } else {
            false
        }

    return validPrerequisites && !hasDisqualifiers
}

private fun OnboardingCardData.isCardEnabled(showDefaultBrowserPage: Boolean): Boolean =
    when (cardType) {
        OnboardingCardType.DEFAULT_BROWSER -> enabled && showDefaultBrowserPage
        else -> enabled
    }

private fun OnboardingCardData.toPageUiData() = cardType?.let { type ->
    OnboardingPageUiData(
        type = type.toPageUiDataType(),
        imageRes = imageRes.resourceId,
        title = title,
        description = body,
        primaryButtonLabel = primaryButtonLabel,
        secondaryButtonLabel = secondaryButtonLabel.ifEmpty { null },
        termsOfService = extraData?.termOfServiceData?.toOnboardingTermsOfService(),
        marketingData = extraData?.marketingData?.toOnboardingMarketingData(),
    )
}

private fun OnboardingCardType.toPageUiDataType() =
    when (this) {
        OnboardingCardType.DEFAULT_BROWSER -> OnboardingPageUiData.Type.DEFAULT_BROWSER
        OnboardingCardType.SYNC_SIGN_IN -> OnboardingPageUiData.Type.SYNC_SIGN_IN
        OnboardingCardType.TERMS_OF_SERVICE -> OnboardingPageUiData.Type.TERMS_OF_SERVICE
        OnboardingCardType.MARKETING_DATA -> OnboardingPageUiData.Type.MARKETING_DATA
    }

private fun TermsOfServiceData.toOnboardingTermsOfService() =
    with(this) {
        OnboardingTermsOfService(
            subheaderOneText = subheaderOneText,
            subheaderTwoText = subheaderTwoText,
            subheaderThreeText = subheaderThreeText,
            lineOneText = lineOneText,
            lineOneLinkText = lineOneLinkText,
            lineOneLinkUrl = lineOneLinkUrl,
            lineTwoText = lineTwoText,
            lineTwoLinkText = lineTwoLinkText,
            lineTwoLinkUrl = lineTwoLinkUrl,
            lineThreeText = lineThreeText,
            lineThreeLinkText = lineThreeLinkText,
        )
    }

private fun MarketingData.toOnboardingMarketingData() =
    OnboardingMarketingData(
        marketingCardVariant = marketingCardVariant,
        bodyOneText = bodyLineOneText,
        bodyOneLinkText = bodyLineOneLinkText,
        bodyTwoText = bodyLineTwoText,
        bodyThreeText = bodyLineThreeText,
    )

/**
 * Mapper to convert [OnboardingPageUiData] to [OnboardingPageState] that is a param for [OnboardingPage] composable.
 */
@Suppress("LongParameterList")
internal fun mapToOnboardingPageState(
    onboardingPageUiData: OnboardingPageUiData,
    isSmallDevice: Boolean = false,
    onMakeFirefoxDefaultClick: () -> Unit,
    onMakeFirefoxDefaultSkipClick: () -> Unit,
    onSignInButtonClick: () -> Unit,
    onSignInSkipClick: () -> Unit,
    onTermsOfServiceButtonClick: () -> Unit,
    onMarketingDataContinueClick: () -> Unit = {},
): OnboardingPageState =
    when (onboardingPageUiData.type) {
        OnboardingPageUiData.Type.DEFAULT_BROWSER ->
            createOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onPositiveButtonClick = onMakeFirefoxDefaultClick,
                onNegativeButtonClick = onMakeFirefoxDefaultSkipClick,
                isSmallDevice = isSmallDevice,
            )

        OnboardingPageUiData.Type.SYNC_SIGN_IN ->
            createOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onPositiveButtonClick = onSignInButtonClick,
                onNegativeButtonClick = onSignInSkipClick,
                isSmallDevice = isSmallDevice,
            )

        OnboardingPageUiData.Type.TERMS_OF_SERVICE ->
            createOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onPositiveButtonClick = onTermsOfServiceButtonClick,
                onNegativeButtonClick = {}, // No negative button option for terms of service.
                isSmallDevice = isSmallDevice,
            )

        OnboardingPageUiData.Type.MARKETING_DATA ->
            createOnboardingPageState(
                onboardingPageUiData = onboardingPageUiData,
                onPositiveButtonClick = onMarketingDataContinueClick,
                onNegativeButtonClick = {}, // No negative button option for marketing data.
                isSmallDevice = isSmallDevice,
            )
    }

private fun createOnboardingPageState(
    onboardingPageUiData: OnboardingPageUiData,
    isSmallDevice: Boolean,
    onPositiveButtonClick: () -> Unit,
    onNegativeButtonClick: () -> Unit,
): OnboardingPageState =
    OnboardingPageState(
        imageRes = onboardingPageUiData.imageRes,
        title = onboardingPageUiData.title,
        description = onboardingPageUiData.description,
        primaryButton = Action(onboardingPageUiData.primaryButtonLabel, onPositiveButtonClick),
        secondaryButton =
            onboardingPageUiData.secondaryButtonLabel?.let {
                Action(it, onNegativeButtonClick)
            },
        termsOfService = onboardingPageUiData.termsOfService,
        marketingData = onboardingPageUiData.marketingData,
        isSmallDevice = isSmallDevice,
    )

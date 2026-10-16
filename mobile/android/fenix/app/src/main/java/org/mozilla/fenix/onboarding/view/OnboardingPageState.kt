/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.annotation.DrawableRes
import org.mozilla.fenix.nimbus.MarketingCardVariant

/**
 * Model containing data for [OnboardingPage].
 *
 * @property imageRes The main image to be displayed on the page.
 * @property title Title of the page.
 * @property description Description of the page.
 * @property primaryButton [Action] for the primary button.
 * @property secondaryButton Optional [Action] for the secondary button.
 * @property termsOfService Optional term of service page data.
 * @property marketingData Optional marketing page data.
 * @property onRecordImpressionEvent Callback for recording impression event.
 * @property isSmallDevice Whether to apply layout optimizations for constrained screen heights.
 */
data class OnboardingPageState(
    @param:DrawableRes val imageRes: Int,
    val title: String,
    val description: String,
    val primaryButton: Action,
    val secondaryButton: Action? = null,
    val termsOfService: OnboardingTermsOfService? = null,
    val marketingData: OnboardingMarketingData? = null,
    val onRecordImpressionEvent: () -> Unit = {},
    val isSmallDevice: Boolean = false,
)

/** Model containing text and action for a button. */
data class Action(
    val text: String,
    val onClick: () -> Unit,
)

/** Model containing data for the terms of service page during onboarding. */
data class OnboardingTermsOfService(
    val subheaderOneText: String? = null,
    val subheaderTwoText: String? = null,
    val subheaderThreeText: String? = null,
    val lineOneText: String,
    val lineOneLinkText: String,
    val lineOneLinkUrl: String,
    val lineTwoText: String,
    val lineTwoLinkText: String,
    val lineTwoLinkUrl: String,
    val lineThreeText: String,
    val lineThreeLinkText: String,
)

/** Model containing data for the marketing data page during onboarding. */
data class OnboardingMarketingData(
    val marketingCardVariant: MarketingCardVariant,
    val bodyOneText: String,
    val bodyOneLinkText: String,
    val bodyTwoText: String? = null,
    val bodyThreeText: String? = null,
)

/** Contains all the events which can happen in terms of service onboarding page. */
interface OnboardingTermsOfServiceEventHandler {

    /** Invoked when the terms of service link is clicked. */
    fun onTermsOfServiceLinkClicked(url: String) = Unit

    /** Invoked when the privacy notice link is clicked. */
    fun onPrivacyNoticeLinkClicked(url: String) = Unit

    /** Invoked when the manage privacy preferences link is clicked. */
    fun onManagePrivacyPreferencesLinkClicked() = Unit

    /** Invoked when the accept button is clicked. */
    fun onAcceptTermsButtonClicked() = Unit
}

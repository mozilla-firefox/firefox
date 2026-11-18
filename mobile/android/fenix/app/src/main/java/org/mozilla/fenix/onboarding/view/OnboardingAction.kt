/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

/** User interactions with the onboarding cards. */
internal sealed interface OnboardingAction {
    /** Actions from the default browser card. */
    sealed interface DefaultBrowser : OnboardingAction {
        data object SetAsDefaultClicked : DefaultBrowser

        data object SkipClicked : DefaultBrowser
    }

    /** Actions from the sync sign in card. */
    sealed interface SyncSignIn : OnboardingAction {
        data object SignInClicked : SyncSignIn

        data object SkipClicked : SyncSignIn
    }

    /** Actions from the sync pairing card. */
    sealed interface SyncPairing : OnboardingAction {
        data object ScanClicked : SyncPairing

        data object EmailClicked : SyncPairing

        data object SkipClicked : SyncPairing
    }

    /** Actions from the terms of service card. */
    sealed interface TermsOfService : OnboardingAction {
        data class TermsOfServiceLinkClicked(val url: String) : TermsOfService

        data class PrivacyNoticeLinkClicked(val url: String) : TermsOfService

        data object ManagePrivacyPreferencesLinkClicked : TermsOfService

        data object AcceptClicked : TermsOfService
    }

    /** Actions from the marketing data card. */
    sealed interface MarketingData : OnboardingAction {
        data object LearnMoreClicked : MarketingData

        data class OptInToggled(val optIn: Boolean) : MarketingData

        data class ContinueClicked(val allowMarketingDataCollection: Boolean) : MarketingData

        data object SkipClicked : MarketingData
    }
}

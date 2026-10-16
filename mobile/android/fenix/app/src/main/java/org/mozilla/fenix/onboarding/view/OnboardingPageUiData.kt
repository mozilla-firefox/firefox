/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.annotation.DrawableRes
import org.mozilla.fenix.nimbus.OnboardingCardData

/** Model containing the required data from a raw [OnboardingCardData] object in a UI state. */
data class OnboardingPageUiData(
    val type: Type,
    @param:DrawableRes val imageRes: Int,
    val title: String,
    val description: String,
    val primaryButtonLabel: String,
    val secondaryButtonLabel: String? = null,
    val termsOfService: OnboardingTermsOfService? = null,
    val marketingData: OnboardingMarketingData? = null,
) {
    /**
     * Model for different types of Onboarding Pages.
     *
     * @property telemetryId Identifier for the page, used in telemetry.
     */
    enum class Type(val telemetryId: String) {
        DEFAULT_BROWSER(telemetryId = "default"),
        SYNC_SIGN_IN(telemetryId = "sync"),
        TERMS_OF_SERVICE(telemetryId = "terms_of_service"),
        MARKETING_DATA(telemetryId = "marketing_data"),
    }
}

/** Returns the sequence position for the given [OnboardingPageUiData.Type]. */
fun List<OnboardingPageUiData>.sequencePosition(type: OnboardingPageUiData.Type): String = indexOfFirst {
    it.type == type
}
    .inc()
    .toString()

/**
 * Helper function for telemetry that maps List<OnboardingPageUiData> to a string of page names separated by an
 * underscore. e.g. [DEFAULT_BROWSER, SYNC_SIGN_IN] will be mapped to "default_sync".
 */
fun List<OnboardingPageUiData>.telemetrySequenceId(): String = joinToString(separator = "_") { it.type.telemetryId }

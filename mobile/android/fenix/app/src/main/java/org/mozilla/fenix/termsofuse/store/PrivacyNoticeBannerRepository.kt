/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.termsofuse.store

import org.mozilla.fenix.termsofuse.TOU_PUBLISHED_TIMES_IN_MILLIS
import org.mozilla.fenix.utils.Settings

/** Repository related to the Privacy Notice banner. */
interface PrivacyNoticeBannerRepository {
    /** Updates the preference that tracks the last time the user saw the Privacy Notice banner. */
    fun updatePrivacyNoticeBannerDisplayedPreference()

    /** Determines if the Privacy Notice banner should be shown. */
    fun shouldShowPrivacyNoticeBanner(): Boolean
}

/**
 * The default implementation of the [PrivacyNoticeBannerRepository]
 *
 * @param settings The [Settings] used to read and store the banner state.
 * @param currentTimeMillis Returns the current time in milliseconds.
 * @param termsOfUsePublishedTimes The timestamps of the Terms of Use published dates.
 */
class DefaultPrivacyNoticeBannerRepository(
    private val settings: Settings,
    private val currentTimeMillis: () -> Long = { System.currentTimeMillis() },
    private val termsOfUsePublishedTimes: List<Long> = TOU_PUBLISHED_TIMES_IN_MILLIS,
) : PrivacyNoticeBannerRepository {
    override fun updatePrivacyNoticeBannerDisplayedPreference() {
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = currentTimeMillis()
    }

    override fun shouldShowPrivacyNoticeBanner(): Boolean {
        val now = currentTimeMillis()
        val termsOfUseTime =
            if (settings.isTermsOfUsePublishedDebugDateEnabled) {
                now
            } else {
                // Grab the most recent ToU time, ignoring any future dates
                termsOfUsePublishedTimes.filter { it <= now }.maxOrNull() ?: return false
            }
        return settings.hasAcceptedTermsOfService &&
            settings.termsOfUseAcceptedTimeInMillis < termsOfUseTime &&
            settings.privacyNoticeBannerLastDisplayedTimeInMillis < termsOfUseTime
    }
}

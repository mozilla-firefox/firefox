/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.termsofuse.store

import org.mozilla.fenix.termsofuse.TOU_TIME_IN_MILLIS
import org.mozilla.fenix.utils.Settings

/** Repository related to the Privacy Notice banner. */
interface PrivacyNoticeBannerRepository {
    /** Updates the preference that tracks the last time the user saw the Privacy Notice banner. */
    fun updatePrivacyNoticeBannerDisplayedPreference()

    /** Determines if the Privacy Notice banner should be shown. */
    fun shouldShowPrivacyNoticeBanner(): Boolean
}

/** The default implementation of the [PrivacyNoticeBannerRepository] */
class DefaultPrivacyNoticeBannerRepository(
    private val settings: Settings,
    private val currentTimeMillis: () -> Long = { System.currentTimeMillis() },
) : PrivacyNoticeBannerRepository {
    override fun updatePrivacyNoticeBannerDisplayedPreference() {
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = currentTimeMillis()
    }

    override fun shouldShowPrivacyNoticeBanner(): Boolean {
        val termsOfUseTime =
            if (settings.isTermsOfUsePublishedDebugDateEnabled) {
                currentTimeMillis()
            } else {
                TOU_TIME_IN_MILLIS
            }
        return settings.hasAcceptedTermsOfService &&
            settings.termsOfUseAcceptedTimeInMillis < termsOfUseTime &&
            settings.privacyNoticeBannerLastDisplayedTimeInMillis < termsOfUseTime
    }
}
